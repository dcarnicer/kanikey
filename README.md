# KaniKey

**Un terminal de repaso de bolsillo para WaniKani, pensado para la M5Stack Cardputer ADV.**

> **Estado: solo diseño (2026-10-25).** Todavía no hay firmware. Este repositorio
> guarda la propuesta de producto, UX, arquitectura y sincronización.
>
> KaniKey es una aplicación **no oficial** de terceros para WaniKani. No está
> afiliada, respaldada ni patrocinada por WaniKani ni por Tofugu LLC. WaniKani es
> una marca registrada de Tofugu LLC.

![Pantallas de KaniKey](docs/screens/overview.png)

## La idea

WaniKani se repasa escribiendo: significado en inglés y lectura en kana. La
Cardputer ADV tiene una pantalla de 240×135, Wi-Fi, tarjeta microSD, batería y,
sobre todo, un teclado real de 56 teclas. Eso la convierte en un aparato mucho
más natural para WaniKani que un reloj o una consola de dos botones: se abre,
aparece el repaso pendiente, se escribe la respuesta y se cierra. Sin teléfono,
sin notificaciones y sin distracciones.

KaniKey no sustituye a WaniKani: es una **terminal de repaso y lecciones** que
sincroniza con la cuenta del usuario. El SRS sigue viviendo en WaniKani.

## Qué hará (MVP)

- Sincronizar el resumen, las asignaciones y los sujetos que tocan.
- Repasar con **respuesta escrita**: significado y lectura, con conversión de
  romaji a kana mientras se teclea.
- Enseñar la respuesta correcta, los significados/lecturas aceptados y una
  pista breve cuando se falla; el ítem vuelve más tarde en la misma sesión.
- Enviar cada repaso a WaniKani al completarlo, con los contadores de fallos
  que pide la API.
- **Modo offline**: sesiones con la caché local y una cola de repasos pendientes
  que se sincroniza al volver la red.
- Lecciones (fase 2): ficha de significado, lecturas, mnemónico y componentes;
  después, `PUT /assignments/{id}/start`.
- Pantalla de inicio con repasos/lecciones pendientes, siguiente hora de repaso
  y distribución por etapas SRS.
- Ajustes, Wi-Fi, hora, OTA y estados de error claros.
- UI en inglés primero (los significados de WaniKani son en inglés); español
  después.

## Qué no hará (por ahora)

- No es una copia de la web de WaniKani ni de su interfaz.
- No trae contenido de WaniKani en el repositorio: significados, lecturas,
  mnemónicos, imágenes de radicales y audio se piden con el token personal del
  usuario y se guardan solo en su tarjeta.
- No implementa un SRS local para los repasos: la programación es la de
  WaniKani. (Un modo de práctica extra con FSRS podría llegar más adelante.)
- No incluye comunidad, foro, reseñas, notificaciones push ni app de móvil.
- No soporta otras placas en la v1. El HAL de Tangocho permite añadirlas, pero
  el diseño se optimiza para la Cardputer ADV.

## Hardware objetivo

| | Cardputer ADV |
|---|---|
| MCU | ESP32-S3FN8, 8 MB flash, **sin PSRAM** |
| Pantalla | 1,14" ST7789V2, 240×135, solo horizontal |
| Teclado | 56 teclas (4×14) tras un TCA8418; G0 aparte |
| Almacenamiento | microSD FAT32 + LittleFS |
| Red | Wi-Fi 2,4 GHz |
| Batería | sí; sueño profundo con despertar por tecla |
| Audio | codec ES8311 + altavoz (fase posterior) |

La parte más difícil —teclado, pantalla, microSD, Wi-Fi/TLS, sueño y OTA— ya
está resuelta y probada en hardware en
[Tangocho](https://github.com/dcarnicer/tamagotcho). KaniKey reutiliza esa capa.

## Documentos

| Documento | Qué contiene |
|---|---|
| [docs/design.md](docs/design.md) | Producto, alcance, arquitectura, memoria, riesgos y hoja de ruta |
| [docs/ux.md](docs/ux.md) | Lenguaje visual, teclado, flujos y pantalla a pantalla |
| [docs/api-sync.md](docs/api-sync.md) | API de WaniKani, caché, cola offline y modelo de datos |
| [docs/reuse-tangocho.md](docs/reuse-tangocho.md) | Qué se reutiliza de Tangocho y cómo se porta |
| [docs/setup.md](docs/setup.md) | Token, pack visual, primer arranque y problemas |
| [docs/decisions.md](docs/decisions.md) | Registro corto de decisiones propuestas |
| [docs/screens/](docs/screens/) | Mockups generados a 240×135 (4×) |

Los mockups se regeneran con:

```sh
python3 tools/mockups/render.py
```

El script reutiliza por defecto la Noto Sans JP que Tangocho descarga en
`tools/pack-builder/cache/noto/`. Si está en otra ruta:

```sh
KANIKEY_FONT=/ruta/NotoSansJP-Regular.otf python3 tools/mockups/render.py
```

## Nombre

**KaniKey** es un nombre de trabajo: *kani* (cangrejo, como en WaniKani) + *key*
(tecla). No incluye "WaniKani" en el nombre del producto, como piden las reglas
de marca de la API. Si el nombre no convence, se puede cambiar antes de publicar;
lo importante del diseño no depende de él.

## Licencia

Todavía por decidir. Si se reutiliza código de Tangocho (MIT), KaniKey tendrá que
incluir su aviso de copyright. La opción natural es MIT para el código y ningún
contenido de WaniKani en el repositorio.
