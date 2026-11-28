/* port/src/leveldecor.c: parse a tiny level glTF written here (two rooms, a collision node, three materials), check the rooms,
 * the primitives' order and fields, the vertex units and colours, the bounds. Pure, no core. */
#include "ge_test.h"
#include "leveldecor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t g_bin[96];
static int g_frees;

static int rd(void *user, const char *path, uint8_t **data, size_t *len)
{
    (void)user;
    if (strcmp(path, "levels/t/t.bin") != 0) return 0;
    *data = (uint8_t *)malloc(sizeof g_bin);
    memcpy(*data, g_bin, sizeof g_bin);
    *len = 92;
    return 1;
}

static void fr(void *user, void *data) { (void)user; g_frees++; free(data); }

static const char kGltf[] =
    "{\"asset\":{\"version\":\"2.0\",\"extras\":{\"exporter_version\":13,\"level\":\"t\",\"level_scale\":0.5}},"
    "\"extensionsUsed\":[\"KHR_materials_unlit\"],\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2]}],"
    "\"nodes\":["
    "{\"name\":\"room_1\",\"mesh\":0,\"translation\":[10,0,0],\"extras\":{\"room\":1,\"room_position\":[5,0,0]}},"
    "{\"name\":\"room_3\",\"mesh\":1,\"translation\":[0,0,0],\"extras\":{\"room\":3}},"
    "{\"name\":\"collision_room_1\",\"mesh\":2},"
    "{\"name\":\"room_2\",\"translation\":[0,0,0],\"extras\":{\"room\":2}}],"
    "\"meshes\":["
    "{\"name\":\"room_1\",\"primitives\":["
    "{\"attributes\":{\"POSITION\":0,\"COLOR_0\":2},\"material\":1,\"mode\":4},"
    "{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1,\"COLOR_0\":2},\"indices\":3,\"material\":0,\"mode\":4}]},"
    "{\"name\":\"room_3\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"COLOR_0\":2},\"indices\":3,\"material\":2,\"mode\":4}]},"
    "{\"name\":\"collision\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":4}]}],"
    "\"materials\":["
    "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"baseColorTexture\":{\"index\":0}},\"alphaMode\":\"OPAQUE\",\"doubleSided\":false,"
    "\"extensions\":{\"KHR_materials_unlit\":{}},\"extras\":{\"texnum\":7,\"texture\":\"7\",\"n64\":{\"combine\":[0,0],\"othermode\":[0,202907768],"
    "\"geometry_mode\":8192,\"list\":\"primary\"}}},"
    "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,0.5]},\"alphaMode\":\"BLEND\",\"doubleSided\":true,"
    "\"extras\":{\"texnum\":-1,\"n64\":{\"othermode\":[0,202918360],\"geometry_mode\":0,\"list\":\"secondary\"}}},"
    "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1]},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5,\"doubleSided\":false,"
    "\"extras\":{\"texnum\":-1,\"n64\":{\"othermode\":[0,202907768],\"geometry_mode\":4096,\"list\":\"primary\"}}}],"
    "\"textures\":[{\"sampler\":0,\"source\":0}],\"images\":[{\"uri\":\"../../textures/7.png\"}],"
    "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":33071,\"wrapT\":33648}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
    "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\",\"normalized\":true},"
    "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24},"
    "{\"buffer\":0,\"byteOffset\":60,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":84,\"byteLength\":6}],"
    "\"buffers\":[{\"uri\":\"t.bin\",\"byteLength\":92}]}";

static void put_f(int off, float f) { memcpy(g_bin + off, &f, 4); }
static void put_u16(int off, uint16_t v) { memcpy(g_bin + off, &v, 2); }

static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

int main(void)
{
    char err[256];
    GE_TEST_BEGIN("leveldecor");

    const float pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const float uv[6] = { 0, 0, 2, 0, 0, 0.5f };
    const uint16_t col[12] = { 65535, 0, 32768, 65535, 0, 65535, 0, 32768, 65535, 65535, 65535, 0 };
    const uint16_t idx[3] = { 2, 1, 0 };
    for (int i = 0; i < 9; i++) put_f(i * 4, pos[i]);
    for (int i = 0; i < 6; i++) put_f(36 + i * 4, uv[i]);
    for (int i = 0; i < 12; i++) put_u16(60 + i * 2, col[i]);
    for (int i = 0; i < 3; i++) put_u16(84 + i * 2, idx[i]);

    LdSet *s = leveldecorParse((const uint8_t *)kGltf, strlen(kGltf), "levels/t/t.gltf", rd, fr, NULL, err, sizeof err);
    GE_CHECK(s != NULL);
    if (!s) { printf("parse: %s\n", err); return GE_TEST_END(); }
    GE_CHECK_STR(s->level, "t");
    GE_CHECK(near((float)s->scale, 0.5f));
    GE_CHECK_EQ_INT(s->nrooms, 4);
    GE_CHECK_EQ_INT(s->rooms[0].present, 0);
    GE_CHECK_EQ_INT(s->rooms[1].present, 1);
    GE_CHECK_EQ_INT(s->rooms[2].present, 1);   /* a node without a mesh: present, empty */
    GE_CHECK_EQ_INT(s->rooms[2].nprims, 0);
    GE_CHECK_EQ_INT(s->rooms[3].present, 1);

    /* room 1: the primary (textured, opaque) primitive comes first although the file lists the secondary one first */
    const LdRoom *r1 = &s->rooms[1];
    GE_CHECK_EQ_INT(r1->nprims, 2);
    GE_CHECK_EQ_INT(r1->nverts, 6);
    GE_CHECK_EQ_INT(r1->ntris[0], 1);
    GE_CHECK_EQ_INT(r1->ntris[1], 1);
    if (r1->nprims == 2) {
        const LdPrim *a = &r1->prims[0], *b = &r1->prims[1];
        GE_CHECK_EQ_INT(a->list, 0);
        GE_CHECK_EQ_INT(a->texnum, 7);
        GE_CHECK_EQ_INT(a->alpha, 0);
        GE_CHECK_EQ_INT(a->cull, 1);
        GE_CHECK_EQ_INT(a->cms, 2);
        GE_CHECK_EQ_INT(a->cmt, 1);
        GE_CHECK_EQ_INT(a->linear, 1);
        GE_CHECK_EQ_INT(a->zupd, 1);
        GE_CHECK_EQ_INT(a->zcmp, 1);
        GE_CHECK_EQ_INT(a->ztest, 1);
        GE_CHECK_EQ_INT(a->first, 0);
        GE_CHECK_EQ_INT(a->count, 3);
        GE_CHECK_EQ_INT(b->list, 1);
        GE_CHECK_EQ_INT(b->texnum, -1);
        GE_CHECK_EQ_INT(b->alpha, 2);
        GE_CHECK_EQ_INT(b->cull, 0);
        GE_CHECK_EQ_INT(b->zupd, 0);   /* 0x0C1849D8 & 0x20 == 0: the translucent list writes no depth */
        GE_CHECK_EQ_INT(b->zcmp, 1);
        GE_CHECK_EQ_INT(b->first, 3);
        /* the indexed primitive is expanded in index order (2, 1, 0); positions x scale (room units) */
        const float *v0 = &r1->v[0];
        GE_CHECK(near(v0[0], 0.0f) && near(v0[1], 0.5f) && near(v0[2], 0.0f));
        GE_CHECK(near(v0[3], 0.0f) && near(v0[4], 0.5f));
        /* COLOR_0 linear -> the shade's sRGB: 1 -> 1, 0.5 -> 0.7354, 0 -> 0; alpha as is */
        GE_CHECK(near(v0[5], 1.0f) && near(v0[6], 1.0f) && near(v0[7], 1.0f) && near(v0[8], 0.0f));
        const float *v2 = &r1->v[2 * LD_VSTRIDE];
        GE_CHECK(near(v2[5], 1.0f) && near(v2[6], 0.0f) && near(v2[7], 0.7354f) && near(v2[8], 1.0f));   /* source vertex 0 */
        /* the secondary primitive (not indexed): vertex alpha x baseColorFactor alpha 0.5 */
        const float *w0 = &r1->v[3 * LD_VSTRIDE];
        GE_CHECK(near(w0[0], 0.0f) && near(w0[8], 0.5f));
        const float *w1 = &r1->v[4 * LD_VSTRIDE];
        GE_CHECK(near(w1[0], 0.5f) && near(w1[8], 0.25f));   /* vertex alpha 0.5 x 0.5 */
    }
    /* world bounds: glTF position + node translation */
    GE_CHECK(near(r1->mn[0], 10.0f) && near(r1->mx[0], 11.0f) && near(r1->mx[1], 1.0f));
    GE_CHECK(near(r1->tr[0], 10.0f));

    const LdRoom *r3 = &s->rooms[3];
    GE_CHECK_EQ_INT(r3->nprims, 1);
    if (r3->nprims == 1) {
        GE_CHECK_EQ_INT(r3->prims[0].alpha, 1);
        GE_CHECK_EQ_INT(r3->prims[0].cull, 2);   /* geometry_mode 0x1000: front */
        GE_CHECK_EQ_INT(r3->prims[0].texnum, -1);
    }
    leveldecorFree(s);
    GE_CHECK_EQ_INT(g_frees, 1);   /* the .bin went back through the reader's free */

    /* a missing buffer is an error, not a crash */
    s = leveldecorParse((const uint8_t *)kGltf, strlen(kGltf), "levels/x/x.gltf", rd, fr, NULL, err, sizeof err);
    GE_CHECK(s == NULL);
    GE_CHECK(err[0] != 0);
    /* not JSON */
    s = leveldecorParse((const uint8_t *)"nope", 4, "levels/t/t.gltf", rd, fr, NULL, err, sizeof err);
    GE_CHECK(s == NULL);
    return GE_TEST_END();
}
