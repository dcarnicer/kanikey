# KaniKey — diseño de producto y arquitectura

> **Estado:** diseño, 2026-10-25. No hay firmware todavía.
>
> **Nombre de trabajo:** KaniKey. Es provisional; el diseño no depende de él.
>
> KaniKey es una aplicación **no oficial** de terceros para WaniKani. No está
> afiliada, respaldada ni patrocinada por WaniKani ni por Tofugu LLC. WaniKani es
> una marca registrada de Tofugu LLC.

---

## 1. Resumen

KaniKey es una terminal de repaso de bolsillo para WaniKani en la **M5Stack
Cardputer ADV**. La idea es sencilla: abrir el aparato, ver los repasos que
tocan, escribirlos con el teclado y cerrarlo. Sin teléfono, sin pestañas, sin
notificaciones.

La Cardputer ADV es especial porque tiene un teclado real. WaniKani no se repasa
eligiendo entre cuatro opciones: se escribe el significado en inglés y la lectura
en kana. Eso hace que una consola de dos botones no sea el sitio natural, y que
un teclado de 56 teclas sí lo sea.

KaniKey **no sustituye a WaniKani**. El SRS, las asignaciones y el progreso
siguen viviendo en la cuenta del usuario. El aparato es una vista local, una
cola de repasos y un sitio agradable donde escribir. Cuando hay red, sincroniza;
cuando no, permite repasar con la caché y deja los resultados en una cola.

| Decisión | Propuesta |
|---|---|
| Entrada | Respuesta escrita, no test de opciones |
| SRS | El de WaniKani; KaniKey no reprograma |
| Red | Sincronización directa contra la API v2, sin servidor propio |
| Offline | Caché de sujetos/asignaciones + cola de repasos |
| Contenido | Nada de WaniKani en el repo; token y caché del usuario |
| Dibujo | Pack de glifos pre-renderizado en la microSD |
| Base | Fork podado de Tangocho (HAL, render, UI, Wi-Fi, OTA) |
| Alcance v1 | Repasos; lecciones en la fase 2 |
| Placa | Solo Cardputer ADV |

---

## 2. Objetivos y no objetivos

### 2.1 Objetivos

1. **Repasar rápido.** Del bolsillo al primer ítem en menos de 15 segundos
   (con Wi-Fi ya configurada y una caché caliente).
2. **Fidelidad a WaniKani.** Mismos significados aceptados, lecturas, tipos de
   sujeto, contadores de fallos y semántica de envío.
3. **Teclado primero.** Escribir es la interacción principal; nada de menús
   escondidos para introducir texto.
4. **Offline honesto.** La pantalla dice si hay red, cuándo se sincronizó y
   cuántos repasos quedan por enviar. Nunca finge que un repaso está enviado.
5. **Legibilidad en 240×135.** Un solo foco por pantalla; tipografía y espaciado
   medidos, no “lo que quepa”.
6. **Mantenible.** Lógica pura testeable en el host; pantallas separadas; sin
   meter la API y el dibujo en `main.cpp`.
7. **Respeto legal y de marca.** Sin contenido de WaniKani en el repositorio, sin
   su arte, sin su UI y sin usar su nombre como nombre del producto.

### 2.2 No objetivos

- Copiar la web o la app oficial de WaniKani.
- Implementar un SRS local para los repasos (FSRS u otro). La programación es
  del servidor. Un modo de práctica extra podría usar FSRS más adelante, pero no
  es el producto.
- Comunidad, foro, reseñas, notificaciones push, app móvil, web.
- Audio de pronunciación, orden de trazos o dibujo de kanji en la v1.
- Soportar StickS3, T-Display ni otras placas en la v1. El HAL lo permitiría,
  pero cada placa añade pruebas y compromisos de layout.
- Multicuenta o perfiles locales.

---

## 3. Por qué la Cardputer ADV

| Restricción | Implicación de diseño |
|---|---|
| 240×135, solo horizontal | Una sola columna o dos zonas; nada de tablas densas |
| Sin PSRAM | Nada de documentos JSON completos, fuentes CJK en RAM ni cachés grandes |
| 8 MB flash, ~1,75 MB por slot OTA | El contenido pesado (glifos, sujetos) va a la microSD |
| Teclado 4×14 con Fn/Shift | Texto real; los atajos deben ser visibles y no robar letras |
| TCA8418 + G0 | Hay que distinguir modo navegación y modo texto |
| Wi-Fi + TLS en ~50 KB | Sincronización secuencial, respuestas en streaming |
| Batería + deep sleep | La radio solo se enciende para sincronizar; el repaso es local |
| MicroSD FAT32 | Caché de contenido y pack visual; progreso crítico en LittleFS |

La alternativa “app móvil” ya existe y funciona (Kakehashi). Lo que aporta
KaniKey es un aparato dedicado, sin distracciones, con teclado y batería. La
alternativa “StickS3” obliga a test de opciones o a un teclado externo; pierde
la gracia.

---

## 4. Usuario y casos de uso

### 4.1 Usuario típico

Alguien que ya usa WaniKani, va por el nivel 5–30, le gustan los cacharros y
quiere repasar en momentos muertos: el sofá, el transporte, una pausa en el
trabajo. No quiere mirar el móvil para “solo 20 repasos” porque acaba en otra
cosa.

### 4.2 Viajes principales

**A. Repaso diario con red**

1. Enciende la Cardputer (o despierta del sueño).
2. KaniKey muestra 28 repasos y 5 lecciones; sincroniza en segundo plano si hace
   más de 15 minutos.
3. `Enter` → sesión. Escribe el significado, luego la lectura.
4. Al completar cada ítem, lo envía a WaniKani. Si falla, lo deja en cola.
5. Al terminar: resumen, siguiente hora de repaso y “todo al día”.

**B. Repaso sin red**

1. La caché tiene los ítems que tocaban y sus sujetos.
2. La pantalla muestra `Offline · 18 cached · 4 to sync`.
3. Repasa igual; los repasos completados se guardan en la cola local.
4. Al volver la red, sincroniza la cola y actualiza las asignaciones.

**C. Lecciones**

1. `L` → lista de lecciones disponibles, ordenadas por nivel y `lesson_position`.
2. Ficha de lección: carácter, significados, lecturas, mnemónico y componentes.
3. `Enter` → `PUT /assignments/{id}/start`; la asignación pasa a la cola de
   repaso.
4. Si no hay red, se puede estudiar la ficha, pero no empezar la lección (v1).

**D. Primera configuración**

1. Wi-Fi: la Cardputer levanta una red WPA2 temporal con una clave en pantalla.
2. El móvil abre el portal, guarda la Wi-Fi y pega el token de WaniKani.
3. El portal valida `GET /user`, muestra usuario/nivel y cierra la red.
4. La Cardputer sincroniza y comprueba el pack visual de la microSD.
5. Si falta el pack, muestra cómo generarlo en el ordenador y copiarlo.

---

## 5. Alcance

### 5.1 MVP (v0.1–v0.3)

| Área | Debe estar |
|---|---|
| Arranque | HAL, pantalla, teclado, microSD, batería, sueño |
| Configuración | Wi-Fi + token + hora; portal temporal |
| Inicio | Nivel, usuario, repasos, lecciones, siguiente repaso, distribución SRS |
| Sincronización | `user`, `summary`, `assignments`, `subjects`, `study_materials` |
| Repaso | Significado y lectura escritos; romaji→kana; feedback; reencolado |
| Envío | `POST /reviews` con contadores de fallos |
| Caché | Sujetos y asignaciones en SD/LittleFS; arranque sin red |
| Offline | Outbox persistente y envío al volver la red |
| Errores | Token inválido, sin red, sin SD, pack visual ausente, 429/5xx |
| Ajustes | Cuenta, estudio, sincronización, dispositivo, acerca de |

### 5.2 Después del MVP (v0.4)

- Reconciliación avanzada de envíos ambiguos y conflictos entre dispositivos.
- Lecciones con mnemónico y `start`.
- Estadísticas de sesión y precisión.
- OTA con manifiesto por HTTPS y SHA-256 (reutilizando Tangocho).
- Pantalla de sincronización con progreso y cancelación.
- UI en español (las respuestas siguen siendo en inglés).

### 5.3 Más adelante

- Audio de pronunciación (ES8311).
- Orden de trazos con KanjiVG.
- Práctica extra con FSRS local (no afecta a WaniKani).
- Sinónimos de usuario y notas.
- Previsión de repasos, heatmap, racha.
- Modo test de opciones para repasar sin teclado.
- Varias cuentas.
- Fuente/pack visual generado desde el móvil, sin ordenador.

### 5.4 Fuera de alcance

- Foro, comunidad, reseñas, notificaciones push.
- Copia de la UI de WaniKani.
- Contenido de WaniKani distribuido con el firmware.
- Compatibilidad con otras placas en la v1.

---

## 6. Principios de producto

1. **Teclado primero.** Si una acción necesita texto, se escribe. Los atajos se
   muestran en la barra inferior, no se esconden.
2. **El servidor manda.** WaniKani decide cuándo toca repasar y qué etapa SRS
   tiene cada ítem. KaniKey no inventa intervalos.
3. **Offline es un estado, no un error.** Se muestra con claridad, se permite
   repasar y se dice qué queda pendiente de enviar.
4. **Una decisión por pantalla.** El ítem, la respuesta, el resultado y la
   acción siguiente. Nada de menús laterales durante el repaso.
5. **Contenido fuera del repo.** El firmware no lleva significados, lecturas,
   mnemónicos ni arte de WaniKani. El pack visual se genera con el token del
   usuario y se queda en su tarjeta.
6. **Reutilizar el hardware, reescribir el producto.** El HAL, el render, la UI
   base, Wi-Fi y OTA vienen de Tangocho. El motor de repaso es nuevo.
7. **Medir en hardware desde el primer día.** Sin PSRAM, la memoria es una
   función del producto, no una optimización posterior.

---

## 7. Flujo principal

```
                         ┌─────────────┐
                         │    Boot     │
                         └──────┬──────┘
                                │
                 ┌──────────────┴──────────────┐
                 │                             │
          sin configurar                  configurado
                 │                             │
          ┌──────▼──────┐              ┌───────▼────────┐
          │   Setup     │              │     Home       │
          │ Wi-Fi/token │              │ due / lessons  │
          │  /hora      │              │ next / sync    │
          └──────┬──────┘              └───┬───┬───┬────┘
                 │                         │   │   │
                 └──────────────►          │   │   └──── Tab ──► Settings
                                      R    │   │ L
                                      ┌────▼───┴────┐
                                      │   Sync      │
                                      │ (si hace    │
                                      │  falta)     │
                                      └──────┬──────┘
                                             │
                                      ┌──────▼──────┐
                                      │  Reviews    │
                                      │  queue      │
                                      └──────┬──────┘
                                             │
                               ┌─────────────┴─────────────┐
                               │                           │
                        significado                   lectura
                               │                           │
                               └──────────┬────────────────┘
                                          │
                                   ¿correcto?
                                    │        │
                                   sí       no
                                    │        │
                                    │   feedback + reencolar
                                    │        │
                                    └──► completar ──► POST /reviews
                                                          │
                                                    ¿red? │
                                                     sí   │   no
                                                          │
                                                   actualizar   outbox
                                                   assignment
                                                          │
                                                          ▼
                                                    Session summary
```

**Estados de un ítem de repaso**

```
pendiente_significado ──acierto──► pendiente_lectura ──acierto──► completado
        │                                  │
        └──fallo──► feedback ──► reencolar ┘
```

- Un fallo incrementa `incorrect_meaning_answers` o
  `incorrect_reading_answers`, muestra la respuesta correcta y devuelve el ítem
  a la cola unas posiciones más tarde.
- El ítem se envía **una sola vez**, cuando todas sus partes están correctas.
- Si el usuario sale a mitad, los ítems no completados no se envían; siguen
  pendientes en WaniKani.

---

## 8. Pantallas

El detalle está en [ux.md](ux.md). Mapa:

| Pantalla | Propósito | Teclas | Fase |
|---|---|---|---|
| Boot | Arranque, montaje, comprobaciones | — | MVP |
| Setup | Wi-Fi, token, hora, primer sync | Escribir, Enter, Esc | MVP |
| Home | Resumen y puerta a todo | Enter, L, S, Tab | MVP |
| Sync | Progreso de la sincronización | Esc | MVP |
| Review | Pregunta y respuesta escrita | Letras, Enter, Esc, Tab | MVP |
| Feedback | Acierto/fallo, respuesta correcta, pista | Enter, Tab | MVP |
| Item detail | Mnemónico, componentes, notas | ↑↓, Space, Esc | MVP |
| Session summary | Resultado y siguiente paso | Enter, R | MVP |
| Lessons | Lista de lecciones disponibles | ↑↓, Enter, Esc | Fase 2 |
| Lesson card | Enseñanza antes de empezar | Space, ←→, Enter | Fase 2 |
| Settings | Cuenta, estudio, sync, dispositivo | ↑↓, Enter, Esc | MVP |
| Stats | Nivel, distribución, precisión | Esc | Fase 2 |
| Error / empty | Qué pasó y qué hacer | Enter, Esc | MVP |

![Home](screens/home.png)
![Repaso de significado](screens/review-meaning.png)
![Feedback correcto](screens/review-correct.png)

---

## 9. Arquitectura

```
                 WaniKani API v2 (HTTPS)
                          │
              ┌───────────▼───────────┐
              │  platform/WkClient    │  HTTP, TLS, streaming JSON
              │  (Arduino/ESP32)      │  rate limit, ETag, reintentos
              └───────────┬───────────┘
                          │ eventos / registros
              ┌───────────▼───────────┐
              │  wk/ (C++ puro)       │  modelos, answer checker,
              │                       │  romaji→kana, cola, sync
              └───────────┬───────────┘
                          │
              ┌───────────▼───────────┐
              │  store/ (C++ puro)    │  subjects, assignments,
              │                       │  outbox, meta, stats
              └───────────┬───────────┘
                          │
              ┌───────────▼───────────┐
              │  app/ (pantallas)     │  Home, Review, Lesson, Sync...
              └───────────┬───────────┘
                          │
      ┌───────────────────┼───────────────────┐
      │                   │                   │
┌─────▼─────┐      ┌──────▼──────┐     ┌──────▼──────┐
│ board/    │      │ render/     │     │ ui/         │
│ HAL       │      │ glifos      │     │ tema, lista │
│ Cardputer │      │ widgets     │     │ teclado     │
└───────────┘      └──────┬──────┘     └─────────────┘
                          │
                    ┌─────▼─────┐
                    │ microSD   │  glyph pack + subjects
                    │ LittleFS  │  assignments + outbox
                    └───────────┘
```

### 9.1 Módulos

| Módulo | Responsabilidad | ¿Arduino? |
|---|---|---|
| `board/` | Pantalla, teclado TCA8418, botones, batería, SD, sueño | Sí |
| `render/` | Glifos 4bpp, bitmaps 1bpp, texto, RLE, widgets | Sí (dibujo) |
| `ui/` | Tema, layout, ListView, HintBar, KeyInput, textos | Parte pura |
| `app/` | Pantallas, boot, power, Wi-Fi, ajustes, portal | Sí |
| `wk/` | Modelos, answer checker, IME, cola, sync, tiempo | **No** |
| `store/` | Formatos de registro, índices, escritura atómica | **No** |
| `platform/` | HTTP/TLS, Wi-Fi, SD, NVS, hora, OTA | Sí |
| `tools/glyph-pack/` | Generador del pack visual (Python) | — |
| `tools/screenshots/` | Remote serie para capturas | — |

Regla: `wk/`, `store/` y la lógica de `ui/` se compilan y testean en el entorno
`native` de PlatformIO sin cabeceras de Arduino. Es la misma disciplina que
Tangocho.

### 9.2 Reparto de estado

| Estado | Dónde | Por qué |
|---|---|---|
| Token, Wi-Fi, preferencias | NVS | Pequeño, persistente, no depende de la SD |
| Asignaciones | LittleFS | Crítico para saber qué toca; sobrevive a una SD fuera |
| Outbox | LittleFS | Crítico; se escribe antes de avanzar de ítem |
| Sujetos (significados/lecturas) | microSD | Grande; se puede volver a descargar |
| Pack visual (glifos/radicales) | microSD | Grande; generado por el usuario |
| Estadísticas locales | LittleFS | Pequeñas; se pueden reconstruir |
| Caché HTTP (ETag/fechas) | LittleFS | Pequeña; evita descargas |

Si se saca la tarjeta durante una sesión, KaniKey pausa y pide insertarla. Las
asignaciones y la cola siguen en LittleFS; no se pierde el progreso.

---

## 10. Reutilización de Tangocho

La recomendación es **forkear Tangocho y podar**, no empezar de cero ni mantener
una librería compartida desde el primer día. Tangocho ya tiene resuelto lo más
caro y específico del hardware:

- HAL de la Cardputer ADV con TCA8418, teclado, SD, batería y deep sleep.
- Render de glifos 4bpp y bitmaps 1bpp, con RLE.
- Tema, widgets, listas, hint bar y fuente de UI.
- Wi-Fi, portal, TLS con CAs, OTA, almacenamiento atómico.
- Remote serie para capturas y pruebas.
- Tests nativos y CI.

Lo que se tira: el formato de packs de kanji, el quiz de opción múltiple, el SRS
FSRS, el catálogo de mazos y las pantallas de Tangocho. Lo que se adapta: el
render para leer de un `GlyphSource` en vez de un pack, el portal para el setup,
y el almacenamiento para las rutas de KaniKey.

El detalle está en [reuse-tangocho.md](reuse-tangocho.md).

---

## 11. Contenido y dibujo

### 11.1 El problema

WaniKani mezcla cuatro tipos de sujeto:

- **Radicales:** a veces tienen un carácter Unicode; a veces solo una imagen SVG.
- **Kanji:** un carácter.
- **Vocabulario:** una palabra de varios caracteres.
- **Kana vocabulary:** solo kana.

La Cardputer no tiene PSRAM y el slot de aplicación es de ~1,75 MB. Una fuente
CJK completa no cabe; rasterizar en el dispositivo es posible pero caro. Por
otro lado, el contenido de WaniKani no se puede distribuir con el firmware.

### 11.2 Decisión propuesta: pack visual en la microSD

Un pequeño programa de escritorio, `kanikey-glyphs`, genera un archivo
`glyphs.kgp` para la tarjeta:

1. Pide a la API los sujetos hasta `subscription.max_level_granted` (60 en una
   cuenta de pago, 3 en una gratuita) usando el token del usuario.
2. Reúne todos los caracteres únicos de kanji, vocabulario y kana.
3. Renderiza cada carácter con **Noto Sans JP** (OFL) a 64, 40 y 20 px, en 4bpp
   con RLE.
4. Descarga las imágenes SVG de los radicales y las rasteriza localmente a
   96 px (4bpp o 1bpp).
5. Escribe un índice ordenado por codepoint / `subject_id` y el pack en la
   tarjeta.

El pack no se commitea ni se redistribuye: se genera con el token del usuario,
en su ordenador, y se queda en su tarjeta. El firmware solo sabe leer el formato.

**Tamaño estimado:** ~2.200 kanji únicos + kana + ~500 radicales.

| Parte | Tamaño aproximado |
|---|---|
| Glifos 64 px 4bpp | 4–5 MB |
| Glifos 40 px 4bpp | 1,5–2 MB |
| Glifos 20 px 4bpp | 0,5–0,7 MB |
| Radicales 96 px | 0,5–1 MB |
| Índices y cabeceras | < 0,2 MB |
| **Total** | **7–9 MB** (con RLE, menos) |

Cabe de sobra en cualquier microSD FAT32. La tarjeta es un requisito de la v1:
sin pack visual no se puede dibujar el ítem. La app lo dice claramente y ofrece
el modo “solo sincronizar” mientras tanto.

### 11.3 Por qué no otras opciones

| Opción | Problema |
|---|---|
| Fuente CJK completa en flash | No cabe en el slot; además el render por software necesita RAM |
| Rasterizar en el dispositivo | stb_truetype/nanosvg + caché es más código y más RAM; el resultado es peor |
| SVG de radicales en el dispositivo | Mismo problema, y las imágenes son arte de WaniKani que no se redistribuye |
| Pack universal en el repo | Incluiría arte/contenido de WaniKani; no es legalmente limpio |
| Imprimir el significado del radical en texto | Pierde el símbolo, que es lo que se repasa |

La opción del pack generado es la que mejor equilibra calidad, memoria y
licencias. En el futuro se puede añadir un generador en el navegador del móvil
para no necesitar ordenador, o rasterizado en el dispositivo para los caracteres
que falten.

### 11.4 Render

Se adapta `render/Renderer` a una interfaz:

```cpp
class GlyphSource {
public:
    virtual bool glyph(uint32_t codepoint, uint8_t size, GlyphBitmap* out) = 0;
    virtual bool radical(uint32_t subjectId, uint8_t size, GlyphBitmap* out) = 0;
};
```

- Glifos de UI: `UiFont.h` (latín + kana) generado como en Tangocho.
- Kanji/vocabulario: `GlyphSource` desde la SD, con una caché pequeña (16 KB).
- Radicales: bitmap 1bpp/4bpp, dibujado en blanco sobre negro.
- Texto de significados: UiFont; texto japonés: glifos del pack.
- El texto se compone carácter a carácter para palabras de vocabulario.

---

## 12. Sincronización y datos

Resumen; el detalle está en [api-sync.md](api-sync.md).

- **Autenticación:** token personal en `Authorization: Bearer`; revisión
  `Wanikani-Revision: 20170710`.
- **Endpoints:** `/user`, `/summary`, `/assignments`, `/subjects`,
  `/study_materials`, `POST /reviews`, `PUT /assignments/{id}/start`.
- **Caché:** `summary` 30–60 min; asignaciones con `updated_after`; sujetos
  inmutables por ID; ETag/If-None-Match cuando se pueda.
- **Repaso:** se envía un `POST /reviews` por ítem completado, con
  `incorrect_meaning_answers` e `incorrect_reading_answers`. La respuesta trae
  la asignación actualizada (`resources_updated.assignment`), que se guarda.
- **Offline:** el repaso completado se escribe en la outbox **antes** de avanzar.
  Al volver la red se envía en orden, uno a uno.
- **Duplicados:** la API no tiene clave de idempotencia. Si un POST queda en
  duda (timeout), no se reintenta a ciegas: se consulta la asignación y se
  reconcilia por `available_at`/`data_updated_at`. Es un riesgo conocido y se
  documenta en la UI.
- **Hora:** la API usa UTC. La Cardputer no tiene RTC con batería; la hora se
  aprende por SNTP o por el portal del móvil. Sin hora fiable, los repasos
  offline se encolan sin `created_at` y se envían con la hora del servidor.

---

## 13. Memoria y rendimiento

### 13.1 Presupuesto

| Uso | Estimación |
|---|---|
| Canvas 240×135×2 | 64,8 KB |
| Stack del loop | 16 KB |
| TLS (solo sync) | ~50 KB |
| Parser JSON en streaming | 8 KB |
| Registro de sujeto en construcción | 8 KB |
| Caché de glifos | 16 KB |
| UI, strings, listas | ~20 KB |
| **Pico durante sync** | **~183 KB** |
| **Objetivo de heap libre en sync** | **> 35 KB** |
| **Objetivo de heap libre en repaso** | **> 80 KB** |

Tangocho midió ~210 KB libres al arrancar en la Cardputer y ~60 KB en el peor
momento de un handshake TLS con el canvas y un pack abierto. KaniKey no abre
packs, pero añade parser y almacén; hay que medir desde el primer prototipo y
poner un `logNetHeap()` en cada conexión.

### 13.2 Reglas

- **Nunca** un documento JSON completo en RAM. El parser lee por trozos y emite
  un sujeto cada vez.
- **Nunca** una fuente CJK en RAM. Glifos desde la SD, con caché pequeña.
- **Nunca** descargar todos los sujetos en el primer arranque. Bajar solo los
  IDs que hacen falta (repasos/lecciones) y crecer la caché.
- **Nunca** mantener la radio encendida fuera de una sincronización.
- **Siempre** escribir la outbox antes de avanzar de ítem.
- **Siempre** medir `heap_caps_get_largest_free_block()` antes y después de TLS.

### 13.3 Rendimiento de pantalla

- Frame completo: 64,8 KB; a 40 MHz SPI ≈ 13 ms de push.
- Nuevo ítem: frame completo.
- Tecleo: solo la región del campo de respuesta (partial push).
- Caret: parpadeo cada 500 ms, región de 2×20 px.
- Sin animaciones que bloqueen la entrada. El repaso debe responder al teclado
  en menos de 50 ms.

---

## 14. Seguridad, privacidad y legal

### 14.1 Token

- Se guarda en NVS, en el namespace de la app.
- Nunca se escribe en logs, pantalla completa, portal ni backups.
- La pantalla de cuenta lo muestra enmascarado (`••••••••`) y permite borrarlo.
- Al cerrar sesión se borra el token, la caché de asignaciones y la outbox. Si
  quedan repasos pendientes, se avisa y se pide confirmación antes de
  descartarlos.
- NVS no está cifrado por defecto. Es un riesgo aceptado para un proyecto
  personal; el token se puede revocar en WaniKani. Se documenta.

### 14.2 Red

- HTTPS con verificación de certificado contra un conjunto de CAs raíz, como
  Tangocho. Nunca `setInsecure()`.
- El portal de configuración es temporal: red WPA2 con clave aleatoria mostrada
  en pantalla, solo durante el setup; no se sirve en la red de casa.
- No hay telemetría ni servidor propio. Las peticiones van directas a
  `api.wanikani.com` y a la CDN de WaniKani para las imágenes de radicales (solo
  en el generador del pack).

### 14.3 Marca y contenido

- El nombre del producto no incluye “WaniKani”. El subtítulo dice “unofficial
  WaniKani companion”.
- La UI es propia: no se copian colores, ilustraciones, mascotas ni layouts de
  WaniKani.
- El repositorio no contiene significados, lecturas, mnemónicos, imágenes de
  radicales, audio ni frases de contexto de WaniKani. Los fixtures de test son
  mínimos, escritos a mano y atribuidos, como en Tangocho.
- El pack visual se genera con el token del usuario y se queda en su tarjeta.
- Se respeta `subscription.max_level_granted`: no se cachea contenido por encima
  del nivel permitido.
- Proyecto personal, no comercial.

### 14.4 Licencias

| Componente | Licencia | Uso |
|---|---|---|
| Tangocho (código) | MIT | Base del fork; conservar aviso |
| Kakehashi (inspiración) | MIT | Ideas de flujo/answer checker; atribuir si se copia código |
| Noto Sans JP | OFL 1.1 | Glifos de UI y pack visual |
| KanjiVG (fase 2) | CC BY-SA 3.0 | Orden de trazos; atribuir |
| WaniKani API/contenido | Términos de Tofugu | Solo token del usuario, uso personal |

---

## 15. Pruebas

### 15.1 Nativas (sin hardware)

- `RomajiIme`: vocales, digrafos, っ, ん, ー, n antes de vocal, borrado,
  cursor, mayúsculas, entrada mixta.
- `AnswerChecker`: significados aceptados, sinónimos, blacklist/whitelist,
  lecturas primarias vs válidas, okurigana, katakana→hiragana, confusión
  significado/lectura, error de `n`.
- `ReviewQueue`: orden, reencolado, contadores, ítem completado, salida a mitad.
- `Outbox`: append, compactación, reintentos, reconciliación, corrupción.
- `JsonStream`: fixtures de WaniKani con radicales, kanji, vocabulario, kana,
  paginación, errores, escapes `\uXXXX`, UTF-8 inválido.
- `Store`: escritura atómica, CRC, recuperación de `.bak`, migración de versión.
- `SyncPolicy`: qué pedir, cuándo, ETag, `updated_after`, rate limit.
- `Time`: ISO 8601, UTC/local, `created_at`, reloj no fiable.

### 15.2 Hardware

- Teclado: todas las teclas, Shift, Fn, Ctrl/Opt/Alt, auto-repeat, ghosting.
- Texto: escribir 200 caracteres seguidos sin perder eventos.
- SD: lectura de glifos, extracción en caliente, FAT32, tarjeta llena.
- Wi-Fi/TLS: handshake, heap libre, 429, certificado inválido, DNS lento.
- Pantalla: legibilidad al sol, ángulos, tamaño de 9–20 px.
- Batería: repaso de 30 min, sync, deep sleep, despertar por tecla.
- Sueño: guardar outbox y asignaciones antes de dormir.
- OTA: actualización, rollback, imagen corrupta.

### 15.3 CI

- `pio run -e cardputer-adv`.
- `pio test -e native`.
- Build de release con `KK_VERSION`.
- Capturas automáticas con el remote serie, como Tangocho.
- `clang-format`/`clang-tidy` si se adopta el estilo de Tangocho.

### 15.4 Observabilidad

- Log serie `KK_LOG` con eventos de sesión, sync, API y memoria; nunca token.
- `logNetHeap("sync")` antes/después de TLS.
- Contadores: peticiones, 429, bytes, tiempo de sync, ítems, fallos.
- Pantalla de diagnóstico en Ajustes › Acerca de (versión, heap, última sync).

---

## 16. Hoja de ruta

### M0 — Diseño (ahora)

- Este documento, UX, API, reutilización y mockups.
- Revisar decisiones abiertas.
- Prototipo de memoria/TLS en la Cardputer con un GET a `/user`.

**Salida:** decisiones aceptadas y un esqueleto de repositorio.

### M1 — Esqueleto y Home

- Fork podado de Tangocho; compila para Cardputer.
- HAL, pantalla, teclado, SD, sueño.
- Setup Wi-Fi/token/hora; `GET /user` + `/summary`.
- Home con datos reales y caché.

**Salida:** enciendes, configuras y ves “28 repasos” de tu cuenta.

### M2 — Pack visual y repaso online

- Generador `kanikey-glyphs` y lectura del pack en la SD.
- Sync de asignaciones + sujetos para los repasos pendientes.
- Pantalla de repaso, IME, answer checker, feedback.
- `POST /reviews` y resumen de sesión.

**Salida:** una sesión completa de repasos contra WaniKani.

### M3 — Offline y cola

- Outbox persistente, reconciliación, reintentos, rate limit.
- Arranque sin red, caché de sujetos, estado “offline”.
- Pantalla de sync con progreso.

**Salida:** repasar en el metro y sincronizar al salir.

### M4 — Lecciones

- Lista de lecciones, ficha con mnemónico/componentes.
- `PUT /assignments/{id}/start`.
- Práctica local opcional.

**Salida:** lecciones completas en el aparato.

### M5 — Pulido

- Estadísticas, ajustes completos, español, OTA, About.
- Accesibilidad, contrastes, textos de error.
- Capturas y documentación de usuario.

**Salida:** v1.0 publicable.

### M6 — Extras

- Audio, orden de trazos, práctica FSRS, previsión, sinónimos.
- Generador de pack desde el móvil.

---

## 17. Riesgos

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Memoria insuficiente con TLS + canvas + parser | Alto | Parser en streaming, caché pequeña, medir en M0, liberar caché antes de TLS |
| Duplicar un repaso por timeout | Alto | No reintentar a ciegas; reconciliar con la asignación; outbox con estado |
| Pack visual difícil de generar | Medio | Herramienta Python simple, instrucciones claras, fallback “solo sync” |
| La API cambia o limita | Medio | Revisión fija, fixtures, capa `wk` aislada, errores claros |
| Teclado/IME con casos raros | Medio | Tests nativos exhaustivos, feedback visible, permitir editar |
| Sin RTC fiable | Medio | SNTP/portal, última hora conocida, aviso y `created_at` opcional |
| SD lenta, llena o extraída | Medio | Caché, escritura atómica, pausa y mensaje claro |
| Legibilidad en 1,14" | Medio | Mockups a tamaño real, pruebas al sol, tipografías 9–20 px |
| Batería con Wi-Fi | Bajo | Radio solo en sync, deep sleep, sync mínimo |
| Alcance enorme | Alto | Fases; lecciones fuera del MVP; no paridad con WaniKani |
| Marca/contenido | Medio | Sin arte ni contenido en el repo; nombre sin “WaniKani”; revisar términos |

---

## 18. Decisiones abiertas

| # | Decisión | Recomendación | Por qué |
|---|---|---|---|
| 1 | Nombre | KaniKey (provisional) | Corto, teclado, no incluye “WaniKani” |
| 2 | Base | Fork podado de Tangocho | El HAL de Cardputer es lo más caro |
| 3 | Pack visual | Generador Python + SD | Mejor calidad/memoria; sin contenido en el repo |
| 4 | Offline | Activado por defecto | Es la gracia del aparato; con reconciliación |
| 5 | Lecciones | Fase 2 | El MVP ya es grande; primero repasos sólidos |
| 6 | Idioma UI | Inglés primero, español después | WaniKani es inglés; el autor es hispanohablante |
| 7 | Corrección | Estricta + avisos | Fiel a WaniKani; los avisos evitan frustración |
| 8 | Tamaño de sesión | 20 por defecto, “todas” opcional | Sesiones cortas en un aparato pequeño |
| 9 | SD obligatoria | Sí en v1 | Sin glifos no hay repaso; se dice claro |
| 10 | Licencia | MIT | Compatible con Tangocho y Kakehashi |

---

## 19. Referencias

- WaniKani API v2, revisión 20170710: <https://docs.api.wanikani.com/20170710/>
- Reglas de marca de WaniKani: <https://docs.api.wanikani.com/20170710/#branding-rules>
- Tangocho: <https://github.com/dcarnicer/tamagotcho>
  - `docs/architecture.md`, `docs/ux-refresh.md`, `docs/microsd.md`,
    `docs/decisions.md`
- Kakehashi: <https://github.com/Portego-00/Kakehashi>
  - Flujo de repaso, answer checker, cola offline, lecciones
- M5Stack Cardputer ADV: <https://docs.m5stack.com/en/core/Cardputer-Adv>
- Noto Sans JP (OFL): <https://fonts.google.com/noto/specimen/Noto+Sans+JP>
- KanjiVG (CC BY-SA, fase 2): <https://kanjivg.tagaini.net/>
