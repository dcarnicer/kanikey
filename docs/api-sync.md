# KaniKey — API, caché y sincronización

> Diseño, 2026-10-25. Este documento es el contrato entre el firmware y la API
> de WaniKani, y entre el firmware y el almacenamiento local.

---

## 1. Principios

1. **WaniKani es la fuente de verdad.** KaniKey no reprograma repasos ni inventa
   etapas SRS. Solo cachea, muestra y envía.
2. **La caché es explícita.** Cada dato tiene un sitio, una vida útil y una regla
   de invalidación. Nada de “lo que haya en RAM”.
3. **La outbox es sagrada.** Un repaso completado se escribe en disco antes de
   que la pantalla avance. Un corte de luz no puede perderlo.
4. **Un fallo de red no es un fallo del repaso.** Se encola y se dice.
5. **Nunca un JSON completo en RAM.** La API devuelve páginas de hasta 1.000
   sujetos; se leen en streaming y se escriben a disco.
6. **Nada de contenido de WaniKani en el repositorio.** Los fixtures de test son
   mínimos, escritos a mano y atribuidos.

---

## 2. La API v2 en una página

| Concepto | Valor |
|---|---|
| Base | `https://api.wanikani.com/v2/` |
| Revisión | Cabecera `Wanikani-Revision: 20170710` |
| Auth | `Authorization: Bearer <token>` |
| Formato | JSON; colecciones con `data`, `pages`, `total_count` |
| Paginación | `page_after_id` / `page_before_id`; 500 por página (1.000 en subjects/reviews) |
| Filtros | `updated_after`, `ids`, `levels`, `subject_ids`, `immediately_available_for_*` |
| Rate limit | 60 peticiones/minuto; cabeceras `RateLimit-*` |
| Condicional | `If-None-Match` / `If-Modified-Since`; 304 sin cuerpo |
| Errores | 401, 403, 404, 409, 422, 429, 5xx; cuerpo `{error, code}` |
| Suscripción | `max_level_granted` (3 o 60); respetarlo siempre |
| Marca | El producto no se llama WaniKani; UI propia; sin arte oficial |

### 2.1 Colecciones y recursos

```
{
  "object": "collection",
  "url": "...",
  "pages": { "next_url": "...", "previous_url": null, "per_page": 500 },
  "total_count": 1600,
  "data_updated_at": "2017-11-29T19:37:03.571377Z",
  "data": [ ... ]
}
```

Un recurso es `{id, object, url, data_updated_at, data}`.

### 2.2 Fechas

ISO 8601 en UTC, con microsegundos y `Z`. KaniKey solo necesita segundos.
`available_at`, `started_at`, `passed_at`, `burned_at`, `created_at` pueden ser
`null`.

---

## 3. Endpoints usados

| Método | Endpoint | Para qué | Frecuencia |
|---|---|---|---|
| GET | `/user` | Usuario, nivel, suscripción, preferencias | Boot / 1 día |
| GET | `/summary` | Repasos/lecciones por hora, `next_reviews_at` | 30–60 min |
| GET | `/assignments` | Etapas, `available_at`, IDs de asignación | Cada sync |
| GET | `/subjects` | Significados, lecturas, componentes, mnemónicos | Al faltar IDs |
| GET | `/study_materials` | Sinónimos del usuario | Cada sync |
| POST | `/reviews` | Enviar un repaso completado | Por ítem |
| PUT | `/assignments/{id}/start` | Empezar una lección | Por lección |

Opcionales para la fase 2:

| Método | Endpoint | Para qué |
|---|---|---|
| GET | `/review_statistics` | Precisión por sujeto |
| GET | `/reviews` | Historial, racha, heatmap |
| GET | `/level_progressions` | Progreso de nivel |

### 3.1 `/user`

Campos que se guardan:

- `data.username`, `data.level`
- `data.subscription.active`, `type`, `max_level_granted`, `period_ends_at`
- `data.preferences.lessons_batch_size`
- `data.preferences.reviews_presentation_order` (`shuffled` /
  `lower_levels_first`)
- `data.preferences.reviews_display_srs_indicator`

`data.current_vacation_started_at` se muestra como aviso si no es `null`.

### 3.2 `/summary`

```
data.lessons[]: { available_at, subject_ids[] }
data.reviews[]: { available_at, subject_ids[] }
data.next_reviews_at
```

- Es un informe, no una colección.
- Los buckets son horarios. Para la home, se suman los `subject_ids` de los
  buckets con `available_at <= now`.
- La suma no sustituye a las asignaciones para repasar: solo da el contador.
- Se cachea 30–60 min; después de una sesión se ajusta localmente o se refresca.

### 3.3 `/assignments`

Filtros que se usan:

| Filtro | Uso |
|---|---|
| `immediately_available_for_review=true` | Cola de repaso |
| `immediately_available_for_lessons=true` | Lecciones |
| `updated_after=<ISO>` | Cambios desde el último sync |
| `hidden=false` | Excluir ocultos |
| `subject_ids=...` | Refrescar asignaciones concretas |
| `levels=...` | Limitar a niveles permitidos |

Campos:

```
id, subject_id, subject_type, srs_stage,
unlocked_at, started_at, passed_at, burned_at,
available_at, resurrected_at, hidden, data_updated_at
```

- `srs_stage` 0 = lección; 1–8 = repaso; 9 = burned.
- `available_at` puede ser `null` (no programado).
- `hidden=true` no se muestra ni se repasa.
- `subject_type` puede ser `radical`, `kanji`, `vocabulary`, `kana_vocabulary`.
- Para la cola: `srs_stage >= 1`, `available_at <= now`, `hidden=false`.
- Para lecciones: `srs_stage == 0`, `started_at == null`, `hidden=false`,
  `unlocked_at != null`.

### 3.4 `/subjects`

Filtros: `ids=...`, `types=...`, `levels=...`, `updated_after=...`.

Campos comunes:

```
id, object (radical|kanji|vocabulary|kana_vocabulary),
data.level, data.characters (puede ser null en radicales),
data.meanings[] { meaning, primary, accepted_answer },
data.auxiliary_meanings[] { meaning, type: whitelist|blacklist },
data.lesson_position, data.meaning_mnemonic, data.reading_mnemonic,
data.meaning_hint, data.reading_hint,
data.component_subject_ids[], data.amalgamation_subject_ids[],
data.visually_similar_subject_ids[],
data.character_images[] (solo radicales; SVG),
data.parts_of_speech[] (vocabulario),
data.context_sentences[] (vocabulario, fase 2),
data.pronunciation_audios[] (vocabulario, fase 2),
data.readings[] { reading, primary, accepted_answer, type }
```

- `type` de lectura: `onyomi`, `kunyomi`, `nanori` (kanji); en vocabulario suele
  faltar.
- `characters` de un radical puede ser `null`: hay que dibujar la imagen.
- Los mnemónicos llevan markup `<radical>`, `<kanji>`, `<vocabulary>`,
  `<meaning>`, `<reading>`; se limpia al mostrar.
- No se guardan `document_url`, `slug`, `created_at`, `hidden_at` ni
  `spaced_repetition_system_id` en la v1 (no hacen falta).

### 3.5 `/study_materials`

Campos: `subject_id`, `meaning_note`, `reading_note`, `meaning_synonyms[]`,
`hidden`. Se usan los sinónimos como respuestas correctas. Se piden solo para
los sujetos en caché, por `subject_ids` o `updated_after`.

### 3.6 `POST /reviews`

Cuerpo:

```json
{
  "review": {
    "assignment_id": 1422,
    "incorrect_meaning_answers": 1,
    "incorrect_reading_answers": 2,
    "created_at": "2026-10-25T08:42:13Z"
  }
}
```

- `incorrect_*` son obligatorios y pueden ser 0.
- `created_at` es opcional; si se omite, el servidor usa la hora de la petición.
- Debe ser pasado y posterior a `assignment.available_at`.
- La respuesta trae `resources_updated.assignment` y
  `resources_updated.review_statistic`; se aplican al estado local.
- No hay clave de idempotencia. Ver §6.

### 3.7 `PUT /assignments/{id}/start`

Cuerpo:

```json
{ "assignment": { "started_at": "2026-10-25T08:42:13Z" } }
```

- `started_at` es opcional; por defecto, ahora.
- Requiere `srs_stage == 0`, `started_at == null`, `unlocked_at != null` y nivel
  permitido.
- Devuelve la asignación con `srs_stage = 1` y `available_at`.

---

## 4. Token y permisos

- Token personal de WaniKani; nunca usuario/contraseña.
- Permisos necesarios (el portal de WaniKani los agrupa por recurso):
  - Lectura de **assignments**.
  - Lectura de **subjects**.
  - Lectura de **study materials**.
  - Lectura de **user** y **summary**.
  - Creación de **reviews**.
  - (Fase 2: lectura de **review statistics** y **reviews**.)
- Se guarda en NVS; se muestra enmascarado; se borra al cerrar sesión.
- Se valida con `GET /user` antes de guardarlo.
- Si el token no tiene permisos, `403`; se muestra qué falta y se ofrece
  actualizarlo.
- Si el token se revoca, `401`; se ofrece volver al setup.

---

## 5. Modelo de sincronización

### 5.1 Estados

| Estado | Qué significa | Qué muestra la UI |
|---|---|---|
| `cold` | Sin caché o sin token | Setup / primera sync |
| `warm` | Caché y token, red disponible | Home normal |
| `syncing` | Peticiones en curso | Progreso |
| `offline` | Sin red, con caché | `Offline · N cached · M to sync` |
| `stale` | Caché vieja (más de 24 h) | Aviso `Last sync 1d ago` |
| `auth_error` | 401/403 | `Token rejected` / `Missing permissions` |
| `server_error` | 5xx persistente | `WaniKani is not answering` |

### 5.2 Sync completa (cold)

```
1. Asegurar hora (SNTP o portal)
2. GET /user                     → usuario, nivel, max_level_granted
3. GET /summary                  → contadores
4. GET /assignments?hidden=false&levels=1..max_level_granted
                                 → todas las asignaciones (paginado)
5. Recolectar subject_ids de las asignaciones relevantes
6. GET /subjects?ids=...         → en lotes de 100
7. GET /study_materials          → sinónimos
8. Guardar meta y ETags
```

- La caché inicial puede ser grande (hasta ~10.000 asignaciones). Se guarda en
  LittleFS; los sujetos solo de los IDs relevantes (repasos/lecciones), no todos.
- Si el usuario quiere caché completa offline, un ajuste “Download all subjects”
  recorre todas las asignaciones y guarda los sujetos en la SD.
- `GET /assignments?hidden=false` puede ser varias páginas; se escribe según
  llegan.

### 5.3 Sync caliente

```
1. Si hay outbox: flush (ver §6)
2. GET /user (ETag)
3. GET /summary (TTL 30 min)
4. GET /assignments?updated_after=<last_sync>
5. GET /assignments?immediately_available_for_review=true
6. GET /assignments?immediately_available_for_lessons=true
7. Para IDs nuevos: GET /subjects?ids=...
8. GET /study_materials?updated_after=<last_sync>
9. Guardar meta
```

- El orden importa: la outbox primero, para que las asignaciones que devuelve el
  servidor ya reflejen los repasos enviados.
- Si el flush falla por red, se pasa a offline y no se piden asignaciones.
- Si el flush falla por 422, se reconcilia el ítem y se sigue.
- `updated_after` puede devolver cambios de asignaciones que no están en la cola
  (porque se repasaron en otro dispositivo); se aplican.

### 5.4 Sesión de repaso

```
session_start:
  - si hay red y last_sync > 15 min: sync
  - due = assignments con stage 1..8, available_at <= now, hidden=false
  - filtrar los que ya están en outbox
  - ordenar según reviews_presentation_order (shuffled por defecto; si es
    lower_levels_first, respetarlo) y congelar la lista
  - para cada subject_id sin sujeto: sync de subjects (si hay red)
  - si falta un sujeto: no ofrecer el ítem; mostrar aviso

item_complete:
  - escribir outbox (assignment_id, counts, created_at, subject_id)
  - si hay red: intentar POST inmediato
      - ok: aplicar assignment, borrar de outbox
      - 429/5xx/timeout: dejar pending
  - si no hay red: dejar pending
  - avanzar
```

- La lista de sesión no cambia si el servidor añade repasos mientras tanto.
- Los ítems completados se quitan de la cola local aunque el POST quede pending.
- Al final, el resumen muestra `N waiting to sync` si queda outbox.

### 5.5 Después de sincronizar

- `summary` se refresca; los contadores locales se recalculan.
- Si el servidor devuelve asignaciones nuevas, la home muestra `N new reviews`.
- Si un ítem de la outbox fue rechazado (422), se elimina, se refresca la
  asignación y se muestra `1 review could not be saved`.
- Si el token caduca, se conserva la outbox y se pide reautenticación.

---

## 6. Outbox y envíos ambiguos

### 6.1 Registro

```
seq              u32   monotónico local
assignment_id    u32
subject_id       u32
incorrect_meaning u16
incorrect_reading u16
created_at       u32   epoch UTC; 0 = omitir
state            u8    pending | sending | ambiguous | rejected
attempts         u8
crc32            u32
```

- Se escribe en LittleFS con `temp + rename` y una copia `.bak`.
- `sending` se marca antes del POST; si hay un corte, al arrancar se trata como
  `ambiguous`, no como `pending` a ciegas.
- `rejected` se conserva hasta que el usuario lo vea; luego se borra.

### 6.2 Flush

1. Ordenar por `seq`.
2. Para cada `pending`:
   - POST.
   - 200: aplicar `resources_updated.assignment`; borrar.
   - 401/403: parar y pedir auth.
   - 422/409: marcar `rejected`; reconciliar.
   - 429: esperar `RateLimit-Reset`; reintentar.
   - 5xx/timeout: marcar `ambiguous`; parar el flush (no seguir con los
     siguientes para no crear un lío).
3. Los `ambiguous` se resuelven antes de reintentar:
   - `GET /assignments/{assignment_id}`.
   - Si `data_updated_at` es posterior a nuestro `created_at` y la etapa SRS
     cambió, asumir que el repaso llegó; borrar.
   - Si la asignación sigue con la misma etapa y `available_at` anterior a
     `created_at`, el POST no llegó; volver a `pending`.
   - Si no se puede decidir, mostrar `Needs attention` y no duplicar.

### 6.3 Por qué no reintentar a ciegas

La API no acepta una clave de idempotencia. Un timeout después de que el servidor
procesara el repaso produce un duplicado si se reenvía. Duplicar un repaso altera
la etapa SRS y las estadísticas del usuario. Preferimos dejar el ítem en
`ambiguous` y reconciliar antes que arriesgar.

### 6.4 Limitaciones

- La reconciliación es heurística: otro dispositivo podría haber repasado el
  mismo ítem en la misma ventana.
- Si el usuario repasa en KaniKey y en la web a la vez, puede haber conflictos.
  La UI avisa y prioriza no duplicar.
- La outbox no se sincroniza entre dispositivos.

---

## 7. Caché

| Dato | Dónde | Vida | Invalidación |
|---|---|---|---|
| Token, Wi-Fi, ajustes | NVS | Hasta logout | Acción del usuario |
| `user` | LittleFS | 1 día | Sync / 401 |
| `summary` | LittleFS | 30–60 min | Sync / tras sesión |
| Assignments | LittleFS | Hasta sync | `updated_after`, respuesta de review |
| Subjects | microSD | Inmutable por ID | `data_updated_at` si se refresca |
| Study materials | LittleFS | Hasta sync | `updated_after` |
| Pack visual | microSD | Hasta regenerar | Versión del builder / caracteres nuevos |
| Stats locales | LittleFS | Persistente | Reset manual |

### 7.1 Tamaños

- Assignments: ~10.000 × 32 B ≈ 320 KB; cabe en LittleFS.
- Outbox: 100 × 32 B ≈ 3,2 KB.
- Subjects: ~300 B de media + mnemónicos; ~20 MB si se cachean todos; por eso
  viven en la SD y se piden por ID.
- Pack visual: 7–9 MB; SD.

### 7.2 Escritura atómica

- Escribir `.tmp`, `fsync`/`close`, renombrar sobre el destino.
- Mantener `.bak` de assignments y outbox.
- Al arrancar, si el principal falla CRC, usar `.bak` y avisar.
- Nunca borrar la outbox antes de que el POST esté confirmado.

---

## 8. Parser JSON en streaming

La API devuelve páginas de hasta 1.000 sujetos; un DOM de ArduinoJson no cabe.
Diseño:

```
HTTPClient / WiFiClientSecure
        │ bytes
        ▼
JsonStream (buffer 4 KB)
        │ eventos: object_start, key, string, number, bool, null, array_start...
        ▼
SubjectSink / AssignmentSink / UserSink / SummarySink
        │ registros
        ▼
store (LittleFS o SD)
```

- Parser propio, sin dependencias, con una pila de contextos de profundidad
  máxima 8.
- Decodifica escapes `\uXXXX` (incluidos pares surrogados) a UTF-8.
- Los strings largos (mnemónicos) se escriben directamente al registro; no se
  copian a un `std::string` global.
- Un objeto de sujeto se construye en una estructura fija de ~8 KB; si un
  mnemónico excede el budget, se guarda truncado con un flag.
- Si el parser falla, se descarta la página, se reintenta una vez y se muestra
  `Bad response from WaniKani`.
- Tests nativos con fixtures y fuzzing.

Alternativa descartada: descargar el JSON a la SD y parsearlo después con un DOM.
Sigue necesitando un DOM y duplica el almacenamiento temporal.

---

## 9. Tiempo

- La API usa UTC. La UI muestra hora local.
- La Cardputer no tiene RTC con batería; la hora se aprende por:
  1. SNTP al conectar.
  2. `POST /time` del portal de setup (hora del navegador).
  3. Última hora conocida guardada en NVS.
- `clock_trusted` solo es true si la hora se sincronizó en este arranque o si
  hay una hora guardada con menos de X horas y no ha habido corte.
- Repaso offline:
  - Si `clock_trusted`: `created_at` = hora de finalización.
  - Si no: `created_at = 0`, se omite; el servidor usará la hora del envío.
- Al enviar un `created_at`, comprobar que es posterior a `available_at`; si no,
  omitirlo.
- Zona horaria: se guarda un offset/POSIX TZ para mostrar; no afecta a la API.

---

## 10. Errores, reintentos y rate limit

| Código | Significado | Acción |
|---|---|---|
| 200 | OK | Aplicar |
| 304 | No modificado | Usar caché |
| 401 | Token inválido | Parar, pedir token |
| 403 | Sin permisos / suscripción | Parar, explicar |
| 404 | Recurso no existe | Descartar, refrescar |
| 409 | Conflicto | Reconciliar |
| 422 | Validación | Reconciliar, no reintentar |
| 429 | Rate limit | Esperar `RateLimit-Reset` |
| 5xx | Servidor | Reintentar con backoff |
| timeout / DNS / TLS | Red | 2 reintentos, luego offline |

- Backoff: 1 s, 2 s, 4 s, 8 s, con jitter; máximo 60 s.
- 429: usar `RateLimit-Reset` (epoch) si está; si no, 60 s.
- Presupuesto: una sync normal < 30 s; una cold < 2 min.
- No se hacen peticiones en paralelo. Una a una.
- La radio se apaga al terminar.

---

## 11. Formato de almacenamiento

### 11.1 Sujeto

```
SubjectRecord (fixed)
  id                u32
  type              u8
  level             u8
  flags             u16   has_characters, hidden, has_mnemonic
  lesson_position   u16
  meaning_count     u8
  reading_count     u8
  component_count   u16
  similar_count     u16
  chars_off         u32   offset al pool
  meanings_off      u32
  readings_off      u32
  auxiliary_off     u32
  components_off    u32
  similar_off       u32
  meaning_mnemonic_off u32
  reading_mnemonic_off u32
  data_updated_at   u32
```

Pool de strings: longitud `u16` + UTF-8, alineado a 2 bytes. Las listas son
`count` + `count` referencias de 8 bytes (`offset`, flags/tipo).

### 11.2 Asignación

```
AssignmentRecord (fixed, 32 B)
  assignment_id     u32
  subject_id        u32
  srs_stage         u8
  subject_type      u8
  flags             u8   hidden, started, passed, burned, pending_review
  available_at      u32  0 = null
  started_at        u32
  unlocked_at       u32
  data_updated_at   u32
```

Se guardan ordenadas por `subject_id`; se reescribe el archivo en cada sync
(320 KB es rápido en LittleFS).

### 11.3 Outbox

Ver §6.1. Archivo append-only; se compacta al vaciar.

### 11.4 Meta

```
magic "KKM1", version u16
user_id, username, level, max_level_granted
last_user_sync, last_summary_sync, last_assignment_sync, last_outbox_flush
etag_user, etag_summary (hash o string corto)
clock_trusted, last_known_utc
schema_version
```

### 11.5 Estadísticas locales

Contadores de sesión y lifetime: repasos, aciertos, fallos por tipo, tiempo,
etapa inicial/final. Se pueden reconstruir parcialmente; no son la fuente de
verdad.

---

## 12. Fixtures y pruebas

Fixtures mínimos, escritos a mano, con atribución:

- `user.json`: nivel, suscripción, preferencias.
- `summary.json`: buckets de repasos/lecciones.
- `assignments.json`: radical, kanji, vocab, kana; stage 0/1/9; hidden.
- `subjects.json`: radical con `characters: null`, kanji con on/kun/nanori,
  vocab con contexto/audio, kana vocab.
- `study_materials.json`: sinónimos.
- `review_ok.json`: `resources_updated.assignment`.
- `errors.json`: 401, 403, 422, 429.

Pruebas:

- Parseo de cada fixture al modelo y vuelta al formato binario.
- Páginas encadenadas con `next_url`.
- `updated_after` y ETag/304.
- Outbox: ok, 429, timeout, 422, corte a mitad, `.bak`.
- Reconciliación: asignación actualizada, no actualizada, ambigua.
- Reloj: `created_at` válido, anterior a `available_at`, no fiable.
- Rate limit: respeta `Reset`.

---

## 13. Decisiones abiertas

| # | Decisión | Recomendación |
|---|---|---|
| 1 | ¿Guardar todos los sujetos o solo los de la cola? | Solo los de la cola; “download all” opcional |
| 2 | ¿Mnemónicos en la caché base? | Sí, para lecciones offline; truncar si hace falta |
| 3 | ¿`updated_after` o full sync? | Delta con `updated_after`; full en cold |
| 4 | ¿Reintentar POST ambiguo? | No; reconciliar primero |
| 5 | ¿Enviar `created_at` offline? | Solo si el reloj es fiable; si no, omitir |
| 6 | ¿Lecciones offline? | Fase 2; v1 requiere red para `start` |
| 7 | ¿Sinónimos de usuario? | Sí, `study_materials`; se muestran en el detalle |
| 8 | ¿Guardar audio/contexto? | Fase 2; no en la v1 |
