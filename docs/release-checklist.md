# Release checklist

The steps the owner runs to publish a firmware release, using v0.2.0 as the
example. Nothing here is automated except `release.yml`, which runs when a
`v*` tag is pushed. Run commands from the repository root unless a step says
otherwise.

## 1. Before tagging

1. **Release commit.** `main` is clean and at the commit to release:
   `git status --short` prints nothing and `git log -1 --oneline` shows it.
2. **Version.** `firmware/src/Version.h` has exactly one release literal, and it
   equals the tag without `v`:
   `grep -n 'kFirmwareVersion = ' firmware/src/Version.h` shows `"0.2.0"`.
3. **Manifest tests.** `python3 tools/release/test_firmware_manifest.py` ends in
   `OK`.
4. **CI green on main.** `gh run list --branch main --limit 6` shows the latest
   runs of the firmware and gitleaks workflows as `success` for this commit.
   Do not tag over a red run.
5. **Notes.** Read [docs/releases/v0.2.0.md](releases/v0.2.0.md) and check that
   each hardware caveat still matches [docs/hardware.md](hardware.md). Decide the
   release body before tagging (see the note below).

> **Release body.** `release.yml` publishes `docs/releases/<tag>.md` as the
> body (`body_path`), not an auto-generated commit list. The file for the tag
> must be on `main` before tagging, or the publish step fails.

## 2. Tag and push (owner)

6. **Tag the release commit** (annotated; the tag name is exactly `vX.Y.Z`):
   `git tag -a v0.2.0 -m "KaniKey v0.2.0" <release-commit>`
7. **Push only the tag:** `git push origin v0.2.0`. Do not push a branch with
   the tag in the same command.

## 3. Wait for the release workflow

8. Watch the `release` run: `gh run watch` (choose the run for `v0.2.0`), or
   open the Actions tab. The job has a 30-minute limit. It runs the native
   tests, the manifest tests, the glyph-pack build, the `cardputer-adv` build,
   the manifest builder (which refuses a tag that does not match `Version.h`)
   and the publish step.
9. If the run fails **before** the publish step, fix the cause on `main` and
   push a new tag only after the old tag is deleted by the owner
   (`git push --delete origin v0.2.0`, `git tag -d v0.2.0`). If the release is
   published with a bad image, do not move the tag: cut `v0.2.1` instead,
   because devices cache the offer.

## 4. Verify the published release

10. **Assets.** `gh release view v0.2.0 --json tagName,isDraft,isPrerelease,assets`
    shows `isDraft: false` and exactly these six assets:

    | Asset | Purpose |
    |---|---|
    | `kanikey-cardputer-adv.bin` | app-only ESP image |
    | `glyphs.kgp` | public font pack (Noto Sans JP, OFL; no WaniKani content) |
    | `kanikey-firmware.txt` | manifest the device reads |
    | `SHA256SUMS` | host-side checks |
    | `LICENSE` | notice |
    | `THIRD_PARTY_NOTICES.md` | notice |

11. **Checksums.** Download into a new empty directory outside the repository:
    `mkdir -p /tmp/kk-v020 && gh release download v0.2.0 -D /tmp/kk-v020`, then
    `cd /tmp/kk-v020 && shasum -a 256 -c SHA256SUMS`. Every line says `OK`.
12. **Manifest.** `cat /tmp/kk-v020/kanikey-firmware.txt` shows:
    - the header `#kanikey-firmware<TAB>1`;
    - `version<TAB>v0.2.0`;
    - an `app` row for `cardputer-adv` whose size equals the size of
      `kanikey-cardputer-adv.bin` (1,665,120 B for the dry run on this commit) and
      whose hash matches the file;
    - a `pack font glyphs.kgp` row with the size and hash of `glyphs.kgp`
      (9,358,845 B in the dry run).
13. **Latest URL.** The device reads the manifest through `releases/latest`:
    `curl -sIL https://github.com/dcarnicer/kanikey/releases/latest/download/kanikey-firmware.txt`
    ends in `200` and the redirect chain points at `v0.2.0`.

## 5. On-device check (owner)

The device checks the manifest only when it runs a version below the release
and the same `KANIKEY_ENV` (`cardputer-adv`). A device on v0.2.0 sees no offer.

> **Arduino-ESP32 3 break (2026-11-20).** The firmware moved from Arduino-ESP32
> 2.0.17 (ESP-IDF 4.4) to 3.3.11 (ESP-IDF 5.5.5), see `docs/decisions.md`. A
> device that runs a 2.0.17 image (v0.1.x, v0.2.0 and the older dev images) needs
> one USB flash of the new image. OTA from those images to this image is not
> supported (the app has no users yet). For steps 14 to 16, use a device that
> already runs a 3.x image: flash it over USB first, then check that it boots
> with its token, Wi-Fi and settings.

14. **v0.1.0 device (expected to fail the check).** Press `Esc` › **Settings** ›
    **Firmware** › **Check now**. It shows a failed check. That is the documented
    state (`docs/setup.md` §7.2), not a release defect. Move it with the USB
    path in the release notes, Option A or B.
15. **Post-v0.1.0 device (the real OTA test).** Use a device that runs an image
    below 0.2.0 with the `cardputer-adv` environment, on the 8 MB table. A
    `cardputer-adv-badapi` image reports 0.1.2, so it is below 0.2.0; it fails
    WaniKani requests, which the firmware check does not use. Flash it over USB,
    then **Check now**. Expect `upd v0.2.0` on the Firmware screen and in About.
16. **Install.** Choose **Install update** and confirm twice. Expect the image to
    stream into the spare slot, the size and SHA-256 checks to pass, and the
    device to restart. The new image is on probation until the first WaniKani
    2xx or ten minutes of `loop()`. Confirm `v0.2.0` after the restart and that
    the token, Wi-Fi and settings are still there.
17. **Afterwards** the device runs the release image. Flash `cardputer-adv-dev`
    over USB only when you need the dev console again; a dev image is never a
    release.

## 6. After publishing

18. Record the release in [ROADMAP.md](../ROADMAP.md) and the README status
    line. Keep the hardware status honest: list the checks that are still open.
19. Keep the release notes file (`docs/releases/v0.2.0.md`) as the record of
    what shipped. Add later releases as new files under `docs/releases/`.
