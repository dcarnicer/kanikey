# Flash headroom

Status: **spike results, 2026-11-20.** FLASH-01 (attribution and the `fs`
probe) is done from builds. FLASH-02 has one trim applied and measured. FLASH-03
is a proposal with a go/no-go; **the partition table is not changed here**. No
device was used: every figure is either a build measurement (**M**) or an
estimate from code constants or arithmetic (**E**). The hardware checks are in
§7.

| ID | What | Status | Result |
|---|---|---|---|
| FLASH-01 | Attribution of the release, dev and PaperDIY images; `fs` probe | done (build) | Release 1,712,269 B at baseline; ours is 27.4% of it; the WiFi/lwIP, ESP-IDF, mbedTLS and M5GFX blocks are 53% (M). Dev-only code is 32.1 KB (M). LittleFS state caps sum to about 352 KB (E). `fs` added (dev only). |
| FLASH-02 | Behaviour-neutral trims | 1 applied, 3 measured and held back | `sscanf` removed from ISO-8601 parsing: **−9,708 B** release (M), native test vs sscanf passes. `CORE_DEBUG_LEVEL=0` would save 38,188 B (M) but drops library diagnostics: not applied. |
| FLASH-03 | 8 MB partition change | proposal; **no-go for the USB table flip now** | The round's ≤250 KB budget does not fit the 132 KB left in the legacy slot after trims (M). Recommend 2 × 0x240000 slots with a state migration through SD; do not flash until the migration is tested on hardware. |

## 1. Baseline and method

Baseline is `main` at `e3837ee` before this spike, built with `pio run`:

| Env | Flash used | Slot / partition | Share | Notes |
|---|---:|---:|---:|---|
| `cardputer-adv` (release) | 1,712,269 B | 1,835,008 B | 93.3% | matches the parent's figure |
| `cardputer-adv-dev` | 1,744,381 B | 1,835,008 B | 95.1% | dev − release = 32,112 B (M) |
| `paperdiy` | 1,740,709 B | 6,291,456 B | 27.7% | own 16 MB table, not affected by the 8 MB plan |

Method (M). The ELF and map come from the default `firmware.map` that the
espressif32 platform writes. A throwaway script (not committed) sums the
loaded input sections per archive and per `src/` directory: `.flash.text`
1,232,887 B, `.flash.rodata` 379,092 B, `.iram0.text` 75,967 B, `.dram0.data`
23,296 B and `.iram0.vectors` 1,027 B sum to exactly 1,712,269 B, the image
size. The map sums are within 0.3% of that total (see the caveat in §2.2).
Symbols come from `xtensa-esp32s3-elf-nm --size-sort -C -S`. Compile flags come
from a throwaway compile database that was deleted afterwards.

## 2. FLASH-01 — attribution

### 2.1 Compile and link facts (M)

- The platform already compiles our code with `-Os -fno-rtti -fexceptions
  -ffunction-sections -fdata-sections -fstack-protector` and links with
  `--gc-sections`. Nothing is left to gain from RTTI or sections.
- `-fno-exceptions` is **not feasible**: `app/CacheController.cpp:315-368`
  catches `std::bad_alloc`, and REV-08 (`docs/hardware.md`, 2026-11-19) needed
  that catch to survive `loadCachedBatch`. Do not propose it.
- The Arduino/IDF libraries are prebuilt for newlib. A `nano.specs` build gave
  the same size (1,702,561 B) because the IDF `libc.a` is what gets linked; it
  is not a trim in this build.
- `-flto` fails to link in the platform's link step (the linker needs the LTO
  plugin, and three tries with `PLATFORMIO_BUILD_FLAGS`/`PLATFORMIO_LINK_FLAGS`
  did not produce a working link). Not measured; a platform link change would be
  needed.

### 2.2 Attribution, release baseline (M, bytes of 1,712,269)

| Component | Bytes | % of image | Notes |
|---|---:|---:|---|
| WiFi / lwIP / ESP-IDF net (prebuilt) | 360,842 | 21.1 | net80211 113,252; lwip 98,995; pp 46,707; wpa_supplicant 42,233; phy 33,257; coexist 4,614 |
| ESP-IDF runtime, non-net | 264,667 | 15.5 | includes `esp_littlefs` 28,223; espcoredump 11,461; app_trace ~20 K; freertos 26,221; hal 22,002 |
| **Our code** | **468,600** | **27.4** | see §2.3 |
| mbedTLS | 142,712 | 8.3 | mbedcrypto 91,873; mbedtls 42,599; x509 8,240 |
| M5GFX (+ fonts) | 134,214 | 7.8 | M5GFX.cpp.o 44,015; lgfx_v1_panel 24,736; lgfx_v1_platforms 21,008; lgfx_fonts 18,321; LGFXBase 11,566; lgfx_v1 11,059 |
| libc / libm / libgcc / libstdc++ | 131,839 | 7.7 | newlib `c` 104,702; stdc++ 13,029; m 4,080; gcc 6,629 |
| M5Unified | 57,249 | 3.3 | includes the BMI270 config blob, 8,192 B |
| Arduino WiFi / WiFiClientSecure / WebServer / Update | 55,471 | 3.2 | WebServer 21,088 is used by the setup portal |
| SD / FS / FATFS | 35,491 | 2.1 | fatfs 18,248; FS 8,778; SD 8,465 |
| `vfs` (IDF VFS layer) | 9,874 | 0.6 | pulled in by FS |
| Arduino core (objects + libs) | 37,130 | 2.2 | FrameworkArduino 27,853; core 9,277 |
| LittleFS (`libLittleFS`) | 1,354 | 0.1 | plus `esp_littlefs` in the runtime row |
| wifi_provisioning, smartconfig, protocomm, json | 5,193 | 0.3 | linked by the Arduino WiFi library; not used by us |
| Fill / alignment | 12,720 | 0.7 | |

Caveat (M): the map-based sum is 1,717,394 B, 5,125 B above the image. `.flash.text`
matches exactly; `.flash.rodata` is 4,731 B high in the map, so the rodata
attributions may be up to about 0.3% high. The component ranking is unaffected.

### 2.3 Our code by directory (M, release baseline)

| Directory | Bytes | Largest objects |
|---|---:|---|
| `app/` | 183,150 | LessonController 19,208; SettingsController 18,032; SyncController 12,663; ReviewController 12,208; ReviewScreens 11,971; CacheController 11,510 |
| `platform/` | 124,432 | SetupPortal 24,193; AudioCache 21,434; Ota 12,327; WkLessons 8,150; WkClient 7,641 |
| `wk/` | 124,271 | ReviewQueue 22,261; Cache 12,498; SubjectParser 8,002; AnswerChecker 7,747 |
| `audio/` | 16,594 | Minimp3.cpp (the decoder, `mp3DecodeFrame` 6,903 B) |
| `ui/` | 10,363 | Text 4,568 |
| `ota/` | 6,468 | Manifest 5,690 |
| `board/` | 3,250 | BoardCardputerAdv |

### 2.4 Dev-only code (M, dev − release = 32,112 B)

| Object | Bytes in the dev image |
|---|---:|
| `app/DevConsole.cpp` | 22,315 |
| `app/RemindDev.cpp` | 3,505 |
| `app/RemProbe.cpp` | 2,086 |
| `app/DebugScreens.cpp` | 1,406 |
| `platform/Ota.cpp` (dev-only HTTP and override paths) | 748 |
| `app/ReminderController.cpp`, `platform/WkClient.cpp`, `app/App.cpp`, `wk/DevRemind.cpp`, `wk/DevOverrides.cpp`, others | about 2,200 |

The release image already excludes these bodies under `#ifdef KANIKEY_DEV`. The
release still carries about 6 KB of DevConsole code, which holds the release
serial commands (`h i S p r n a up`). That is not a trim: the commands are used.

### 2.5 Biggest 30 image-resident symbols (M, release baseline)

RAM-only symbols (`.bss`, for example `gBodyBuffer` 32,768 B and `gAppState`
11,896 B) are excluded: they are not in flash.

| # | Bytes | Symbol | Owner / note |
|---:|---:|---|---|
| 1 | 14,583 | `tgc::kRootCaPem` | 11 root CAs; all needed for trust (§3) |
| 2 | 12,250 | `_vfprintf_r` | newlib full printf; `snprintf("%.1f")` in SettingsController needs it |
| 3 | 11,979 | `_svfprintf_r` | same |
| 4 | 9,341 | `__ssvfscanf_r` | **removed** by FLASH-02 (only our `sscanf`) |
| 5 | 8,354 | `_vfiprintf_r` | `sniprintf`, from `strftime` (`__strftime` 2,806) |
| 6 | 8,192 | `m5::bmi270_config_file` | M5Unified IMU blob |
| 7 | 8,130 | `_svfiprintf_r` | `sniprintf` |
| 8 | 8,077 | `__ssvfiscanf_r` | `siscanf`, kept by libc `tzset_r` |
| 9 | 6,903 | `kk::audio::mp3DecodeFrame` | minimp3 (audio) |
| 10 | 6,626 | `mbedtls_ssl_handshake_server_step` | mbedTLS server half; we only act as a client, but the library links it |
| 11 | 6,150 | `mbedtls_ssl_handshake_client_step` | used (WaniKani, GitHub) |
| 12 | 4,840 | `ciphersuite_definitions` | mbedTLS |
| 13 | 4,328 | `L3_huffman(...)::tabs` | minimp3 tables |
| 14 | 4,062 | `kk::(anon)::request` | OTA `request()` |
| 15 | 3,893 | `kk::audioCachePrefetch` | audio |
| 16 | 3,733 | `tcp_input` | lwIP |
| 17 | 3,590 | `nd6_input` | lwIP IPv6 (IDF prebuilt) |
| 18 | 3,460 | `WebServer::_parseForm` | setup portal |
| 19 | 3,318 | `kk::checkAnswer` | review |
| 20 | 3,312 | `_strtod_l` | JSON number parse |
| 21 | 3,247 | `WebServer::_parseRequest` | setup portal |
| 22 | 3,230 | `kk::app::drawReviewSummaryScreen` | UI |
| 23 | 3,224 | `m5::Speaker_Class::spk_task` | M5Unified |
| 24 | 3,200 | `_dtoa_r` | float formatting (`%.1f`) |
| 25 | 3,126 | `kk::app::syncAdvance` | sync |
| 26 | 2,899 | `kk::app::startLessons` | lessons |
| 27 | 2,846 | `tcp_receive` | lwIP |
| 28 | 2,817 | `ieee80211_sta_new_state` | WiFi |
| 29 | 2,806 | `__strftime` | localtime/strftime (clock display) |
| 30 | 2,699 | `kk::app::drawReviewDetailScreen` | UI |

### 2.6 Fonts and panels (M)

- LovyanGFX built-in font data (`lgfx::v1::fonts::*`) totals **14,899 B** of
  symbols; `lgfx_fonts.cpp.o` is 18,321 B. Our code calls `setTextFont` with ids
  0, 1, 2 and 4 only (GLCD 6×8 and the 16 px, 26 px bitmaps). Fonts 6, 7 and 8
  are not used by us, but M5GFX's font table references them, so they are
  linked. Dropping them needs a patch to M5GFX (a library change, not a trim
  here). Estimate (E): at most about 6 KB.
- The panel drivers (`lgfx_v1_panel` 24.7 KB, `lgfx_v1_platforms` 21.0 KB) are
  linked through `M5GFX.cpp.o` (44 KB), which selects a panel at run time. Also a
  library patch. Not estimated further.

### 2.7 LittleFS state (E from code caps; actual usage is not measured)

LittleFS (`spiffs` partition, 4,653,056 B) holds only `/state/*`. Subjects and
audio live on SD. The steady-state caps from the code:

| File | Owner | Cap (code constant) | Bytes (E) |
|---|---|---|---:|
| `cache.bin` | `CacheStore` (`kMaxSnapshotBytes`) | 256 KB | 262,144 |
| `journal.bin` | `wk/Journal.h` (125 blocks × 520 B + 32) | streak history | 65,032 |
| `critical.bin` | `CriticalStore` (20 + 2000 × 8) | 2,000 items | 16,020 |
| `practice.bin` | `PracticeStore` (16 + 256 × 24 + 4) | 256 entries | 6,164 |
| `outbox.bin` | `wk/Outbox.h` (16 + 128 × 24) | 128 items | 3,088 |
| **Steady-state sum** | | | **352,448** |

Transients: each atomic write keeps `.tmp` and `.bak` copies, so `cache.bin`
can briefly occupy about 3 × 256 KB (E: about 0.8 MB worst case).

**Measured (2026-11-20, Cardputer ADV, real account after a full sync, `fs`):**
LittleFS total 4,653,056 B, **used 94,208 B (2.0%)**, free 4,558,848 B.
`/state` holds 7 files, 74,888 B: `journal.bin` 65,032, `practice.bin` 3,620,
`cache.bin` 3,064 + `cache.bak` 3,064, `critical.bin` 76, `outbox.bin` 16 +
`outbox.bak` 16. The rest of "used" is LittleFS metadata/blocks. Even the code
caps with the worst-case transient (~0.8 MB) leave the partition mostly empty,
so a smaller LittleFS (e.g. 2 MiB) would still keep about 2.5× headroom over
that worst case. The same run checked the scanf-free parser on the panel:
`Synced 12:53`, `L8 3% since 03 Feb` and `Next review now` rendered as before.

## 3. FLASH-02 — trims

Only the first row is applied. T1 is measured against the baseline (1,712,269 B).
T2 to T6 are measured on top of T1 (1,702,561 B), each as a clean release build
with one change, so they are not additive with the baseline.

| # | Candidate | Measured saving | Status | Why |
|---|---|---:|---|---|
| T1 | Replace `std::sscanf("%4d-%2d-%2dT%2d:%2d:%2d")` in `wk/Time.h` with a fixed parser | **−9,708 B** (M) | **applied** | Only user of newlib's scanf engine in our code. The parser matches sscanf for the accept/refuse decision and the six fields: `test_iso8601_scan_matches_sscanf` (`test/test_wk/test_wk.cpp`) runs 40,000 inputs (mutated timestamps and random strings) against host sscanf and passes. `parseIso8601` keeps the fixed 19-character tail check, so malformed input is accepted or refused exactly as before. `siscanf` stays only because libc's `tzset_r` uses it. |
| T2 | `CORE_DEBUG_LEVEL=1` → `0` | −38,188 B (M) | held back | Removes the Arduino/M5 library `log_e`/`log_w` output from the release image. Control flow is unchanged, but field diagnosis loses those lines. A decision for the owner; a dev-only override could keep level 1 in `cardputer-adv-dev`. |
| T3 | `-fno-threadsafe-statics` | −1,368 B (M) | held back | Removes the init guards for function-local statics. Safe only if no two FreeRTOS tasks ever race a first use; not proven. |
| T4 | `-fno-stack-protector` | 0 B (M) | not applied | No measurable effect with these flags, and it weakens hardening. |
| T5 | `-flto` | not measured | not applied | Link fails with the platform's link step (§2.1). |
| T6 | newlib nano (`nano.specs`) | 0 B (M) | not applied | The IDF prebuilt `libc.a` is linked; a change needs an `sdkconfig` rebuild of the libraries. Also `%.1f` in SettingsController would need `-u _printf_float`. |
| T7 | Drop root CAs from `kRootCaPem` | would save ≤14,583 B (M) | not applied | 11 roots are the trust set for WaniKani and the GitHub redirect chain (see the header of `RootCerts.h`). Removing one changes which hosts verify. Security-relevant; not behaviour-neutral. |
| T8 | Drop fonts 6/7/8 and M5GFX panels | fonts ≤ ~6 KB (E); panels not estimated | not applied | Needs a patch to M5GFX (a library), not our code. |
| T9 | Dev-only code | 32,112 B already excluded (M) | nothing to do | `#ifdef KANIKEY_DEV` already removes it from release. |
| T10 | `-fno-exceptions` | — | **refused** | `CacheController.cpp:315-368` catches `std::bad_alloc` (REV-08). |
| T11 | `-fno-rtti` | — | already applied | The platform compiles our code with `-fno-rtti`; no `typeid`/`dynamic_cast` in `src/`. |

Result of T1 on the three envs (clean builds, M):

| Env | Before | After T1 | Delta |
|---|---:|---:|---:|
| `cardputer-adv` | 1,712,269 | 1,702,561 | −9,708 |
| `cardputer-adv-dev` | 1,744,381 | 1,735,633 | −8,748 (includes the `fs` probe, +960 B, E) |
| `paperdiy` | 1,740,709 | 1,730,945 | −9,764 |

Clean rebuilds of identical sources vary by up to 16 B (two release builds gave
1,702,545 and 1,702,561). The percentages are of the 1,835,008 B slot:
release 92.8%, dev 94.6%. Free space in the legacy slot: 132,447 B (release) and
99,375 B (dev).

## 4. The `fs` probe (dev only)

`fs` is a multi-letter serial command in `app/DevConsole.cpp` under
`KANIKEY_DEV`, listed in the dev help line as `fs=littlefs-usage`. It is
read-only:

```
[fs] LittleFS total 4653056 B, used <n> B, free <n> B
[fs]   /state/cache.bin          <n> B
[fs]   /state/journal.bin        <n> B
...
[fs] /state: <n> files, <n> B
```

If LittleFS is not mounted it prints `[fs] LittleFS not mounted`; if `/state`
is missing it prints `[fs] /state: missing`. It adds about 960 B to the dev
image (E: derived from the dev totals) and nothing to release.

## 5. FLASH-03 — partition proposal

### 5.1 Options (sizes are arithmetic from the table, E)

The table is changed only at offset 0x8000. Keep `nvs` (0x9000, 0x5000) and
`otadata` (0xE000, 0x2000) and `app0` at 0x10000 in every option, so the same
offsets apply to both layouts. The slots must be 64 KiB-aligned.

| Option | app0 | app1 | spiffs (LittleFS) | Slot size | LittleFS | Legacy slot headroom after the round's +250 KB (M, from 1,702,561 B) |
|---|---|---|---|---:|---:|---:|
| (a) keep | 0x10000–0x1D0000 | 0x1D0000–0x390000 | 0x390000–0x800000 | 1,835,008 | 4,653,056 | −123,553 B: does not fit |
| (b1) 2 × 2 MiB | 0x10000–0x210000 | 0x210000–0x410000 | 0x410000–0x800000 | 2,097,152 | 4,128,768 | +138,591 B |
| **(b2) 2 × 2.25 MiB** | 0x10000–0x250000 | 0x250000–0x490000 | 0x490000–0x800000 | 2,359,296 | **3,604,480** | +400,735 B |
| (b3) 2 × 2.5 MiB | 0x10000–0x290000 | 0x290000–0x510000 | 0x510000–0x800000 | 2,621,440 | 3,080,192 | +662,879 B |
| (c) 2 × 3 MiB | 0x10000–0x310000 | 0x310000–0x610000 | 0x610000–0x800000 | 3,145,728 | 1,998,848 | +1,187,167 B |

The "headroom" column assumes the round adds 250 KB (256,000 B) to the release
image. The LittleFS column is what remains after each change. Against the
steady-state sum of 352,448 B and the transient peak of about 0.8 MB (§2.7),
every option except (a) has room; LittleFS is not the constraint.

### 5.2 Recommendation and go/no-go

- **Recommend (b2): 2 × 0x240000 slots, LittleFS 3,604,480 B.** It gives the
  round room and leaves 10× the state caps for LittleFS. (b1) is the minimum
  that fits the round, with 139 KB of margin; it is the fallback if the owner
  wants less LittleFS change.
- **Go** for the design, the migration code in the bridge release, and the
  workflow changes (§5.4–5.5), built and tested on the dev image first.
- **No-go** for the USB table flip until (1) the `fs` probe on a real device
  shows the actual LittleFS use (the cap-based estimate is 0.35–0.8 MB), (2) the
  migration round trip passes on hardware (§7), and (3) features actually need
  the space beyond 132 KB. Do not flip the table for the current, smaller image.

### 5.3 OTA implications

- **The partition table cannot be changed over the air.** It lives at 0x8000,
  which OTA never writes. The table change is a USB (esptool) flash.
- **An app image carries no table.** The bootloader picks the slot from the
  table on the device, LittleFS is found by label (`spiffs`), and the OTA
  client sizes its slot from `esp_ota_get_next_update_partition()`. So a
  legacy-size image runs and updates on either layout.
- **Refusing oversize images already exists.** `Ota.cpp` rejects `app.bytes >
  slotBytes()` as `TooLarge` (line 539) and `Update` returns `UPDATE_ERROR_SIZE`
  (line 571). `tools/release/firmware_manifest.py` refuses an image larger than
  its CSV slot. Both keep an old device from installing a large image.
- **Do not bump the manifest version.** A v1 reader refuses any other header
  (`docs/ota.md`), so a v2 manifest would stop every existing device from seeing
  any update. Keep version 1.
- **Route by environment, not by a new record.** The device matches `app` rows by
  its compiled `KANIKEY_ENV` (`Manifest::find`), and the manifest tool already
  takes `--env`. Publish the legacy image under `cardputer-adv` and the new layout
  under a second env (name TBD, e.g. `cardputer-adv-l2`). Legacy devices never see
  the second row; the only devices that can match it are those flashed with it.
  Check that a duplicate env is refused and that two `app` rows parse before this
  is relied on (`ota/Manifest.cpp:235`).
- **otadata must be erased on the USB flash.** `otadata` (0xE000) keeps its
  offset, but its sequence may point at `app1`, which in the new layout starts
  at 0x250000 and holds old data. The flash script erases 0xE000–0x10000 so the
  bootloader starts at `app0`.
- **Rollback is unchanged.** `PENDING_VERIFY`, the two-strike rule and the
  `fwfailed` record are per slot and per version; none depends on the table.

### 5.4 Data migration (LittleFS moves, so its content is lost unless copied)

Changing the table moves `spiffs` from 0x390000 to 0x490000 (b2), so the old
LittleFS is not valid and the new area may hold old bytes. The copy must happen
on the old table and SD must be the destination. Proposed path:

1. **Bridge release (legacy env `cardputer-adv`, ≤ 1,835,008 B, OTA-able).**
   Adds an *export* step: copy `/state/outbox.bin`, `journal.bin`,
   `critical.bin`, `practice.bin` and `cache.bin` to
   `/kanikey/migrate/v1/`, write a CRC32 (`wk/Crc32`) and size per file to a
   manifest, read each copy back to check it, and write a `COMMITTED` marker
   last. Runs on first boot of the bridge, and again from Settings. The
   LittleFS files stay in place until the new layout is running.
2. **USB flash of the new layout** (`cardputer-adv-l2`). A script
   (`tools/usb_layout.py`, not written here) refuses to run unless the device
   reports `migrate: committed` (a dev-serial status line, to be added); then it:
   erases `otadata` (0xE000, 0x2000) and the new LittleFS area (0x490000
   onward), writes `partitions.bin` at 0x8000 and the app at 0x10000.
3. **First boot of the new layout.** `LittleFS.begin(false)` fails (the
   FIX-21 path). When `/kanikey/migrate/v1/COMMITTED` exists, the screen offers
   **Restore from SD** instead of format: Enter copies the files back, checks the
   CRCs and loads the outbox and journal. Only after both loads succeed is the
   SD copy kept as a backup; it is removed after the next successful sync.
4. Without the `COMMITTED` marker, the screen keeps the FIX-21 choice (format or
   continue) and never formats silently.

What each file needs:

| File | Loss if not migrated | Plan |
|---|---|---|
| `outbox.bin` | unsent reviews are lost (must not happen) | drain the outbox online before the flash when possible; otherwise copy to SD (step 1) |
| `journal.bin` | streak and history reset | copy (step 1) |
| `practice.bin` | local practice stats reset | copy (step 1) |
| `critical.bin` | critical-item list rebuilt only by a sync (verify) | copy (step 1) |
| `cache.bin` | Home/batch cache rebuilt by the next sync; offline use lost until then | copy (step 1), which is cheap at ≤256 KB |

**Outbox rule.** The USB flash never runs unless the migration marker is present
and the SD copy has been read back. If SD is missing or the copy fails, the
device shows *Insert SD to migrate* and the script refuses. SD is already needed
for the font pack, so this costs no new requirement.

### 5.5 Release and workflow changes

- `release.yml` builds the legacy env (as today) and, once the layout is adopted,
  the new env. Both build from the same commit; the legacy one must keep
  passing the slot check at 1,835,008 B.
- `tools/release/firmware_manifest.py` writes one `app` row per call. A second
  call for the new env must append a row to the same manifest, and it must check
  each image against its own partition CSV (`partition_table`). Add a test for
  two rows and for a duplicate env.
- `SHA256SUMS`, the attached assets and `fail_on_unmatched_files` list both
  images.
- `Version.h` does not change. The tag check stays.
- Do not change the PaperDIY table (16 MB) in this plan.

### 5.6 Docs to update when implemented

`docs/ota.md` (slots, env rows, the bridge and the migration), `docs/decisions.md`
(a new dated entry), `docs/hardware.md` (the hardware run), the header comment of
`firmware/partitions/kanikey_8MB.csv` (its "two 1.75 MiB slots" text is now
out of date), `docs/api-sync.md` (where the state files live), and this spec.
`ROADMAP.md` and `README.md` are not touched by this spike.

## 6. Risks

- **T1 parity on device.** The equivalence test runs against host glibc
  `sscanf`; newlib (on the device) may treat white space inside a `%Nd` width
  differently for unusual input. WaniKani timestamps are fixed-format, so the
  risk is limited to malformed input, and the hardware sync check (§7) covers
  the real format. Mitigation: keep the fixed-width test corpus.
- **Diagnostics.** The largest trim (T2, −38 KB) removes library error lines from
  release; it stays unapplied until the owner decides.
- **The USB flash can lose LittleFS silently** if the migration gate is skipped
  or the new area is not erased. Mitigation: the `COMMITTED` gate and the erase
  step in §5.4.
- **otadata pointing at a stale slot** after a table change: the rollback cannot
  help, because the bootloader cannot find a valid image. Mitigation: erase
  otadata in the flash script.
- **Legacy devices that never reflash** stay on the legacy layout and only get
  images that fit 1,835,008 B. Those images must be built from the legacy env
  for as long as such devices exist.
- **Format time.** First format of a 3.6 MB LittleFS may take seconds on the
  device; measure it before the boot screen is designed.
- **The figures are a build.** No number here is a hardware measurement; the
  heap and boot timings of the new layout are unknown.

## 7. Hardware checks (for the parent)

1. Flash the dev image; run `fs` after boot, after a sync, after several reviews,
   and after a forced offline period. Record total, used and each `/state` file.
2. Sync with the T1 build: Home shows the next review time and the due counts;
   the reminder and the review queue order use the parsed timestamps. A
   sync with a bad timestamp is not expected from WaniKani, so this checks the
   normal path only.
3. `fs` with LittleFS not mounted (format prompt) prints `not mounted`.
4. Bridge round trip (later, dev image first): export on the old table, check
   the SD copy's CRCs, USB-flash the new layout with the script, restore, and
   compare the outbox count, streak and critical list with the values before the
   flash.
5. Refusal paths: no SD card and a corrupted SD copy must both stop the USB flash
   script and must not erase LittleFS.
6. OTA on the legacy layout: a legacy-size image installs and rolls back as
   before (FIX-20 procedure). A larger image is refused with `too large`.
7. Time the first format and the first mount of the 3.6 MB LittleFS.
8. Heap after boot and during a sync must match the baseline (the RAM image is
   105,736 B static on release; T1 does not change it).

## 8. Reproduce

```
cd firmware
pio run -e cardputer-adv        # 1,702,561 B after T1 (clean build)
pio run -e cardputer-adv-dev    # 1,735,633 B
pio run -e paperdiy             # 1,730,945 B
pio test -e native              # 554 test cases
```

Map and symbol analysis: the espressif32 build writes
`.pio/build/<env>/firmware.map`; sum the loaded input sections of
`.flash.text`, `.flash.rodata`, `.iram0.text`, `.dram0.data`, `.iram0.vectors`
by archive and by `src/<dir>/`; use `xtensa-esp32s3-elf-nm --size-sort -C -S` for
the symbol list. The scripts used for this spike were throwaway and are not
committed.
