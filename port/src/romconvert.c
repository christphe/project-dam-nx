/*
 * romconvert.c — first-run sidecar conversion ("drop in your ROM and play").
 *
 * The engine needs two ROM-derived sidecar trees that are NOT shipped with a
 * release (they are copyrighted game data derived from the user's own ROM):
 *
 *     $S/pcmodels-<region>/   (pcmodels.bin + manifest.csv)  — port/src/pcmodels.c
 *     $S/pccg-<region>/       (pccg.bin    + manifest.csv)  — port/src/pccg.c
 *
 * The conversion is the in-process C converter (port/convert/, a port of the
 * old tools_pc/d43/d69/d88_emit.py scripts): when the sidecars are missing at
 * boot, romdataInit() calls romConvertEnsureSidecars(), which converts the ROM
 * into <exedir>/data/ and re-checks. The conversion is deterministic and takes
 * a few seconds; after the first run the sidecars exist and this path is a
 * no-op (two fsExists). On Switch the output lands next to the NRO
 * (sdmc:/switch/<app>/data/), since libnx starts with that as the CWD.
 *
 * The sidecars carry a converter version (pcmodels-<region>/converter.ver,
 * written by the converter). When it is missing or differs from the compiled
 * one (CV_CONVERTER_VERSION) everything is converted again, once, so a user
 * with older sidecars gets the current models and setups without deleting anything.
 *
 * After the sidecars, the game's base texture pack is made the same way:
 * <game>/texturepacks/ge007.ptexpack (port/export/texpack_export.c, the pack of
 * docs/specs/texture-packs.md) is written from the ROM when it is missing or its
 * exporter_version differs from the compiled one.
 *
 * Returns 1 when both sidecars are present (already or after converting),
 * -1 when they could not be produced (caller should abort boot with a
 * message rather than crash later in modelPromoteNodeOffsetsToPointers, D179).
 */

#include <stdlib.h>
#include <string.h>

/* D38: <stdio.h>/<stdlib.h> resolve to the decomp's N64 stubs via the include
 * path; declare the host functions this file uses. */
extern void *malloc(unsigned long long size);
extern int snprintf(char *str, size_t maxsize, const char *format, ...);

#include "platform.h"

#include "system.h"
#include "fs.h"
#include "bootscreen.h"
#include "texpack.h"
#include "ge_pkg.h"

/* Same country→region map as pcmodels.c / pccg.c (finding: plan Q3). */
static const char *rcRegionForCountry(unsigned char country)
{
    switch (country) {
    case 'E': return "ntsc-final";
    case 'P': return "pal-final";
    case 'J': return "jpn-final";
    default:  return NULL;
    }
}

/* sysResolvePath() returns a static buffer — copy before the next call. */
static int rcExistsResolved(const char *rel)
{
    const char *p = sysResolvePath(rel);
    return fsExists(p);
}

static int rcSidecarsPresent(const char *region)
{
    char rel[256];
    snprintf(rel, sizeof(rel), "$S/pcmodels-%s/pcmodels.bin", region);
    if (!rcExistsResolved(rel))
        return 0;
    snprintf(rel, sizeof(rel), "$S/pccg-%s/pccg.bin", region);
    return rcExistsResolved(rel);
}

/* Version of the model sidecars on disk: the number in converter.ver, 1 when
 * the file is missing (sidecars made before versioning), 0 when unreadable. */
static int rcModelsVersion(const char *region)
{
    char rel[256];
    char buf[16];
    const char *p;
    FSFile *f;
    int32_t n;
    int v = 0;

    snprintf(rel, sizeof(rel), "$S/pcmodels-%s/converter.ver", region);
    p = sysResolvePath(rel);
    if (!fsExists(p))
        return 1;
    f = fsOpen(p, "rb");
    if (!f)
        return 0;
    n = fsRead(f, buf, (int32_t)sizeof(buf) - 1);
    fsClose(f);
    for (int32_t i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v;
}

/* port/convert/conv_main.c (CV_STEP_ALL == 7). Declared here, not via
 * convert.h: this TU sees the decomp include path. */
extern int cv_convert_rom(const char *rom_path, const char *region, const char *out_root, int steps);
extern int cv_converter_version(void);
typedef void (*CvProgressFn)(void *user, int stage, int done, int total);
extern void cv_set_progress(CvProgressFn fn, void *user);

/* Models-only pass (version bump): the one stage fills the whole bar. */
static int s_modelsOnly;

/* Stages match convert.h (CV_NSTAGES == 3), weighted equally on the bar. */
static void rcProgress(void *user, int stage, int done, int total)
{
    static const char *const labels[3] = {
        "Models", "Stage backgrounds", "Stage setups"
    };
    float frac = total > 0 ? (float)done / (float)total : 0.f;
    (void)user;
    if (stage < 0 || stage > 2) return;
    if (s_modelsOnly) { bootscreenProgress(labels[stage], frac); return; }
    bootscreenProgress(labels[stage], ((float)stage + frac) / 3.f);
}

/* steps: CV_STEP_* mask (7 = everything, 1 = the models only) */
static int rcConvert(const char *rom, const char *out, int steps)
{
    int code;
    s_modelsOnly = steps == 1;
    bootscreenProgress("Starting", 0.f);
    cv_set_progress(rcProgress, NULL);
    code = cv_convert_rom(rom, NULL, out, steps);
    cv_set_progress(NULL, NULL);
    if (code == 0)
        bootscreenDone();
    bootscreenClose();
    return code;
}

/* port/export/pack_export.c, texpack_export.c, export_common.c. Declared here for the same reason as the converter. */
typedef void (*ExLogFn)(void *user, const char *line);
typedef void (*ExProgressFn)(void *user, int done, int total);
extern void ex_set_progress(ExProgressFn fn, void *user);
extern int ex_ensure_base_asset_pack(const char *rom_path, const char *pack_path, const char *old_texture_pack, ExLogFn log, void *user);
extern int ex_mkdir_p(const char *path);

static void rcTexProgress(void *user, int done, int total)
{
    (void)user;
    bootscreenProgress("Textures", total > 0 ? (float)done / (float)total : 0.f);
}

static void rcTexLog(void *user, const char *line)
{
    (void)user;
    sysLogPrintf(LOG_INFO, "romconvert: %s", line);
}

/* The base asset pack, $S/../assetpacks/ge007.passets (docs/specs/asset-packs.md). 1 = boot goes on. A pack that cannot be
 * written stops boot with a message (-1): the game reads its assets from the packs, not from the ROM. An older pack still in
 * place after a failed rewrite is kept and boot goes on. A pack written for the first time is enabled, last in the order; one
 * re-written after an exporter update keeps the player's choice. */
static int rcEnsureBasePack(const char *romRelPath)
{
    char romPath[1024];
    char packDir[1024], oldDir[1024], folder[1024];
    int code;

    snprintf(romPath, sizeof(romPath), "%s", sysResolvePath(romRelPath));
    snprintf(folder, sizeof(folder), "%s", sysResolvePath("$S/../assetpacks"));
    ex_mkdir_p(folder);
    snprintf(packDir, sizeof(packDir), "%s/ge007.passets", folder);
    snprintf(oldDir, sizeof(oldDir), "%s/ge007.ptexpack", sysResolvePath("$S/../texturepacks"));
    const int had = pkgExists(packDir);
    ex_set_progress(rcTexProgress, NULL);
    code = ex_ensure_base_asset_pack(romPath, packDir, oldDir, rcTexLog, NULL);
    ex_set_progress(NULL, NULL);
    if (code < 0) {
        if (pkgExists(packDir)) {   /* the rewrite failed but the older pack is still there */
            sysLogPrintf(LOG_WARNING, "romconvert: the base asset pack could not be rewritten; keeping the older %s", packDir);
            bootscreenClose();
            return 1;
        }
        sysLogPrintf(LOG_ERROR, "romconvert: the base asset pack could not be written to %s - cannot continue", packDir);
        bootscreenMessage("Asset pack not written",
            "The game's assets could not be written from your ROM\n"
            "into the assetpacks folder next to the game.\n"
            "Check that the folder can be written and that there is free space.\n"
            "The log has the details.");
        return -1;
    }
    if (code == 0) {
        sysLogPrintf(LOG_INFO, "romconvert: base asset pack written to %s", packDir);
        texpackPackWritten("ge007.passets", !had);
        bootscreenClose();
    }
    return 1;
}

int romConvertEnsureSidecars(const unsigned char *romImg, const char *romRelPath)
{
    const char *region = rcRegionForCountry(romImg[0x3E]);
    if (!region)
        return 1;   /* unknown country — romHeaderValid() rejects it anyway */

    const int present = rcSidecarsPresent(region);
    const int stale = present && rcModelsVersion(region) != cv_converter_version();
    if (present && !stale)
        return rcEnsureBasePack(romRelPath);

    /* Resolve the ROM path we were given ("$S/...", "$E/..." or "./...") to
     * an absolute form for the child's argv. */
    char romPath[1024];
    snprintf(romPath, sizeof(romPath), "%s", sysResolvePath(romRelPath));

    const char *exedir = sysGetExeDir();
    char outDir[1024];
    snprintf(outDir, sizeof(outDir), "%s", exedir[0] ? exedir : ".");

    if (stale)
        sysLogPrintf(LOG_INFO,
            "romconvert: sidecars are from an older converter - converting the ROM again (one-time)...");
    else
        sysLogPrintf(LOG_INFO,
            "romconvert: sidecars missing - converting the ROM (one-time, a few seconds)...");
    int code = rcConvert(romPath, outDir, 7);
    if (code != 0)
        sysLogPrintf(LOG_ERROR,
            "romconvert: conversion failed (code %d) - a non-retail or "
            "byte-swapped ROM will fail here", code);

    if (!rcSidecarsPresent(region)) {
        sysLogPrintf(LOG_ERROR,
            "romconvert: data/pcmodels-%s/ and data/pccg-%s/ are still "
            "missing — cannot continue", region, region);
        bootscreenMessage("Conversion failed",
            "The game data could not be generated from your ROM.\n"
            "Use an unmodified retail GoldenEye 007 ROM in big-endian .z64 format\n"
            "(byte-swapped .n64 / .v64 dumps are not supported).\n"
            "The log has the details.");
        return -1;
    }
    sysLogPrintf(LOG_INFO, "romconvert: sidecars %s — continuing boot",
                 stale ? "updated" : "generated");
    return rcEnsureBasePack(romRelPath);
}
