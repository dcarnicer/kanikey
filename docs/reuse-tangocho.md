# KaniKey — qué reutilizar de Tangocho

> Diseño, 2026-10-25. Tangocho se toma como base en
> `33a28f670b07e6c574d98a9c40550a7aac3bf537` (v0.7.0, 2026-10-24), licencia MIT.

---

## 1. Por qué

La Cardputer ADV ya está probada en hardware dentro de Tangocho:

- Pantalla, rotación fija horizontal y back buffer.
- Teclado TCA8418, eventos, repetición, letras y flechas.
- microSD FAT32, con sus pines, montaje y extracción en caliente.
- Wi-Fi, portal, TLS con CAs raíz, OTA y sueño profundo.
- Render de glifos 4bpp, bitmaps 1bpp, RLE, widgets y fuente de UI.
- Remote serie para pulsar teclas y leer la pantalla en pruebas.

Eso es exactamente la parte que no aporta valor diferencial a KaniKey y que
costaría semanas volver a depurar. El producto nuevo es el motor de repaso, la
API de WaniKani, el IME y las pantallas.

---

## 2. Estrategia recomendada: fork podado

1. Crear `kanikey` como fork de Tangocho en el commit indicado.
2. Mantener `upstream` como remoto para poder traer arreglos de hardware.
3. Podar en una rama `bootstrap`:
   - fuera `pack/`, `quiz/`, `srs/`, `catalog/`, `portal/` (salvo helpers),
     `app/` de repaso y mazos;
   - fuera `tools/pack-builder/` y los packs;
   - dentro `board/`, `render/`, `ui/`, `platform/`, `tools/screenshots/`,
     `tools/ui-font/`.
4. Renombrar namespace `tgc` → `kk` (o mantener `tgc` durante la transición).
5. Compilar `cardputer-adv` y ejecutar los tests nativos.
6. A partir de ahí, construir `wk/`, `store/` y las pantallas nuevas.

No se recomienda una librería compartida desde el día uno: añade versionado,
submódulos y coordinación entre dos proyectos para un pet project. Si Tangocho y
KaniKey siguen vivos, extraer un `cardputer-kit` más adelante es más fácil que
mantenerlo ahora.

---

## 3. Matriz de reutilización

| Módulo de Tangocho | Qué hace | Decisión | Cambios para KaniKey |
|---|---|---|---|
| `board/Board.h` | HAL: pantalla, botones, teclado, SD, batería, sueño | **Se queda** | Añadir eventos de texto + modificadores; G0 |
| `board/BoardCardputerAdv.cpp` | TCA8418, SD, deep sleep, batería | **Se queda** | Extender el mapeo de teclas a caracteres; nada más |
| `board/BoardM5.cpp` | Imagen única StickS3+Cardputer | **Se tira** | Solo Cardputer en v1 |
| `render/Renderer` | Glifos 4bpp, texto, caché | **Se adapta** | `GlyphSource` en vez de pack; mismas primitivas |
| `render/Rle` | Compresión de glifos | **Se queda** | Reutilizar en el pack visual |
| `render/Widgets` | Header, tiles, rows, pills, barras | **Se adapta** | Nuevos tokens y algún componente (input, feedback) |
| `render/Blend565`, `GlcdCp437` | Mezcla y mapa CP437 | **Se queda** | Sin cambios |
| `ui/Theme.h` | Tokens de Tangocho | **Se reescribe** | Paleta y tipografía de KaniKey |
| `ui/ListView.h` | Foco, ventana, scroll | **Se queda** | Sin cambios |
| `ui/HintBar.h` | Barra de pistas y keycaps | **Se queda** | Nuevos textos/teclas |
| `ui/KeyInput.h` | Mapa TCA8418 → acciones | **Se amplía** | Capas normal/Shift/Fn, caracteres, modificadores |
| `ui/TextLayout.h` | Wrap y paginación | **Se queda** | Reutilizar en mnemónicos |
| `ui/UiFace.h`, `UiFont.h` | Fuente de UI pre-renderizada | **Se adapta** | Añadir 10/14/20 px y glifos necesarios |
| `ui/Icons.h` | Iconos 1bpp | **Se adapta** | Check, cross, wifi, sync, SD, llave |
| `app/Display` | Canvas, present, rotación | **Se queda** | Sin rotación; partial push para el input |
| `app/ScreenKit` | Header, hints, listas, errores | **Se adapta** | Nuevo layout 240×135 y textos |
| `app/Input` | Hold, teclado, keyAction | **Se reescribe** | Modo texto vs navegación |
| `app/Power` | Dim, idle, sleep, batería | **Se queda** | Ajustar tiempos y guardar outbox antes de dormir |
| `app/Clock` | Fecha/hora, SNTP | **Se adapta** | UTC/ISO 8601, `clock_trusted` |
| `app/Wifi`, `WifiMenu` | Wi-Fi y portal | **Se adapta** | Portal solo de setup; WPA2 |
| `app/Net` | Sesiones de red, heap | **Se adapta** | `WkClient` en vez de catálogo de mazos |
| `app/Firmware` | OTA | **Se queda** | Cambiar manifiesto/URL |
| `app/Remote` | Remote serie para capturas | **Se queda** | Añadir teclas de texto |
| `platform/Storage` | LittleFS + SD, rutas | **Se adapta** | Rutas `/kanikey/...`; asignaciones/outbox |
| `platform/HomeWifi` | Wi-Fi de casa en NVS | **Se queda** | Sin cambios |
| `platform/NetFetch` | HTTP/TLS, streaming a sink | **Se adapta** | Cliente API con streaming JSON |
| `platform/WifiPortal` | Portal HTTP | **Se adapta** | Setup: Wi-Fi + token + hora; WPA2 |
| `platform/Ota` | OTA con manifiesto | **Se queda** | Manifiesto de KaniKey |
| `platform/RootCerts` | CAs raíz | **Se queda** | Añadir las de WaniKani si faltan |
| `platform/Psram` | Detección de PSRAM | **Se queda** | Cardputer no tiene; no asumir |
| `pack/` | Formato `.tgc` | **Se tira** | Solo se reutiliza la idea de tablas + RLE |
| `quiz/` | Preguntas de opción múltiple | **Se tira** | Respuesta escrita |
| `srs/` | FSRS local | **Se tira** | WaniKani manda; opcional para práctica extra |
| `catalog/` | Catálogo de mazos | **Se tira** | No aplica |
| `portal/` | Lógica del portal de mazos | **Se tira** | El portal de KaniKey es de setup |
| `tools/screenshots/` | Capturas por serie | **Se queda** | Adaptar escenarios |
| `tools/ui-font/` | Generador de UiFont | **Se queda** | Añadir tamaños/glifos |
| `tools/pack-builder/` | KANJIDIC2, packs | **Se tira** | Nuevo `tools/glyph-pack/` |

---

## 4. Qué no reutilizar (y por qué)

- **El formato de pack `.tgc`.** Está pensado para cartas de kanji con
  significados/lecturas y distractores. WaniKani tiene radicales, vocabulario,
  kana, mnemónicos, componentes y asignaciones. Forzarlo sería peor que un
  formato nuevo.
- **El quiz de opción múltiple.** La gracia de la Cardputer es escribir. Un test
  de opciones sería una versión peor de Tangocho.
- **FSRS.** WaniKani ya tiene su SRS; llevar dos schedulers crea divergencia.
  Si algún día hay “práctica extra”, puede ser un módulo separado que no envíe
  nada a WaniKani.
- **El catálogo de mazos y su portal.** No hay mazos; hay una cuenta.
- **La imagen única StickS3+Cardputer.** Añade ramas y pruebas; la v1 es solo
  Cardputer.

---

## 5. Lecciones de hardware que ya están pagadas

Números medidos en Tangocho (Cardputer ADV, sin PSRAM):

| Cosa | Dato |
|---|---|
| Canvas 240×135×2 | 64,8 KB |
| Heap libre al arrancar | ~210 KB |
| TLS handshake | ~50 KB; mínimo visto ~60 KB libres con canvas y pack |
| SD montada | ~13–15 KB de RAM (FatFs, buffers) |
| Frame completo | 64,8 KB ≈ 13 ms por SPI a 40 MHz |
| Pines SD | SCK 40, MISO 39, MOSI 14, CS 12; `otherCs` 5 para la Cap LoRa |
| Teclado | TCA8418 en I²C interno, INT en G11 |
| Pantalla | rotación 1 fija, teclado debajo |
| Batería | G10, divisor 1:2 |
| Sueño | despierta con G0 o cualquier tecla (EXT1 en G11) |
| Reloj | sin RTC con batería; SNTP o portal |

KaniKey tiene que respetar esas cifras. En particular:

- No abrir un pack de Tangocho; pero el canvas y TLS siguen ahí.
- El parser JSON y el almacén no pueden parecerse a un DOM.
- La caché de glifos debe caber en ~16 KB.
- La sincronización debe liberar caché antes de TLS.
- El sueño debe guardar la outbox y las asignaciones.

---

## 6. Kakehashi como inspiración

Kakehashi es una app iOS/Android de WaniKani, MIT, con una implementación muy
completa. No se puede reutilizar su código React Native en el firmware, pero sí
sus decisiones de producto:

- **Respuesta escrita** con conversión de romaji a kana y edición de texto.
- **Answer checker** con significados aceptados, sinónimos, blacklist/whitelist,
  lecturas primarias vs válidas, okurigana y avisos de “n” antes de vocal.
- **Sesión estable:** se congela la lista al empezar; los repasos nuevos no
  aparecen a mitad.
- **Guardar antes de avanzar:** el resultado se persiste antes de la siguiente
  pregunta; la cola se sincroniza después.
- **Lecciones** con ficha, mnemónico y componentes.
- **Indicador de etapa SRS** después de completar un ítem.
- **Estados offline** honestos y reintentos acotados.

Si se copia código concreto (por ejemplo, la tabla de normalización o los tests
del checker), hay que conservar el aviso MIT de Pedro Ortego. La UI y el arte no
se copian.

---

## 7. Licencias y avisos

- **Tangocho:** MIT, Copyright (c) 2026 Tamagotcho contributors. El fork debe
  mantener `LICENSE` y el aviso en los archivos copiados.
- **Kakehashi:** MIT, Copyright (c) 2026 Pedro Ortego. Añadir aviso si se copia
  código; si solo se usan ideas, mencionarlo en `THIRD_PARTY.md`.
- **Noto Sans JP:** SIL OFL 1.1. Incluir la licencia y el aviso al distribuir el
  pack visual.
- **KanjiVG:** CC BY-SA 3.0, solo si se añade orden de trazos.
- **WaniKani:** contenido © Tofugu LLC; no se redistribuye. El pack visual se
  genera con el token del usuario y se queda en su tarjeta.

---

## 8. Esfuerzo relativo

| Bloque | Esfuerzo | Nota |
|---|---|---|
| Fork + poda + compilar | S | Sobre todo borrar y renombrar |
| Adaptar render a `GlyphSource` | M | Mantener las primitivas |
| Teclado con texto/capas | M | TCA8418 ya funciona; falta el mapa de caracteres |
| `wk/` + `store/` + tests | L | La parte más técnica |
| `WkClient` + JSON streaming | L | Memoria y TLS |
| Pantallas de repaso | M | La UX ya está diseñada |
| Lecciones | M | Texto paginado y `start` |
| Outbox/reconciliación | M | Cuidado con duplicados |
| Pack visual (Python) | M | Fuente, SVG, RLE |
| Pulido/OTA/docs | M | Se puede iterar |

No son compromisos de calendario; es una forma de ver dónde está el riesgo. La
primera semana de firmware debería ser un `GET /user` en la Cardputer con TLS y
medición de heap, antes de escribir pantallas.

---

## 9. Riesgos del fork

| Riesgo | Mitigación |
|---|---|
| Arrastrar código de mazos sin querer | Poda agresiva; compilar y borrar warnings |
| Divergencia con Tangocho | Mantener `upstream`; cherry-pick de arreglos de HAL |
| Namespace y nombres confusos | Renombrar `tgc` a `kk` en una rama dedicada |
| Tests de Tangocho que ya no aplican | Conservar los de render/UI; borrar los de packs/quiz/SRS |
| Licencia | Mantener MIT y avisos desde el primer commit |
