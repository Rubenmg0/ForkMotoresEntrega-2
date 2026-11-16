# Fuentes del editor

Fuentes que carga `ModuleUI::LoadEditorFonts()` al arrancar. Esta carpeta tiene que acompañar siempre
al ejecutable (junto a `Assets/`): si falta, el editor arranca con la fuente por defecto de ImGui y sin iconos.

| Fichero | Uso | Origen | Licencia |
|---|---|---|---|
| `Roboto-Medium.ttf` | Texto de la interfaz (16 px) | [ocornut/imgui `misc/fonts`](https://github.com/ocornut/imgui/tree/master/misc/fonts) (Roboto, Christian Robertson / Google) | Apache License 2.0 — `LICENSE-Roboto.txt` |
| `fa-solid-900.ttf` | Iconos (Font Awesome 6 Free, estilo *Solid*), fusionados con la fuente de texto | [FortAwesome/Font-Awesome, rama `6.x`](https://github.com/FortAwesome/Font-Awesome/tree/6.x/webfonts) | SIL OFL 1.1 — `LICENSE-FontAwesome.txt` |

Los códigos de cada icono (`ICON_FA_*`) están en `Source/External/IconFontCppHeaders/IconsFontAwesome6.h`
([juliettef/IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders), licencia zlib). Esa cabecera y
`fa-solid-900.ttf` se generan a partir de la misma versión de Font Awesome: si se actualiza una, hay que actualizar la otra.
