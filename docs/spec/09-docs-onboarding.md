# Docs alignment and onboarding

The documentation still describes the design mockups rather than the device: `docs/screens` and the README overview use Noto Sans with a grey palette, while the firmware draws M5GFX built-in fonts on an RGB332 canvas whose surfaces come out navy. At the same time the visual pack is one personal file that mixes OFL font glyphs with WaniKani-derived radical bitmaps, so a new user needs a computer and a token before the device is useful. This file aligns the docs with decisions D1–D6, splits the pack into a publishable font pack and a personal radical pack, and lets the setup portal fetch or receive them.

| ID | Title | Priority | Size | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| DOC-01 | Mockups match the device | P2 | M | none | any | todo |
| DOC-02 | `ux.md` alignment | P2 | S | DOC-01, REV-10, LES-01…LES-04, CFG | any | todo |
| DOC-03 | `decisions.md` entries | P2 | S | REV-02, LES-01…LES-03, SPEED-06 | any | todo |
| DOC-04 | ROADMAP round 2 | P2 | S | none | any | todo |
| DOC-05 | Hardware check log template | P3 | S | none | any | todo |
| ONB-01 | Split the visual pack | P2 | M | none | 2 (after ARCH-01) | todo |
| ONB-02 | Pack from the setup portal | P2 | L | ONB-01, FIX-01 | 2 (after ONB-01) | todo |
| ONB-03 | `setup.md` for non-developers | P3 | S | ONB-01, ONB-02, FIX-01 | any | todo |

### DOC-01 — Mockups match the device

- **Priority:** P2
- **Size:** M
- **Depends on:** none
- **Touches:** `tools/mockups/render.py` (palette 34–51, `font()`/`text()` 53–69, `save()` 409–412, `overview()` 415–433, `main()` 436–459); `docs/screens/*.png`; `README.md:62,140,147–158`; `docs/ux.md:7,673–693`; `docs/design.md:324–326`; `tools/shot.py` (`capture()` 73–113)
- **Settings:** none

**Problem / goal.** `render.py` says the UI font is Noto Sans JP (`render.py:6,24–29`) and draws every screen with TrueType sizes from 8 to 40 px (`font()` 56–60) on a hand-picked hex palette (34–51). The device has neither: `Widgets.cpp:12`, `HomeScreen.cpp:14,21,44,54,60` use M5GFX `Font0`/`Font1`/`Font2`/`Font4`, and the 8-bit canvas converts the RGB565 constants in `Theme.h:8–23` through RGB332 (`color_convert<rgb332_t, rgb565_t>` in M5GFX `lgfx/v1/misc/colortype.hpp`, line 531 (verify)). `kSurface` 0x0108 therefore shows as #002455, not #151A22. `README.md:62` and `docs/ux.md:7` publish the wrong result.

**Behaviour.** Every committed image in `docs/screens` shows the same 240×135 pixels as the panel: 6×8 Latin text for labels, 16/26 px built-ins for titles and big values, and only colours reachable from `Theme.h` after RGB332 quantization. The README overview and `ux.md` §8 say whether an image is a capture or a render. `render.py` still runs offline and is either the faithful generator or no longer the documented path.

**Design.**
- Preferred: capture the screens on a `cardputer-adv-dev` build. Seed a synthetic cache with `C` (`App.cpp:4788` → `seedDevCache()` 3986; send any non-empty token with `k` first, `seedDevCache()` checks `DevToken::has()` at 3987 and makes no network call), then save with `tools/shot.py` (`capture()` 73–113). Serial drivers: home `C`, review `v`, feedback `y eat` or `y eta` + `e`, detail `D`, lesson card `l` + `e`, quiz `Q`, setup `P`; settings, sync and summary are driven by hand on the same seeded cache.
- Store 1× PNGs exactly as `shot.py` writes them; keep the 4× copies only where a doc displays them, and rebuild `overview.png` with `overview()` 415–433.
- No real data: the seed uses `dev` and `dev`-level items; addresses stay in 192.168.1.0/24 as in commit `0386bff` (`192.168.1.42`, `192.168.1.10`); no token, SSID or live AP password in an image (do not capture the AP page while the password is shown).
- If a screen cannot be captured reproducibly, fix `render.py` for it: replace the TTF calls with bitmap tables copied from the M5GFX fonts actually used (`Font0`/`Font1` — both the 6×8 GLCD font in M5GFX — `Font2` 16 px, `Font4` 26 px), and quantize each pixel with `(((((c>>13)&7)<<3)+((c>>8)&7))<<2)+((c>>3)&3)` then expand with `rgb332_t::R8()/G8()/B8()` (same expression as `shot.py` 59–65). Keep nearest-neighbour 4×.
- Add a `--check` mode that fails if an output pixel is not an expanded colour from `Theme.h`.
- Update every image link and the "mockups" wording in the same pass.

**Tests.** `python3 tools/mockups/render.py --check` accepts all images (if the generator remains). `python3 tools/shot.py --out /tmp/kk.png` on the dev build produces the same layout as `docs/screens/home.png`. Manual side-by-side of each image with the panel. A pixel probe confirms `surface` is #002455 and `surface-hi` is #242455.

**Docs.** `README.md:62,140,147–158`; `docs/ux.md:7,673–693`; `docs/design.md:324–326`; `THIRD_PARTY_NOTICES.md:79–85` (the mockup font note changes if the TTF is dropped).

**Acceptance.**
- [ ] Every image in `docs/screens` is either a `tools/shot.py` capture or a render using M5GFX bitmap fonts and RGB332 colours.
- [ ] No image contains Noto Sans Latin text or a colour outside the `Theme.h` set; a spot check shows `surface` #002455.
- [ ] `README.md:62` and `docs/ux.md:7` show the new images and label them accurately (capture vs render).
- [ ] Captures contain only seeded `dev` data and 192.168.1.x addresses; no token, SSID or AP password.
- [ ] The documented regeneration path still works, or the obsolete instructions are removed.

### DOC-02 — `ux.md` alignment

- **Priority:** P2
- **Size:** S
- **Depends on:** DOC-01, REV-10, LES-01…LES-04, CFG
- **Touches:** `docs/ux.md:33–56` (§2.1), `58–72` (§2.2), `165–176` (§3.4), `239–254` (§4.4), `494–517` (§5.9), `557–584` (§5.12); `docs/design.md:688`
- **Settings:** none

**Problem / goal.** `ux.md` describes a font, a palette and behaviours the device does not have. §2.2 (58–72) claims Noto Sans JP, a pre-rendered `UiFont.h` (which does not exist in the repo) and no rasterization; §3.4 (167) says Backspace repeats, but `KeyInput.h:42–44` repeats only arrows; §4.4 (239–254) and §5.9 (494–517) still say `Enter` immediately sends `PUT /assignments/{id}/start` (`App.cpp:1976 startCurrentLesson`), which D2 and the LES tasks replace; §5.12 (557–584) lists today's flat rows instead of the CFG registry. §2.1 (33–56) never says the hexes are RGB565 constants displayed through RGB332.

**Behaviour.** §2.2 names the built-in fonts and the pack: `Font0`/`Font1` (both the 6×8 GLCD font in M5GFX; `setTextFont(1)` maps to `Font0`) for body text and labels, `Font2` 16 px and `Font4` 26 px for titles and big values, pack glyphs at 64/32/20 px for kana/kanji; no Noto UI font. §2.1 adds one paragraph: the table is the design value; `Theme.h` stores RGB565 constants and the panel shows the quantized colour (`surface` #002455, `surface-hi` #242455). §3.4 states the current repeat set and names REV-10 for Backspace. §4.4 and §5.9 describe the D2/LES batch flow (browse the batch, quiz, one confirmed start for the batch) and name LES-04. §5.12 points at the CFG sections/registry and no longer duplicates the row list. Each paragraph says which task made it true.

**Design.**
- Rewrite §2.2's size table around real roles, not mockup pixel sizes; cite `HomeScreen.cpp:14,44` and `Widgets.cpp:12,47`.
- Add the RGB332 note under the §2.1 table with two displayed values; keep the design hex column so the palette stays reviewable.
- In §3.4, replace the flat claim with "arrows repeat today; Backspace repeats after REV-10" and cite `KeyInput.h:42–44`; the 400 ms/120 ms constants are at `KeyInput.h:147–148`.
- Rewrite §4.4's flow diagram and §5.9's `Enter start` bullets against LES-01…LES-04 and D2; remove "the current screen does not yet show `Needs connection`" if LES adds it (verify).
- In §5.12, keep only "Settings is the sectioned menu of CFG; the rows and defaults are the registry in `03-settings.md`", plus the keys and the non-setting facts (Setup portal, Sync now, About).
- Grep for leftovers: `Noto Sans`, `UiFont`, `Backspace`, `Enter start`, `Implemented rows`; fix `docs/design.md:688` ("UI glyphs and visual pack" → pack glyphs only).
- Do not copy the registry into `ux.md`; link it.

**Tests.** `grep -n "Noto Sans\|UiFont" docs/ux.md` returns no UI-font claim. `grep -n "Backspace" docs/ux.md` matches `KeyInput.h` today plus the REV-10 note. Section 5.12's row list is gone and the CFG link resolves. A read-through against the cited code lines.

**Docs.** `docs/ux.md` (six sections); `docs/design.md:688`.

**Acceptance.**
- [ ] §2.2 names the M5GFX fonts and the pack, with file:line references, and claims no Noto UI text.
- [ ] §2.1 explains RGB332 and shows at least two displayed values.
- [ ] §3.4 matches `KeyInput.h` and names REV-10 for Backspace.
- [ ] §4.4 and §5.9 match D2/LES-01…LES-04 and name the task.
- [ ] §5.12 links to the CFG registry and lists no stale rows.
- [ ] `docs/design.md:688` no longer calls the pack font the UI font.

### DOC-03 — `decisions.md` entries

- **Priority:** P2
- **Size:** S
- **Depends on:** REV-02, LES-01…LES-03, SPEED-06 (for the supersede notes)
- **Touches:** `docs/decisions.md` (append after 694; edit 200–212, 214–227, 509–523)
- **Settings:** none

**Problem / goal.** The newest decision is dated 2026-10-29 (line 674); the round-2 decisions D1–D6 exist only in `docs/spec/README.md` §1. Three entries now contradict the plan: "Strict checking with warnings" (214–227) vs REV-02, "Lesson quiz is optional and local-only" (509–523) vs LES-01…03, and "Idle dim, blank, then deep sleep" (200–212) if SPEED-06 changes the timings. A reader cannot tell which rule is current.

**Behaviour.** Six entries dated 2026-11-01 appear at the end, each with Decision, Why and Implications, in the existing format. Superseded entries keep their text and gain a bold "Superseded on 2026-11-01 (`<ID>`): …" line, as the `efontJA_16` entry already does (141–154). No entry is deleted.

**Design.**
- D1 "Keep the built-in UI font; docs follow the device": flash/size fidelity, no CJK UI font; implies DOC-01/DOC-02.
- D2 "Lessons follow WaniKani's batch flow": browse, quiz, one confirmed start; no irreversible `Enter`; implies LES-01…04 and supersedes the quiz-only flow.
- D3 "A configurable app with a settings registry": every new behaviour behind a setting, default faithful to WaniKani; implies CFG and NVS.
- D4 "Kakehashi blocks in scope": review options, critical items/extra study, statistics, reminders, audio; practice never writes to WaniKani.
- D5 "English" for docs, comments and UI strings.
- D6 "Delegated implementation": subagents, reviewed before merge; implies README §7.
- Add the dated supersede lines to the strict-checking and lesson-quiz entries; add the deep-sleep line only if SPEED-06 changes the timings.
- Update the header's status line to mention the 2026-11-01 additions.

**Tests.** Every ID in `README.md` §1 has one entry. Every superseded entry names the superseding ID. `git diff docs/decisions.md` shows no removed entry text.

**Docs.** `docs/decisions.md` only.

**Acceptance.**
- [ ] Six 2026-11-01 entries for D1–D6, each with Why and Implications.
- [ ] The strict-checking, lesson-quiz and (when applicable) deep-sleep entries carry a dated supersede line naming the task.
- [ ] Each case states the current rule; no entry was deleted.
- [ ] The header reflects the round-2 additions.

### DOC-04 — ROADMAP round 2

- **Priority:** P2
- **Size:** S
- **Depends on:** none
- **Touches:** `ROADMAP.md:1–15` (status), `129–142` (priority table), `144–162` (execution order), `632–661` (§10), new section before 662
- **Settings:** none

**Problem / goal.** `ROADMAP.md` presents the v1 backlog as the only plan: the priority table's "Start with" cells point at EXT-03/04, EXT-05/WK-11, EXT-06, EXT-09 and LESSON-08 (133–141), and §10 (632–661) describes those items without saying that `docs/spec` supersedes them. Nothing links to the round-2 spec.

**Behaviour.** A "Round 2" section after §10 links to `docs/spec/README.md` and maps each remaining item to its spec task. The priority table and execution order use the spec IDs, and the status snapshot says round 2 is specified, not started.

**Design.**
- Mapping table: EXT-03 → STUDY (critical items/leeches); EXT-04 → STUDY (extra study); EXT-05 + WK-11 → STATS (forecast, history, streak); EXT-06 → AUD; EXT-09 → REM; LESSON-08 → LES-04. Keep EXT-01/02 as done and EXT-07/08 out of round 2.
- Priority table: row 3 "Start with" → LES-04; row 4 → AUD, STATS; row 10 → STUDY. Add "round 2 specified in `docs/spec`" to the status column.
- Execution order: add a row that points at spec waves 0a–4 instead of listing the same work twice; keep the v1 rows as history.
- Statuses are `todo` per spec §6; do not claim any round-2 work is done.
- Add one sentence to the top paragraph (3–15) and update its snapshot date.

**Tests.** `grep -n "docs/spec" ROADMAP.md` finds the link in the top and the Round 2 section. Each of the six mappings appears once. No stale EXT/WK "Start with" remains for the mapped items.

**Docs.** `ROADMAP.md` only.

**Acceptance.**
- [ ] `ROADMAP.md` links to `docs/spec/README.md` from the top and from the Round 2 section.
- [ ] The six mappings are present; no other EXT item is claimed.
- [ ] The priority table and execution order use LES-04/AUD/STATS/STUDY/REM instead of stale EXT/WK IDs.
- [ ] Round-2 work is marked `todo`, never `done`.
- [ ] The status snapshot date is updated.

### DOC-05 — Hardware check log template

- **Priority:** P3
- **Size:** S
- **Depends on:** none
- **Touches:** `docs/hardware.md` (new section at the end, after 440; structure follows 1–10 and 213–256)
- **Settings:** none

**Problem / goal.** `docs/hardware.md` is a chronological log (sections at 6, 213, 302, 335, 403) with no place to record the hardware checks that round-2 P0/P1 tasks require. A reviewer cannot tell whether a task's Acceptance boxes were checked, because results live only in prose inside unrelated dated sections.

**Behaviour.** A "Round 2 hardware checks" section holds one table row per hardware check copied from a P0/P1 task's **Tests** line: task ID, check, build/env, date, result (`todo`/`pass`/`fail`/`blocked`) and evidence. Rows start as `todo`; a run fills the date/result and may append a short dated subsection with the log. The section states that a task stays `review` until all its P0/P1 rows pass.

**Design.**
- Columns: `Task | Check | Build | Date | Result | Evidence`.
- Start with one empty template row; add rows when each task's Tests line is final. Source tasks: P0/P1 in `01-fixes.md`, `02-speed.md` and the other spec files.
- Evidence is a serial line (`[pack]`, `[cache]`, `[ota]`), a `tools/shot.py` image under `tools/screenshots/out` (not committed) or a committed `docs/screens` image; say which.
- Point at `tools/device.py` (`monitor`, `cmd`, `upload`) and `tools/shot.py`.
- Do not rewrite the historical sections; the table links to them by date.

**Tests.** None (documentation). A read-through confirms the table can be filled without reading the firmware.

**Docs.** `docs/hardware.md`; `README.md` §6 status meaning is unchanged but referenced.

**Acceptance.**
- [ ] A "Round 2 hardware checks" section exists with the six columns and a template row.
- [ ] It says rows come from P0/P1 **Tests** and that `review` requires them.
- [ ] It names `tools/device.py`, `tools/shot.py` and where evidence goes.
- [ ] No historical entry is modified.

### ONB-01 — Split the visual pack

- **Priority:** P2
- **Size:** M
- **Depends on:** none
- **Touches:** `tools/glyph-pack/build.py` (`collect_from_api()` 127–145, `build()` 148–202, `verify()` 205–230, `main()` 233–271); `firmware/src/platform/GlyphPack.h:8–18,47–120`; `firmware/src/platform/GlyphPack.cpp` (`close()` 29–39, `open()` 41–104, `findRadical()` 174–190); `firmware/src/ui/Widgets.h:14–28`, `Widgets.cpp:32–57`; `ReviewScreens.cpp:64–66,224–226`; `LessonScreens.cpp:185–187,256–258`; `DebugScreens.cpp:92`; `App.cpp:903,1643,1722,1799,2892–2898,2983–2998,4358–4401,4447–4499,4862`
- **Settings:** none

**Problem / goal.** One file, `/kanikey/glyphs.kgp`, holds both kinds of content: 7,269 font glyphs rendered from Noto Sans JP (OFL) and 498 radical bitmaps rasterized from WaniKani SVG URLs fetched with the user's token (`build.py:127–145,167–181`; `docs/setup.md:80–81`; `docs/hardware.md:257–262`). Because of the radicals the whole file is personal and cannot be published, so every user must run `build.py` with a token before the device works. `GlyphPack::open()` (`GlyphPack.cpp:41–104`) also assumes both tables in one file and rejects `glyphCount_ == 0` at line 71.

**Behaviour.** The card holds `/kanikey/glyphs.kgp` (font; no WaniKani content, downloadable) and `/kanikey/radicals.kgp` (personal; token-generated). Boot opens both; About and the setup page show two statuses, e.g. `7269 glyphs, 64/32/20 px` and `498 radicals`. A missing font pack is the existing `Pack missing` boot state (`drawBoot()` `App.cpp:3787–3807`); a missing radical pack leaves all text working, and a radical whose subject has no `characters` draws the explicit `? / no glyph` state (`Widgets.cpp:45–57`). A v1 combined pack still opens and serves both roles.

**Design.**
- Format: keep the 32-byte header; bump `version` to 2 and use the reserved `u32` as `kind` (0 = combined/legacy, 1 = font, 2 = radical). The body and CRC are unchanged. The reader accepts v1 (combined) and v2; a v1 file is opened for both roles. Font pack: glyphs > 0, radicals = 0. Radical pack: radicals > 0, glyphs = 0, and `open()` must allow zero glyphs for kind 2 (relax `GlyphPack.cpp:71` for that case).
- App state: replace `gPack` with `gFontPack` and `gRadicalPack`; every `open(SD, "/kanikey/glyphs.kgp")` call in the Touches list opens the matching file. `portalRecheckPack()` 2892 and `portalPackStatus()` 2898 return both.
- Drawing: `ui::drawSubjectPrompt(d, fontPack, radicalPack, …)` tries the radical pack first, then `fontPack.drawTextCentered()`; `drawMissingGlyph` takes both and prints `no pack` only when the font pack is missing, `no glyph` for a radical-only miss. Update the call sites listed.
- `build.py`: keep the current single-file `--out` default; add `--split DIR` writing `glyphs.kgp` (kind 1) and `radicals.kgp` (kind 2), and extend `verify()` to check version 2 and the kind/table consistency. A font-only build needs no token; radicals still come from `collect_from_api()` and `--token`.
- Public font pack: build it from a fixed, account-independent character set (kana + CJIS X 0208 kanji is the suggested set; verify coverage, count and file size, and do not use the WaniKani subject list). A personal pack can still add any character the account uses.
- Publish it as a GitHub release asset with a SHA-256, and add a `pack` record to the OTA manifest later (ONB-02); `Manifest.h` ignores unknown records today, so the format stays 1.
- Cost: two open files and two 16-entry caches instead of one (`GlyphPack.h:108–119`); about 0.4 KB more static RAM (measure), no flash library.

**Tests.** `build.py --split` writes both files and `verify()` passes; the font file has `radicals=0`, the radical file has `glyphs=0`; a v1 combined file still verifies. If the header parser is moved to a small pure helper, add native cases `pack_v2_font_header`, `pack_v2_radical_header`, `pack_v1_still_readable`, `pack_wrong_kind_missing`. Hardware: boot with both packs; remove `radicals.kgp` and boot (text renders, radical prompt shows `no glyph`); boot with a v1 combined pack (both work); About shows two status lines.

**Docs.** `docs/setup.md` §3, §6, §7; `docs/hardware.md:257–262`; `THIRD_PARTY_NOTICES.md:79–101`; `GlyphPack.h` format comment.

**Acceptance.**
- [ ] `build.py --split` writes a font pack and a radical pack, both verified; a v1 combined pack is still accepted.
- [ ] Firmware opens `glyphs.kgp` and `radicals.kgp`; a missing radical pack changes only radical prompts, to the explicit `no glyph` state.
- [ ] Boot, About and the setup page report both packs; no screen assumes one `gPack`.
- [ ] A font pack built without a token contains no radical table and no WaniKani-derived character list.
- [ ] The format is version 2 with a documented `kind`, and v1 remains readable.

### ONB-02 — Pack from the setup portal

- **Priority:** P2
- **Size:** L
- **Depends on:** ONB-01, FIX-01
- **Touches:** `firmware/src/platform/SetupPortal.cpp` (`registerRoutes()` ~220–235, `sendPage()` 287–410 with the pack block 332–335, `handlePack()` 456–467); `SetupPortal.h:22–34,63–78`; `firmware/src/platform/Ota.cpp` (`request()` 135–..., `install()` 364–470) and/or the shared fetch helper from FIX-01; `App.cpp:2935–2939,2892–2898,2927,2983,3787`
- **Settings:** none

**Problem / goal.** The setup page only re-checks the card (`handlePack()` `SetupPortal.cpp:456–467`; the pack block is 332–335). A user without a pack must use a computer, Python, `uv`, a Noto font and a WaniKani token (`docs/setup.md:17–21,49–82`), and the portal cannot receive the file. There is no pack download, no upload form and no checksum check on the card. The OTA side already streams a size/SHA-256-checked image (`Ota.cpp:364–470`), but the portal cannot reach it.

**Behaviour.** On the station page, when `gFontPack` is not ready, a "Download pack" button fetches the pack record from the release manifest, streams it to `/kanikey/glyphs.kgp.tmp`, shows `Pack 34%` on the device and a waiting page in the browser, then renames and reopens on success. Failures (offline, TLS, HTTP, size, hash) delete the `.tmp`, keep the old file and show the reason. `Esc` cancels and leaves the card unchanged. An upload form for `radicals.kgp` streams the multipart part to `/kanikey/radicals.kgp.tmp`; only a file that opens as a kind-2 pack is renamed, otherwise the `.tmp` is deleted and the page says why. Both forms keep `Check pack again`, and no token leaves the device.

**Design.**
- Add a `pack` record to the OTA manifest (`pack<TAB>font<TAB>glyphs.kgp<TAB><bytes><TAB><sha256>`), resolved against the manifest URL like `app`; extend the parser used for the setup check only (the firmware update parser can keep ignoring it).
- Add `kk::ota::downloadToFile(url, expectedBytes, expectedSha, path.tmp, ProgressFn)` next to `install()` (`Ota.cpp:364`): reuse the `request()` body callback (135–...) or the reusable helper FIX-01 extracts; hash while writing with `mbedtls_sha256_*` (as in `install()` 390–440); compare size and digest; `SD.rename` only on success; delete the `.tmp` otherwise. No 3.7 MB buffer.
- Portal hooks: add `downloadPack` and `uploadRadicalPack` callbacks (`SetupPortal.h:22–34`), filled in `App.cpp:2935–2939`. The handlers call the hook, which draws progress with `drawLoading()`, calls `tgc::board::update()` every chunk and returns `Cancelled` on `Esc`, so the §2 "over 1 s is cancellable" rule holds while the POST is open.
- Upload: read `Content-Length` and the boundary from `server_->header("Content-Type")` (verify the WebServer API), read from `server_->client()` in 1 KB chunks, skip the part headers, stop at the closing boundary, and never buffer the body in a `String`.
- Validate the upload with `GlyphPack::open(SD, tmp)` plus the kind check (ONB-01). The header/table is checked; the CRC is not (`GlyphPack.cpp` does not verify it) — either add a CRC check in ONB-01 or state that the upload relies on the structure check (verify).
- Keep the portal local; no new server and no new NVS key.
- Page text: `Download pack` / `Upload radical pack`, with the size and the free-space check (`SD.totalBytes()`/`usedBytes()`).

**Tests.** Native: `manifest_pack_record_parse` (valid, missing, bad hash, wrong size); `download_status_maps_size_hash_short_cancel` if the helper can be host-tested. `build.py` test writes a pack record. Hardware: (1) empty card + station portal → download completes, `[pack] 7269 glyphs…`, a review starts; (2) cut Wi-Fi mid-download → `.tmp` removed, the page explains; (3) upload a token-generated `radicals.kgp` from a phone → renamed, About shows 498 radicals; (4) upload a truncated file → rejected, old pack kept; (5) `Esc` during the download → cancelled, card unchanged; (6) record free heap before/after the download.

**Docs.** `docs/setup.md` §3.1 and §4; `docs/ota.md` (the `pack` record); `docs/hardware.md` (the heap/size check); task acceptance follows DOC-05.

**Acceptance.**
- [ ] The station page offers "Download pack" only when the font pack is missing, and the download is HTTPS + size + SHA-256 checked.
- [ ] The download writes `.tmp` and replaces `glyphs.kgp` only after the checks; `Esc` leaves the card unchanged.
- [ ] The upload form streams multipart to `radicals.kgp.tmp`, validates it as a radical pack and renames on success.
- [ ] No pack body is held in RAM and no new large buffer is added; heap before/after is recorded.
- [ ] `docs/setup.md` explains both paths without a computer.

### ONB-03 — `setup.md` for non-developers

- **Priority:** P3
- **Size:** S
- **Depends on:** ONB-01, ONB-02, FIX-01
- **Touches:** `docs/setup.md` (whole file; §1 9–24, §2 28–45, §3 49–92, §4 95–127, §5 131–148, §6 152–168, §7 172–186, §8 190–196, §9 200–203); `README.md:137`
- **Settings:** none

**Problem / goal.** `docs/setup.md` is written for a developer: §1 requires Python, `uv`, Noto Sans JP and a computer (17–21), §3 is a `build.py` tutorial (49–92) and §9 still lists a browser pack generator as future (200–203). It predates the split pack and the OTA fixes: the pack is one token-generated file (80–81) and updating is only "Settings › Firmware" (145–148). A non-developer cannot follow it.

**Behaviour.** The main path needs no computer: insert a FAT32 card, power on, join `KaniKey-XXXX` at `http://192.168.4.1/`, save Wi-Fi, create a token, paste it on the station page, press "Download pack" when offered, then start. The token permissions are listed once, exactly as the portal and sync use them. "Offline" is explained: cached Home/batch reviews work and writes queue in the outbox; new data needs Wi-Fi. Updating distinguishes OTA (Settings › Firmware after FIX-01) from USB (a v0.1.0 device). The `build.py` material becomes an optional appendix for a custom font or pack. Troubleshooting gains the pack-download/upload errors.

**Design.**
- One-page quick start (5 steps), then sections: What you need · Your WaniKani token · The visual pack · First boot · Daily use · Offline · Updating · Problems · Privacy.
- Token list: read assignments, subjects, study materials, review statistics, level progressions, user, summary; create reviews (matches `docs/setup.md:32–41` and `api-sync.md`; verify against the portal's `GET /user` validation).
- Pack section: describe `glyphs.kgp` and `radicals.kgp`, the "Download pack" button and the upload form from ONB-02, and the `no glyph` case for a missing radical pack.
- Update section: OTA after FIX-01; for a v0.1.0 image, flash the release asset over USB (name the `cardputer-adv` app-only bin; verify the release filename).
- Keep the troubleshooting table (172–186) and add rows for download failure, wrong pack kind and a cancelled download.
- State the privacy facts (190–196): token in NVS, packs on the card, no telemetry, no KaniKey server.
- Remove promises from §9 or put them under a clear "not yet" heading.

**Tests.** Read-through by someone who has not built the firmware. Every command in the appendix runs as written. All links resolve. The steps match the actual portal labels and screen text.

**Docs.** `docs/setup.md`; `README.md:137` (description row).

**Acceptance.**
- [ ] The main path needs no computer, Python or font, only a WaniKani token.
- [ ] First boot, Wi-Fi, token permissions, pack, offline and updating each have their own section.
- [ ] Updating distinguishes OTA (after FIX-01) from USB (v0.1.0) and names the file to use.
- [ ] The pack section describes both files and the `no glyph` case.
- [ ] No future promise remains outside a clearly labelled "not yet" note.
