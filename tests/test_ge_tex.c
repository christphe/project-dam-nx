/* Texture resolution (port/assets/ge_tex.c, ge_assets): a key -> RGBA + entry through the mod over the chosen pack over
 * ge007.passets; the cache; the warnings; the pack list. Pure, no core. */
#include "ge_test.h"
#include "mission_dir.h"
#include "ge_pkg.h"
#include "ge_png.h"
#include "ge_tex.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#endif

#define MAXF 200
static char g_files[MAXF][700];
static char g_dirs[MAXF][700];
static int g_nf, g_nd;

static void mkd(const char *dir)
{
    for (int i = 0; i < g_nd; i++) if (!strcmp(g_dirs[i], dir)) return;
    missionMakeDir(dir);
    snprintf(g_dirs[g_nd++], sizeof g_dirs[0], "%s", dir);
}

static void putBytes(const char *root, const char *rel, const uint8_t *data, size_t n)
{
    char p[700];
    size_t k = strlen(root);
    FILE *f;
    mkd(root);
    memcpy(p, root, k);
    p[k++] = '/';
    for (const char *s = rel; *s; s++) {
        if (*s == '/') { p[k] = 0; mkd(p); }
        p[k++] = *s;
    }
    p[k] = 0;
    f = fopen(p, "wb");
    if (!f) { printf("  cannot create %s\n", p); return; }
    fwrite(data, 1, n, f);
    fclose(f);
    snprintf(g_files[g_nf++], sizeof g_files[0], "%s", p);
}

static void putText(const char *root, const char *rel, const char *text) { putBytes(root, rel, (const uint8_t *)text, strlen(text)); }

/* A 2x2 PNG of one colour. */
static void putPng(const char *root, const char *rel, int r, int g, int b, int a)
{
    uint8_t px[16], *png = NULL;
    size_t len = 0;
    for (int i = 0; i < 4; i++) { px[i * 4] = (uint8_t)r; px[i * 4 + 1] = (uint8_t)g; px[i * 4 + 2] = (uint8_t)b; px[i * 4 + 3] = (uint8_t)a; }
    if (gePngEncode(2, 2, px, 0, &png, &len)) putBytes(root, rel, png, len);
    free(png);
}

static int g_warns;
static char g_lastWarn[300];
static void onWarn(void *u, const char *msg) { (void)u; g_warns++; snprintf(g_lastWarn, sizeof g_lastWarn, "%s", msg); }

int main(void)
{
    char tmp[500], packs[600], base[600], hd[600], lack[600], mod[600], mod2[600];
    const char *t = getenv("TEMP");
    GeTexSet *s;
    GeTexImage im;

    GE_TEST_BEGIN("ge_tex");
    if (!t || !*t) t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    snprintf(tmp, sizeof tmp, "%s/ge_tex_test", t);
    for (char *c = tmp; *c; c++) if (*c == '\\') *c = '/';   /* a Windows TEMP has backslashes; geTexBaseRoot answers with slashes */
    snprintf(packs, sizeof packs, "%s/assetpacks", tmp);
    snprintf(base, sizeof base, "%s/ge007.passets", packs);
    snprintf(hd, sizeof hd, "%s/hd.passets", packs);
    snprintf(lack, sizeof lack, "%s/lack.ptexpack", packs);
    snprintf(mod, sizeof mod, "%s/m.pmod", tmp);
    snprintf(mod2, sizeof mod2, "%s/m2.pmod", tmp);
    mkd(tmp);
    mkd(packs);

    /* ge007: keys 5 (red, stone), 6 (blue, wood, alpha luminance), 7 (no picture) */
    putText(base, "pack.json", "{\"name\":\"GoldenEye 007\",\"base\":true,\"exporter_version\":11}");
    putText(base, "textures.json", "{\"version\":1,\"textures\":{\"5\":{\"image\":\"textures/5.png\",\"surface\":\"stone\",\"alpha\":\"image\",\"width\":32,\"height\":16},"
                                   "\"6\":{\"image\":\"textures/6.png\",\"surface\":\"wood\",\"alpha\":\"luminance\"},\"7\":{\"image\":null,\"surface\":\"glass\"}}}");
    putPng(base, "textures/5.png", 255, 0, 0, 255);
    putPng(base, "textures/6.png", 0, 0, 255, 255);
    /* hd: complete, other pictures */
    putText(hd, "pack.json", "{\"name\":\"HD\",\"author\":\"tests\"}");
    putText(hd, "textures.json", "{\"version\":1,\"textures\":{\"5\":{\"image\":\"textures/5.png\",\"surface\":\"stone\"},\"6\":{\"image\":\"textures/6.png\",\"surface\":\"wood\"},\"7\":{\"image\":null,\"surface\":\"glass\"}}}");
    putPng(hd, "textures/5.png", 0, 255, 0, 255);
    putPng(hd, "textures/6.png", 255, 255, 0, 255);
    /* lack: no key 6 */
    putText(lack, "textures.json", "{\"version\":1,\"textures\":{\"5\":{\"image\":\"textures/5.png\",\"surface\":\"stone\"},\"7\":{\"image\":null,\"surface\":\"glass\"}}}");
    putPng(lack, "textures/5.png", 9, 9, 9, 255);
    /* a mod: key 5's picture, key 6's surface only, a new key */
    putText(mod, "textures.json", "{\"version\":1,\"textures\":{\"6\":{\"surface\":\"metal\"},\"mymod/rusty\":{\"image\":\"textures/mymod/rusty.png\",\"surface\":\"metalobj\"},"
                                  "\"5\":{\"image\":\"textures/five.png\"}}}");
    putPng(mod, "textures/five.png", 1, 2, 3, 255);
    putPng(mod, "textures/mymod/rusty.png", 7, 7, 7, 255);
    /* mod2: its picture for key 5 is not a PNG: the pack's is used, with a warning */
    putText(mod2, "textures/5.png", "this is not a png");

    s = geTexSetNew();
    GE_CHECK(s != NULL);

    /* ge007 alone */
    geTexSetOpen(s, "", "", base);
    GE_CHECK(geTexSetBase(s) != NULL && geTexSetPack(s) == geTexSetBase(s) && geTexSetMod(s) == NULL && geTexSetNote(s)[0] == 0);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 1);
    GE_CHECK(im.w == 2 && im.h == 2 && im.rgba[0] == 255 && im.rgba[1] == 0 && im.source == GE_TEX_FROM_BASE && im.from == TEXPACK_FROM_PACK);
    GE_CHECK(!strcmp(texpackSurfaceName(im.tex.surface), "stone"));
    GE_CHECK(im.tex.width == 32 && im.tex.height == 16);
    GE_CHECK(strstr(im.path, "ge007.passets/textures/5.png") != NULL);
    {   /* cached: the same pixels, not decoded again */
        const uint8_t *first = im.rgba;
        GeTexImage again;
        GE_CHECK(geTexResolve(s, "5", &again, NULL, NULL) == 1 && again.rgba == first);
    }
    GE_CHECK(geTexResolve(s, "6", &im, NULL, NULL) == 1 && im.tex.alphaLuminance == 1 && im.rgba[3] == (uint8_t)((114 * 255) / 1000));   /* luminance alpha */
    GE_CHECK(geTexResolve(s, "7", &im, NULL, NULL) == 0);          /* no picture */
    GE_CHECK(geTexResolve(s, "99", &im, NULL, NULL) == 0);         /* unknown */
    GE_CHECK(geTexResolve(s, "Bad Key", &im, NULL, NULL) == 0);    /* not a valid key */

    /* another complete pack */
    geTexSetOpen(s, "", hd, base);
    GE_CHECK(geTexSetPack(s) != geTexSetBase(s) && geTexSetNote(s)[0] == 0);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 1 && im.rgba[0] == 0 && im.rgba[1] == 255 && im.source == GE_TEX_FROM_PACK);

    /* an incomplete pack is refused: ge007 stays */
    geTexSetOpen(s, "", lack, base);
    GE_CHECK(geTexSetPack(s) == geTexSetBase(s) && strstr(geTexSetNote(s), "incomplete") != NULL);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 1 && im.rgba[0] == 255);
    geTexSetOpen(s, "", hd, "");
    GE_CHECK(geTexSetPack(s) == NULL && strstr(geTexSetNote(s), "no ge007.passets") != NULL);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 0);

    /* the mod over the pack, field by field */
    geTexSetOpen(s, mod, hd, base);
    GE_CHECK(geTexSetMod(s) != NULL);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 1 && im.rgba[0] == 1 && im.rgba[2] == 3 && im.source == GE_TEX_FROM_MOD && im.from == TEXPACK_FROM_MOD);
    GE_CHECK(geTexResolve(s, "6", &im, NULL, NULL) == 1 && im.rgba[0] == 255 && im.rgba[1] == 255 && im.source == GE_TEX_FROM_PACK   /* the pack's picture */
             && !strcmp(texpackSurfaceName(im.tex.surface), "metal"));                                                                  /* the mod's surface */
    GE_CHECK(geTexResolve(s, "mymod/rusty", &im, NULL, NULL) == 1 && im.rgba[0] == 7 && !strcmp(texpackSurfaceName(im.tex.surface), "metalobj"));
    /* the mod with ge007 as the chosen pack */
    geTexSetOpen(s, mod, "", base);
    GE_CHECK(geTexResolve(s, "6", &im, NULL, NULL) == 1 && im.rgba[0] == 0 && im.rgba[2] == 255 && !strcmp(texpackSurfaceName(im.tex.surface), "metal"));

    /* a mod picture that does not decode gives way to the pack's, with a warning (also on the cached answer) */
    geTexSetOpen(s, mod2, "", base);
    g_warns = 0;
    GE_CHECK(geTexResolve(s, "5", &im, onWarn, NULL) == 1 && im.rgba[0] == 255 && g_warns == 1 && strstr(g_lastWarn, "not readable") != NULL);
    GE_CHECK(geTexResolve(s, "5", &im, onWarn, NULL) == 1 && g_warns == 2);
    /* the cache is dropped by every open: the mod is read afresh */
    putPng(mod2, "textures/5.png", 4, 5, 6, 255);
    geTexSetOpen(s, mod2, "", base);
    GE_CHECK(geTexResolve(s, "5", &im, NULL, NULL) == 1 && im.rgba[0] == 4 && im.source == GE_TEX_FROM_MOD);

    /* the pack list: ge007 first, then the folder's packs, each checked against ge007 */
    {
        GeTexPackChoice c[8];
        char root[1024];
        int n = geTexPackList(packs, "", c, 8);
        GE_CHECK_EQ_INT(n, 3);
        GE_CHECK(n == 3 && c[0].base && !c[0].reason[0] && !strcmp(c[0].name, "GoldenEye 007") && !strcmp(c[1].file, "hd.passets") && !c[1].reason[0]
                 && !strcmp(c[1].name, "HD") && !strcmp(c[2].file, "lack.ptexpack") && c[2].missing == 1 && c[2].reason[0]);
        geTexBaseRoot(packs, "", root, sizeof root);
        GE_CHECK(strstr(root, "ge007.passets") != NULL);
        geTexBaseRoot("", "", root, sizeof root);
        GE_CHECK(root[0] == 0);
        n = geTexPackList("", "", c, 8);
        GE_CHECK(n == 1 && c[0].base && c[0].reason[0] && !c[0].path[0]);
        geTexPackPath("hd.passets", packs, "", root, sizeof root);
        GE_CHECK(strstr(root, "assetpacks/hd.passets") != NULL);
        geTexPackPath("ge007.ptexpack", packs, "", root, sizeof root);   /* the older base name stands for the base too */
        GE_CHECK(strstr(root, "assetpacks/ge007.passets") != NULL);
        geTexBaseRoot("", base, root, sizeof root);                     /* an export root that is the pack itself */
        GE_CHECK(!strcmp(root, base));
        geTexBaseRoot("", tmp, root, sizeof root);                      /* no packs folder and no pack in the export root */
        GE_CHECK(root[0] == 0);
        geTexBaseRoot("", packs, root, sizeof root);                    /* an export root holding the pack */
        GE_CHECK(strstr(root, "ge007.passets") != NULL);
    }

    geTexSetFree(s);
    for (int i = 0; i < g_nf; i++) remove(g_files[i]);
    for (int i = g_nd - 1; i >= 0; i--) RMDIR(g_dirs[i]);
    return GE_TEST_END();
}
