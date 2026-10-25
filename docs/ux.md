# KaniKey — UX, teclado y pantallas

> Diseño, 2026-10-25. Los mockups están a 240×135: los individuales se muestran
> a 4× y el contact sheet a 2×. La geometría y los tamaños de texto son los de
> 1×; el upscale es nearest-neighbour para no mentir sobre la rejilla de píxeles.

![Pantallas](screens/overview.png)

---

## 1. Principios

1. **El teclado es la interfaz.** Escribir una respuesta no es un modo raro: es
   la acción principal. El resto de la app se navega con flechas y Enter.
2. **Un foco por pantalla.** En el repaso, el ítem y la respuesta. En el inicio,
   los repasos. En la lección, el mnemónico. Nada compite.
3. **La barra inferior siempre dice qué hacen las teclas.** No hay atajos
   invisibles. Si una tecla cambia de significado, la barra cambia.
4. **Color + forma + palabra.** Correcto no es solo verde; lleva ✓ y la palabra
   `CORRECT`. Incorrecto no es solo rojo; lleva ✗ y `NOT QUITE`.
5. **Offline se dice.** Un icono, una palabra y un número: `Offline · 18 cached ·
   4 to sync`. Nunca un error genérico.
6. **Textos cortos.** La pantalla es de 240×135; las frases largas se parten en
   varias líneas o se van a una pantalla de detalle.

---

## 2. Lenguaje visual

### 2.1 Paleta

| Token | Hex | Uso |
|---|---|---|
| `bg` | `#0B0D10` | Fondo |
| `surface` | `#151A22` | Paneles, filas, tarjetas |
| `surface-hi` | `#202733` | Campo de respuesta, pills, pistas |
| `line` | `#33404F` | Bordes de 1 px, separadores |
| `text` | `#F4F7FB` | Texto principal, kanji |
| `text-dim` | `#9BA8BA` | Texto secundario, etiquetas |
| `text-faint` | `#647184` | Metadatos, ayudas |
| `accent` | `#6C8CFF` | Foco, progreso, `MEANING` |
| `accent-hi` | `#9DB2FF` | Acento sobre negro |
| `brand` | `#FF7A59` | Marca, logotipo, detalles |
| `reading` | `#5EEAD4` | Lecturas, modo `READING` |
| `ok` | `#6EE7A8` | Acierto |
| `ok-bg` | `#123326` | Fondo de acierto |
| `err` | `#FF6B6B` | Fallo |
| `err-bg` | `#351A1C` | Fondo de fallo |
| `warn` | `#FBBF24` | Avisos, offline, batería baja |
| `gold` | `#F5C451` | Nivel |

La paleta es propia: negro cálido, índigo y coral. No se copian los colores,
ilustraciones ni componentes de WaniKani.

### 2.2 Tipografía

| Rol | Tamaño | Uso |
|---|---|---|
| Display | 32 px | Números grandes: repasos, precisión, tiempo |
| Prompt | 64 / 48 / 32 px | Kanji, radical, palabra de vocabulario |
| Answer | 20 px | Respuesta escrita, significado/lectura |
| Title | 18 px | Título de lección, significado principal |
| Body | 14 px | Frases, mnemonics, valores |
| Caption | 10 px | Etiquetas, cabecera, pills |
| Micro | 9 px | Metadatos que no son esenciales |

La UI usa **Noto Sans JP** (OFL) para latín y kana; los kanji/vocabulario salen
del pack visual. En el firmware, la fuente de UI se pre-renderiza como en
Tangocho (`UiFont.h`), así que no hay rasterizado en el dispositivo.

### 2.3 Rejilla

- Unidad base: 4 px.
- Márgenes: 4 px.
- Cabecera: 16 px (título + progreso de 2 px o línea de 1 px).
- Barra de pistas: 12 px.
- Pills: 13 px de alto.
- Campo de respuesta: 32 px de alto.
- Filas de lista: 18–20 px.
- Esquinas rectas; sin sombras; bordes de 1 px.

### 2.4 Componentes

- **Header:** título o marca a la izquierda; icono de Wi-Fi, batería y un valor a
  la derecha; barra de progreso de 2 px abajo en el repaso.
- **Pill:** etiqueta corta en mayúsculas (`MEANING`, `READING`, `CORRECT`,
  `KANJI · L5`) con fondo `surface-hi` y color semántico.
- **Input:** rectángulo `surface-hi` con borde `line`; texto de 20 px; caret de
  2 px en `accent`; borde de 2 px en `accent` cuando el teclado está en modo
  texto.
- **Keycap:** cuadrado de 11 px con borde; la tecla en 8 px; la acción al lado
  en 9–10 px.
- **Progress:** barra de 2–3 px; track `surface-hi`, relleno `accent`.
- **Feedback:** pill + icono + palabra; nunca solo color.
- **List row:** fondo `surface`, foco `accent`, etiqueta 10 px, valor 9 px,
  chevron.

---

## 3. Teclado

### 3.1 Mapa físico

La Cardputer ADV tiene 4 filas × 14 teclas. La capa principal es US; Shift da
mayúsculas y símbolos; Fn da función.

```
  `  1  2  3  4  5  6  7  8  9  0  -  =  del
 tab q  w  e  r  t  y  u  i  o  p  [  ]  \
 fn  sh a  s  d  f  g  h  j  k  l  ;  '  enter
 ctrl opt alt z  x  c  v  b  n  m  ,  .  /  space
```

| Tecla | Capa normal | Shift | Fn |
|---|---|---|---|
| `` ` `` | `` ` `` | `~` | `Esc` |
| `1`–`0` | dígitos | `!@#$%^&*()` | `F1`–`F10` |
| `-` / `=` | `-` / `=` | `_` / `+` | `F11` / `F12` |
| `del` | Backspace | Backspace | Delete |
| `;` | `;` | `:` | `↑` |
| `,` | `,` | `<` | `←` |
| `.` | `.` | `>` | `↓` |
| `/` | `/` | `?` | `→` |
| `'` | `'` | `"` | — |
| `[` `]` `\` | literales | `{ } |` | — |

### 3.2 Dos modos

| Modo | Cuándo | Qué hacen `; , . /` | Qué hacen las letras |
|---|---|---|---|
| **Navegación** | Menús, home, feedback, lecciones | Flechas (sin Fn) | Atajos (`R`, `L`, `S`) |
| **Texto** | Campo de respuesta activo | Caracteres `; , . /` | Se escriben |

En modo texto, las flechas siguen disponibles con `Fn + ; , . /`. El campo se
marca con borde `accent` y caret; la barra inferior cambia a
`Enter check · Esc pause · Tab hint`.

Al terminar una respuesta (Enter) o cancelar (Esc), se vuelve a navegación. No
hay que pulsar un botón para “entrar” en el campo: al abrir una pregunta, el
campo está activo.

### 3.3 Atajos globales

| Tecla | Acción |
|---|---|
| `Enter` | Comprobar respuesta / continuar / abrir |
| `Esc` | Volver / cancelar / pausar sesión |
| `Tab` | Detalle del ítem / pista; en menús, ajustes |
| `↑` `↓` | Mover foco; en texto, `Fn` + flechas |
| `←` `→` | Página anterior/siguiente en lecciones |
| `Space` | Página siguiente en lecciones; espacio en texto |
| `R` | Repasos (home) |
| `L` | Lecciones (home) |
| `S` | Sincronizar (home) |
| `Ctrl+S` | Sincronizar desde cualquier menú |
| `G0` | Atrás / pausa (mantener en el repaso abre el menú de sesión) |

`Tab` en el repaso no abre ajustes: abre la ficha del ítem. Para ajustes, `Esc`
→ menú de sesión → Settings, o `Tab` desde el home.

### 3.4 Repetición y modificadores

- Se repiten solo Backspace y las flechas (400 ms de retardo, 120 ms de rate).
- Las letras no se repiten: evita respuestas accidentales por mantener pulsado.
- Shift, Ctrl, Opt y Alt se rastrean como estado; por sí mismos no generan
  carácter (Shift cambia la capa del siguiente).
- Si el TCA8418 pierde eventos (overflow), se suelta todo y se muestra un aviso
  discreto; nunca se queda una tecla “pegada”.

---

## 4. Flujos

### 4.1 Primera configuración

```
Boot
 └─ ¿Wi-Fi guardada? ──no──► Setup
                              ├─ 1. Red WPA2 temporal + QR/URL
                              ├─ 2. Portal: Wi-Fi + token
                              ├─ 3. GET /user: usuario/nivel
                              ├─ 4. Hora (NTP o navegador)
                              ├─ 5. Primer sync (summary/assignments)
                              └─ 6. ¿Pack visual? ──no──► instrucciones SD
```

- El portal se abre solo durante el setup y se cierra al terminar.
- La red temporal usa WPA2 con una clave aleatoria de 8–10 caracteres visible en
  pantalla. No es una red abierta.
- El token se pega en el portal; se valida con `GET /user` antes de guardarlo.
- Si el usuario cancela, el aparato queda en “Setup pendiente” y no sincroniza.

### 4.2 Repaso diario

```
Home ──Enter──► Sync (si toca) ──► Review
                                   ├─ Meaning: escribe, Enter
                                   ├─ Reading: romaji→kana, Enter
                                   ├─ acierto: feedback breve, Enter
                                   ├─ fallo: feedback + reencolar
                                   └─ completado: POST /reviews
                                         ├─ ok: actualiza asignación
                                         └─ sin red: outbox
                                   ──► Summary
```

- La sesión se congela al empezar: los ítems que se añadan después no aparecen
  hasta la siguiente.
- Si el usuario sale, se guarda la posición y la outbox; al volver, retoma.
- El orden es aleatorio estable por sesión, mezclando tipos; los ítems con
  fallo vuelven 3–5 posiciones después.

### 4.3 Offline

```
Home (Offline) ──► Review con caché ──► completados a outbox
                                         │
                                    al volver red
                                         │
                                    Sync outbox (1 a 1)
```

- La home muestra `Offline · 18 cached · 4 to sync`.
- Si un ítem no tiene sujeto cacheado, no se ofrece; se dice
  `1 item needs a connection`.
- Al volver la red, se envía la outbox antes de pedir asignaciones nuevas.

### 4.4 Lecciones

```
Home ──L──► Lessons ──Enter──► Lesson card
                               ├─ Space: siguiente página
                               ├─ ←→: páginas
                               ├─ Tab: componentes/notas
                               └─ Enter: Start lesson
                                     └─ PUT /assignments/{id}/start
```

- La ficha muestra significado(s), lectura(s), mnemónico y componentes.
- “Start lesson” requiere red en la v1. Si no hay, se puede estudiar la ficha
  pero el botón queda deshabilitado con `Needs connection`.
- Tras empezar, la asignación pasa a la cola de repaso con la etapa 1.

### 4.5 Conflictos y errores

| Situación | Qué ve el usuario | Qué hace la app |
|---|---|---|
| Token inválido (401) | `Token rejected` + `Update token` | Abre setup de token |
| Sin permiso (403) | `Missing API permissions` | Lista los scopes necesarios |
| Rate limit (429) | `WaniKani is busy · retrying in 42s` | Backoff con `RateLimit-Reset` |
| Sin red | `Offline` + caché | Sigue con la caché |
| 422 al enviar | `Review already changed` | Reconciliación y refresco |
| SD fuera | `Insert the SD card` | Pausa la sesión |
| Glifo ausente | `Visual pack is missing 休` | Bloquea el ítem y explica |
| Hora no fiable | `Clock not set` | Pide sync; si no, `created_at` se omite |

---

## 5. Pantallas

### 5.1 Boot

- Fondo negro, `KaniKey` en coral, barra de progreso fina.
- Estados: `Mounting SD…`, `Reading cache…`, `Wi-Fi…`, `Syncing…`.
- Si falla algo crítico, una sola frase y una acción:
  - `No SD card · Insert a FAT32 card` / `Enter retry`.
  - `Visual pack missing · See docs/setup.md`.
- No muestra logs técnicos.

### 5.2 Setup

- Cabecera `WELCOME`.
- `KaniKey` grande, subtítulo `an unofficial WaniKani companion`.
- Pasos: `Wi-Fi`, `API token`, `Clock`, `First sync`, con el actual marcado.
- En el paso 1: lista de redes, `Enter` elegir, texto para la contraseña.
- En el paso 2: URL del portal y clave de la red temporal; el móvil hace el
  resto. También se puede pegar el token con el teclado.
- En el paso 4: barra de progreso y `Cancel`.
- En el paso 5: si falta el pack visual, una pantalla con:
  `Copy glyphs.kgp to /kanikey/ on the SD card` y `Enter: check again`.

### 5.3 Home

![Home](screens/home.png)

- Cabecera: `KaniKey` (marca), Wi-Fi, batería.
- `LEVEL 12` en oro y el nombre de usuario al lado.
- Número grande de repasos (`28`) y `REVIEWS DUE`.
- Lecciones (`5`) y `LESSONS`.
- `Next review 18:00 · in 42 min`.
- Barra de distribución SRS: aprendiz, guru, master, enlightened, burned, con
  conteos pequeños.
- Barra de pistas: `Enter reviews`, `L lessons`, `S sync`.
- Estados:
  - **Sync:** `Syncing…` con spinner y progreso en la cabecera.
  - **Offline:** línea en `warn`: `Offline · 18 cached · 4 to sync`.
  - **Vacío:** `All caught up` + `Next review 18:00`; `Enter` abre el detalle de
    la previsión.
  - **Error:** `Can't reach WaniKani` + `Enter retry` / `S sync`.
- No se muestran menús laterales ni tarjetas que tapen los números.

### 5.4 Review

![Repaso de significado](screens/review-meaning.png)
![Repaso de lectura](screens/review-reading.png)

- Cabecera `REVIEWS`, `12/48`, batería; barra de progreso de 2 px.
- Panel izquierdo de 88×88:
  - Kanji/radical de 64 px centrado.
  - Pill `KANJI · L5` o `RADICAL · L3` debajo.
  - Vocabulario: la palabra se ajusta a 48/32/20 px para caber; si no cabe, se
    parte en dos líneas.
  - Radical sin Unicode: bitmap del pack visual.
- Panel derecho:
  - Pill `MEANING` (índigo) o `READING` (teal).
  - Instrucción: `Type the meaning` / `Type the reading`.
  - Campo de 32 px con la respuesta; caret.
  - En lectura, debajo: `romaji: tabe` en gris; el campo muestra `たべ`.
  - `Tab: details` en la esquina inferior.
- Barra: `Enter check`, `Esc pause`.
- El campo está activo desde que aparece la pregunta. No hay que pulsar nada.
- Si la respuesta es larga, el texto se desplaza horizontalmente; el caret queda
  visible. No se parte en dos líneas para no cambiar la altura.
- El botón G0 mantiene: abre `Pause` (Continue / End session / Settings).

### 5.5 Feedback

![Feedback correcto](screens/review-correct.png)
![Feedback incorrecto](screens/review-wrong.png)

- El panel izquierdo no cambia; el derecho se convierte en feedback.
- Acierto:
  - Pill `CORRECT` en verde + ✓.
  - `You typed` y la respuesta en gris.
  - `Meaning` / `Reading` con la respuesta correcta en verde/teal.
  - `Enter next`, `Tab details`.
- Fallo:
  - Pill `NOT QUITE` en rojo + ✗.
  - `You typed` y la respuesta del usuario.
  - `Correct` con la respuesta correcta.
  - `Comes back later in this session`.
- El feedback no dura un tiempo fijo: espera a `Enter` (o a que el usuario
  empiece a escribir la siguiente, si se habilita auto-avance).
- `Tab` abre la ficha completa: significados, lecturas, mnemónico y componentes.

### 5.6 Item detail

- Cabecera `ITEM` con tipo y nivel.
- Carácter/radical a la izquierda; significados y lecturas a la derecha.
- Páginas con `←` `→` o `Space`:
  1. Significados + lecturas.
  2. Mnemónico de significado.
  3. Mnemónico de lectura.
  4. Componentes (radicales/kanji) con sus significados.
- Si el mnemónico no está cacheado: `Mnemonic not available offline`.
- `Esc` vuelve al repaso sin perder la respuesta escrita.

### 5.7 Session summary

![Resumen](screens/summary.png)

- `SESSION`.
- Tres columnas: repasos, precisión, tiempo.
- `8 items moved up` (o `No stage changes`).
- `Next review 18:00` y, si toca, `All caught up for now`.
- Si hay outbox: `4 reviews waiting to sync` en `warn`.
- `Enter done`, `R review more`.

### 5.8 Lessons

- `LESSONS` con lista de ítems disponibles: carácter, tipo, nivel, significado
  principal.
- `↑↓` mueve, `Enter` abre, `Esc` vuelve.
- Cada fila: `休 · KANJI · L5 · rest`.
- Si no hay lecciones: `No lessons right now` + `Next unlock at level 13`.
- La lista se ordena por nivel y `lesson_position`.

### 5.9 Lesson card

![Lección](screens/lesson.png)

- Cabecera `LESSON`, `2/5`, progreso.
- Panel izquierdo: carácter/radical de 48 px.
- Derecha: pill `KANJI · L5`, significado principal en 20 px, lecturas en teal
  de 14 px, `person + tree` en gris.
- Mnemónico en un panel de 3 líneas y 10 px; `Space` pasa de página.
- `p1/3` en la esquina.
- `Enter start` / `Needs connection` si no hay red.
- Al empezar, animación breve de progreso y vuelta a la lista.

### 5.10 Sync

![Sync](screens/sync.png)

- Lista de pasos: Account, Summary, Reviews, Subjects, Outbox.
- Cada fila: nombre, valor y estado (✓, spinner, reloj, ✗).
- Barra de progreso y `Fetching subjects… 64/128`.
- `Esc cancel`; si se cancela, lo ya descargado se conserva.
- Al terminar: `Synced · 2 min ago` y vuelve a Home.
- Si un paso falla, se queda en `warn` con `Retry` y no bloquea los demás.

### 5.11 Settings

![Ajustes](screens/settings.png)

- Filas: Account, Study, Sync, Device, About.
- Account: usuario, nivel, token enmascarado, `Sign out`.
- Study: `Meaning first` / `Reading first`, `Auto-advance`, `Session size`,
  `Strict answers`.
- Sync: `Sync on boot`, `Sync now`, `Last sync`, `Clear cache`.
- Device: `Brightness`, `Sleep after`, `Wi-Fi`, `Time`, `Firmware`.
- About: versión, licencias, atribuciones, disclaimer, heap y última sync.
- `↑↓` mueve, `Enter` abre, `Esc` vuelve.

### 5.12 Stats (fase 2)

- Nivel actual y progreso del nivel.
- Distribución SRS con barras y conteos.
- Precisión de la sesión y global (si se cachea).
- Racha y repasos de los últimos 7 días.
- `Esc` vuelve.

### 5.13 Errores y vacíos

- **Sin configurar:** `Welcome to KaniKey` + `Enter setup`.
- **Sin SD:** `No SD card` + `Insert a FAT32 card` + `Enter retry`.
- **Pack visual:** `Visual pack missing` + `Copy glyphs.kgp` + `Enter check`.
- **Sin red:** `Offline` + `Using cached data` + `S sync`.
- **Token:** `Token rejected` + `Update token`.
- **Servidor:** `WaniKani is not answering` + `Enter retry`.
- **Todo al día:** `All caught up` + `Next review 18:00`.
- **Sesión vacía:** `No reviews due` + `Next review 18:00` + `L lessons`.

Las pantallas de error nunca muestran stack traces, URLs con token ni JSON.

---

## 6. Accesibilidad

- **Contraste:** texto principal sobre fondo > 15:1; texto secundario > 7:1;
  los colores semánticos se acompañan de ✓/✗ y palabra.
- **Daltonismo:** verde y rojo no son la única señal; la forma (pill + icono) y
  el texto cambian.
- **Tamaño:** mínimo 9 px para metadatos no esenciales; 10 px para cualquier cosa
  que haya que leer; 20 px para la respuesta.
- **Teclado:** todo se puede hacer sin tocar la pantalla; no hay gestos.
- **Movimiento:** solo el caret y el progreso; nada de animaciones que bloqueen.
- **Idioma:** inglés primero; español después. Los significados/lecturas son
  siempre los de WaniKani (inglés).
- **Texto largo:** se pagina; no se reduce la fuente por debajo de 9 px.

---

## 7. Copy y tono

- Corto, directo, sin jerga de WaniKani.
- “Correct” / “Not quite”, no “¡Bien hecho!” ni “Has fallado”.
- “Reviews”, “Lessons”, “Meaning”, “Reading”, “Item”, “Session”.
- “Offline · 18 cached · 4 to sync”, no “Error de red”.
- “Comes back later in this session”, no “Incorrecto. Se repetirá”.
- Disclaimer en Setup y About:
  `KaniKey is an unofficial third-party app for WaniKani. Not affiliated with
  WaniKani or Tofugu LLC.`

---

## 8. Mockups

| Archivo | Pantalla |
|---|---|
| `screens/home.png` | Home con repasos, lecciones y distribución SRS |
| `screens/review-meaning.png` | Pregunta de significado |
| `screens/review-reading.png` | Pregunta de lectura con romaji |
| `screens/review-correct.png` | Feedback correcto |
| `screens/review-wrong.png` | Feedback incorrecto |
| `screens/lesson.png` | Ficha de lección |
| `screens/summary.png` | Resumen de sesión |
| `screens/sync.png` | Sincronización |
| `screens/settings.png` | Ajustes |
| `screens/setup.png` | Primer arranque |
| `screens/overview.png` | Contacto de todas |

Para regenerarlos:

```sh
python3 tools/mockups/render.py
```
