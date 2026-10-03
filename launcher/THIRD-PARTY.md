# Launcher licensing and credits

The Python launcher, including the adapted Mii editor, is distributed under
**GNU GPL version 3**; see `LICENSE`. This does not change the separate licenses
of the native game runtime, Qt, decomp-toolkit, or other dependencies.

## Team Wheel Wizard

Source: https://github.com/TeamWheelWizard/WheelWizard

Revision: `2a68c02d2c33c0acdf62a10b39e932abc42b634e` (retrieved 2026-10-03).
Authors: Team Wheel Wizard, including Patchzy and WantToBeeMe.
License: GPL-3.0 (copy in `licenses/WheelWizard-GPL-3.0.txt`).

Adaptations in this project:

- `mii_data.py`: Python implementation of Wii Mii serialization, field ranges,
  default records, Mii IDs, RNOD/RNHD database initialization and CRC handling,
  adapted from `MiiSerializer.cs`, `MiiRepositoryService.cs`, `MiiDbService.cs`,
  `MiiFactory.cs`, domain classes and `Shared/Binary/Crc.cs`.
- `assets/mii/*.svg`: feature picker drawings converted from
  `WheelWizard/Views/Styles/Resources/MiiIcons/*.axaml`. The converter is
  `scripts/release/import_mii_icons.py`. Paths and strokes are retained; template
  colors become a fixed sample palette for the thumbnails.
- `mii_editor.py`: a Qt editor using those feature pickers and Wii field ranges.
  The live 3D head view is implemented separately in `mii_scene.py` and
  `mii_preview.py`, reading the installed Wii RFL resource.

Modifications add atomic writes, checksum validation, backups, import merging,
concurrent-change detection and integration with the Resort installation.
Release packaging includes this launcher source, assets, build script and license
under `source/` so the adapted launcher can be rebuilt.

No Nintendo Mii artwork/database is included in these launcher sources or the
release payload. `RFL_DB.dat` is generated on the user's machine; `RFL_Res.dat`
is recovered locally from their disc's System Menu update package.

## Wii 3D preview references

`mii_scene.py` and `mii_preview.py` are an independently written resource reader,
face texture composer and OpenGL renderer. Format research used these primary
references (no third-party renderer source code or game assets are bundled):

- Revolution Face Library decompilation, particularly `RFL_Model.c`,
  `RFL_MakeTex.c`, `RFL_NANDLoader.c` and the face configuration headers:
  https://github.com/koopthekoopa/RFL
- ariankordi's Wii resource format documentation:
  https://gist.github.com/ariankordi/15c713e1208d7a5d534152dc276bab4a
- Qt's QOpenGLWidget lifecycle:
  https://doc.qt.io/qtforpython-6/PySide6/QtOpenGLWidgets/QOpenGLWidget.html

The view uses Wii fixed-point vertex/normal/UV data, triangulates the resource's
GX primitives, decodes its tiled textures, places/recolors facial features in
a mask, and attaches face/hair/nose/beard/glasses meshes. Lighting is a simple
preview material; in-game lighting, expressions and sport bodies can differ.
NumPy and PyOpenGL are additional launcher dependencies.
