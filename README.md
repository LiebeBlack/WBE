# S.M.A.C Web Core

**Navegador web ultra-minimalista para Windows**, diseñado para hardware de muy
bajos recursos (target de desarrollo: **Intel Celeron N4120**, 4 núcleos / 4
hilos, gráficos UHD 600) con un objetivo de consumo estricto: **< 300 MB de RAM
reproduciendo YouTube a 1080p**.

No es un "reproductor de YouTube": es un **navegador general completo** —
pestañas con suspensión automática, omnibox con buscador configurable,
favoritos, historial, descargas, zoom, búsqueda en página, pantalla completa y
restauración de sesión — construido sobre el motor **WebView2** (Chromium) con
una shell **Win32 pura** y **uBlock Origin** integrado.

## Arquitectura (Separación de Responsabilidades)

```
src/
├── main.cpp              Punto de entrada (wWinMain, 3 líneas reales)
├── app/                  Orquestación (App, Log)
├── window/               Shell Win32: frameless, DWM, tira, omnibox, menús
├── browser/              Motor: entorno, pestañas, uBO, cirugía DOM
├── system/               Memoria (mimalloc+trim), prioridades, rutas
└── config/               smac.ini
```

Cada módulo expone una interfaz estrecha (`ClientBridge`, `TabManager::Delegate`,
`TabHost`): **ningún módulo inferior conoce a la App** y `window/` no sabe que
existe WebView2.

## Optimizaciones clave

| Área | Medida |
|---|---|
| Shell | Frameless real (`WM_NCCALCSIZE=0`), fondo `#000000` compuesto por DWM, borde neón de 1 px (`DWMWA_BORDER_COLOR`), drag/snap/redimensionado 100 % nativos (`WM_NCHITTEST`) |
| Motor | `--renderer-process-limit=2`, `--max_old_space_size=350`, `--disable-features=IsolateOrigins,site-per-process`, telemetría/sync/crash-report **OFF**, `ExclusiveUserDataFolderAccess` |
| GPU | `--enable-zero-copy`, `--enable-gpu-rasterization`, `--ignore-gpu-blocklist`, `--enable-features=Vulkan` (conmutable en el ini) |
| RAM | `mimalloc` estático sustituyendo `new/delete`, trimming del Working Set (host + hijos) tras 30 s de inactividad (nunca con audio), pestañas de fondo **suspendidas** (`TrySuspend`), cap configurable de pestañas vivas |
| Scheduler | Host a `ABOVE_NORMAL_PRIORITY_CLASS`; renderers a `BELOW_NORMAL`; el GPU process lo gestiona Chromium |
| Binario | C++26, `-Os`, ThinLTO, `-fno-exceptions`, `-fno-rtti`, `-march=goldmont-plus` (SSE4.2; **sin AVX**), CRT estático, loader estático de WebView2 |

## Requisitos

* Windows 10 1809+ o Windows 11 (x64).
* **WebView2 Runtime Evergreen** (incluido en Windows 11; en Windows 10:
  https://developer.microsoft.com/microsoft-edge/webview2/).
* Para compilar: CMake ≥ 3.28, Ninja, **clang-cl** (LLVM 17+) y el Windows SDK.

## Compilar

```bat
cmake -S . -B build -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --parallel
```

El SDK de WebView2 (1.0.2957.106) y **mimalloc** se descargan solos durante la
configuración. El resultado es `build/SMAC-WebCore.exe`: un binario
autocontenido (sin DLLs propias).

Los tags `v*` publican un release con el exe + uBlock Origin empaquetados
(`.github/workflows/release.yml`).

## uBlock Origin

1. El release de CI ya lo trae en `extensions/uBlock`.
2. Si compilas a mano, descarga el zip "chromium" de
   https://github.com/gorhill/uBlock/releases, descomprímelo y deja la carpeta
   como `extensions/uBlock` (con `manifest.json` en su raíz) junto al exe.

La instalación usa la API oficial `ICoreWebView2Profile7::AddBrowserExtension`
con `AreBrowserExtensionsEnabled=TRUE`. Nota: el popup de ajustes de uBO no es
accesible desde WebView2; los filtros sí se aplican. Complementa con la
**cirugía DOM** propia (script inyectado en cada documento que elimina Shorts,
sidebar, chat y estanterías de YouTube antes de pintarlos).

## Configuración (`%LOCALAPPDATA%\SMAC\WebCore\smac.ini`)

```ini
[window]
accent=cyan            ; cyan | purple | green | off
maximized=0

[engine]
vulkan=1               ; 0 por si el driver UHD 600 de tu equipo falla
v8HeapMB=350
rendererLimit=2
homepage=smac://newtab
searchEngine=https://duckduckgo.com/?q=%s

[system]
trimIdleSeconds=30
maxAliveTabs=8
childrenBelowNormal=1
```

## Medir el objetivo de RAM

1. Abre YouTube, reproduce un vídeo 1080p.
2. Administrador de tareas → detalles → suma de `SMAC-WebCore.exe` +
   todos los `msedgewebview2.exe` hijos (memoria privada de trabajo).
3. Tras 30 s sin tocar nada, el trim pagina las páginas inactivas: el consumo
   baja aún más (con audio activo el trim se omite a propósito).

## Atajos

`Ctrl+L` omnibox · `Ctrl+T` nueva pestaña · `Ctrl+W` cerrar · `Ctrl+Tab`
cambiar · `Ctrl+1..9` saltar a pestaña · `Ctrl+/-/0` zoom · `Ctrl+F` buscar ·
`F5` recargar · `F11` pantalla completa · `Alt+←/→` atrás/adelante.

## Licencia

MIT (ver `LICENSE`).
