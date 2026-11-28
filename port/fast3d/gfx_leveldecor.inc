/* gfx_leveldecor.inc -- the level decor from the asset packs (port/include/leveldecor.h), included by gfx_pc.cpp.
 *
 * G_LEVELDECOR_EXT (room, list) stands where the game would call a room's primary or secondary display list, right after
 * the room matrix (bg.c, #ifdef PORT). Here that room's glTF primitives of that list are drawn with the static-mesh API:
 * the vertices are in the room's own units, so the clip matrix is the RSP's current modelview x projection, as the room
 * list's own vertices would get; fog is the live RDP / RSP fog (colour, multiplier, offset). Combiner: TEXEL0 x SHADE
 * (SHADE untextured), unlit; the alpha mode picks blend / cut-out; depth, culling and wrap come from the material.
 *
 * The meshes and textures belong to this (decoder) thread: built at a room's first draw, dropped when the game thread
 * publishes another set (a level or a stack change, leveldecorTake). Textures are the asset stack's pictures by texture
 * number (texpackPixels), the same ones fast3d draws for the ROM lists.
 *
 * GE_LDSTATS=<frame>: on that frame, one log line per room list drawn (pack path) or walked (ROM path, GE_ROM_LEVELS=1,
 * bracketed by the game): triangles and world-space bounds, for the pack-vs-ROM geometry check. */

#include "leveldecor.h"

namespace {

struct LdGpuPrim {
    uint32_t byte_offset, nverts;
    struct ShaderProgram* prg;
    uint32_t tex;          /* GPU texture, 0 none */
    const LdPrim* p;
    bool blend;
};

struct LdGpuRoom {
    uint32_t mesh = 0;
    int built = 0;         /* 0 not yet, 1 built, -1 nothing drawable */
    bool fog = false;      /* what it was built for (the vertex layout has the fog slot) */
    std::vector<LdGpuPrim> prims;
};

}   // namespace

static LdSet* s_ld_set = nullptr;
static std::vector<LdGpuRoom> s_ld_rooms;
static std::unordered_map<int, uint32_t> s_ld_tex;
static std::vector<uint8_t> s_ld_room_logged;
static int s_ld_prog_logged = 0;

/* GE_LDSTATS */
static int s_ld_stat_frame = -2;
static int s_ld_stat_room = -1, s_ld_stat_list = 0;
static unsigned long long s_ld_stat_tris = 0, s_ld_stat_fogtris = 0;
static float s_ld_stat_mn[3], s_ld_stat_mx[3];
static float s_ld_stat_ob[MAX_VERTICES + 4][3];

static void ld_teardown(void) {
    for (LdGpuRoom& r : s_ld_rooms) {
        if (r.mesh) gfx_rapi->mesh_delete(r.mesh);
    }
    for (auto& kv : s_ld_tex) {
        if (kv.second) gfx_rapi->delete_texture(kv.second);
    }
    s_ld_rooms.clear();
    s_ld_tex.clear();
    s_ld_room_logged.clear();
    leveldecorFree(s_ld_set);
    s_ld_set = nullptr;
}

static void ld_sync(void) {
    LdSet* n = leveldecorTake();
    if (n == nullptr) return;
    gfx_flush();
    ld_teardown();
    s_ld_set = n;
    s_ld_rooms.resize((size_t)(n->nrooms > 0 ? n->nrooms : 0));
    s_ld_room_logged.assign(s_ld_rooms.size() + 1, 0);
}

static uint32_t ld_texture(int texnum) {
    auto it = s_ld_tex.find(texnum);
    if (it != s_ld_tex.end()) return it->second;
    int w = 0, h = 0;
    const uint8_t* px = texpackPixels(texnum, G_IM_FMT_RGBA, s_max_tex_size > 0 ? s_max_tex_size : 4096, &w, &h);
    uint32_t id = 0;
    if (px != nullptr && w > 0 && h > 0) {
        id = gfx_rapi->new_texture();
        gfx_rapi->select_texture(0, id, true);
        gfx_rapi->upload_texture(px, (uint32_t)w, (uint32_t)h, true);
    }
    s_ld_tex[texnum] = id;
    return id;
}

static bool ld_zcut(const LdPrim& p) {
    return p.alpha == 2 && !p.zupd && p.ztest && p.zcmp && p.zmode != ZMODE_DEC && gfx_zcut_gate();
}

static void ld_build_room(LdGpuRoom& g, const LdRoom& rm, bool fog) {
    if (g.mesh) gfx_rapi->mesh_delete(g.mesh);
    g.mesh = 0;
    g.prims.clear();
    g.fog = fog;
    g.built = -1;
    std::vector<float> vb;
    for (int i = 0; i < rm.nprims; i++) {
        const LdPrim& p = rm.prims[i];
        if (p.cull == 3 || p.count == 0) continue;
        const bool textured = p.texnum >= 0;
        uint8_t c[2][2][4] = {};
        for (int k = 0; k < 2; k++) {
            if (textured) { c[0][k][0] = SHADER_TEXEL0; c[0][k][2] = SHADER_INPUT_1; }
            else c[0][k][3] = SHADER_INPUT_1;
        }
        const uint64_t id0 = ge_render_cc_id(c);
        const uint32_t id1 = (p.alpha ? SHADER_OPT_ALPHA : 0) | (p.alpha == 1 ? SHADER_OPT_TEXTURE_EDGE : 0) |
                             (fog ? SHADER_OPT_FOG : 0) | (ld_zcut(p) ? SHADER_OPT_ZCUT : 0);
        if (gfx_rapi->mesh_supported != nullptr && !gfx_rapi->mesh_supported(id0, id1)) {
            if (s_ld_prog_logged++ < 16)
                sysLogPrintf(LOG_WARNING, "level decor: combiner 0x%016llx 0x%08x has no mesh program in this shader pack: not drawn",
                             (unsigned long long)id0, id1);
            continue;
        }
        struct ShaderProgram* prg = gfx_lookup_or_create_shader_program(id0, id1);
        uint8_t nin = 0;
        bool used[2] = { false, false };
        gfx_rapi->shader_get_info(prg, &nin, used);
        if (prg == nullptr || nin != 1 || used[0] != textured || used[1]) continue;
        LdGpuPrim gp;
        gp.byte_offset = (uint32_t)(vb.size() * sizeof(float));
        gp.nverts = p.count;
        gp.prg = prg;
        gp.tex = textured ? ld_texture(p.texnum) : 0;
        gp.p = &p;
        gp.blend = p.alpha == 2;
        if (textured && gp.tex == 0) continue;
        const int ncol = p.alpha ? 4 : 3;
        for (uint32_t k = 0; k < p.count; k++) {
            const float* v = &rm.v[(size_t)(p.first + k) * LD_VSTRIDE];
            vb.push_back(v[0]); vb.push_back(v[1]); vb.push_back(v[2]); vb.push_back(0.0f);   /* matrix 0 */
            if (textured) { vb.push_back(v[3]); vb.push_back(v[4]); }
            if (fog) { vb.push_back(0.0f); vb.push_back(0.0f); vb.push_back(0.0f); vb.push_back(0.0f); }   /* live: fog_from_z */
            for (int q = 0; q < ncol; q++) vb.push_back(v[5 + q]);
        }
        g.prims.push_back(gp);
    }
    if (!vb.empty()) {
        g.mesh = gfx_rapi->mesh_create(vb.data(), vb.size());
        if (g.mesh) g.built = 1;
    }
}

static void ld_stat_line(const char* path, int room, int list, unsigned long long tris, const float mn[3], const float mx[3]) {
    sysLogPrintf(LOG_NOTE, "LDSTAT %s room %d list %d tris %llu min %.3f %.3f %.3f max %.3f %.3f %.3f", path, room, list, tris,
                 mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
}

static bool ld_stat_frame_now(void) {
    if (s_ld_stat_frame == -2) {
        const char* e = getenv("GE_LDSTATS");
        s_ld_stat_frame = (e && e[0]) ? atoi(e) : -1;
    }
    return s_ld_stat_frame >= 0 && (int)videoGetFrameCount() == s_ld_stat_frame;
}

/* ROM path bracket (GE_LDSTATS only): the room list walked between begin and end. */
static void ld_stat_vtx(size_t n, size_t dest, const Vtx* v) {
    for (size_t i = 0; i < n && dest + i < (size_t)MAX_VERTICES + 4; i++) {
        for (int k = 0; k < 3; k++) s_ld_stat_ob[dest + i][k] = v[i].v.ob[k];
    }
}

static void ld_stat_tri(uint8_t a, uint8_t b, uint8_t c) {
    const uint8_t ix[3] = { a, b, c };
    s_ld_stat_tris++;
    if (((rdp.other_mode_l >> 30) == G_BL_CLR_FOG) || ((rdp.other_mode_l >> 26) == G_BL_A_FOG)) s_ld_stat_fogtris++;
    for (int i = 0; i < 3; i++) {
        if (ix[i] >= MAX_VERTICES + 4) continue;
        for (int k = 0; k < 3; k++) {
            const float f = s_ld_stat_ob[ix[i]][k];
            if (f < s_ld_stat_mn[k]) s_ld_stat_mn[k] = f;
            if (f > s_ld_stat_mx[k]) s_ld_stat_mx[k] = f;
        }
    }
}

static void ld_rom_bracket(int room, int list, bool begin) {
    ld_sync();
    if (begin) {
        if (!ld_stat_frame_now()) return;
        s_ld_stat_room = room; s_ld_stat_list = list;
        s_ld_stat_tris = s_ld_stat_fogtris = 0;
        for (int k = 0; k < 3; k++) { s_ld_stat_mn[k] = 1e30f; s_ld_stat_mx[k] = -1e30f; }
        s_ld_stat_on = true;
        sysLogPrintf(LOG_NOTE, "LDSTAT fogstate room %d list %d geom_fog %d fog_mul %d fog_off %d fogcol %d %d %d",
                     room, list, (rsp.geometry_mode & G_FOG) != 0, (int)rsp.fog_mul, (int)rsp.fog_offset,
                     rdp.fog_color.r, rdp.fog_color.g, rdp.fog_color.b);
        return;
    }
    if (!s_ld_stat_on) return;
    s_ld_stat_on = false;
    float mn[3], mx[3];
    const LdRoom* rm = (s_ld_set && room < s_ld_set->nrooms) ? &s_ld_set->rooms[room] : nullptr;
    for (int k = 0; k < 3; k++) {   /* raw room units -> world: raw / scale + the node translation */
        const double sc = s_ld_set && s_ld_set->scale > 0 ? s_ld_set->scale : 1.0;
        const float t = rm ? rm->tr[k] : 0.0f;
        mn[k] = (float)(s_ld_stat_mn[k] / sc) + t;
        mx[k] = (float)(s_ld_stat_mx[k] / sc) + t;
    }
    ld_stat_line(rm ? "rom" : "rom(no-set)", room, list, s_ld_stat_tris, mn, mx);
    if (s_ld_stat_fogtris) sysLogPrintf(LOG_NOTE, "LDSTAT romfog room %d list %d fogtris %llu", room, list, s_ld_stat_fogtris);
}

static void ld_draw(int room, int list) {
    ld_sync();
    if (s_ld_set == nullptr || s_ld_set->missing) return;
    if (room <= 0 || room >= s_ld_set->nrooms || !s_ld_set->rooms[room].present) {
        const size_t li = (size_t)(room > 0 && room < (int)s_ld_rooms.size() ? room : 0);
        if (li < s_ld_room_logged.size() && !(s_ld_room_logged[li] & (1 << list))) {
            s_ld_room_logged[li] |= (uint8_t)(1 << list);
            sysLogPrintf(LOG_WARNING, "level decor: room %d is not in levels/%s/%s.gltf: nothing drawn for it", room,
                         s_ld_set->level, s_ld_set->level);
        }
        return;
    }
    const LdRoom& rm = s_ld_set->rooms[room];
    if (ld_stat_frame_now()) {   /* this list's world bounds: v / scale + the node translation */
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (int i = 0; i < rm.nprims; i++) {
            const LdPrim& p = rm.prims[i];
            if (p.list != list) continue;
            for (uint32_t k = 0; k < p.count; k++)
                for (int c = 0; c < 3; c++) {
                    const float w = (float)(rm.v[(size_t)(p.first + k) * LD_VSTRIDE + c] / s_ld_set->scale) + rm.tr[c];
                    mn[c] = std::min(mn[c], w);
                    mx[c] = std::max(mx[c], w);
                }
        }
        ld_stat_line("pack", room, list, rm.ntris[list], mn, mx);
    }
    if (rm.ntris[list] == 0) return;

    /* fog: the room's draws blend with fog when the game has the RSP fog on (bg.c's room state) */
    const bool fog = (rsp.geometry_mode & G_FOG) != 0;
    LdGpuRoom& g = s_ld_rooms[(size_t)room];
    if (g.built == 0 || (g.built == 1 && g.fog != fog)) {
        gfx_flush();
        ld_build_room(g, rm, fog);
    }
    if (g.built != 1) return;

    gfx_flush();
    const struct GfxClipParameters clip = gfx_rapi->get_clip_parameters();
    static struct GfxMeshDraw d;
    memset(&d, 0, sizeof(d));
    {   /* as the mesh cache's GPU replay: aspect fix on x (unless a framebuffer is the target), y flip of the target */
        float* m = d.mtx[0];
        for (int i = 0; i < 4; i++) {
            const float* p = rsp.MP_matrix[i];
            m[i * 4 + 0] = fbActive ? p[0] : (rsp.aspect_ofs * p[3] + p[0]) * rsp.aspect_scale / gfx_current_dimensions.aspect_ratio;
            m[i * 4 + 1] = clip.invert_y ? -p[1] : p[1];
            m[i * 4 + 2] = clip.z_is_from_0_to_1 ? (p[2] + p[3]) * 0.5f : p[2];
            m[i * 4 + 3] = p[3];
        }
    }
    d.nmtx = 1;
    d.invert_y = clip.invert_y;
    d.fog_from_z = fog;
    d.fog_mul = rsp.fog_mul;
    d.fog_off = rsp.fog_offset;
    d.fog_color[0] = rdp.fog_color.r / 255.0f;
    d.fog_color[1] = rdp.fog_color.g / 255.0f;
    d.fog_color[2] = rdp.fog_color.b / 255.0f;
    d.fog_color[3] = rdp.fog_color.a / 255.0f;
    d.noise_scale = 1.0f;
    gfx_apply_viewport_scissor();
    gfx_dynlights_sync();
    for (const LdGpuPrim& gp : g.prims) {
        const LdPrim& p = *gp.p;
        if (p.list != list) continue;
        const uint8_t dm = (p.ztest ? 1 : 0) | (p.zupd ? 2 : 0) | (p.zcmp ? 4 : 0) | (uint8_t)(p.zmode >> 6);
        if (dm != rendering_state.depth_mode) {
            gfx_rapi->set_depth_mode(p.ztest, p.zupd, p.zcmp, false, p.zmode);
            rendering_state.depth_mode = dm;
        }
        if (gp.tex) {
            gfx_rapi->select_texture(0, gp.tex, p.linear != 0);
            gfx_rapi->set_sampler_parameters(0, p.linear != 0, p.cms, p.cmt, true);
            rendering_state.textures[0] = nullptr;   /* not a cache node: fast3d binds its own again */
        }
        if (gp.prg != rendering_state.shader_program) {
            gfx_rapi->unload_shader(rendering_state.shader_program);
            gfx_rapi->load_shader(gp.prg);
            rendering_state.shader_program = gp.prg;
        }
        if (gp.blend != rendering_state.alpha_blend || rendering_state.modulate != 0) {
            gfx_rapi->set_use_alpha(gp.blend, 0);
            rendering_state.alpha_blend = gp.blend;
            rendering_state.modulate = 0;
        }
        d.byte_offset = gp.byte_offset;
        d.nverts = gp.nverts;
        d.cull = p.cull;
        gfx_rapi->mesh_draw(g.mesh, &d);
    }
    rdp.textures_changed[0] = true;
}

static void ld_marker(uint32_t w0) {
    const int room = (int)(w0 & 0xFFFF), list = (int)((w0 >> 16) & 1);
    if (w0 & (1u << 17)) ld_rom_bracket(room, list, true);
    else if (w0 & (1u << 18)) ld_rom_bracket(room, list, false);
    else ld_draw(room, list);
}
