# Third-party notices

KaniKey is an unofficial third-party app for WaniKani. It is not affiliated
with, endorsed by or sponsored by WaniKani or Tofugu LLC. WaniKani is a
registered trademark of Tofugu LLC.

KaniKey's own source is released under the MIT License; see [LICENSE](LICENSE).
The components below keep their own licenses and notices. No WaniKani subject
content (meanings, readings, mnemonics, radical images, audio or context
sentences) is distributed in this repository. The visual pack and the caches
are generated locally with the user's own token and stay on the user's card.

## Tangocho — MIT

Parts of the firmware are adapted from
[Tangocho](https://github.com/dcarnicer/tamagotcho): the Cardputer ADV board
HAL, the TCA8418 keyboard mapping, the root CA bundle, the 4bpp blend helper
and the OTA/rollback pattern. Tangocho is distributed under the MIT License:

```
MIT License

Copyright (c) 2026 Tamagotcho contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Kakehashi — MIT (product inspiration)

Product and interaction ideas are inspired by
[Kakehashi](https://github.com/Portego-00/Kakehashi), Copyright (c) 2026 Pedro
Ortego, MIT License. No Kakehashi source code is included; if code is copied
later, the MIT notice above applies with Pedro Ortego's copyright.

## M5Unified and M5GFX — MIT

The firmware builds against
[M5Unified and M5GFX](https://github.com/m5stack/M5Unified), fetched by
PlatformIO. They are MIT-licensed and are not vendored in this repository.

## Arduino-ESP32 core — LGPL-2.1

The firmware is statically linked with the
[Arduino-ESP32 core](https://github.com/espressif/arduino-esp32), licensed
under the GNU Lesser General Public License 2.1. The core and its build
scripts are fetched by PlatformIO; the corresponding source is available from
the upstream project and can be rebuilt with `firmware/platformio.ini`.

## ESP-IDF and bundled components

The Arduino core uses [ESP-IDF](https://github.com/espressif/esp-idf)
(Apache License 2.0) and its components, including:

- **FreeRTOS** — MIT.
- **mbedTLS** — Apache License 2.0 (used by `WiFiClientSecure` for HTTPS).
- **LittleFS** — BSD-3-Clause (the `littlefs` data partition).
- **ESP HTTPS OTA / Update** — Apache License 2.0.

These components are fetched by PlatformIO and are not vendored. Their
license texts live in the PlatformIO package tree and in the upstream
repositories.

## Noto Sans JP — SIL Open Font License 1.1

The visual-pack generator can rasterize **Noto Sans JP** (SIL Open Font
License 1.1). The font is not distributed in this repository; the generated
glyph pack is personal and is not committed. The mockups use the same font.

- https://fonts.google.com/noto/specimen/Noto+Sans+JP
- https://openfontlicense.org/

## KanjiVG — CC BY-SA 3.0 (planned, not included)

Stroke-order data, if added in a later phase, comes from
[KanjiVG](https://kanjivg.tagaini.net/), licensed CC BY-SA 3.0. Attribution
will be included with the feature.

## WaniKani API and content

The WaniKani API is documented at https://docs.api.wanikani.com/20170710/.
WaniKani subject data, mnemonics, radical images, audio and context sentences
are © Tofugu LLC and are subject to the WaniKani terms of service. KaniKey
does not redistribute them: the user's own token is used to cache them locally
for personal study, and the generated visual pack stays on the user's SD card.

The app name does not include "WaniKani"; the UI identifies KaniKey as
unofficial; API requests identify the client with a KaniKey user agent; and
the client respects the API's rate-limit headers.
