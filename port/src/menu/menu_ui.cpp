/* menu_ui.cpp -- the fullscreen menu, written against Dear ImGui only (see menu_ui.h).
 *
 * The content comes from menu.json through menu_model: tabs along the top, a list of entries on the left, the
 * form of the selected entry on the right, a footer of button prompts. The custom look is drawn with ImDrawList;
 * ImGui provides the hit testing and the mouse. Gamepad and keyboard navigation is the menu's own: the list on
 * the left has the focus until A or right enters the form, up / down move between controls, left / right change
 * the value, B leaves the form, B on the list asks the game to close the menu. */
#include "menu_ui.h"
#include "menu_model.h"
#include "../../third_party/imgui/imgui.h"

#include <cstdio>
#include <cstring>
#include <set>

namespace menu {

namespace {

const ImU32 kGold = IM_COL32(230, 194, 90, 255);
const ImU32 kGoldFill = IM_COL32(230, 194, 90, 48);
const ImU32 kGoldSoft = IM_COL32(230, 194, 90, 24);
const ImU32 kGoldDim = IM_COL32(141, 122, 61, 255);
const ImU32 kRule = IM_COL32(108, 106, 94, 255);
const ImU32 kRow = IM_COL32(18, 19, 15, 255);
const ImU32 kInk = IM_COL32(243, 238, 224, 255);
const ImU32 kMut = IM_COL32(169, 165, 147, 255);
const ImU32 kLine = IM_COL32(63, 62, 55, 255);
const ImU32 kDisabled = IM_COL32(110, 108, 98, 255);

/* the game defines its own toupper (src/str.c) and shadows <math.h>: plain helpers only */
inline char upc(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

ImFont* g_body = nullptr;   /* the two families; each text size is drawn from a font baked at that pixel size */
ImFont* g_head = nullptr;
/* Fonts baked at the sizes the menu draws: a glyph squeezed from a 48 / 72 px raster to ~30 px smears. A size not
 * baked yet is noted and drawn from the nearest one meanwhile; the next frame rebuilds the atlas (fontsBuild). */
const void* g_ttf[2] = { nullptr, nullptr };
size_t g_ttfLen[2] = { 0, 0 };
std::map<int, ImFont*> g_baked[2];   /* family (0 body, 1 head) -> px -> font */
std::set<int> g_want[2];
bool g_fontsDirty = false;
float g_fontsW = 0.0f;   /* the width the sizes were asked for: a new window size starts a new set */

float snap(float v) { const float r = (float)(int)v; return v - r >= 0.5f ? r + 1.0f : (v - r <= -0.5f ? r - 1.0f : r); }   /* nearest whole pixel */

ImFont* pick(ImFont* f, float px, float* drawPx) {
    *drawPx = px;
    if (!f) return f;
    const int fam = f == g_head ? 1 : 0;
    const int k = (int)(px + 0.5f);   /* px > 0 */
    if (k <= 0) return f;
    auto it = g_baked[fam].find(k);
    if (it != g_baked[fam].end()) { *drawPx = (float)k; return it->second; }   /* one texel per pixel */
    if (g_want[fam].insert(k).second) g_fontsDirty = true;
    auto up = g_baked[fam].lower_bound(k);   /* meanwhile: the nearest larger raster, else the base one */
    return up != g_baked[fam].end() ? up->second : f;
}

Model g_model;
State g_state;
std::string g_error;

int g_tab = 0;
int g_entry = 0;          /* index among the visible entries of the tab */
int g_focus = -1;         /* index among the visible controls of the entry, or -1 while the list has the focus */
bool g_closeReq = false;
float g_scroll = 0, g_scrollTarget = 0;
std::string g_actionFired;   /* the id of the last action control activated (the game's adapter reads it) */
std::string g_pressId;       /* the action button last pressed, and when: drawAction flashes it */
double g_pressTime = -1.0;
const double kPressFlash = 0.28;   /* seconds */
std::string g_selId;         /* the select whose list is open, or "" */
int g_ownerSeat = -1;
float g_padUv[4] = { 0, 0, 0, 0 }, g_padAspect = 0;
int g_padFam = -1;   /* setPadFamily: 0 Xbox, 1 PlayStation, 2 Nintendo; -1 = not told (the viewer): by platform */
float g_iconUv[4][4];   /* setIconImage, kMenuIcon* order (menu_icons.h) */
bool g_iconOk[4];
bool padNin() { return g_padFam >= 0 ? g_padFam == 2 : !g_state.pc; }
bool padPs() { return g_padFam == 1 && g_iconOk[0] && g_iconOk[1] && g_iconOk[2] && g_iconOk[3]; }
/* the PlayStation icon at a face button's place, in Xbox names (a bottom, b right, x left, y top); -1 = none */
int psIcon(const char* pos) {
    if (!pos || !pos[0] || pos[1]) return -1;
    switch (pos[0]) { case 'a': return 0; case 'b': return 1; case 'x': return 2; case 'y': return 3; default: return -1; }
}
void drawIcon(ImDrawList* dl, int k, ImVec2 c, float size, ImU32 col) {
    const float h = size * 0.5f;
    dl->AddImage(ImGui::GetIO().Fonts->TexRef, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h),
                 ImVec2(g_iconUv[k][0], g_iconUv[k][1]), ImVec2(g_iconUv[k][2], g_iconUv[k][3]), col);
}
/* how far a wheel notch scrolls an area that has nothing to select (the release text, a form of text only) */
float wheelScrollStep(float wheel, float u) { return wheel * 6.0f * u; }

bool g_updPanel = false; /* the release text is open (in the first menu) */
bool g_root = true;     /* the first menu: Resume / Options / Restart / Quit */
int g_rootSel = 0, g_rootArmed = -1;
std::string (*g_keyText)(const char*, int) = nullptr;
void (*g_keyBegin)(const char*, int) = nullptr;
bool (*g_keyActive)() = nullptr;
void (*g_actionHandler)(const char*) = nullptr;
int (*g_packRows)(std::vector<PackRow>*) = nullptr;   /* the asset-pack list: the game fills the rows ... */
int (*g_packAct)(int, int) = nullptr;                  /* ... and does what a row is asked (0 toggle, 1 up, 2 down) */
std::vector<PackRow> g_packs;                          /* the rows as of this frame */
int g_packRow = 0, g_packCol = 0;                      /* the focused row; 0 its checkbox, 1 up, 2 down */
std::string g_capId;
int g_capSlot = 0, g_slotSel = 0;
bool g_capturing = false;   /* a key or button is being captured: the menu keeps still */
bool (*g_bindGet)(const char*, double*) = nullptr;
int (*g_picture)(int, const unsigned char**) = nullptr;   /* the crosshair sprite: 32x32 texels of RGBA */
void (*g_tint)(int*, int*, int*) = nullptr;   /* the crosshair colour the game would draw now (colour, custom, health) */
const char* (*g_readout)(const char*, double) = nullptr;   /* a note computed by the game for a slider ("hfov") */
void (*g_bindSet)(const char*, double) = nullptr;
std::map<std::string, double> g_synced;   /* control id -> the value last agreed with the game */
int g_selCursor = 0, g_selScroll = 0, g_selOpenFrame = -1;
ImVec2 g_selAnchor(0, 0);
float g_selW = 0, g_selH = 0;
bool g_blockHits = false;    /* while a list is open it takes every click: nothing under it is hit-tested */

struct Vis {   /* what is visible on the current tab, rebuilt every frame */
    std::vector<int> entries;                        /* item indexes of the visible entries */
    std::vector<const Control*> controls;           /* the visible controls of the selected entry */
};

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = upc(c);
    return o;
}

ImVec2 textSize(ImFont* f, float px, const char* s) { float d; ImFont* b = pick(f, px, &d); return b->CalcTextSizeA(d, 1e9f, 0.0f, s); }
ImVec2 textSizeWrapped(ImFont* f, float px, float wrap, const char* s) { float d; ImFont* b = pick(f, px, &d); return b->CalcTextSizeA(d, 1e9f, wrap, s); }
void text(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 col, const char* s, float wrap = 0.0f) {
    float d;
    ImFont* b = pick(f, px, &d);
    dl->AddText(b, d, ImVec2(snap(p.x), snap(p.y)), col, s, nullptr, wrap);   /* whole pixels: no half-texel smear */
}
void textCenter(ImDrawList* dl, ImFont* f, float px, ImVec2 a, ImVec2 b, ImU32 col, const char* s) {
    const ImVec2 sz = textSize(f, px, s);
    text(dl, f, px, ImVec2(a.x + (b.x - a.x - sz.x) * 0.5f, a.y + (b.y - a.y - sz.y) * 0.5f), col, s);
}
void box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 border) {
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, border, 0.0f, 0, 1.0f);
}
bool hit(const char* id, ImVec2 a, ImVec2 size) {
    if (g_blockHits) return false;
    ImGui::SetCursorScreenPos(a);
    return ImGui::InvisibleButton(id, size);
}

/* an action control: "__ResetEntry" puts every control of its entry back to its default, anything else is the game's */
/* the focused toggle / button: a gold frame around what A presses (Left / Right do nothing there) */
void focusFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float u)
{
    const float p = 0.45f * u;
    dl->AddRect(ImVec2(a.x - p, a.y - p), ImVec2(b.x + p, b.y + p), kGold, 0.0f, 0, 1.5f);
}

void fireAction(const Control& c) {
    g_actionFired = c.id;
    g_pressId = c.id;
    g_pressTime = ImGui::GetTime();
    if (c.key == "__ResetEntry") {
        for (const Tab& t : g_model.tabs)
            for (const Item& it : t.items) {
                bool mine = false;
                for (const Control& k : it.controls) if (&k == &c) mine = true;
                if (!mine) continue;
                for (const Control& k : it.controls)
                    if (k.type != CT_ACTION && k.type != CT_BIND) g_state.v[k.id] = k.def[g_state.pc ? 1 : 0];
            }
        return;
    }
    if (g_actionHandler) g_actionHandler(c.key.c_str());
}

void changeValue(const Control& c, int d) {
    double& v = g_state.v[c.id];
    if (c.type == CT_CHOICE || c.type == CT_SELECT) {
        const int n = (int)c.options.size();
        int k = (int)v;
        for (int tries = 0; tries < n; tries++) {   /* the next option that is offered */
            k = (k + d + n) % n;
            if (optionVisible(c, k, g_model, g_state)) break;
        }
        v = (double)k;
    } else if (c.type == CT_TOGGLE) {
        v = v != 0 ? 0 : 1;
    } else if (c.type == CT_SLIDER) {
        v += d * c.step;
        if (v < c.vmin) v = c.vmin;
        if (v > c.vmax) v = c.vmax;
    } else {
        fireAction(c);
    }
}

bool pressed(ImGuiKey a, ImGuiKey b, ImGuiKey c, bool repeat) {
    return ImGui::IsKeyPressed(a, repeat) || ImGui::IsKeyPressed(b, repeat) || (c != ImGuiKey_None && ImGui::IsKeyPressed(c, repeat));
}

Vis visibleOf(const Tab& tab) {
    Vis v;
    for (size_t i = 0; i < tab.items.size(); i++)
        if (!tab.items[i].group && itemVisible(tab.items[i], g_model, g_state)) v.entries.push_back((int)i);
    return v;
}

/* ---- the controls ---- */
struct Slot { float x, y, w; };

/* the buttons of a choice, wrapped over several lines when they do not fit */
float layoutChoice(const Control& c, float x0, float availW, float u, std::vector<Slot>* out) {
    const float bh = 3.1f * u, gap = 0.8f * u;
    float x = 0, y = 0;
    for (size_t oi = 0; oi < c.options.size(); oi++) {
        const Option& o = c.options[oi];
        if (!optionVisible(c, (int)oi, g_model, g_state)) { out->push_back({ x0, 0, 0 }); continue; }   /* keeps the slot index */
        const ImVec2 sz = textSize(g_head, 1.55f * u, tr(o.label).c_str());
        const float bw = maxf(6.5f * u, sz.x + 2.6f * u);
        if (x > 0 && x + bw > availW) { x = 0; y += bh + 0.6f * u; }
        out->push_back({ x0 + x, y, bw });
        x += bw + gap;
    }
    return y + bh;
}

void drawChoice(ImDrawList* dl, const Control& c, int fi, float x0, float y0, float availW, float u, bool disabled) {
    std::vector<Slot> slots;
    layoutChoice(c, x0, availW, u, &slots);
    const int cur = (int)g_state.get(c.id);
    const float bh = 3.1f * u;
    for (size_t i = 0; i < slots.size(); i++) {
        if (slots[i].w <= 0) continue;   /* an option that is not offered */
        char id[64];
        snprintf(id, sizeof id, "##%s_%d", c.id.c_str(), (int)i);
        const ImVec2 a(slots[i].x, y0 + slots[i].y), b(slots[i].x + slots[i].w, y0 + slots[i].y + bh);
        if (hit(id, a, ImVec2(slots[i].w, bh)) && !disabled) { g_state.v[c.id] = (double)i; g_focus = fi; }
        const bool on = (int)i == cur;
        box(dl, a, b, on ? (disabled ? kGoldSoft : kGoldFill) : IM_COL32(21, 22, 15, 255), on ? (disabled ? kRule : kGold) : kRule);
        if (on && !disabled) dl->AddRectFilled(ImVec2(a.x, b.y - 0.22f * u), b, kGold);
        textCenter(dl, g_head, 1.55f * u, a, b, disabled ? kDisabled : kInk, tr(c.options[i].label).c_str());
    }
}

void drawToggle(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float s = 2.1f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(s + 6.0f * u, s)) && !disabled) { changeValue(c, 1); g_focus = fi; }
    const bool on = g_state.get(c.id) != 0;
    box(dl, ImVec2(x, y), ImVec2(x + s, y + s), IM_COL32(13, 14, 12, 255), on && !disabled ? kGold : kRule);
    if (on) {
        const float k = s * 0.18f;
        const ImU32 col = disabled ? kDisabled : kGold;
        /* one polyline, so the two strokes join at the corner (two thick lines leave a notch there) */
        const ImVec2 tick[3] = { ImVec2(x + s * 0.2f, y + s * 0.52f), ImVec2(x + s * 0.42f, y + s * 0.74f), ImVec2(x + s * 0.82f, y + s * 0.26f) };
        dl->AddPolyline(tick, 3, col, ImDrawFlags_None, k);
    }
    text(dl, g_body, 1.6f * u, ImVec2(x + s + 0.9f * u, y + (s - 1.6f * u) * 0.5f - 0.1f * u), disabled ? kDisabled : kInk, on ? tr("ON").c_str() : tr("OFF").c_str());
    if (g_focus == fi && !disabled) focusFrame(dl, ImVec2(x, y), ImVec2(x + s, y + s), u);
}

void drawSlider(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float w = 22.0f * u, h = 2.6f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    const bool submitted = !g_blockHits;
    if (submitted) hit(id, ImVec2(x, y), ImVec2(w, h));
    if (submitted && ImGui::IsItemActive() && !disabled) {
        const float f = minf(1.0f, maxf(0.0f, (ImGui::GetIO().MousePos.x - x) / w));
        int v = (int)((c.vmin + f * (c.vmax - c.vmin)) / c.step + 0.5f) * c.step;
        if (v < c.vmin) v = c.vmin;
        if (v > c.vmax) v = c.vmax;
        g_state.v[c.id] = v;
        g_focus = fi;
    }
    const double val = g_state.get(c.id);
    float f = (float)(val - c.vmin) / (float)(c.vmax - c.vmin);
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);   /* a value outside the range still draws inside the track */
    const float ty = y + h * 0.5f;
    dl->AddRectFilled(ImVec2(x, ty - 0.2f * u), ImVec2(x + w, ty + 0.2f * u), IM_COL32(58, 59, 51, 255));
    dl->AddRectFilled(ImVec2(x, ty - 0.2f * u), ImVec2(x + 0.45f * u + (w - 0.9f * u) * f, ty + 0.2f * u), disabled ? kDisabled : kGold);
    if (!c.options.empty() && c.options.size() > 1)   /* a notch per option */
        for (size_t i = 0; i < c.options.size(); i++) {
            const float nx = x + w * (float)i / (float)(c.options.size() - 1);
            dl->AddRectFilled(ImVec2(nx - 0.12f * u, ty - 0.7f * u), ImVec2(nx + 0.12f * u, ty + 0.7f * u), disabled ? kDisabled : kMut);
        }
    const float kx = x + 0.45f * u + (w - 0.9f * u) * f;
    box(dl, ImVec2(kx - 0.45f * u, ty - 0.95f * u), ImVec2(kx + 0.45f * u, ty + 0.95f * u), disabled ? kDisabled : kInk, IM_COL32(13, 14, 12, 255));
    std::string shown = sliderText(c, val);
    if (!c.readout.empty() && g_readout) shown += "   " + std::string(g_readout(c.readout.c_str(), controlToConfig(c, val)));
    text(dl, g_head, 1.7f * u, ImVec2(x + w + 1.4f * u, ty - 0.85f * u), disabled ? kDisabled : kGold, shown.c_str());
}

void drawAction(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bh = 3.1f * u;
    const ImVec2 sz = textSize(g_head, 1.55f * u, tr(controlLabel(c, g_state)).c_str());
    const float bw = maxf(14.0f * u, sz.x + 3.0f * u);
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(bw, bh)) && !disabled) { fireAction(c); g_focus = fi; }
    /* the press: the button sinks a little and flashes gold, then fades back */
    const double t = (g_pressId == c.id && g_pressTime >= 0) ? (ImGui::GetTime() - g_pressTime) / kPressFlash : 1.0;
    const float k = (t >= 0 && t < 1) ? (float)(1.0 - t) : 0.0f;
    const float in = 0.35f * u * k;
    const ImVec2 a(x + in, y + in), b(x + bw - in, y + bh - in);
    box(dl, a, b, IM_COL32(21, 22, 15, 255), k > 0 ? kGold : kRule);
    if (k > 0) dl->AddRectFilled(a, b, IM_COL32(230, 194, 90, (int)(110 * k)));
    textCenter(dl, g_head, 1.55f * u, a, b, disabled ? kDisabled : kInk, tr(controlLabel(c, g_state)).c_str());
    if (g_focus == fi && !disabled) focusFrame(dl, ImVec2(x, y), ImVec2(x + bw, y + bh), u);
}

void startCapture(const Control& c, int slot) {
    g_capId = c.id;
    g_capSlot = slot;
    g_slotSel = slot;
    if (g_keyBegin) g_keyBegin(c.key.c_str(), slot);
}

/* The asset packs, one row each: "1. Title" with the author under it (faded), then the checkbox and the two arrows on the
 * right. A row is taller when it has an author line. */
float packRowH(int i, float u) { return i >= (int)g_packs.size() ? 4.4f * u : (g_packs[i].author.empty() ? 4.2f : 5.6f) * u; }   /* i == the count: the Apply / Discard row */
float packListH(float u) {
    float h = 0.4f * u + (g_packs.empty() ? 4.6f * u : 0.0f);
    for (size_t i = 0; i <= g_packs.size(); i++) h += packRowH((int)i, u);
    return h;
}
bool packCanMove(int i, int dir) {
    if (i < 0 || i >= (int)g_packs.size()) return false;
    const int j = i + dir;   /* any pack moves, enabled or not; only the ends of the list stop it */
    return j >= 0 && j < (int)g_packs.size();
}
void packDo(int row, int what) {
    if (!g_packAct) return;
    if (what != 0 && !packCanMove(row, what == 1 ? -1 : 1)) return;
    g_packRow = g_packAct(row, what);
    if (g_packRows) g_packRows(&g_packs);
    if (g_packRow >= (int)g_packs.size()) g_packRow = (int)g_packs.size() - 1;
    if (g_packRow < 0) g_packRow = 0;
}

void drawPackList(ImDrawList* dl, const Control& c, int fi, float x, float y, float w, float u) {
    const float bs = 2.8f * u, cb = 2.1f * u;
    float ry = y + 0.2f * u;
    if (g_packs.empty()) {
        text(dl, g_body, 1.6f * u, ImVec2(x + 1.2f * u, y + (4.6f * u - 1.6f * u) * 0.5f), kMut, tr("No asset packs in the assetpacks folder").c_str());
        ry += 4.6f * u;
    }
    int num = 0;
    for (int i = 0; i < (int)g_packs.size(); i++) {
        const PackRow p = g_packs[i];   /* a copy: an action below replaces the list */
        const float rh = packRowH(i, u);
        const bool rowFocus = g_focus == fi && g_packRow == i;
        if (rowFocus) dl->AddRectFilled(ImVec2(x, ry), ImVec2(x + w, ry + rh), kGoldSoft);
        const float downX = x + w - 1.2f * u - bs, upX = downX - 0.6f * u - bs, cbX = upX - 1.8f * u - cb;
        const float by = ry + (rh - bs) * 0.5f, cy = ry + (rh - cb) * 0.5f;
        char id[64];
        snprintf(id, sizeof id, "##%s_c%d", c.id.c_str(), i);
        if (hit(id, ImVec2(cbX, cy), ImVec2(cb + 1.0f * u, cb))) { g_focus = fi; g_packRow = i; g_packCol = 0; packDo(i, 0); return; }
        box(dl, ImVec2(cbX, cy), ImVec2(cbX + cb, cy + cb), IM_COL32(13, 14, 12, 255), p.on ? kGold : kRule);
        if (p.on) {
            const float k = cb * 0.18f;
            const ImVec2 tick[3] = { ImVec2(cbX + cb * 0.2f, cy + cb * 0.52f), ImVec2(cbX + cb * 0.42f, cy + cb * 0.74f), ImVec2(cbX + cb * 0.82f, cy + cb * 0.26f) };
            dl->AddPolyline(tick, 3, kGold, ImDrawFlags_None, k);
        }
        if (rowFocus && g_packCol == 0) focusFrame(dl, ImVec2(cbX, cy), ImVec2(cbX + cb, cy + cb), u);
        for (int d = 0; d < 2; d++) {   /* the arrows: up, down */
            const float bx = d == 0 ? upX : downX;
            const bool can = packCanMove(i, d == 0 ? -1 : 1);
            snprintf(id, sizeof id, "##%s_m%d_%d", c.id.c_str(), i, d);
            if (hit(id, ImVec2(bx, by), ImVec2(bs, bs)) && can) { g_focus = fi; g_packRow = i; g_packCol = 1 + d; packDo(i, 1 + d); return; }
            box(dl, ImVec2(bx, by), ImVec2(bx + bs, by + bs), IM_COL32(21, 22, 15, 255), kRule);
            const float m = bs * 0.5f, t = bs * 0.2f;
            const ImU32 col = can ? kInk : kDisabled;
            if (d == 0) dl->AddTriangleFilled(ImVec2(bx + m, by + m - t), ImVec2(bx + m - t * 1.3f, by + m + t * 0.8f), ImVec2(bx + m + t * 1.3f, by + m + t * 0.8f), col);
            else dl->AddTriangleFilled(ImVec2(bx + m, by + m + t), ImVec2(bx + m + t * 1.3f, by + m - t * 0.8f), ImVec2(bx + m - t * 1.3f, by + m - t * 0.8f), col);
            if (rowFocus && g_packCol == 1 + d && can) focusFrame(dl, ImVec2(bx, by), ImVec2(bx + bs, by + bs), u);
        }
        const std::string title = p.on ? std::to_string(++num) + ". " + p.name : p.name;   /* numbered by place among the enabled */
        const float tx = x + 1.2f * u, th = 1.7f * u, ah = 1.3f * u;
        const float blockH = th + (p.author.empty() ? 0.0f : 0.5f * u + ah);
        const float ty = ry + (rh - blockH) * 0.5f;
        text(dl, g_head, th, ImVec2(tx, ty), p.on ? kInk : kMut, title.c_str());
        if (!p.author.empty()) text(dl, g_body, ah, ImVec2(tx, ty + th + 0.5f * u), kMut, (tr("by") + " " + p.author).c_str());
        ry += rh;
    }
    {   /* Apply and Discard changes, side by side under the list: only while something is pending */
        double dirtyV = 0;
        const bool dirty = g_bindGet && g_bindGet("__AssetPackDirty", &dirtyV) && dirtyV != 0;
        const int n = (int)g_packs.size();
        const float rh = packRowH(n, u), bh = 3.1f * u, by = ry + (rh - bh) * 0.5f;
        const bool rowFocus = g_focus == fi && g_packRow == n;
        const char* const labels[2] = { "Apply", "Discard changes" };
        const char* const keys[2] = { "__AssetPackApply", "__AssetPackDiscard" };
        float bx = x + 1.2f * u;
        for (int b = 0; b < 2; b++) {
            const std::string lab = tr(labels[b]);
            const float bw = maxf(14.0f * u, textSize(g_head, 1.55f * u, lab.c_str()).x + 3.0f * u);
            char id[64];
            snprintf(id, sizeof id, "##%s_b%d", c.id.c_str(), b);
            if (hit(id, ImVec2(bx, by), ImVec2(bw, bh)) && dirty) {
                g_focus = fi; g_packRow = n; g_packCol = b;
                if (g_actionHandler) g_actionHandler(keys[b]);
                return;
            }
            box(dl, ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(21, 22, 15, 255), kRule);
            textCenter(dl, g_head, 1.55f * u, ImVec2(bx, by), ImVec2(bx + bw, by + bh), dirty ? kInk : kDisabled, lab.c_str());
            if (rowFocus && g_packCol == b && dirty) focusFrame(dl, ImVec2(bx, by), ImVec2(bx + bw, by + bh), u);
            bx += bw + 1.0f * u;
        }
    }
}

void drawBind(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bw = 15.0f * u, bh = 3.1f * u, gap = 1.0f * u;
    for (int slot = 0; slot < 2; slot++) {
        const ImVec2 a(x + slot * (bw + gap), y), b(a.x + bw, y + bh);
        char id[64];
        snprintf(id, sizeof id, "##%s_k%d", c.id.c_str(), slot);
        if (hit(id, a, ImVec2(bw, bh)) && !disabled && !g_capturing) { startCapture(c, slot); g_focus = fi; }
        const bool cap = g_capturing && g_capId == c.id && g_capSlot == slot;
        const bool sel = g_focus == fi && g_slotSel == slot;
        std::string t = cap ? tr("Press a key") : (g_keyText ? g_keyText(c.key.c_str(), slot) : std::string());
        if (t.empty()) t = "-";
        box(dl, a, b, cap ? kGoldFill : IM_COL32(21, 22, 15, 255), cap || sel ? kGold : kRule);
        textCenter(dl, g_head, 1.55f * u, a, b, disabled ? kDisabled : (cap ? kGold : kInk), t.c_str());
    }
}

/* The crosshair as the game draws it: a square of (32 / 240) of the screen height times the size, in the chosen style, colour
 * and opacity (the game draws it at 0x6E / 255, times the opacity). The shape is a drawing of it, not the game's sprite. */
void drawCrosshairPreview(ImDrawList* dl, float x, float y, float w, float h, float u) {
    const float screenH = ImGui::GetIO().DisplaySize.y;
    const float side = screenH * (32.0f / 240.0f) * (float)g_state.get("xsize") / 100.0f;
    const int style = (int)g_state.get("xstyle");
    int r = 255, g = 0, b = 0;
    if (g_tint) g_tint(&r, &g, &b);
    const int tintR = r, tintG = g, tintB = b;   /* what the game multiplies the sprite by */
    if ((int)g_state.get("xcolor") == 0 && (int)g_state.get("xhealth") == 0) { r = 255; g = 0; b = 0; }   /* the drawn cross: Original is red (its tint is the identity) */
    const float a = (110.0f / 255.0f) * (float)g_state.get("xalpha") / 100.0f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(8, 9, 7, 200));
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), kRule);
    const ImVec2 c(x + w * 0.5f, y + h * 0.5f);
    const float half = side * 0.5f;
    const ImU32 col = IM_COL32(r, g, b, (int)(a * 255.0f + 0.5f));
    const unsigned char* tex = nullptr;
    if (g_picture && g_picture(style, &tex) && tex) {
        /* the game's own sprite (red in the original): the Original colour is the texel's own colour times the identity tint; any
         * other colour (or the health colour) replaces it and keeps the texel's alpha as the shape */
        const bool recolor = (int)g_state.get("xcolor") != 0 || (int)g_state.get("xhealth") != 0;
        const float t = side / 32.0f, ox = c.x - half, oy = c.y - half;
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
        for (int j = 0; j < 32; j++)
            for (int i = 0; i < 32; i++) {
                const unsigned char* tp = tex + 4 * (j * 32 + i);
                const int al = tp[3];
                if (al == 0) continue;
                const int cr = recolor ? tintR : tp[0] * tintR / 255, cg = recolor ? tintG : tp[1] * tintG / 255, cb = recolor ? tintB : tp[2] * tintB / 255;
                const int ca = (int)((al / 255.0f) * a * 255.0f + 0.5f);
                dl->AddRectFilled(ImVec2(ox + i * t, oy + j * t), ImVec2(ox + (i + 1) * t + 0.6f, oy + (j + 1) * t + 0.6f), IM_COL32(cr, cg, cb, ca));
            }
        dl->PopClipRect();
        const std::string note2 = tr("The size the game draws it at") + ": " + std::to_string((int)g_state.get("xsize")) + "%";
        text(dl, g_body, 1.4f * u, ImVec2(x + 1.0f * u, y + h - 2.2f * u), kMut, note2.c_str());
        return;
    }
    const float th = style == 1 ? maxf(1.0f, half * 0.05f) : maxf(1.0f, half * 0.12f);
    const float in = style == 1 ? 0.0f : half * 0.30f;   /* the original has a gap at the centre, the thin cross is whole */
    const float out = half * 0.9f;
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    dl->AddLine(ImVec2(c.x - out, c.y), ImVec2(c.x - in, c.y), col, th);
    dl->AddLine(ImVec2(c.x + in, c.y), ImVec2(c.x + out, c.y), col, th);
    dl->AddLine(ImVec2(c.x, c.y - out), ImVec2(c.x, c.y - in), col, th);
    dl->AddLine(ImVec2(c.x, c.y + in), ImVec2(c.x, c.y + out), col, th);
    dl->PopClipRect();
    const std::string note = tr("The size the game draws it at") + ": " + std::to_string((int)g_state.get("xsize")) + "%";
    text(dl, g_body, 1.4f * u, ImVec2(x + 1.0f * u, y + h - 2.2f * u), kMut, note.c_str());
}

/* the gamepad drawn with what each button does in the chosen layout: the left half's labels on the left, the right half's on the right */
void drawDiagram(ImDrawList* dl, const Control& c, float x, float y, float w, float u) {
    if (c.widget == "crosshair") { drawCrosshairPreview(dl, x, y, w, 16.0f * u, u); return; }
    if (c.layouts.empty()) return;
    const auto& L = c.layouts.begin()->second;   /* the one layout of the diagram */
    const float pw = 30.0f * u, ph = g_padAspect > 0 ? pw / g_padAspect : 19.0f * u, px = x + (w - pw) * 0.5f + 3.0f * u, py = y + 3.5f * u;
    const float fs = 1.35f * u;
    const ImU32 ink = kInk, line = kRule;
    if (g_padAspect > 0)   /* the outline (a mask: its lines take this colour) */
        dl->AddImage(ImGui::GetIO().Fonts->TexRef, ImVec2(px, py), ImVec2(px + pw, py + ph), ImVec2(g_padUv[0], g_padUv[1]), ImVec2(g_padUv[2], g_padUv[3]), IM_COL32(150, 150, 135, 255));
    else
        box(dl, ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(21, 22, 15, 255), kRule);
    /* where each element is on the drawing, as fractions of it */
    auto at = [&](float fx, float fy) { return ImVec2(px + fx * pw, py + fy * ph); };
    const ImVec2 ls = at(0.333f, 0.62f), rs = at(0.656f, 0.62f), dp = at(0.24f, 0.33f);
    /* PC draws an Xbox pad, the Switch a Pro Controller: same places, other names. A layout is written with Xbox names;
     * one that goes by letter ("_by" absent) puts a Nintendo letter where that letter is printed, one that goes by position
     * ("_by": "position") keeps the place. */
    const bool nin = padNin(), ps = padPs();
    auto byPos = L.find("_by");
    const bool positional = byPos != L.end() && byPos->second == "position";
    auto place = [&](const std::string& slot) -> std::string {   /* where a slot's button sits, in Xbox names */
        if (!nin || positional) return slot;
        if (slot == "x") return "y";   /* the Nintendo X is on top */
        if (slot == "y") return "x";
        if (slot == "a") return "b";   /* the Nintendo A is on the right */
        if (slot == "b") return "a";
        return slot;
    };
    std::map<std::string, std::string> at_;   /* place -> text */
    for (const auto& kv : L)
        if (kv.first != "_by") at_[place(kv.first)] = kv.second;
    auto nameAt = [&](const char* pos, const char* xbox) -> const char* {
        if (ps) {   /* the face buttons are icons (psIcon); the shoulders have numbers */
            static const char* const kPs[][2] = { { "lt", "L2" }, { "rt", "R2" }, { "lb", "L1" }, { "rb", "R1" } };
            for (const auto& p : kPs) if (!strcmp(p[0], pos)) return p[1];
            return xbox;
        }
        if (!nin) return xbox;
        static const char* const kNin[][2] = { { "lt", "ZL" }, { "rt", "ZR" }, { "lb", "L" }, { "rb", "R" },
                                               { "y", "X" }, { "x", "Y" }, { "b", "A" }, { "a", "B" } };
        for (const auto& p : kNin) if (!strcmp(p[0], pos)) return p[1];
        return xbox;
    };
    /* the letters printed on the face buttons */
    const struct { const char* pos; float fx, fy; } faces[4] = { { "y", .735f, .2226f }, { "x", .6566f, .3435f }, { "b", .8142f, .3435f }, { "a", .735f, .45f } };
    for (const auto& f : faces) {
        const ImVec2 c0 = at(f.fx, f.fy);
        if (ps) { drawIcon(dl, psIcon(f.pos), c0, 1.05f * u, kMut); continue; }
        const char* letter = nameAt(f.pos, f.pos[0] == 'y' ? "Y" : f.pos[0] == 'x' ? "X" : f.pos[0] == 'b' ? "B" : "A");
        const ImVec2 ts = textSize(g_head, 1.15f * u, letter);
        text(dl, g_head, 1.15f * u, ImVec2(c0.x - ts.x * 0.5f, c0.y - ts.y * 0.5f), kMut, letter);
    }
    struct Lab { const char* slot; const char* name; bool left; ImVec2 to; };
    const Lab labs[] = {
        { "lt", "LT", true, at(0.27f, 0.0f) }, { "lb", "LB", true, at(0.30f, 0.07f) },
        { "lstick", "Left stick", true, ls }, { "ls", "L3", true, at(0.333f, 0.70f) }, { "dpad", "D-pad", true, dp },
        { "rt", "RT", false, at(0.73f, 0.0f) }, { "rb", "RB", false, at(0.70f, 0.07f) },
        { "y", "Y", false, at(0.735f, 0.2226f) }, { "x", "X", false, at(0.6566f, 0.3435f) },
        { "b", "B", false, at(0.8142f, 0.3435f) }, { "a", "A", false, at(0.735f, 0.45f) },
        { "rstick", "Right stick", false, rs }, { "rs", "R3", false, at(0.656f, 0.70f) },
    };
    int rowL = 0, rowR = 0;
    for (const Lab& lb : labs) {
        auto it = at_.find(lb.slot);
        if (it == at_.end() || it->second.empty()) continue;
        const int icon = ps ? psIcon(lb.slot) : -1;   /* a face button on a PlayStation pad: its icon, then the action */
        const std::string t = icon >= 0 ? tr(it->second) : std::string(nameAt(lb.slot, lb.name)) + ": " + tr(it->second);
        const float ry = y + 0.6f * u + (lb.left ? rowL++ : rowR++) * 2.9f * u;
        if (icon >= 0 && !lb.left) {   /* the face buttons are all on the right */
            const float lx = px + pw + 3.0f * u;
            drawIcon(dl, icon, ImVec2(lx + fs * 0.5f, ry + fs * 0.55f), fs, ink);
            text(dl, g_body, fs, ImVec2(lx + fs * 1.4f, ry), ink, t.c_str());
            dl->AddLine(ImVec2(lx - 0.4f * u, ry + fs * 0.55f), lb.to, line, 1.0f);
        } else if (lb.left) {
            const ImVec2 ts = textSize(g_body, fs, t.c_str());
            const float lx = px - 3.0f * u;
            text(dl, g_body, fs, ImVec2(lx - ts.x, ry), ink, t.c_str());
            dl->AddLine(ImVec2(lx + 0.4f * u, ry + fs * 0.55f), lb.to, line, 1.0f);
        } else {
            const float lx = px + pw + 3.0f * u;
            text(dl, g_body, fs, ImVec2(lx, ry), ink, t.c_str());
            dl->AddLine(ImVec2(lx - 0.4f * u, ry + fs * 0.55f), lb.to, line, 1.0f);
        }
    }
}

void openSelect(const Control& c) {
    g_selId = c.id;
    g_selCursor = (int)g_state.get(c.id);
    g_selScroll = 0;
    g_selOpenFrame = ImGui::GetFrameCount();
}

void drawSelect(ImDrawList* dl, const Control& c, int fi, float x, float y, float u, bool disabled) {
    const float bh = 3.1f * u, bw = 24.0f * u;
    char id[64];
    snprintf(id, sizeof id, "##%s", c.id.c_str());
    if (hit(id, ImVec2(x, y), ImVec2(bw, bh)) && !disabled) { openSelect(c); g_focus = fi; }
    const bool open = g_selId == c.id;
    box(dl, ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(21, 22, 15, 255), open ? kGold : kRule);
    const int cur = (int)g_state.get(c.id);
    const std::string label = cur >= 0 && cur < (int)c.options.size() ? tr(c.options[cur].label) : std::string();
    text(dl, g_head, 1.55f * u, ImVec2(x + 1.2f * u, y + (bh - 1.55f * u) * 0.5f), disabled ? kDisabled : kInk, label.c_str());
    const ImU32 ac = disabled ? kDisabled : kInk;   /* the little down arrow */
    dl->AddTriangleFilled(ImVec2(x + bw - 2.6f * u, y + bh * 0.4f), ImVec2(x + bw - 1.2f * u, y + bh * 0.4f), ImVec2(x + bw - 1.9f * u, y + bh * 0.64f), ac);
    if (open) { g_selAnchor = ImVec2(x, y); g_selW = bw; g_selH = bh; }
}

}  // namespace

void fontsBuild() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig base;
    base.FontDataOwnedByAtlas = false;   /* the caller keeps the TTF bytes */
    base.OversampleH = 2;
    base.OversampleV = 2;
    g_body = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(g_ttf[0]), (int)g_ttfLen[0], 48.0f, &base);
    g_head = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(g_ttf[1]), (int)g_ttfLen[1], 72.0f, &base);
    ImFontConfig exact;   /* drawn at its own size on whole pixels: no oversampling needed */
    exact.FontDataOwnedByAtlas = false;
    exact.OversampleH = 1;
    exact.OversampleV = 1;
    exact.PixelSnapH = true;
    for (int fam = 0; fam < 2; fam++) {
        g_baked[fam].clear();
        for (int k : g_want[fam])
            g_baked[fam][k] = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(g_ttf[fam]), (int)g_ttfLen[fam], (float)k, &exact);
    }
    g_fontsDirty = false;
}

bool fontsFrame(float W) {
    if (W != g_fontsW) {   /* another window size: other pixel sizes; the old ones go at the next rebuild */
        g_fontsW = W;
        g_want[0].clear();
        g_want[1].clear();
        g_baked[0].clear();
        g_baked[1].clear();
        g_fontsDirty = true;
    }
    return g_fontsDirty;
}

void init(const void* bodyTtf, size_t bodyLen, const void* headTtf, size_t headLen) {
    ImGuiIO& io = ImGui::GetIO();
    g_ttf[0] = bodyTtf;
    g_ttfLen[0] = bodyLen;
    g_ttf[1] = headTtf;
    g_ttfLen[1] = headLen;
    fontsBuild();
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);   /* the menu has its own focus model */
}

bool setData(const char* json, size_t len, std::string* err) {
    Model m;
    std::string e;
    if (!loadModel(json, len, &m, &e)) {
        g_error = e;
        if (err) *err = e;
        return false;
    }
    g_model = std::move(m);
    /* rebuild the id index against the moved model */
    g_model.byId.clear();
    for (const Tab& t : g_model.tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) g_model.byId[c.id] = &c;
    State fresh;
    fresh.pc = g_state.pc;
    fresh.console = g_state.console;   /* set before setData (menu_game.cpp): a fresh State would show the window rows */
    fresh.handheld = g_state.handheld;
    fresh.dev = g_state.dev;
    fresh.reset(g_model);
    for (auto& kv : fresh.v) {   /* keep what was set */
        auto old = g_state.v.find(kv.first);
        if (old != g_state.v.end()) kv.second = old->second;
    }
    g_state = std::move(fresh);
    g_error.clear();
    g_tab = g_tab < (int)g_model.tabs.size() ? g_tab : 0;
    g_entry = 0;
    g_focus = -1;
    return true;
}

void setOwner(int seat) { g_ownerSeat = seat; }

void setIconImage(int icon, float u0, float v0, float u1, float v1) {
    if (icon < 0 || icon >= 4) return;
    g_iconUv[icon][0] = u0; g_iconUv[icon][1] = v0; g_iconUv[icon][2] = u1; g_iconUv[icon][3] = v1;
    g_iconOk[icon] = true;
}

void setPadImage(float u0, float v0, float u1, float v1, float aspect) {
    g_padUv[0] = u0; g_padUv[1] = v0; g_padUv[2] = u1; g_padUv[3] = v1;
    g_padAspect = aspect;
}

void setContext(bool fileSelect, bool inStage, bool updateAvailable, bool profile) {
    g_state.profile = profile;
    g_state.updateAvailable = updateAvailable;
    g_state.fileSelect = fileSelect;
    g_state.inStage = inStage;
}

void setUpdate(int state, const char* tag, const char* notes, const char* version, bool installable, bool busy, const char* status) {
    g_state.updateInstallable = installable;
    g_state.updateBusy = busy;
    g_state.updateStatus = status ? status : "";
    g_state.updateState = state;
    g_state.updateTag = tag ? tag : "";
    g_state.updateNotes = notes ? notes : "";
    g_state.version = version ? version : "";
}

void showRoot() {
    g_root = true;
    g_updPanel = false;
    g_rootSel = 0;
    g_rootArmed = -1;
    g_selId.clear();
}

void debugGoto(int tab, int entry, int focus) {
    g_root = false;
    if (tab >= 0 && tab < (int)g_model.tabs.size()) g_tab = tab;
    g_entry = entry < 0 ? 0 : entry;
    g_focus = focus;
}

void setKeyBinding(std::string (*text)(const char*, int), void (*begin)(const char*, int), bool (*active)()) {
    g_keyText = text;
    g_keyBegin = begin;
    g_keyActive = active;
}

void setActionHandler(void (*handler)(const char*)) { g_actionHandler = handler; }
void setPackList(int (*rows)(std::vector<PackRow>* out), int (*act)(int row, int what)) { g_packRows = rows; g_packAct = act; }

void setReadout(const char* (*f)(const char*, double)) { g_readout = f; }
void setTint(void (*f)(int*, int*, int*)) { g_tint = f; }
void setPicture(int (*f)(int, const unsigned char**)) { g_picture = f; }

void setBinding(bool (*get)(const char*, double*), void (*set)(const char*, double)) {
    g_bindGet = get;
    g_bindSet = set;
    g_synced.clear();
}

void setOptionLabels(const char* id, const std::vector<std::string>& labels) {
    auto it = g_model.byId.find(id);
    if (it == g_model.byId.end()) return;
    Control* c = const_cast<Control*>(it->second);
    c->options.clear();
    for (size_t i = 0; i < labels.size(); i++) {
        Option o;
        o.id = std::to_string(i);
        o.label = labels[i];
        o.value = (double)i;
        c->options.push_back(o);
    }
}

void setPlatform(bool pc) {
    if (g_state.pc == pc) return;
    g_state.pc = pc;
    g_state.reset(g_model);
    g_entry = 0;
    g_focus = -1;
}
void setConsole(bool console) { g_state.console = console; g_entry = 0; g_focus = -1; }
void setHandheld(bool handheld) { g_state.handheld = handheld; g_entry = 0; g_focus = -1; }
void setPadFamily(int fam) { g_padFam = fam; }
void setDev(bool dev) { g_state.dev = dev; g_entry = 0; g_focus = -1; }
bool isPc() { return g_state.pc; }
bool devOn() { return g_state.dev; }

void drawFooter(ImDrawList* dl, float W, float H, float footH, float u, const char* (*hints)[2], int n) {
    dl->AddRectFilled(ImVec2(0, H - footH), ImVec2(W, H), IM_COL32(13, 14, 12, 208));
    dl->AddLine(ImVec2(0, H - footH), ImVec2(W, H - footH), kGoldDim, 1.0f);
    const float px = 1.75f * u, cy = H - footH * 0.5f - px * 0.5f;
    float x = 2.4f * u;
    for (int i = 0; i < n; i++) {
        const char* const* h = hints[i];
        /* the keys are written with the confirm / cancel letters (A, B): a PlayStation pad shows its icons there */
        const int icon = padPs() ? (!strcmp(h[0], "A") ? 0 : !strcmp(h[0], "B") ? 1 : -1) : -1;
        const char* key = padPs() && !strcmp(h[0], "L / R") ? "L1 / R1" : h[0];
        const ImVec2 ks = icon >= 0 ? ImVec2(px, px) : textSize(g_head, px, key);
        const float bw = ks.x + 1.2f * u;
        dl->AddRectFilled(ImVec2(x, cy - 0.3f * u), ImVec2(x + bw, cy + px + 0.3f * u), kInk, 0.0f);
        if (icon >= 0)
            drawIcon(dl, icon, ImVec2(x + bw * 0.5f, cy + px * 0.5f), px * 0.95f, IM_COL32(21, 22, 15, 255));
        else
            text(dl, g_head, px, ImVec2(x + 0.6f * u, cy), IM_COL32(21, 22, 15, 255), key);
        x += bw + 0.9f * u;
        const std::string label = tr(h[1]);
        text(dl, g_body, px, ImVec2(x, cy), kInk, label.c_str());
        x += textSize(g_body, px, label.c_str()).x + 3.0f * u;
    }
}

float g_updScroll = 0.0f;  /* lines scrolled */
bool g_updArmed = false;   /* Switch: the first A of the install was pressed */
double g_updArmedAt = 0.0;

/* a release's text: the version, then the notes, scrolled with up / down; A opens the page, B goes back */
void drawUpdatePanel(ImDrawList* dl, float W, float H, float u, float hh) {
    const float footH = 4.0f * u, px = 1.9f * u, lineH = px * 1.35f;
    const bool up = pressed(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, true);
    const bool down = pressed(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, true);
    const bool ok = pressed(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown, ImGuiKey_None, false);
    const bool back = pressed(ImGuiKey_Backspace, ImGuiKey_GamepadFaceRight, ImGuiKey_None, false);
    if (back) { g_updPanel = false; g_updArmed = false; return; }
    const double now = ImGui::GetTime();
    if (g_updArmed && now - g_updArmedAt > 3.0) g_updArmed = false;   /* the confirm lasts 3 s */
    if (ok && g_actionHandler) {
        if (!g_state.updateInstallable) g_actionHandler("__UpdateOpen");
        else if (!g_state.updateBusy) {
            if (g_updArmed) { g_updArmed = false; g_actionHandler("__UpdateInstall"); }
            else { g_updArmed = true; g_updArmedAt = now; }
        }
    }
    const float x0 = 10.0f * u, w = W - 2 * x0, top = hh + 2.0f * u;
    {   /* one line: the release, then what you have */
        const std::string title = upper(tr("Update available") + ": " + g_state.updateTag);
        const float tp = 2.0f * u;
        text(dl, g_head, tp, ImVec2(x0, top), kGold, title.c_str());
        std::string have = "(" + tr("you have") + " " + g_state.version + ")";
        if (g_state.updateInstallable && g_state.updateStatus.compare(0, 16, "Update available") != 0) have = g_state.updateStatus;   /* downloading, done, failed */
        else if (g_updArmed) have = tr("Press A again to install");
        text(dl, g_body, px * 0.9f, ImVec2(x0 + textSize(g_head, tp, title.c_str()).x + 1.2f * u, top + tp * 0.18f), kMut, have.c_str());
    }
    /* the notes: wrapped to the width; # / ## / ### lines are headings (the only markdown handled); runs of blank
     * lines count as one, and a heading has one above it */
    struct PLine { std::string s; int lvl; };
    std::vector<PLine> lines;
    auto blank = [&]() { if (!lines.empty() && !lines.back().s.empty()) lines.push_back({ "", 0 }); };
    const std::string& n = g_state.updateNotes;
    size_t i = 0;
    while (i <= n.size()) {
        size_t e = n.find('\n', i);
        if (e == std::string::npos) e = n.size();
        std::string para = n.substr(i, e - i);
        i = e + 1;
        while (!para.empty() && (para.back() == ' ' || para.back() == '\t')) para.pop_back();
        if (para.empty()) { blank(); continue; }
        int lvl = 0;
        if (para[0] == '#') {
            size_t h = 0;
            while (h < para.size() && para[h] == '#') h++;
            lvl = h > 3 ? 3 : (int)h;
            while (h < para.size() && para[h] == ' ') h++;
            para = para.substr(h);
            if (para.empty()) continue;
            blank();
        }
        const float wpx = lvl == 1 ? 2.4f * u : lvl == 2 ? 2.1f * u : px;
        ImFont* wf = lvl ? g_head : g_body;
        std::string cur;
        size_t k = 0;
        while (k < para.size()) {
            size_t sp = para.find(' ', k);
            if (sp == std::string::npos) sp = para.size();
            const std::string word = para.substr(k, sp - k), tryS = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && textSize(wf, wpx, tryS.c_str()).x > w) { lines.push_back({ cur, lvl }); cur = word; }
            else cur = tryS;
            k = sp + 1;
        }
        if (!cur.empty()) lines.push_back({ cur, lvl });
    }
    while (!lines.empty() && lines.back().s.empty()) lines.pop_back();
    const float y0 = top + 4.2f * u, areaH = H - footH - y0 - 1.0f * u;
    const int visible = (int)(areaH / lineH), total = (int)lines.size(), maxScroll = total > visible ? total - visible : 0;
    g_updScroll -= wheelScrollStep(ImGui::GetIO().MouseWheel, u) / lineH;   /* the release text has nothing to select: the wheel scrolls it */
    if (up) g_updScroll -= 1.0f;
    if (down) g_updScroll += 1.0f;
    if (g_updScroll < 0) g_updScroll = 0;
    if (g_updScroll > maxScroll) g_updScroll = (float)maxScroll;
    dl->PushClipRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + areaH), true);
    for (int l = (int)g_updScroll; l < total && l - (int)g_updScroll <= visible; l++)
    {
        const PLine& pl = lines[l];
        const float lp = pl.lvl == 1 ? 2.4f * u : pl.lvl == 2 ? 2.1f * u : px;
        text(dl, pl.lvl ? g_head : g_body, lp, ImVec2(x0, y0 + (l - (int)g_updScroll) * lineH + (lineH - lp) * 0.3f), pl.lvl ? kGold : kInk, pl.s.c_str());
    }
    dl->PopClipRect();
    static const char* hintsPc[3][2] = { { "A", "Open the page" }, { "Up/Down", "Scroll" }, { "B", "Back" } };
    static const char* hintsNx[3][2] = { { "A", "Install" }, { "Up/Down", "Scroll" }, { "B", "Back" } };
    drawFooter(dl, W, H, footH, u, g_state.updateInstallable ? hintsNx : hintsPc, 3);
}

/* the first menu: what a pause menu offers, then the options */
void drawRoot(ImDrawList* dl, float W, float H, float u, float hh) {
    if (g_updPanel) { drawUpdatePanel(dl, W, H, u, hh); return; }
    static const char* const ids[6] = { "Resume", "Options", "Controllers", "Updates", "Restart", "Quit" };
    static const char* const actions[6] = { "", "", "__AssignDevices", "__UpdateCheck", "__RestartGame", "__QuitToDesktop" };
    std::vector<int> shown;   /* no self-relaunch on the Switch: no Restart there */
    for (int i = 0; i < 6; i++)
        if (i != 4 || g_state.pc) shown.push_back(i);
    const int n = (int)shown.size();
    const float footH = 4.0f * u, bw = 30.0f * u, bh = 5.4f * u, gap = 1.2f * u;
    const bool up = !g_capturing && pressed(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, true);
    const bool down = !g_capturing && pressed(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, true);
    const bool ok = pressed(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown, ImGuiKey_None, false);
    const bool back = pressed(ImGuiKey_Backspace, ImGuiKey_GamepadFaceRight, ImGuiKey_None, false);
    if (g_rootSel >= n) g_rootSel = n - 1;
    if (up && g_rootSel > 0) { g_rootSel--; g_rootArmed = -1; }
    if (down && g_rootSel < n - 1) { g_rootSel++; g_rootArmed = -1; }
    if (back) { g_closeReq = true; return; }   /* B resumes */
    const float total = n * bh + (n - 1) * gap, y0 = hh + (H - hh - footH - total) * 0.45f, x0 = (W - bw) * 0.5f;
    int fire = ok ? g_rootSel : -1;
    for (int k = 0; k < n; k++) {
        const int i = shown[k];
        const ImVec2 a(x0, y0 + k * (bh + gap)), b(a.x + bw, a.y + bh);
        char id[24];
        snprintf(id, sizeof id, "##root%d", i);
        if (hit(id, a, ImVec2(bw, bh))) { g_rootSel = k; fire = k; }
        else if ((ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0) && ImGui::IsMouseHoveringRect(a, b)) {
            if (g_rootSel != k) g_rootArmed = -1;
            g_rootSel = k;   /* the mouse over a row selects it, as the pad does */
        }
        const bool on = k == g_rootSel, armed = i == g_rootArmed;
        box(dl, a, b, on ? kGoldFill : IM_COL32(21, 22, 15, 255), on ? kGold : kRule);
        if (on) dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 0.4f * u, b.y), kGold);
        std::string label;
        if (i == 3) {   /* the update button says where the check stands */
            static const char* const upd[5] = { "Check for updates", "Checking...", "Up to date: check again", "Update available", "Could not check: try again" };
            label = upper(tr(upd[g_state.updateState >= 0 && g_state.updateState < 5 ? g_state.updateState : 0]));
        } else label = upper(tr(ids[i]));
        if (armed) label += "?  " + upper(tr("Press again"));
        textCenter(dl, g_head, 2.3f * u, a, b, armed ? kGold : kInk, label.c_str());
    }
    if (fire >= 0) {
        const int i = shown[fire];
        if (i == 0) { g_closeReq = true; return; }
        if (i == 1) { g_root = false; g_rootArmed = -1; return; }
        if (i == 2) { if (g_actionHandler) g_actionHandler(actions[i]); return; }   /* the device screen replaces the menu */
        if (i == 3) {   /* the update button: the release text when there is one, else a check */
            if (g_state.updateState == 3) { g_updPanel = true; g_updScroll = 0; }
            else if (g_state.updateState != 1 && g_actionHandler) g_actionHandler(actions[i]);
            return;
        }
        if (g_rootArmed == i) { g_rootArmed = -1; if (g_actionHandler) g_actionHandler(actions[i]); }   /* a second press confirms */
        else g_rootArmed = i;
    }
}

/* sine and cosine without libm (the game's math.h shadows the system one here) */
void sincosApprox(float a, float* s, float* c) {
    const float pi = 3.14159265f;
    while (a > pi) a -= 2 * pi;
    while (a < -pi) a += 2 * pi;
    const float a2 = a * a;
    *s = a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42 * (1 - a2 / 72))));
    *c = 1 - a2 / 2 * (1 - a2 / 12 * (1 - a2 / 30 * (1 - a2 / 56 * (1 - a2 / 90))));
}

/* Input.CrosshairCursor: a crosshair replaces the system pointer over the menu (the game hides the system one then).
 * It shows while the mouse is the device in use: a pad or keyboard press hides it until the mouse moves again. */
void drawPointer(bool crosshair, float W, float H) {
    static bool mouseOn = true;
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(0)) mouseOn = true;
    for (ImGuiKey k : { ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Enter,
                        ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,
                        ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight })
        if (ImGui::IsKeyPressed(k, false)) mouseOn = false;
    if (!crosshair || !mouseOn || io.MousePos.x < 0 || io.MousePos.x > W || io.MousePos.y < 0 || io.MousePos.y > H) return;
    const float u = W / 100.0f, r = 1.1f * u, gap = 0.35f * u, th = 0.28f * u;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c = io.MousePos;
    const ImVec2 d[4][2] = { { ImVec2(-r, 0), ImVec2(-gap, 0) }, { ImVec2(gap, 0), ImVec2(r, 0) },
                             { ImVec2(0, -r), ImVec2(0, -gap) }, { ImVec2(0, gap), ImVec2(0, r) } };
    for (int pass = 0; pass < 2; pass++)   /* a dark outline, then the white lines */
        for (const auto& s : d)
            dl->AddLine(ImVec2(c.x + s[0].x, c.y + s[0].y), ImVec2(c.x + s[1].x, c.y + s[1].y),
                        pass == 0 ? IM_COL32(0, 0, 0, 200) : IM_COL32(255, 255, 255, 235), pass == 0 ? th * 2.4f : th);
}

void draw(float W, float H) {
    const float u = W / 100.0f;   /* 1 unit = 1% of the width: the design is laid out in these */
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.WindowBorderSize = 0.0f;

    /* the values live in the game: a change made here is stored, then every control reads back what the game holds */
    if (g_bindGet && g_bindSet) {
        for (const Tab& t : g_model.tabs)
            for (const Item& it : t.items)
                for (const Control& c : it.controls) {
                    if (c.key.empty() || c.type == CT_ACTION || c.type == CT_BIND || c.type == CT_DIAGRAM) continue;
                    auto sy = g_synced.find(c.id);
                    const double cur = g_state.get(c.id);
                    if (sy != g_synced.end() && sy->second != cur) g_bindSet(c.key.c_str(), controlToConfig(c, cur));
                    double cfg, ctl;
                    if (g_bindGet(c.key.c_str(), &cfg) && controlFromConfig(c, cfg, &ctl)) g_state.v[c.id] = ctl;
                    g_synced[c.id] = g_state.get(c.id);
                }
    }

    /* a value whose option is no longer offered (the display mode changed) moves to the first one that is */
    for (const Tab& t : g_model.tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) {
                if (c.options.empty() || c.type == CT_SLIDER) continue;
                if (optionVisible(c, (int)g_state.get(c.id), g_model, g_state)) continue;
                const std::vector<int> offered = visibleOptions(c, g_model, g_state);
                if (!offered.empty()) g_state.v[c.id] = offered[0];
            }

    if (g_packRows) g_packRows(&g_packs);
    if (g_packRow > (int)g_packs.size()) g_packRow = (int)g_packs.size();   /* the last row is the buttons */
    if (g_packRow < 0) g_packRow = 0;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_blockHits = !g_selId.empty();

    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(40, 43, 34, 235), IM_COL32(26, 28, 22, 240),
                                IM_COL32(16, 17, 14, 245), IM_COL32(26, 28, 22, 240));

    /* ---- header ---- */
    const float hh = 4.6f * u;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, hh), IM_COL32(13, 14, 12, 208));
    dl->AddLine(ImVec2(0, hh), ImVec2(W, hh), kGoldDim, 1.0f);
    {
        const float px = 2.4f * u;
        text(dl, g_head, px, ImVec2(2.4f * u, (hh - px) * 0.5f), kInk, "PROJECT");
        text(dl, g_head, px, ImVec2(2.4f * u + textSize(g_head, px, "PROJECT ").x, (hh - px) * 0.5f), kGold, "DAM");
        if (!g_state.version.empty()) {   /* very small, at the bottom right of the title */
            const float vp = 1.15f * u, tx = 2.4f * u + textSize(g_head, px, "PROJECT DAM").x + 0.6f * u;
            text(dl, g_body, vp, ImVec2(tx, (hh - px) * 0.5f + px - vp * 0.75f), kMut, g_state.version.c_str());
        }
    }
    const int nTabs = (int)g_model.tabs.size();
    if (nTabs == 0) {   /* the content did not load: say why */
        text(dl, g_body, 1.8f * u, ImVec2(4.0f * u, hh + 3.0f * u), kInk, g_error.empty() ? "No menu content." : g_error.c_str());
        ImGui::End();
        return;
    }
    if (g_tab >= nTabs) g_tab = 0;
    if (!g_root && !g_capturing && (ImGui::IsKeyPressed(ImGuiKey_GamepadL1) || ImGui::IsKeyPressed(ImGuiKey_PageUp))) g_tab = (g_tab + nTabs - 1) % nTabs, g_entry = 0, g_focus = -1;
    if (!g_root && !g_capturing && (ImGui::IsKeyPressed(ImGuiKey_GamepadR1) || ImGui::IsKeyPressed(ImGuiKey_PageDown))) g_tab = (g_tab + 1) % nTabs, g_entry = 0, g_focus = -1;
    {
        const float tw = 12.0f * u, th = 2.8f * u, gap = 0.9f * u;   /* the tabs: shorter buttons, same place */
        float x = (W - (nTabs * tw + (nTabs - 1) * gap)) * 0.5f;
        for (int i = 0; i < (g_root ? 0 : nTabs); i++) {
            char id[16];
            snprintf(id, sizeof id, "##tab%d", i);
            const ImVec2 a(x, (hh - th) * 0.5f), b(x + tw, (hh - th) * 0.5f + th);
            if (hit(id, a, ImVec2(tw, th))) g_tab = i, g_entry = 0, g_focus = -1;
            const bool on = i == g_tab;
            box(dl, a, b, on ? kGoldFill : IM_COL32(21, 22, 15, 255), on ? kGold : kRule);
            if (on) dl->AddRectFilled(ImVec2(a.x, b.y - 0.25f * u), b, kGold);
            textCenter(dl, g_head, 1.9f * u, a, b, kInk, upper(tr(g_model.tabs[i].name)).c_str());
            x += tw + gap;
        }
        const std::string paused = upper(tr("Paused"));
        const ImVec2 ps = textSize(g_head, 1.9f * u, paused.c_str());
        text(dl, g_head, 1.9f * u, ImVec2(W - 2.4f * u - ps.x, (hh - ps.y) * 0.5f), kInk, paused.c_str());
        float rx = W - 2.4f * u - ps.x;   /* left edge of what the header has drawn so far */
        if (g_ownerSeat >= 0) {   /* whose pad drives the menu */
            const std::string who = upper(tr("Player")) + " " + std::to_string(g_ownerSeat + 1);
            const float bw = textSize(g_head, 1.5f * u, who.c_str()).x + 2.4f * u, bx = rx - 1.2f * u - bw;
            box(dl, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kGoldFill, kGold);
            textCenter(dl, g_head, 1.5f * u, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kInk, who.c_str());
            rx = bx;
        }
        if (g_state.dev) {   /* a reminder that rows beyond the everyday menu are showing */
            const float bx = rx - 1.2f * u - 5.0f * u;
            box(dl, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + 5.0f * u, (hh + 2.6f * u) * 0.5f), IM_COL32(60, 40, 20, 255), kGold);
            textCenter(dl, g_head, 1.5f * u, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + 5.0f * u, (hh + 2.6f * u) * 0.5f), kGold, "DEV");
        }
    }

    if (g_root) {
        drawRoot(dl, W, H, u, hh);
        static const char* rootHints[2][2] = { { "A", "Select" }, { "B", "Resume" } };
        if (!g_updPanel) drawFooter(dl, W, H, 4.0f * u, u, rootHints, 2);   /* the release text has its own */
        ImGui::End();
        return;
    }

    /* ---- what is visible, and the navigation ---- */
    const Tab& tab = g_model.tabs[g_tab];
    Vis vis = visibleOf(tab);
    if (vis.entries.empty()) {
        text(dl, g_body, 1.8f * u, ImVec2(4.0f * u, hh + 3.0f * u), kMut, tr("Nothing to show on this tab.").c_str());
        ImGui::End();
        return;
    }
    if (g_entry >= (int)vis.entries.size()) g_entry = (int)vis.entries.size() - 1;
    auto controlsOf = [&](int visEntry) {
        std::vector<const Control*> cs;
        for (const Control& c : tab.items[vis.entries[visEntry]].controls)
            if (controlVisible(c, g_model, g_state) && c.widget != "crosshair") cs.push_back(&c);   /* the preview is pinned on the left */
        return cs;
    };
    vis.controls = controlsOf(g_entry);
    if (g_focus >= (int)vis.controls.size()) g_focus = (int)vis.controls.size() - 1;
    {
        g_capturing = g_keyActive && g_keyActive();
        bool up = pressed(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, true);
        bool down = pressed(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, true);
        bool left = pressed(ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, true);
        bool right = pressed(ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, true);
        bool ok = pressed(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown, ImGuiKey_None, false);
        bool back = pressed(ImGuiKey_Backspace, ImGuiKey_GamepadFaceRight, ImGuiKey_None, false);
        if (g_capturing) up = down = left = right = ok = back = false;
        {   /* The mouse wheel: where there is something to select it is the up / down keys, and the scroll follows the
             * selection (the one thing that moves the scroll); where there is nothing to select (text only) it scrolls. */
            const ImGuiIO& wio = ImGui::GetIO();
            if (wio.MouseWheel != 0 && g_selId.empty() && !g_capturing) {
                const bool overForm = wio.MousePos.x > 32.9f * u;   /* right of the entries' column */
                if (overForm && controlsOf(g_entry).empty()) g_scrollTarget -= wheelScrollStep(wio.MouseWheel, u);   /* a form with no control in it */
                else if (overForm && g_focus < 0) g_focus = 0;   /* the first notch takes the first control */
                else {
                    if (!overForm && g_focus >= 0) g_focus = -1;   /* over the entries: back to the list */
                    if (wio.MouseWheel < 0) down = true; else up = true;
                }
            }
        }
        if (!g_selId.empty()) {   /* an open list takes the keys: up / down move in it, A picks, B closes it */
            auto sit = g_model.byId.find(g_selId);
            const std::vector<int> offered = sit == g_model.byId.end() ? std::vector<int>() : visibleOptions(*sit->second, g_model, g_state);
            const int n = (int)offered.size();
            int pos = 0;
            for (int k = 0; k < n; k++) if (offered[k] == g_selCursor) pos = k;
            if (up && pos > 0) g_selCursor = offered[pos - 1];
            if (down && pos < n - 1) g_selCursor = offered[pos + 1];
            if (ok && n > 0) { g_state.v[g_selId] = g_selCursor; g_selId.clear(); }
            else if (back) g_selId.clear();
        } else if (g_focus < 0) {
            if (up && g_entry > 0) { g_entry--; g_scrollTarget = 0; }
            if (down && g_entry < (int)vis.entries.size() - 1) { g_entry++; g_scrollTarget = 0; }
            if (ok && !controlsOf(g_entry).empty()) g_focus = 0;   /* A goes into the form; Right does not, since Left would not come back */
            if (back) { g_root = true; g_rootSel = 1; g_rootArmed = -1; }
            vis.controls = controlsOf(g_entry);
        } else {
            const bool inPacks = vis.controls[g_focus]->type == CT_PACKLIST;
            const int nPacks = (int)g_packs.size();
            if (up) {
                if (inPacks && g_packRow > 0) g_packRow--;
                else if (g_focus > 0) { g_focus--; if (vis.controls[g_focus]->type == CT_PACKLIST) g_packRow = nPacks; }
            }
            if (down) {
                if (inPacks && g_packRow < nPacks) g_packRow++;
                else if (g_focus < (int)vis.controls.size() - 1) { g_focus++; if (vis.controls[g_focus]->type == CT_PACKLIST) g_packRow = 0; }
            }
            const Control& c = *vis.controls[g_focus];
            const bool dis = controlDisabled(c, g_model, g_state);
            if (c.type == CT_PACKLIST) {   /* Left / Right pick the checkbox or an arrow of the row, A does it */
                const int maxCol = g_packRow == nPacks ? 1 : 2;   /* the buttons row has two */
                if (left && g_packCol > 0) g_packCol--;
                if (right && g_packCol < maxCol) g_packCol++;
                if (g_packCol > maxCol) g_packCol = maxCol;
                if (ok) {
                    double dv = 0;
                    if (g_packRow < nPacks) packDo(g_packRow, g_packCol);
                    else if (g_bindGet && g_bindGet("__AssetPackDirty", &dv) && dv != 0 && g_actionHandler) g_actionHandler(g_packCol == 0 ? "__AssetPackApply" : "__AssetPackDiscard");
                }
            } else if (!dis && c.type == CT_BIND) {
                if (left) g_slotSel = 0;
                if (right) g_slotSel = 1;
                if (ok) startCapture(c, g_slotSel);
            } else if (!dis) {
                const bool press = c.type == CT_ACTION || c.type == CT_TOGGLE || c.type == CT_SELECT;   /* pressed only by A / Enter / a click: */
                if (left && !press) changeValue(c, -1);                          /* a stick passing through Left / Right */
                if (right && !press) changeValue(c, +1);                         /* must not fire or flip them */
                if (ok && c.type == CT_SELECT) openSelect(c);
                else if (ok && c.type != CT_SLIDER) changeValue(c, +1);
            }
            if (back && g_selId.empty()) g_focus = -1;
        }
    }

    const float footH = 4.0f * u, bodyTop = hh, bodyBot = H - footH;
    const float leftX = 3.1f * u, leftW = 28.0f * u, divX = leftX + leftW + 1.8f * u;
    dl->AddLine(ImVec2(divX, bodyTop + 1.2f * u), ImVec2(divX, bodyBot - 1.2f * u), kGoldDim, 1.0f);

    /* ---- the entries, with their group headings ---- */
    {
        float y = bodyTop + 2.2f * u;
        const float eh = 3.4f * u;
        int visIdx = 0;
        for (size_t i = 0; i < tab.items.size(); i++) {
            const Item& it = tab.items[i];
            if (it.group) {
                bool anyBelow = false;   /* a heading with no visible entry under it is not drawn */
                for (size_t j = i + 1; j < tab.items.size() && !tab.items[j].group; j++)
                    if (itemVisible(tab.items[j], g_model, g_state)) { anyBelow = true; break; }
                if (anyBelow && itemVisible(it, g_model, g_state)) {
                    text(dl, g_head, 1.3f * u, ImVec2(leftX, y + 0.4f * u), kMut, upper(tr(it.name)).c_str());
                    y += 2.4f * u;
                }
                continue;
            }
            if (!itemVisible(it, g_model, g_state)) continue;
            const float ind = it.sub ? 2.2f * u : 0.0f;
            const float h = it.sub ? 3.1f * u : eh;
            char id[32];
            snprintf(id, sizeof id, "##ent%d", (int)i);
            if (hit(id, ImVec2(leftX + ind, y), ImVec2(leftW - ind, h))) { g_entry = visIdx; g_focus = -1; g_scrollTarget = 0; }
            const bool on = visIdx == g_entry;
            if (it.sub) dl->AddLine(ImVec2(leftX + ind - 1.3f * u, y + h * 0.5f), ImVec2(leftX + ind - 0.3f * u, y + h * 0.5f), kRule, 1.0f);
            box(dl, ImVec2(leftX + ind, y), ImVec2(leftX + leftW, y + h), on ? (g_focus >= 0 ? kGoldSoft : kGoldFill) : kRow, on ? kGold : kRule);
            if (on) dl->AddRectFilled(ImVec2(leftX + ind, y), ImVec2(leftX + ind + 0.45f * u, y + h), kGold);
            const float px = it.sub ? 1.6f * u : 1.75f * u;
            text(dl, g_body, px, ImVec2(leftX + ind + 1.6f * u, y + (h - px) * 0.5f - 0.15f * u), kInk, tr(it.name).c_str());
            y += h + 0.5f * u;
            visIdx++;
        }
    }

    /* ---- the crosshair preview, pinned under the entries where the changes show while they are made ---- */
    {
        const Item& pe = tab.items[vis.entries[g_entry]];
        for (const Control& pc : pe.controls)
            if (pc.widget == "crosshair" && controlVisible(pc, g_model, g_state)) {
                drawCrosshairPreview(dl, leftX, bodyBot - 14.4f * u, leftW, 13.2f * u, u);
                break;
            }
    }

    /* ---- the form of the selected entry, scrolled to keep the focused control in view ---- */
    {
        const Item& e = tab.items[vis.entries[g_entry]];
        const float fx = divX + 2.6f * u, fw = W - fx - 2.6f * u;
        float y = bodyTop + 2.2f * u;
        text(dl, g_head, 1.3f * u, ImVec2(fx, y + 0.4f * u), kInk, upper(tr(e.name)).c_str());
        y += 2.6f * u;
        dl->AddLine(ImVec2(fx, y), ImVec2(fx + fw, y), kGold, 1.0f);
        y += 1.0f * u;
        const float top = y, bottom = bodyBot - 1.4f * u;

        const float cx0 = fx + 20.5f * u, availW = fx + fw - cx0 - 1.0f * u;
        struct Row { float y, h, helpH; };
        std::vector<Row> rows;
        float cy = 0;
        for (const Control* c : vis.controls) {
            const std::string help = tr(helpFor(*c, g_state.get(c->id)));
            float rowH = 4.6f * u;
            if (c->type == CT_DIAGRAM) rowH = (c->widget == "crosshair" ? 17.0f : 27.0f) * u;
            if (c->type == CT_PACKLIST) rowH = packListH(u);
            if (c->type == CT_CHOICE) {
                std::vector<Slot> slots;
                const float ch = layoutChoice(*c, 0, availW, u, &slots);
                rowH = maxf(rowH, ch + 1.5f * u);
            }
            const float helpH = help.empty() ? 0.0f : textSizeWrapped(g_body, 1.4f * u, fw - 2.4f * u, help.c_str()).y + 0.6f * u;   /* wrapped */
            const float total = rowH + helpH;
            rows.push_back({ cy, total, helpH });
            cy += total + 0.4f * u;
        }
        if (g_focus >= 0 && g_focus < (int)rows.size()) {   /* keep the focused row in view: the only thing that moves the scroll */
            float rt = rows[g_focus].y, rb = rt + rows[g_focus].h;
            if (vis.controls[g_focus]->type == CT_PACKLIST) {   /* follow the row, not the whole list */
                float o = 0.2f * u + (g_packs.empty() ? 4.6f * u : 0.0f);
                for (int k = 0; k < g_packRow; k++) o += packRowH(k, u);
                rt = rows[g_focus].y + o;
                rb = rt + packRowH(g_packRow, u);
            }
            if (rt < g_scrollTarget) g_scrollTarget = rt;
            if (rb > g_scrollTarget + (bottom - top)) g_scrollTarget = rb - (bottom - top);
        }
        const float maxScroll = maxf(0.0f, cy - (bottom - top));
        ImGuiIO& io = ImGui::GetIO();
        if (g_scrollTarget > maxScroll) g_scrollTarget = maxScroll;
        if (g_scrollTarget < 0) g_scrollTarget = 0;
        g_scroll += (g_scrollTarget - g_scroll) * 0.35f;
        if ((g_scrollTarget - g_scroll) * (g_scrollTarget - g_scroll) < 0.25f) g_scroll = g_scrollTarget;

        ImGui::PushClipRect(ImVec2(fx, top), ImVec2(fx + fw, bottom), true);
        for (size_t i = 0; i < vis.controls.size(); i++) {
            const Control& c = *vis.controls[i];
            const int fi = (int)i;
            const bool dis = controlDisabled(c, g_model, g_state);
            const float y0 = top + rows[i].y - g_scroll, rowH = rows[i].h;
            if (y0 + rowH < top || y0 > bottom) continue;
            const std::string help = tr(helpFor(c, g_state.get(c.id)));
            const float baseH = rowH - rows[i].helpH;
            if (g_focus == fi && c.type != CT_PACKLIST) dl->AddRectFilled(ImVec2(fx, y0), ImVec2(fx + fw, y0 + rowH), kGoldSoft);
            const std::string lab = (c.type == CT_ACTION || c.type == CT_DIAGRAM || c.type == CT_PACKLIST) ? std::string() : tr(controlLabel(c, g_state));
            text(dl, g_head, 1.7f * u, ImVec2(fx + 1.2f * u, y0 + (4.6f * u - 1.7f * u) * 0.5f), dis ? kDisabled : kInk, lab.c_str());
            std::string tagText = c.tag.empty() ? std::string() : tr(c.tag);
            if (c.restart) tagText += (tagText.empty() ? "" : " ") + tr("(restart)");
            if (dis && !c.disabledTag.empty()) tagText += (tagText.empty() ? "" : " ") + tr(c.disabledTag);
            if (!tagText.empty())
                text(dl, g_body, 1.15f * u, ImVec2(fx + 1.2f * u + textSize(g_head, 1.7f * u, lab.c_str()).x + 0.7f * u, y0 + (4.6f * u - 1.15f * u) * 0.5f), kMut, tagText.c_str());
            const float cy2 = y0 + (4.6f * u - 3.1f * u) * 0.5f;
            if (c.type == CT_CHOICE) drawChoice(dl, c, fi, cx0, cy2, availW, u, dis);
            else if (c.type == CT_SELECT) drawSelect(dl, c, fi, cx0, cy2, u, dis);
            else if (c.type == CT_TOGGLE) drawToggle(dl, c, fi, cx0, y0 + (4.6f * u - 2.1f * u) * 0.5f, u, dis);
            else if (c.type == CT_SLIDER) drawSlider(dl, c, fi, cx0, y0 + (4.6f * u - 2.6f * u) * 0.5f, u, dis);
            else if (c.type == CT_BIND) drawBind(dl, c, fi, cx0, cy2, u, dis);
            else if (c.type == CT_PACKLIST) drawPackList(dl, c, fi, fx, y0, fw, u);
            else if (c.type == CT_DIAGRAM) drawDiagram(dl, c, fx + 1.0f * u, y0, fw - 2.0f * u, u);   /* the whole width of the form */
            else drawAction(dl, c, fi, cx0, cy2, u, dis);
            if (!help.empty()) text(dl, g_body, 1.4f * u, ImVec2(fx + 1.2f * u, y0 + baseH - 0.2f * u), kMut, help.c_str(), fw - 2.4f * u);
            dl->AddLine(ImVec2(fx, y0 + rowH + 0.2f * u), ImVec2(fx + fw, y0 + rowH + 0.2f * u), kLine, 1.0f);
        }
        ImGui::PopClipRect();
        if (maxScroll > 0.0f) {   /* a thin scrollbar */
            const float sh = (bottom - top) * (bottom - top) / (cy > 0 ? cy : 1), sy = top + (bottom - top - sh) * (g_scroll / maxScroll);
            dl->AddRectFilled(ImVec2(fx + fw + 0.6f * u, top), ImVec2(fx + fw + 0.9f * u, bottom), IM_COL32(58, 59, 51, 255));
            dl->AddRectFilled(ImVec2(fx + fw + 0.6f * u, sy), ImVec2(fx + fw + 0.9f * u, sy + sh), kGold);
        }
    }

    /* ---- footer ---- */
    {
        static const char* hints[4][2] = { { "A", "Select" }, { "<  >", "Change" }, { "L / R", "Tabs" }, { "B", "Back" } };
        drawFooter(dl, W, H, footH, u, hints, 4);
    }
    if (!g_selId.empty()) {   /* the open list, drawn last so it sits over everything */
        auto sit = g_model.byId.find(g_selId);
        if (sit == g_model.byId.end() || g_selW <= 0) {
            g_selId.clear();
        } else {
            const Control& c = *sit->second;
            const std::vector<int> offered = visibleOptions(c, g_model, g_state);
            const int n = (int)offered.size();
            const float ih = 3.1f * u;
            /* the list opens where there is more room, and shows as many rows as fit between the header and the footer */
            const float below = (H - footH) - (g_selAnchor.y + g_selH + 0.3f * u), above = g_selAnchor.y - hh - 0.3f * u;
            const bool up = (n * ih + 0.6f * u) > below && above > below;
            const float room = (up ? above : below) - 0.6f * u;
            int vis = n < 8 ? n : 8;
            const int fit = (int)(room / ih);
            if (vis > fit) vis = fit < 3 ? (n < 3 ? n : 3) : fit;
            const float ph = vis * ih + 0.6f * u;
            float py = up ? g_selAnchor.y - ph - 0.3f * u : g_selAnchor.y + g_selH + 0.3f * u;
            if (py < hh + 0.2f * u) py = hh + 0.2f * u;
            if (py + ph > H - footH - 0.2f * u) py = H - footH - 0.2f * u - ph;
            const ImVec2 a(g_selAnchor.x, py), b(g_selAnchor.x + g_selW, py + ph);
            dl->AddRectFilled(a, b, IM_COL32(13, 14, 12, 250));
            dl->AddRect(a, b, kGold, 0.0f, 0, 1.0f);
            int curPos = 0;   /* the cursor and the scroll count offered options, not list positions */
            for (int k = 0; k < n; k++) if (offered[k] == g_selCursor) curPos = k;
            if (curPos < g_selScroll) g_selScroll = curPos;
            if (curPos >= g_selScroll + vis) g_selScroll = curPos - vis + 1;
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel > 0 && g_selScroll > 0) g_selScroll--;
            if (wheel < 0 && g_selScroll < n - vis) g_selScroll++;
            const int curVal = (int)g_state.get(c.id);
            for (int k = 0; k < vis; k++) {
                const int i = offered[g_selScroll + k];
                const ImVec2 ia(a.x + 0.3f * u, a.y + 0.3f * u + k * ih), ib(b.x - 0.3f * u, ia.y + ih);
                char id[64];
                snprintf(id, sizeof id, "##opt_%d", i);
                ImGui::SetCursorScreenPos(ia);
                if (ImGui::InvisibleButton(id, ImVec2(ib.x - ia.x, ih))) { g_state.v[c.id] = i; g_selId.clear(); }
                if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0) g_selCursor = i;
                if (i == g_selCursor) dl->AddRectFilled(ia, ib, kGoldFill);
                text(dl, g_head, 1.55f * u, ImVec2(ia.x + 0.9f * u, ia.y + (ih - 1.55f * u) * 0.5f), i == curVal ? kGold : kInk, tr(c.options[i].label).c_str());
            }
            if (n > vis) {   /* a thin scrollbar on the list */
                const float sh = (ph - 0.6f * u) * vis / n, sy = a.y + 0.3f * u + (ph - 0.6f * u - sh) * (g_selScroll / (float)(n - vis));
                dl->AddRectFilled(ImVec2(b.x - 0.5f * u, sy), ImVec2(b.x - 0.25f * u, sy + sh), kGold);
            }
            const ImVec2 m = ImGui::GetIO().MousePos;
            if (ImGui::IsMouseClicked(0) && ImGui::GetFrameCount() != g_selOpenFrame && !(m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y))
                g_selId.clear();   /* a click outside closes it */
        }
    }
    ImGui::End();
}

bool takeCloseRequest() {
    const bool r = g_closeReq;
    g_closeReq = false;
    return r;
}

const char* debugState() {
    static char buf[192];
    if (g_model.tabs.empty()) { snprintf(buf, sizeof buf, "no content: %s", g_error.c_str()); return buf; }
    const Tab& tab = g_model.tabs[g_tab < (int)g_model.tabs.size() ? g_tab : 0];
    Vis vis = visibleOf(tab);
    if (vis.entries.empty()) { snprintf(buf, sizeof buf, "tab=%s (empty)", tab.name.c_str()); return buf; }
    const int ei = g_entry < (int)vis.entries.size() ? g_entry : 0;
    const Item& e = tab.items[vis.entries[ei]];
    std::vector<const Control*> cs;
    for (const Control& c : e.controls)
        if (controlVisible(c, g_model, g_state)) cs.push_back(&c);
    std::string fv = "-";
    if (g_focus >= 0 && g_focus < (int)cs.size()) {
        const Control& c = *cs[g_focus];
        fv = c.id + "=" + (hasOptions(c) ? c.options[(int)g_state.get(c.id)].id : std::to_string((int)g_state.get(c.id)));
        if (!g_selId.empty()) fv += " (list open, cursor " + std::to_string(g_selCursor) + ")";
    }
    snprintf(buf, sizeof buf, "tab=%s entry=%s controls=%d focus=%s close=%d", tab.name.c_str(), e.name.c_str(), (int)cs.size(), fv.c_str(), g_closeReq ? 1 : 0);
    return buf;
}

}  // namespace menu

namespace menu {

void drawWheel(float W, float H, int seat, float x0, float y0, float x1, float y1, const char* title, const std::vector<std::string>& names, int sel) {
    if (names.empty() || !g_head) return;
    const float pi = 3.14159265f;
    const float rw = (x1 - x0) * W, rh = (y1 - y0) * H, u = (rw < rh ? rw : rh) / 60.0f;   /* sized to the player's own picture */
    char wid[16];
    snprintf(wid, sizeof wid, "##wheel%d", seat);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin(wid, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(x0 * W + rw * 0.5f, y0 * H + rh * 0.5f);   /* the middle of that picture */
    const float R = 24.0f * u, r = 9.0f * u, mid = (R + r) * 0.5f, thick = R - r;
    const int n = (int)names.size();
    dl->AddCircleFilled(c, R + 1.2f * u, IM_COL32(13, 14, 12, 150), 64);
    if (n == 1) {   /* one item: a full ring, there is nothing to pick between */
        dl->AddCircle(c, mid, sel == 0 ? kGoldFill : IM_COL32(21, 22, 15, 235), 72, thick);
        dl->AddCircle(c, R, sel == 0 ? kGold : kRule, 72, 1.0f);
        dl->AddCircle(c, r, sel == 0 ? kGold : kRule, 72, 1.0f);
    }
    for (int i = 0; i < n; i++) {
        const float th = 2 * pi * i / n, half = pi / n - 0.02f;   /* from the top, clockwise */
        const bool on = i == sel;
        if (n > 1) {
            dl->PathArcTo(c, mid, th - half - pi * 0.5f, th + half - pi * 0.5f, 24);
            dl->PathStroke(on ? kGoldFill : IM_COL32(21, 22, 15, 235), 0, thick);
            dl->PathArcTo(c, R, th - half - pi * 0.5f, th + half - pi * 0.5f, 24);
            dl->PathStroke(on ? kGold : kRule, 0, 1.0f);
        }
        float s, co;
        sincosApprox(th, &s, &co);
        const std::string lab = tr(names[i]);
        const ImVec2 ts = textSize(g_head, 1.6f * u, lab.c_str());
        text(dl, g_head, 1.6f * u, ImVec2(c.x + mid * s - ts.x * 0.5f, c.y - mid * co - ts.y * 0.5f), on ? kInk : kMut, lab.c_str());
    }
    const std::string t = upper(tr(title));
    const ImVec2 tz = textSize(g_head, 1.9f * u, t.c_str());
    text(dl, g_head, 1.9f * u, ImVec2(c.x - tz.x * 0.5f, c.y - tz.y * 0.5f), kGold, t.c_str());
    ImGui::End();
}

void drawAssign(float W, float H, const std::vector<std::string>& names, int step, int count) {
    if (!g_head) return;
    const float u = W / 100.0f;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##assign", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(13, 14, 12, 235));
    const float pw = 52.0f * u, rowH = 5.4f * u, gap = 1.0f * u;
    const float ph = 9.0f * u + count * (rowH + gap), x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
    box(dl, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(21, 22, 15, 255), kGold);
    const std::string head = upper(tr("Who plays with what"));
    text(dl, g_head, 2.4f * u, ImVec2(x0 + 2.0f * u, y0 + 1.6f * u), kInk, head.c_str());
    for (int i = 0; i < count; i++) {
        const ImVec2 a(x0 + 2.0f * u, y0 + 6.0f * u + i * (rowH + gap)), b(x0 + pw - 2.0f * u, a.y + rowH);
        const bool now = i == step, done = i < step;
        box(dl, a, b, now ? kGoldFill : IM_COL32(13, 14, 12, 255), now ? kGold : kRule);
        const std::string who = upper(tr("Player")) + " " + std::to_string(i + 1);
        text(dl, g_head, 1.9f * u, ImVec2(a.x + 1.4f * u, a.y + (rowH - 1.9f * u) * 0.5f), kInk, who.c_str());
        const std::string what = done && i < (int)names.size() ? names[i] : (now ? tr("Press a button") : std::string("-"));
        const ImVec2 ws = textSize(g_body, 1.7f * u, what.c_str());
        text(dl, g_body, 1.7f * u, ImVec2(b.x - 1.4f * u - ws.x, a.y + (rowH - ws.y) * 0.5f), done ? kGold : kMut, what.c_str());
    }
    const std::string hint = tr(step == 0 ? "A button takes that controller, any key takes the keyboard and mouse. Esc skips." : "Press a button on the next controller. Esc skips.");
    text(dl, g_body, 1.4f * u, ImVec2(x0 + 2.0f * u, y0 + ph - 2.6f * u), kMut, hint.c_str());
    ImGui::End();
}

void drawCredits(float W, float H, const CreditsView& v) {
    if (!g_head || !g_body) return;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##credits", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (v.backdrop > 0.0f) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(255.0f * v.backdrop)));
    const float L = H / v.view;             /* one line, in pixels */
    const float margin = 0.12f * W, gutter = 0.8f * L;
    auto withAlpha = [](ImU32 c, float a) {
        const float base = (float)(c >> IM_COL32_A_SHIFT) * a;
        return (c & ~IM_COL32_A_MASK) | ((ImU32)(base < 0.0f ? 0.0f : base > 255.0f ? 255.0f : base) << IM_COL32_A_SHIFT);
    };
    for (const CreditsView::Line& ln : v.lines) {
        const float top = ln.y * L;
        if (ln.pair) {
            const float px = 0.62f * L;
            if (!ln.left.empty()) {
                const ImVec2 sz = textSize(g_body, px, ln.left.c_str());
                text(dl, g_body, px, ImVec2(W * 0.5f - gutter - sz.x, top + (L - sz.y) * 0.5f), withAlpha(kMut, ln.alpha), ln.left.c_str());
            }
            if (!ln.right.empty()) {
                const ImVec2 sz = textSize(g_body, px, ln.right.c_str());
                text(dl, g_body, px, ImVec2(W * 0.5f + gutter, top + (L - sz.y) * 0.5f), withAlpha(kInk, ln.alpha), ln.right.c_str());
            }
            continue;
        }
        const bool title = ln.style == 2, heading = ln.style == 1;
        const float px = (title ? 1.5f : heading ? 0.8f : 0.62f) * L;
        ImFont* font = title || heading ? g_head : g_body;
        const std::string s = title || heading ? upper(ln.left) : ln.left;
        const ImVec2 sz = textSize(font, px, s.c_str());
        const float x = ln.align == 1 ? margin : ln.align == 2 ? W - margin - sz.x : (W - sz.x) * 0.5f;
        text(dl, font, px, ImVec2(x, top + (ln.h * L - sz.y) * 0.5f), withAlpha(title ? kInk : heading ? kGold : kInk, ln.alpha), s.c_str());
    }
    ImGui::End();
}

void drawCoopCount(float W, float H, int sel, int* hover, bool* click) {
    *hover = -1;
    *click = false;
    if (!g_head) return;
    const float u = W / 100.0f;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##coopcount", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(13, 14, 12, 235));
    const int count = 3;
    const float pw = 36.0f * u, rowH = 5.4f * u, gap = 1.0f * u;
    const float ph = 9.0f * u + count * (rowH + gap), x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
    box(dl, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(21, 22, 15, 255), kGold);
    const std::string head = upper(tr("Co-op"));
    text(dl, g_head, 2.4f * u, ImVec2(x0 + 2.0f * u, y0 + 1.6f * u), kInk, head.c_str());
    for (int i = 0; i < count; i++) {
        const ImVec2 a(x0 + 2.0f * u, y0 + 6.0f * u + i * (rowH + gap)), b(x0 + pw - 2.0f * u, a.y + rowH);
        const ImGuiIO& io = ImGui::GetIO();
        const bool over = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;
        if (over && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) *hover = i;   /* a resting pointer leaves the pad's choice alone */
        if (over && ImGui::IsMouseClicked(0)) { *hover = i; *click = true; }
        const bool on = i == sel;
        box(dl, a, b, on ? kGoldFill : IM_COL32(13, 14, 12, 255), on ? kGold : kRule);
        const std::string lab = std::to_string(i + 2) + " " + upper(tr("Players"));
        text(dl, g_head, 1.9f * u, ImVec2(a.x + 1.4f * u, a.y + (rowH - 1.9f * u) * 0.5f), on ? kInk : kMut, lab.c_str());
    }
    const std::string hint = tr("A starts, B goes back.");
    text(dl, g_body, 1.4f * u, ImVec2(x0 + 2.0f * u, y0 + ph - 2.6f * u), kMut, hint.c_str());
    ImGui::End();
}

/* ---- the Mods screen ---- */
namespace {
float g_thumbUv[256][4];
bool g_thumbOk[256];
int g_modsFirst = 0;   /* the first card shown in the list */

void modsPicture(ImDrawList* dl, int id, ImVec2 a, ImVec2 b, ImU32 tint, const char* alt, float u) {
    if (id >= 0 && id < 256 && g_thumbOk[id]) {
        dl->AddImage(ImGui::GetIO().Fonts->TexRef, a, b, ImVec2(g_thumbUv[id][0], g_thumbUv[id][1]), ImVec2(g_thumbUv[id][2], g_thumbUv[id][3]), tint);
        dl->AddRect(a, b, kRule, 0.0f, 0, 1.0f);
    } else {
        box(dl, a, b, IM_COL32(24, 25, 20, 255), kRule);
        textCenter(dl, g_head, 1.7f * u, a, b, kGoldDim, alt);
    }
}
}  // namespace

void setThumbImage(int id, float u0, float v0, float u1, float v1) {
    if (id < 0) { for (bool& b : g_thumbOk) b = false; return; }
    if (id >= 256) return;
    g_thumbUv[id][0] = u0; g_thumbUv[id][1] = v0; g_thumbUv[id][2] = u1; g_thumbUv[id][3] = v1;
    g_thumbOk[id] = true;
}

void drawModsScreen(float W, float H, const ModsView& v, int* hover, bool* click) {
    *hover = -1;
    *click = false;
    if (!g_head || v.entries.empty()) return;
    const float u = minf(W / 100.0f, H / 56.25f), ox = (W - 100.0f * u) * 0.5f;
    const ImGuiIO& io = ImGui::GetIO();
    const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    auto over = [&](ImVec2 a, ImVec2 b) { return io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y; };
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##mods", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(40, 43, 34, 252), IM_COL32(26, 28, 22, 252),
                                IM_COL32(16, 17, 14, 252), IM_COL32(26, 28, 22, 252));
    const float hh = 4.6f * u, footH = 4.0f * u;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, hh), IM_COL32(13, 14, 12, 208));
    dl->AddLine(ImVec2(0, hh), ImVec2(W, hh), kGoldDim, 1.0f);
    text(dl, g_head, 2.4f * u, ImVec2(ox + 2.4f * u, (hh - 2.4f * u) * 0.5f), kInk, upper(tr("Mods")).c_str());
    if (g_ownerSeat >= 0) {   /* whose pad drives it */
        const std::string who = upper(tr("Player")) + " " + std::to_string(g_ownerSeat + 1);
        const float bw = textSize(g_head, 1.5f * u, who.c_str()).x + 2.4f * u, bx = ox + 97.6f * u - bw;
        box(dl, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kGoldFill, kGold);
        textCenter(dl, g_head, 1.5f * u, ImVec2(bx, (hh - 2.6f * u) * 0.5f), ImVec2(bx + bw, (hh + 2.6f * u) * 0.5f), kInk, who.c_str());
    }
    const float top = hh + 2.0f * u, bottom = H - footH - 1.6f * u;
    const int n = (int)v.entries.size();
    const int sel = v.sel < 0 ? 0 : (v.sel >= n ? n - 1 : v.sel);

    /* ---- the list of cards ---- */
    {
        const float lx = ox + 2.4f * u, lw = 33.0f * u, ch = 7.2f * u, gap = 0.8f * u;
        int visible = (int)((bottom - top + gap) / (ch + gap));
        if (visible < 1) visible = 1;
        if (sel < g_modsFirst) g_modsFirst = sel;
        if (sel >= g_modsFirst + visible) g_modsFirst = sel - visible + 1;
        if (g_modsFirst + visible > n) g_modsFirst = n - visible;
        if (g_modsFirst < 0) g_modsFirst = 0;
        for (int k = 0; k < visible && g_modsFirst + k < n; k++) {
            const int i = g_modsFirst + k;
            const ModsView::Entry& e = v.entries[i];
            const ImVec2 a(lx, top + k * (ch + gap)), b(lx + lw, a.y + ch);
            if (over(a, b) && moved) *hover = i;
            if (over(a, b) && ImGui::IsMouseClicked(0)) { *hover = i; *click = true; }
            const bool on = i == sel;
            box(dl, a, b, on ? (v.focus ? kGoldSoft : kGoldFill) : kRow, on ? kGold : kRule);
            const float th = ch - 1.2f * u, tw = th * 4.0f / 3.0f;
            modsPicture(dl, e.thumb, ImVec2(a.x + 0.6f * u, a.y + 0.6f * u), ImVec2(a.x + 0.6f * u + tw, a.y + 0.6f * u + th), IM_COL32(255, 255, 255, 255), i == 0 ? "007" : "MOD", u);
            const float tx = a.x + 0.6f * u + tw + 1.0f * u;
            dl->PushClipRect(a, b, true);
            text(dl, g_head, 1.9f * u, ImVec2(tx, a.y + 0.7f * u), kInk, upper(e.name).c_str());
            std::string sub;
            if (i == 0) sub = tr("The original game, no mod");
            else {
                sub = e.author;
                if (!e.progress.empty()) sub += std::string(sub.empty() ? "" : "  \xC2\xB7  ") + tr(e.progress);
            }
            text(dl, g_body, 1.45f * u, ImVec2(tx, a.y + 3.0f * u), kMut, sub.c_str());
            float cx = tx;
            const char* chip[2] = { "Story", "Multiplayer" };
            for (int c = 0; c < 2; c++) {
                if (!(e.affects & (1 << c)) || i == 0) continue;
                const std::string t = upper(tr(chip[c]));
                const float cw = textSize(g_head, 1.15f * u, t.c_str()).x + 1.2f * u;
                box(dl, ImVec2(cx, a.y + 4.9f * u), ImVec2(cx + cw, a.y + 6.5f * u), kGoldSoft, kGoldDim);
                textCenter(dl, g_head, 1.15f * u, ImVec2(cx, a.y + 4.9f * u), ImVec2(cx + cw, a.y + 6.5f * u), kGold, t.c_str());
                cx += cw + 0.6f * u;
            }
            if (i == v.chosen) {
                const std::string t = upper(tr("In use"));
                const ImVec2 ts = textSize(g_head, 1.1f * u, t.c_str());
                text(dl, g_head, 1.1f * u, ImVec2(b.x - 0.8f * u - ts.x, a.y + 0.8f * u), kGold, t.c_str());
            }
            dl->PopClipRect();
        }
        if (n > visible) {   /* more cards than fit: a thin scroll bar */
            const float bx = lx + lw + 0.6f * u, bh = bottom - top;
            dl->AddRectFilled(ImVec2(bx, top), ImVec2(bx + 0.3f * u, top + bh), kLine);
            dl->AddRectFilled(ImVec2(bx, top + bh * g_modsFirst / n), ImVec2(bx + 0.3f * u, top + bh * (g_modsFirst + visible) / n), kGoldDim);
        }
    }

    /* ---- the highlighted mod ---- */
    {
        const ModsView::Entry& e = v.entries[sel];
        const float px = ox + 38.0f * u, pw = 59.6f * u, pad = 2.0f * u;
        const ImVec2 pa(px, top), pb(px + pw, bottom);
        box(dl, pa, pb, IM_COL32(21, 22, 15, 255), kRule);
        float y = top + 1.8f * u;
        dl->PushClipRect(pa, pb, true);
        text(dl, g_head, 3.0f * u, ImVec2(px + pad, y), kInk, upper(e.name).c_str());
        y += 4.0f * u;
        if (!e.author.empty()) {
            text(dl, g_body, 1.7f * u, ImVec2(px + pad, y), kMut, (tr("by") + " " + e.author).c_str());
            y += 2.6f * u;
        }
        if (!e.desc.empty()) {
            const float dh = minf(textSizeWrapped(g_body, 1.55f * u, pw - 2 * pad, e.desc.c_str()).y, 6.4f * u);
            dl->PushClipRect(ImVec2(px, y), ImVec2(px + pw, y + dh + 0.2f * u), true);
            text(dl, g_body, 1.55f * u, ImVec2(px + pad, y), kInk, e.desc.c_str(), pw - 2 * pad);
            dl->PopClipRect();
            y += dh + 0.8f * u;
        }
        const float btnH = 3.4f * u, btnY = bottom - 1.6f * u - btnH;
        if (!e.missions.empty()) {
            dl->AddLine(ImVec2(px + pad, y + 0.4f * u), ImVec2(px + pw - pad, y + 0.4f * u), kLine, 1.0f);
            y += 1.4f * u;
            text(dl, g_head, 1.7f * u, ImVec2(px + pad, y), kGold, upper(tr("Missions")).c_str());
            y += 2.8f * u;
            const float tgap = 0.9f * u, tileW = (pw - 2 * pad - tgap) * 0.5f, tileH = 5.6f * u, limit = btnY - 1.0f * u;
            int rows = (int)((limit - y + tgap) / (tileH + tgap));
            if (rows < 1) rows = 1;
            const int m = (int)e.missions.size(), shown = rows * 2 < m ? rows * 2 : m;
            for (int k = 0; k < shown; k++) {
                const ModsView::Mission& ms = e.missions[k];
                const ImVec2 a(px + pad + (k % 2) * (tileW + tgap), y + (k / 2) * (tileH + tgap)), b(a.x + tileW, a.y + tileH);
                const bool lock = ms.state == 0;
                box(dl, a, b, kRow, kLine);
                const float th = tileH - 0.8f * u, tw = th * 4.0f / 3.0f;
                modsPicture(dl, ms.thumb, ImVec2(a.x + 0.4f * u, a.y + 0.4f * u), ImVec2(a.x + 0.4f * u + tw, a.y + 0.4f * u + th),
                            lock ? IM_COL32(48, 48, 46, 255) : IM_COL32(255, 255, 255, 255), std::to_string(k + 1).c_str(), u);
                const float tx = a.x + 0.4f * u + tw + 0.9f * u;
                dl->PushClipRect(a, b, true);
                text(dl, g_head, 1.55f * u, ImVec2(tx, a.y + 0.8f * u), lock ? kDisabled : kInk, (std::to_string(k + 1) + ". " + ms.name).c_str());
                const std::string st = ms.state == 2 ? tr("Completed") + (ms.done.empty() ? std::string() : "  \xC2\xB7  " + ms.done)
                                                     : tr(ms.state == 1 ? "Available" : "Locked");
                text(dl, g_body, 1.3f * u, ImVec2(tx, a.y + 3.1f * u), lock ? kDisabled : (ms.state == 2 ? kGold : kMut), st.c_str());
                dl->PopClipRect();
            }
            if (shown < m)
                text(dl, g_body, 1.3f * u, ImVec2(px + pad, limit - 0.2f * u), kMut, ("+ " + std::to_string(m - shown) + " " + tr("more")).c_str());
        }
        /* the buttons: Back, Play this mod */
        const std::string lab[2] = { upper(tr("Back")), upper(tr(sel == 0 ? "Play GoldenEye" : "Play this mod")) };
        const float bw[2] = { 9.0f * u, maxf(16.0f * u, textSize(g_head, 1.8f * u, lab[1].c_str()).x + 3.0f * u) };
        const float bx[2] = { px + pw - pad - bw[1] - 1.0f * u - bw[0], px + pw - pad - bw[1] };
        for (int bi = 0; bi < 2; bi++) {
            const ImVec2 a(bx[bi], btnY), b(bx[bi] + bw[bi], btnY + btnH);
            const int id = bi ? 1001 : 1000;
            if (over(a, b) && moved) *hover = id;
            if (over(a, b) && ImGui::IsMouseClicked(0)) { *hover = id; *click = true; }
            const bool f = v.focus == 1 && v.btn == bi;
            box(dl, a, b, f ? kGoldFill : IM_COL32(21, 22, 15, 255), f ? kGold : kRule);
            if (f) focusFrame(dl, a, b, u);
            textCenter(dl, g_head, 1.8f * u, a, b, kInk, lab[bi].c_str());
        }
        dl->PopClipRect();
    }
    if (v.focus == 0) {
        static const char* hints[3][2] = { { "Up / Down", "Choose a mod" }, { "A", "Open" }, { "B", "Back" } };
        drawFooter(dl, W, H, footH, u, hints, 3);
    } else {
        static const char* hints[3][2] = { { "<  >", "Choose" }, { "A", "Confirm" }, { "B", "Back" } };
        drawFooter(dl, W, H, footH, u, hints, 3);
    }
    ImGui::End();
}

}  // namespace menu
