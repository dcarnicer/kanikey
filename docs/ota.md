# Firmware updates over the air

KaniKey updates itself from a GitHub release over HTTPS. The device has two
1.75 MiB app slots (`app0`, `app1`) and an `otadata` record; one slot runs and
the other receives the new image. Settings and Wi-Fi live in NVS and survive
the update. `LittleFS` stays on its partition.

## Release assets

Every `vX.Y.Z` tag publishes:

| Asset | Purpose |
|---|---|
| `kanikey-cardputer-adv.bin` | app-only ESP image (magic `0xE9`) |
| `kanikey-firmware.txt` | manifest read by the device |
| `SHA256SUMS` | host-side checks |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | notices |

The manifest is a tab-separated UTF-8 text file, at most 8 KiB:

```
#kanikey-firmware	1
version	v0.1.0
app	cardputer-adv	kanikey-cardputer-adv.bin	1461584	<sha256 hex>
```

- Line 1 is the header and format version. A reader of version 1 refuses any
  other version or a missing header.
- `version` appears once, before the first `app`, as `vMAJOR.MINOR.PATCH`.
- `app` is `app`, the PlatformIO environment (`[a-z0-9-]{1,31}`), the image
  file name (or an absolute `https://` URL), the exact size and the SHA-256.
- Relative file names resolve against the manifest URL's directory.
- Blank lines and `#` lines are ignored; unknown record types are ignored for
  forward compatibility. A malformed known record rejects the whole manifest.

The device reads the latest release:

```
https://github.com/dcarnicer/kanikey/releases/latest/download/kanikey-firmware.txt
```

A development build may point at another manifest with the serial `U <url>`
command; `http://` is accepted only in a `KANIKEY_DEV` build, for a local test
server. Release builds require HTTPS.

## Device flow

1. The device checks the manifest after a successful sync at most once a day,
   or when **Settings › Firmware › Check now** is used.
2. A newer version for the running `KANIKEY_ENV` is shown in Settings and in
   About (`upd vX.Y.Z`). The offer is cached in NVS so it survives a reboot.
3. **Install** (two Enters) saves the cache and outbox, streams the image into
   `Update`, checks the exact size and SHA-256 while it writes, then calls
   `Update.end()` and switches the boot slot. The device restarts.
4. The new image boots in `PENDING_VERIFY`. After 10 seconds of `loop()` it is
   marked valid; a crash, watchdog, reset or power cut before that makes the
   bootloader return to the previous slot. A deep sleep before the 10 seconds
   also marks it valid, because the device has already run its setup and is
   about to leave the loop normally.
5. After a rollback, the failed version is recorded in NVS and is never offered
   again. The next boot is the old image, with settings and progress intact.

The install needs enough contiguous heap for TLS. The review batch is evicted
while the user is on Home and during lessons, and the UI canvas is released for
the download, so a 1.75 MiB slot has comfortable headroom (the release image is
about 1.46 MB).

## Trust and safety

- The manifest and the image come from GitHub over HTTPS with the embedded root
  CAs in `platform/RootCerts.h`; certificate verification is never disabled.
- The size and SHA-256 from the manifest are enforced before the boot slot
  changes. `Update` also verifies the ESP image checksum and writes the image
  header last, so a cut download cannot boot.
- Only `app0`/`app1` of the two-slot table are accepted; a single-slot or
  launcher layout reports "No update slot" and stays on USB updates.
- A release build ignores the serial manifest override. The token, Wi-Fi
  credentials and cached WaniKani content are never part of an update.

## API and branding review

- The product name is KaniKey; "WaniKani" is not part of the app name or icon.
- About says "unofficial" and `README.md`/`THIRD_PARTY_NOTICES.md` carry the
  full disclaimer.
- Requests send `User-Agent: KaniKey/<version> (unofficial WaniKani client)` and
  `Wanikani-Revision: 20170710`; the shared 429/401 gates respect the API's
  rate-limit and auth headers.
- Release assets contain only KaniKey code and notices: no subjects, mnemonics,
  radical images, audio or cached user data. The token stays in NVS and is never
  logged or included in an update.

## Release process

1. Bump `firmware/src/Version.h` and commit.
2. Tag the commit `vX.Y.Z` and push the tag.
3. `.github/workflows/release.yml` runs the native tests, builds
   `cardputer-adv`, writes the manifest with
   `tools/release/firmware_manifest.py` (which checks the tag against
   `Version.h` and refuses an image larger than its slot) and publishes the
   release.

The first OTA-capable image must be installed over USB. Every later release
can arrive over the air. The partition table already has the two slots, so no
data migration is needed for KaniKey.
