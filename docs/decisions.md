# Decisiones

Registro corto de decisiones de diseño. Estado: **propuesta** hasta que el
mantenedor las acepte. Formato: fecha, decisión, por qué y qué implica.

---

## 2026-10-25 — KaniKey es un compañero, no un sustituto

**Decisión:** KaniKey sincroniza con WaniKani y muestra repasos/lecciones. El
SRS, las asignaciones y el progreso siguen en WaniKani.

**Por qué:** llevar dos schedulers crea divergencia y duplica el trabajo. El
valor está en el aparato y en la escritura, no en reimplementar WaniKani.

**Implica:** no hay FSRS para los repasos; no se inventan intervalos; la API es
la fuente de verdad.

---

## 2026-10-25 — Respuesta escrita, no test de opción múltiple

**Decisión:** el usuario escribe significado y lectura. No hay cuatro opciones en
el flujo principal.

**Por qué:** es el corazón de WaniKani y la razón de usar un teclado. Un test de
opciones convertiría KaniKey en un Tangocho peor.

**Implica:** IME romaji→kana, answer checker, edición de texto y teclado en dos
modos. Un “modo test” puede llegar después como accesibilidad.

---

## 2026-10-25 — Offline por defecto, con outbox

**Decisión:** se puede repasar sin red; los repasos completados se guardan en una
outbox persistente y se envían al volver la conexión.

**Por qué:** es la gracia de un aparato de bolsillo. Kakehashi demuestra que se
puede hacer sin bloquear la sesión.

**Implica:** escribir antes de avanzar, reconciliar envíos ambiguos y mostrar el
estado pendiente con honestidad. La API no tiene idempotencia; hay que tener
cuidado con los timeouts.

---

## 2026-10-25 — Solo Cardputer ADV en la v1

**Decisión:** el firmware se optimiza y se prueba solo para la Cardputer ADV.

**Por qué:** el teclado es el producto; las otras placas cambian el layout y las
pruebas. Tangocho ya mantiene varias.

**Implica:** se puede conservar el HAL de otras placas en el fork, pero no se
promete soporte. El layout es 240×135 horizontal.

---

## 2026-10-25 — Fork podado de Tangocho

**Decisión:** KaniKey empieza como fork de Tangocho en v0.7.0, conserva HAL,
render, UI base, Wi-Fi, SD, OTA y tests, y tira packs/quiz/SRS/catálogo.

**Por qué:** el hardware ya está resuelto y probado; reescribirlo es la parte
cara y aburrida.

**Implica:** mantener el aviso MIT, un remoto `upstream` y una poda agresiva.
Si los dos proyectos crecen, extraer un `cardputer-kit` más adelante.

---

## 2026-10-25 — Pack visual en la microSD

**Decisión:** los glifos de kanji/vocabulario y los radicales se generan en el
ordenador con `kanikey-glyphs` y se guardan en `/kanikey/glyphs.kgp` en la SD.

**Por qué:** una fuente CJK no cabe en flash; rasterizar en el dispositivo gasta
RAM y es más código. El pack da calidad y mantiene el firmware simple.

**Implica:** la SD es necesaria para repasar. El pack se genera con el token del
usuario y no se redistribuye. Si falta un glifo, se bloquea el ítem con un aviso.

---

## 2026-10-25 — Lecciones en la fase 2

**Decisión:** el MVP se centra en repasos. Las lecciones llegan después.

**Por qué:** el repaso ya obliga a API, caché, IME, answer checker, outbox y
pantallas. Añadir lecciones y mnemónicos al mismo tiempo multiplica el alcance.

**Implica:** la home puede mostrar el contador de lecciones desde el principio,
pero la pantalla de lecciones se pospone.

---

## 2026-10-25 — UI en inglés primero

**Decisión:** los textos de la interfaz se escriben primero en inglés; el español
llega después. Los significados/lecturas son los de WaniKani (inglés).

**Por qué:** WaniKani es una comunidad mayoritariamente inglesa y el proyecto
puede publicarse. El autor es hispanohablante, así que el español es prioridad
alta, pero no bloquea el MVP.

**Implica:** `UiStrings` bilingüe desde el diseño, como en Tangocho; no hardcodear
textos en las pantallas.

---

## 2026-10-25 — Corrección estricta con avisos

**Decisión:** se aceptan las respuestas exactas de WaniKani (incluidos sinónimos
del usuario y whitelist). Las lecturas válidas no primarias, el okurigana
incorrecto y el error de `n` muestran un aviso y permiten reintentar, sin contar
como fallo automáticamente.

**Por qué:** es fiel a WaniKani y evita castigar conocimientos válidos por una
tecla. Un ajuste “strict” puede contar el aviso como fallo.

**Implica:** el answer checker devuelve un resultado con tipo (`Precise`,
`Imprecise`, `Warning`, `Incorrect`) y la UI decide.

---

## 2026-10-25 — Sin backend propio

**Decisión:** la Cardputer llama directa a `api.wanikani.com` con el token del
usuario. No hay servidor de KaniKey.

**Por qué:** menos infraestructura, menos privacidad que ceder y menos latencia.
La API permite CORS y tokens personales.

**Implica:** TLS, rate limit y streaming JSON en el dispositivo. El portal de
setup es local y temporal.

---

## 2026-10-25 — Portal de setup WPA2 y temporal

**Decisión:** el portal de configuración se sirve en una red WPA2 propia, con una
clave aleatoria en pantalla, y se apaga al terminar. No se sirve en la red de
casa.

**Por qué:** el token no debe viajar por una red abierta. No hace falta un portal
permanente porque la sincronización la hace el aparato.

**Implica:** el usuario configura desde el móvil una vez; después todo se hace
con el teclado. El portal se reabre solo si se pide.

---

## 2026-10-25 — Nada de contenido de WaniKani en el repositorio

**Decisión:** no se commitean significados, lecturas, mnemónicos, imágenes de
radicales, audio ni frases de contexto. Los fixtures de test son mínimos,
escritos a mano y atribuidos.

**Por qué:** los términos de WaniKani y el respeto al trabajo de Tofugu. El pack
visual se genera con el token del usuario.

**Implica:** el repo se puede publicar sin material de WaniKani; el setup
incluye generar/copiar el pack.

---

## 2026-10-25 — Sesiones de 20 por defecto

**Decisión:** la sesión de repaso toma 20 ítems por defecto; “todas” es una
opción.

**Por qué:** en un aparato pequeño, una sesión de 200 ítems es una trampa. 20 es
una unidad manejable y se puede repetir.

**Implica:** el resumen ofrece “review more”; la cola se congela por sesión.

---

## 2026-10-25 — Nombre de trabajo KaniKey

**Decisión:** el proyecto se llama KaniKey mientras no haya uno mejor.

**Por qué:** *kani* + *key*, corto, evoca teclado y kanji, y no incluye
“WaniKani” en el nombre del producto (regla de marca).

**Implica:** el nombre se puede cambiar antes de publicar; los documentos y el
namespace pueden seguir usando `kanikey`.

---

## 2026-10-25 — Licencia MIT (propuesta)

**Decisión:** el código se publicaría con MIT, como Tangocho y Kakehashi.

**Por qué:** compatibilidad con el fork y con las ideas copiadas; es lo habitual
en este tipo de proyecto.

**Implica:** conservar los avisos de Tangocho/Kakehashi y las licencias de las
fuentes. El contenido de WaniKani no se licencia con el código.

---

## 2026-10-25 — Audio y orden de trazos fuera de la v1

**Decisión:** no se implementan en el MVP.

**Por qué:** añaden decodificación de audio, memoria, altavoz y datos extra. El
repaso escrito ya justifica el aparato.

**Implica:** el diseño deja sitio para ellos (ficha del ítem, ajustes, pack
visual) pero no los promete.
