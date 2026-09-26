# Carpeta de extensiones de S.M.A.C Web Core

Esta carpeta debe contener, en producción, el directorio `uBlock/` con
**uBlock Origin** descomprimido (build Chromium):

```
extensions/
└── uBlock/
    ├── manifest.json     <- debe existir en la raíz
    ├── background.html
    ├── js/ ...
```

## Cómo dejarla lista (si compilas a mano)

1. Descarga el zip `uBlock0_<versión>.chromium.zip` desde
   https://github.com/gorhill/uBlock/releases
2. Descomprímelo y renombra la carpeta resultante a `uBlock` dentro de
   `extensions/`.
3. Copia `extensions/` junto al ejecutable `SMAC-WebCore.exe`.

El release de CI hace todo esto automáticamente. Si la carpeta no existe,
S.M.A.C arranca igualmente (la cirugía DOM interna sigue activa) y lo
registra en el log.
