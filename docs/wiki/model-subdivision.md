---
title: Character model subdivision and modelview
sources:
  - port/convert/conv_subdiv.c
  - port/convert/conv_subdiv.h
  - port/convert/conv_d43.c
  - port/convert/convert.h
  - port/src/romconvert.c
  - src/game/gun.c
  - src/game/front.c
  - src/game/initmenus.c
  - tests/test_subdiv.c
  - tools_pc/modelview/modelview.py
  - tools_pc/modelview/README.md
  - tools_pc/modelview/pc/mv_export.cpp
  - tools_pc/modelview/pc/modelview.cpp
  - port/assets/ge_texpacks.h
  - port/assets/ge_tex.h
  - port/assets/ge_png.c
verified: 5f029f7b
---

# Character model subdivision and modelview

The first-launch converter rounds the characters' heads: it subdivides them once with PN
triangles and keeps the original mesh as the far LOD. `tools_pc/modelview` shows the same
pass, and experimental ones, on the user's own models. Read this page before changing the
pass, its parameters, or the buffers that hold character models.

## What the converter does

`cv_subdiv_model()` ([conv_subdiv.c](../../port/convert/conv_subdiv.c)) runs from
[conv_d43.c](../../port/convert/conv_d43.c) on the decompressed N64 image of every model file
whose name starts with `C`. It rewrites the image; the unchanged converter then lays it out,
validates and compresses it. Other files never enter the pass and come out byte-identical.

| Part | Near | Far |
|---|---|---|
| Head: every `Chead*` file, and the head region of the 14 bodies with an integrated head | subdivided (PN, crease 130°) | original |
| Body: everything else, the first-person hand included | original | original (the game's simplified tier is dropped) |

- **Head region of a body:** triangles whose three corners load under the matrices of the op-2
  group with `JointID == 3` (SKEL_NECK). Bodies with an integrated head are those whose switch
  table has no head attachment (`Switches[4] == NULL`).
- **Subdivision:** vertices weld by rest position (joint-local position plus the summed op-2
  `Origin` up the tree); normals are area-weighted per weld class. An edge is split only when
  every triangle on it is a head triangle and it stays under one matrix, so neck edges and joint
  seams stay whole. A split edge gets a PN midpoint when it has two faces and a dihedral angle up
  to the crease limit, otherwise a straight one; a PN bulge above 0.15 of the edge length falls
  back to straight. UV and colour are linear midpoints. One midpoint per welded edge: no cracks,
  no T-junctions.
- **Display lists:** every rebuilt DL is self-contained (its own `G_MTX`, vertex runs in slots
  0..25) and keeps the original vertices at their original indices, which blood paint relies on.
  `verify_dl` re-parses every rebuilt DL during the conversion.
- **LOD:** a head part under a game LOD pair becomes near = subdivided, far = original; otherwise
  it gets a new pair, subdivided up to `CV_SD_NEAR_DIST` (30 m) and the original beyond.
  `Video.LodDistance` scales both like any game LOD. A body part's game LOD pair is collapsed to
  the detailed mesh.
- **Re-conversion:** `CV_CONVERTER_VERSION` ([convert.h](../../port/convert/convert.h)) is written
  to `pcmodels-<region>/converter.ver`. [romconvert.c](../../port/src/romconvert.c) converts the ROM
  again once at boot when it differs (a missing file counts as version 1; before version 4 only the
  models step ran).
- **Dev switch:** `GE_CONVERT_SUBDIV=0` in the converter's environment gives the pre-subdivision
  output, byte for byte.

Traps met while building it (vertex-cache reach, Secondary DLs, the fixed root node, editing the
N64 image): [porting-notes.md](../porting-notes.md) D62.

## Buffers that bound character file size

Bigger character files must fit three fixed buffers, all under `#ifdef PORT`:

| Buffer | File | Size |
|---|---|---|
| Bond body + head + prop chain (`size_item_buffer`) | [gun.c](../../src/game/gun.c) | `0x3E000` |
| Cast screen model region (`bufferRemaining`) | [front.c](../../src/game/front.c) | `0x46000` |
| Region holding the cast screen (pool + models + z-buffer) | [initmenus.c](../../src/game/initmenus.c) | `0xA6000` |

The first-person hand region (`0x18000`) is unchanged: the hand is a body part. A change to the
pass that grows the files re-sizes these from the new worst case
(decompressed size + about 554 bytes per texture marker) plus 25%.

## modelview

`python tools_pc/modelview/modelview.py pc` builds and opens the viewer. It reads the
characters from the character export (`characters/characters.json` + glTF,
[docs/specs/characters.md](../specs/characters.md)): `--export DIR`, else `MODELVIEW_EXPORT`,
else `tools_pc/modelview/data/export`, which `modelview.py export` writes from the user's ROM
(`ge007-export` in the build image). The viewer never reads the ROM; nothing ROM-derived is
ever tracked, and `shots` refuses an output directory inside the repo outside `data/`.

- Drop-down lists pick a **body** and a **head**, put together as `makeonebody` does (the head
  on the neck joint; a `has_head` body keeps its own), with the game's parameters: sunglasses,
  hair, the table's body scale. The animations are the body glTF's.
- **GAME** is the converter's own pass (it links `conv_subdiv.c`), so it shows what the game
  gets, here on the real neck seam. FLAT, PHONG, BUTTERFLY, LOOP and HEAD are viewer-only
  experiments.
- **Textures** are resolved by key (material `extras.texture`) through the active mod (`--mod`, partial: its
  `textures/<key>.png` and `textures.json` merge field by field) over the chosen texture pack (`--texpack`, complete;
  `ge007.ptexpack`, the export's own, by default), [docs/specs/texture-packs.md](../specs/texture-packs.md). The game folder
  with `texturepacks/` and `mods/` comes from `--game`, `MODELVIEW_GAME` or a build folder of the checkout; the panel's
  **Mod** and **Pack** lists show its entries, a pack that lacks textures of ge007 greyed out. The resolution is `ge_assets`' (`port/assets/ge_tex.h`, the same call
  the editor uses), compiled without zlib (`GE_PNG_NO_ZLIB`: its own inflate).
- `modelview.py selftest` checks the PNG reader, the pose maths and the HEAD method without an
  export. `modelview.py shots --out DIR` renders a grid of model (`BODY+HEAD`) × method.
- Controls, flags and the panel: [tools_pc/modelview/README.md](../../tools_pc/modelview/README.md).

## Tests

[tests/test_subdiv.c](../../tests/test_subdiv.c) (run with the other test executables): PN
midpoint values, crease and boundary edges straight, UV seams, one midpoint per shared edge,
the 0..3 split cases and their winding, the head/body rule and the neck edge.
