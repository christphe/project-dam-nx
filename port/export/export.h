/*
 * export.h -- static level export (issue #150): ROM -> level decor mesh +
 * textures -> glTF 2.0 (.gltf + .bin) + the base texture pack (PNG).
 *
 * CLEAN ROOM: everything in port/export/ is written from docs/specs/ only.
 * No decomp header, no src/game, no include/, no PR/gbi.h, no code derived
 * from them. Freestanding like port/convert/: libc + zlib (+ libm), and the
 * converter library for the ROM file table, rarezip inflate and the ROM check
 * (port/convert/convert.h, conv_romcheck.h).
 *
 * Output is ROM-derived: it goes only to the directory given, never into the
 * repo or a mod.
 */
#ifndef PORT_EXPORT_H
#define PORT_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EX_EXPORTER_VERSION 14  /* written to every glTF (asset.extras.exporter_version); bump on any output change */

/* ---- textures (tex_decode.c, docs/specs/textures.md) --------------------- */

typedef struct ExTexTable {
    const uint8_t *rom;        /* whole .z64 image (big-endian), not owned */
    size_t rom_size;
    uint32_t images_start;     /* ROM offset of the images segment */
    uint32_t count;            /* textures in the size table (before the 0xFFFF terminator) */
    uint32_t *offset;          /* count + 1 entries: start of texture n, relative to images_start; offset[count] = total */
    uint32_t *word1;           /* count entries: word 1 of the size-table entry (detail fields), raw */
    uint8_t *cls;              /* count entries: bits 31-24 of word 0 of the size-table entry (impact sound << 4 | surface) */
} ExTexTable;

typedef struct ExTexture {
    int texnum;
    int width, height;         /* level 0 */
    int format;                /* 0..12, textures.md */
    int zlib;                  /* 1 = zlib family, 0 = bit-packed */
    int method;                /* bit-packed method 0..9; -1 for zlib */
    int explicit_lods;         /* header bit 7 (the UV half-texel rule needs it) */
    uint8_t *rgba;             /* width * height * 4, row 0 at the top; malloc'd */
} ExTexture;

/* Locates the size table and the images segment for this ROM's region
 * ("ntsc-final" uses the measured constants; other regions: the checked search
 * of textures.md, or failure). 0 on success; on failure *err names why. */
int ex_tex_table_open(ExTexTable *t, const uint8_t *rom, size_t rom_size, const char *region, const char **err);
void ex_tex_table_close(ExTexTable *t);

/* Decodes level 0 of texture `texnum` to RGBA8. 0 on success; on failure
 * *err names why (unknown format / method, size out of range, truncated). */
int ex_tex_decode(const ExTexTable *t, int texnum, ExTexture *out, const char **err);
void ex_tex_free(ExTexture *tex);

/* PNG: the exporter writes its pictures with ge_assets' writer (port/assets/ge_png.h: gePngWrite / gePngEncode). */

typedef void (*ExLogFn)(void *user, const char *line);

/* ---- the base texture pack (texpack_export.c, docs/specs/texture-packs.md) --- */

/* Progress of ex_export_textures: done of total textures. Set once, NULL to clear. */
typedef void (*ExProgressFn)(void *user, int done, int total);
void ex_set_progress(ExProgressFn fn, void *user);

/* Writes the game's base texture pack into the folder `pack_dir` (normally <out>/ge007.ptexpack): pack.json,
 * textures.json (all 2698 keys) and textures/<n>.png. The ROM must pass cv_romcheck. Returns 0 on success, else
 * the number of problems (1 when the ROM or the texture table is refused). `log` may be NULL. */
int ex_export_textures(const char *rom_path, const char *pack_dir, ExLogFn log, void *user);

/* ---- the base asset pack, ge007.passets (pack_export.c, docs/specs/asset-packs.md) ----
 * Writes every kind (textures at the root, levels/, models/, characters/, labels/, music/, sounds/) into <pack_path>.tmp/,
 * then packs it into an uncompressed tar at pack_path (or renames the folder there when as_folder). Returns the number of
 * parts that had failures, -1 when no pack could be written. */
int ex_export_pack(const char *rom_path, const char *pack_path, int as_folder, ExLogFn log, void *user);
/* The exporter_version in a pack's pack.json (a tar or a folder), -1 when it cannot be read. */
int ex_pack_version(const char *pack_path);
/* Writes pack_path when it is missing or from another exporter version, then removes old_texture_pack (the older
 * texturepacks/ge007.ptexpack; NULL or "" for none). 1 = already current, 0 = written, -1 = failed. */
int ex_ensure_base_asset_pack(const char *rom_path, const char *pack_path, const char *old_texture_pack, ExLogFn log, void *user);

/* ---- levels (level_export.c, docs/specs/level-decor.md, display-lists.md) */

/* Exports every decor file of the ROM (the levels of level-decor.md) into
 * <out_dir>/<level>/<level>.gltf + <level>.bin (its images point into ../ge007.ptexpack/textures/, the base pack of
 * ex_export_textures, which the caller writes first) and
 * <out_dir>/<level>/setup.json + setup_mp.json (setup_export.c, docs/specs/setup.md) and
 * <out_dir>/<level>/report.txt (opcode census, skipped commands, warnings).
 * The ROM must pass cv_romcheck. Returns the number of levels that failed
 * (0 = all exported). `log` may be NULL. */
int ex_export_levels(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

/* ---- object models (model_export.c, docs/specs/models.md) ---------------- */

/* Exports every object model of the model-number table into
 * <out_dir>/<file name without the trailing Z>/<same>.gltf + .bin (images: ../../ge007.ptexpack/textures/<n>.png,
 * relative to an export root where <out_dir> is <root>/models) and <out_dir>/models.json (one entry per model number: file, base scale,
 * bounding box, node and triangle counts, textures, notes). Returns the number
 * of models that failed, or 1000 + the count when the model table is refused
 * (its invariants do not hold for this ROM). `log` may be NULL. */
int ex_export_models(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

/* ---- game text (text_export.c, docs/specs/text.md) ---------------------- */

/* Exports the text banks of every language file set the ROM carries to <out_dir>/text_<lang>.json
 * and the item / mission folder / briefing / multiplayer name tables to <out_dir>/names.json.
 * Returns the number of failures (0 = all written). `log` may be NULL. */
int ex_export_text(const char *rom_path, const char *out_dir, ExLogFn log, void *user);
/* ---- fonts (font_export.c, docs/specs/fonts.md) ---- */

/* Exports the four fonts to <out_dir>/fonts.json and <out_dir>/fonts/{small,large,jp_narrow,jp_wide}.png (8-bit greyscale).
 * Returns the number of failures (0 = all written). `log` may be NULL. */
int ex_export_fonts(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

/* ---- characters (char_export.c, docs/specs/characters.md) ----------------- */

/* Exports every guard body (skinned, with the selected animations) and head (rigid) of the character table into
 * <out_dir>/<file name without C and Z>/<same>.gltf + .bin (images: ../../ge007.ptexpack/textures/<n>.png,
 * <out_dir> being <root>/characters) and <out_dir>/characters.json.
 * The first-person hand (id 41) is listed with kind "hand" and not exported. Returns the number of characters that
 * failed, or 1000 when the character table or the animation data is refused. `log` may be NULL. */
int ex_export_characters(const char *rom_path, const char *out_dir, ExLogFn log, void *user);

#ifdef __cplusplus
}
#endif

#endif
