# KaniKey — puesta en marcha (diseño)

> Este documento describe cómo se configurará KaniKey cuando exista el
> firmware. Las herramientas y comandos son parte del diseño, todavía no están
> implementados.

---

## 1. Lo que hace falta

- M5Stack Cardputer ADV.
- Tarjeta microSD **FAT32** (exFAT no vale; el framework de Arduino-ESP32 no lo
  soporta).
- Cuenta de WaniKani y un **token personal** con permisos de lectura de
  assignments, subjects, study materials, user y summary, y de creación de
  reviews.
- Un ordenador con Python 3.11+ y [`uv`](https://docs.astral.sh/uv/) para
  generar el pack visual la primera vez.
- Fuente **Noto Sans JP** (OFL); el generador la descarga o acepta una ruta
  local.

La tarjeta es necesaria para los glifos. Sin pack visual se puede sincronizar,
pero no repasar kanji/vocabulario.

---

## 2. Crear el token de WaniKani

1. Entra en WaniKani → **Settings → API Tokens**.
2. Crea un token nuevo. Nombre sugerido: `KaniKey Cardputer`.
3. Marca los permisos:
   - Assignments: lectura.
   - Subjects: lectura.
   - Study materials: lectura.
   - User: lectura.
   - Summary: lectura.
   - Reviews: creación.
   - (Fase 2: review statistics y reviews, lectura.)
4. Copia el token. No lo pegues en un chat ni lo commitees.

Se puede revocar en cualquier momento. KaniKey lo guarda en NVS y lo muestra
enmascarado.

---

## 3. Generar el pack visual

El pack contiene los glifos de los caracteres que aparecen en los sujetos a los
que tu cuenta tiene acceso, y los radicales rasterizados. Se genera en local con
tu token; no se sube a ningún servidor.

```sh
cd tools/glyph-pack
uv run kanikey-glyphs build \
  --token "$WANIKANI_TOKEN" \
  --out /Volumes/KANIKEY/kanikey/glyphs.kgp
```

Qué hace:

1. `GET /user` para leer `level` y `subscription.max_level_granted`.
2. `GET /subjects` por niveles permitidos (hasta 60 si la suscripción lo permite).
3. Reúne los caracteres únicos de kanji, vocabulario y kana.
4. Renderiza cada carácter con Noto Sans JP a 64, 40 y 20 px, 4bpp + RLE.
5. Descarga las imágenes SVG de los radicales y las rasteriza a 96 px.
6. Escribe el índice y el pack.

Opciones previstas:

```sh
--max-level N       # limitar el pack a un nivel (por defecto, el permitido)
--font PATH         # otra fuente OFL
--no-radicals       # solo glifos (para pruebas)
--check             # comparar un pack con los sujetos actuales y listar faltantes
```

Tamaño orientativo: 7–9 MB. En una tarjeta de 8 GB o más no es problema.

### 3.1 Copiar a la tarjeta

```
/kanikey/glyphs.kgp
```

La tarjeta debe estar en FAT32. Si el sistema no la monta, formatea con FAT32
(no exFAT).

---

## 4. Primer arranque

1. Inserta la tarjeta y enciende la Cardputer.
2. Si no hay Wi-Fi configurada, KaniKey levanta una red WPA2 temporal
   `KaniKey-XXXX` con una clave en pantalla.
3. En el móvil, conéctate a esa red y abre la URL que muestra la pantalla
   (normalmente `http://192.168.4.1/`).
4. El portal pide:
   - SSID y contraseña de tu Wi-Fi de casa.
   - Token de WaniKani (se valida con `GET /user`).
   - Hora (la del navegador).
5. Al guardar, la Cardputer se conecta a tu Wi-Fi, sincroniza y cierra el
   portal.
6. Comprueba el pack visual. Si falta o está incompleto:
   `Visual pack missing · Copy glyphs.kgp to /kanikey/`.
7. Vuelve al home. Deberías ver tus repasos y lecciones.

---

## 5. Uso diario

- Enciende; si hace más de 15 minutos de la última sync, sincroniza.
- `Enter` → repasos.
- Escribe el significado; `Enter`.
- Escribe la lectura en romaji; el campo muestra kana; `Enter`.
- Si fallas, lee la respuesta correcta y pulsa `Enter`; el ítem vuelve luego.
- Al terminar, resumen. `Enter` para volver al home.
- `L` → lecciones (fase 2).
- `S` → sincronizar.
- `Tab` → ajustes desde el home.

---

## 6. Actualizar el pack visual

Hay que regenerarlo si:

- Subes de nivel y aparecen kanji nuevos (si el pack se generó solo hasta el
  nivel actual).
- Pasas de cuenta gratuita a suscripción (más niveles permitidos).
- Cambias de fuente o de tamaño de glifo.

Con el pack generado hasta `max_level_granted`, los cambios de nivel no deberían
requerir regenerarlo. Aun así, `--check` lista los caracteres que faltan:

```sh
uv run kanikey-glyphs check \
  --pack /Volumes/KANIKEY/kanikey/glyphs.kgp \
  --token "$WANIKANI_TOKEN"
```

Si faltan pocos, se puede regenerar solo el pack parcial con `--ids` (previsto).

---

## 7. Problemas frecuentes

| Síntoma | Causa probable | Solución |
|---|---|---|
| `No SD card` | Tarjeta ausente o no FAT32 | Inserta/formatea en FAT32 |
| `Visual pack missing` | No está `/kanikey/glyphs.kgp` | Copia el pack |
| `Visual pack is missing 休` | Pack antiguo o generado con otro nivel | Regenera con `--check` |
| `Token rejected` (401) | Token revocado o mal copiado | Crea uno nuevo y actualízalo |
| `Missing API permissions` (403) | Faltan permisos | Edita el token en WaniKani |
| `Offline` | Sin Wi-Fi o DNS | Revisa la red; el repaso offline sigue |
| `WaniKani is busy` (429) | Rate limit | Espera; KaniKey reintenta |
| `Clock not set` | Sin SNTP y sin hora del portal | Conéctate una vez o ajusta la hora |
| `Review already changed` (422) | El ítem se repasó en otro sitio | KaniKey reconcilia y refresca |

---

## 8. Privacidad

- El token se queda en la Cardputer (NVS). No se envía a ningún servidor de
  KaniKey.
- El pack visual se genera en tu ordenador y se queda en tu tarjeta.
- No hay telemetría.
- Al cerrar sesión se borran token, asignaciones y outbox (con confirmación).

---

## 9. Futuro

- Generador de pack en el navegador del móvil, sin ordenador.
- Rasterizado en el dispositivo para caracteres que falten.
- Audio de pronunciación y orden de trazos.
