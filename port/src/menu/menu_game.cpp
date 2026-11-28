/* menu_game.cpp -- the new menu inside the game: the glue between menu_ui.cpp (Dear ImGui only) and the port.
 *
 * Each frame, while the options overlay is open and GE_NEWMENU=1, it feeds the game's input to ImGui, runs
 * menu::draw and hands the triangles to fast3d (port/fast3d/gfx_ui.h), which draws them through the rendering
 * API (OpenGL, Vulkan, deko3d). The old overlay keeps its logic; only its drawing and navigation are replaced.
 *
 * Buttons are used by label: A confirms and B cancels on every pad. ImGui's "face down" (activate) is fed from
 * the pad's A and "face right" (cancel) from its B, so the confirm button is the bottom one on an Xbox pad and
 * the right one on a Switch pad, as on each console. */
#include <PR/gbi.h>
#include <SDL.h>
#include "sdl_compat.h"   /* SDL_GetTicks64 on SDL < 2.0.18 */
#include "../../third_party/imgui/imgui.h"
#include "../../fast3d/gfx_api.h"
#include "../../fast3d/gfx_ui.h"
#include "menu_ui.h"
#include "menu_model.h"
#include "menu_fonts.h"
#include "menu_data.h"
#include "menu_pad.h"
#include "console.h"
#include "menu_icons.h"
#include "../../mission/mission_host_credits.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
int inputPadButton(int idx, SDL_GameControllerButton b);
short inputPadAxis(int idx, SDL_GameControllerAxis a);
int inputOverlayOwnerPad(void);
int inputOverlayOwnerSeat(void);
int inputPadFamily(int seat);
int portDevMode(void);
const char* portMenuLanguage(void);
const char* sysResolvePath(const char* path);
int portWheelState(int seat, char names[][24], int max, int* sel, int* kind);
void portSeatRegion(int seat, float* x0, float* y0, float* x1, float* y1);
int portAssignState(char names[][48], int* step, int* count);
void optionsMenuAssetPackDiscard(void);
void optionsMenuSetSeat(int seat);
int optionsMenuFileSelect(void);
int optionsMenuInStage(void);
int optionsMenuUpdateAvailable(void);
int optionsMenuUpdateState(void);
const char* optionsMenuUpdateTag(void);
const char* optionsMenuUpdateNotes(void);
const char* optionsMenuVersion(void);
int optionsMenuUpdateInstallable(void);
int optionsMenuUpdateBusy(void);
const char* optionsMenuUpdateStatus(void);
int optionsMenuWheel(void);
float portFovHorizDegrees(int pct);
void portCrosshairTint(int* r, int* g, int* b);
int portCrosshairPicture(int style, const unsigned char** px);
int inputCrosshairCursorOn(void);
int optionsMenuProfileAvailable(void);
/* port/src/missions_screen.c: the Mods screen (the mode select's MODS row) */
int modsScreenActive(void);
void modsScreenPointer(int hover, int click);
int modsScreenCount(void);
int modsScreenSel(void);
int modsScreenFocus(void);
int modsScreenButton(void);
int modsScreenChosen(void);
const char* modsScreenName(int e);
const char* modsScreenAuthor(int e);
const char* modsScreenDescription(int e);
const char* modsScreenProgress(int e);
int modsScreenAffects(int e);
int modsScreenMissions(int e);
const char* modsScreenMissionName(int e, int m);
int modsScreenMissionState(int e, int m);
const char* modsScreenMissionDone(int e, int m);
int modsScreenThumbId(int e, int m);
int modsScreenThumbCount(void);
int modsScreenThumbRev(void);
const unsigned char* modsScreenThumbPixels(int id, int* w, int* h);
}

namespace {

SDL_atomic_t s_back;   // Esc pressed while the menu is open
ImGuiContext* s_ctx = nullptr;
std::vector<GfxUiVertex> s_vtx;
std::vector<uint32_t> s_idx;
std::vector<GfxUiCmd> s_cmd;
Uint64 s_lastTicks = 0;

extern "C" void menuBindGame(void);

/* The texture: the fonts at the sizes the menu draws (menu::fontsBuild) plus the gamepad drawing. Rebuilt before a
 * frame when the menu asks for a size it hasn't baked, or the window changed size. */
static int s_thumbRev = -1;   /* the Mods screen's picture set the atlas was built with */

static void buildAtlas() {
    ImGuiIO& io = ImGui::GetIO();
    menu::fontsBuild();
    s_thumbRev = modsScreenThumbRev();
    const int nThumb = modsScreenThumbCount() < 256 ? modsScreenThumbCount() : 256;   /* the mods' pictures, likewise */
    std::vector<int> thumbRect;
    for (int k = 0; k < nThumb; k++) {
        int tw = 0, th = 0;
        thumbRect.push_back(modsScreenThumbPixels(k, &tw, &th) ? io.Fonts->AddCustomRect(tw, th) : -1);
    }
    const int padRect = io.Fonts->AddCustomRect(kMenuPadW, kMenuPadH);   /* the gamepad drawing, in the same texture */
    int iconRect[kMenuIconCount];   /* the PlayStation face-button icons, likewise */
    for (int k = 0; k < kMenuIconCount; k++) iconRect[k] = io.Fonts->AddCustomRect(kMenuIconSize, kMenuIconSize);
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    if (ImFontAtlasRect rr; io.Fonts->GetCustomRect(padRect, &rr)) {
        const ImFontAtlasRect* r = &rr;
        for (int y = 0; y < kMenuPadH; y++)
            for (int x = 0; x < kMenuPadW; x++) {
                unsigned char* d = px + ((size_t)(r->y + y) * (size_t)w + (size_t)(r->x + x)) * 4;
                d[0] = d[1] = d[2] = 255;
                d[3] = kMenuPad[y * kMenuPadW + x];
            }
        menu::setPadImage((float)r->x / (float)w, (float)r->y / (float)h, (float)(r->x + kMenuPadW) / (float)w,
                          (float)(r->y + kMenuPadH) / (float)h, (float)kMenuPadW / (float)kMenuPadH);
    }
    for (int k = 0; k < kMenuIconCount; k++) {
        ImFontAtlasRect rr;
        if (!io.Fonts->GetCustomRect(iconRect[k], &rr)) continue;
        const ImFontAtlasRect* r = &rr;
        for (int y = 0; y < kMenuIconSize; y++)
            for (int x = 0; x < kMenuIconSize; x++) {
                unsigned char* d = px + ((size_t)(r->y + y) * (size_t)w + (size_t)(r->x + x)) * 4;
                d[0] = d[1] = d[2] = 255;
                d[3] = kMenuIcons[k][y * kMenuIconSize + x];
            }
        menu::setIconImage(k, (float)r->x / (float)w, (float)r->y / (float)h, (float)(r->x + kMenuIconSize) / (float)w,
                           (float)(r->y + kMenuIconSize) / (float)h);
    }
    menu::setThumbImage(-1, 0, 0, 0, 0);
    for (int k = 0; k < nThumb; k++) {
        int tw = 0, th = 0;
        const unsigned char* src = thumbRect[k] >= 0 ? modsScreenThumbPixels(k, &tw, &th) : nullptr;
        ImFontAtlasRect rr;
        if (!src || !io.Fonts->GetCustomRect(thumbRect[k], &rr)) continue;
        const ImFontAtlasRect* r = &rr;
        for (int y = 0; y < th; y++)
            memcpy(px + ((size_t)(r->y + y) * (size_t)w + (size_t)r->x) * 4, src + (size_t)y * (size_t)tw * 4, (size_t)tw * 4);
        menu::setThumbImage(k, (float)r->x / (float)w, (float)r->y / (float)h, (float)(r->x + tw) / (float)w, (float)(r->y + th) / (float)h);
    }
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    gfx_ui_set_atlas(px, w, h);
}

void init() {
    IMGUI_CHECKVERSION();
    s_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.MouseDrawCursor = false;   /* the pointer is drawn by menu::drawPointer, only when Input.CrosshairCursor is on */
    menu::init(kMenuBodyTtf, kMenuBodyTtfLen, kMenuHeadTtf, kMenuHeadTtfLen);
#ifdef __SWITCH__
    menu::setPlatform(false);
#else
    menu::setPlatform(true);
#endif
    menu::setConsole(portIsConsole() != 0);   /* the Switch, or GE_CONSOLE=1 / System.Console on a handheld */
#ifndef __SWITCH__
    menu::setHandheld(portIsHandheld() != 0);   /* GE_HANDHELD=1 / System.Handheld; the Switch's handheld state is live, not a menu flag */
#endif
    menu::setDev(portDevMode() != 0);   /* Dev.Mode in the ini */
    {
        std::string err;
        if (!menu::setData(kMenuJson, kMenuJsonLen, &err)) fputs(("menu: " + err + "\n").c_str(), stderr);
    }
    menuBindGame();
    if (const char* lang = portMenuLanguage(); lang && lang[0]) {   /* Menu.Language: a flat table, English text -> translated text */
        const std::string path = std::string("lang/") + lang + ".json";
        if (FILE* f = fopen(sysResolvePath(path.c_str()), "rb")) {
            std::string text;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);
            std::string err;
            if (!menu::loadTranslations(text.c_str(), text.size(), &err)) fputs(("menu: " + path + ": " + err + "\n").c_str(), stderr);
        }
    }
    if (const char* g = getenv("GE_MENUGOTO")) {   /* tests: "tab,entry,focus" */
        int t = 0, e = 0, f = -1;
        if (sscanf(g, "%d,%d,%d", &t, &e, &f) >= 2) menu::debugGoto(t, e, f);
    }
    buildAtlas();
}

void feedMouse(ImGuiIO& io, float W, float H);

void feedInput(ImGuiIO& io, float W, float H) {
    const int pad = inputOverlayOwnerPad();
    menu::setPadFamily(inputPadFamily(pad));   /* the Switch is Nintendo; elsewhere SDL's pad type, or GE_PADFAMILY on a handheld */
    auto btn = [&](SDL_GameControllerButton b) { return inputPadButton(pad, b) != 0; };
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, btn(SDL_CONTROLLER_BUTTON_DPAD_UP));
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, btn(SDL_CONTROLLER_BUTTON_A));    /* confirm: the A label */
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, btn(SDL_CONTROLLER_BUTTON_B));   /* cancel: the B label */
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, btn(SDL_CONTROLLER_BUTTON_X));
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp, btn(SDL_CONTROLLER_BUTTON_Y));
    io.AddKeyEvent(ImGuiKey_GamepadL1, btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    io.AddKeyEvent(ImGuiKey_GamepadR1, btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    const float ax = inputPadAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    const float ay = inputPadAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    /* Past sqrt(1/2) the two axes can't both fire on a round gate: moving up or down a row never also steps the
     * slider / choice it passes (at 0.3 an angled push did both). 0.75 keeps a margin and is still reached by a
     * worn stick. */
    const float kNav = 0.75f;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, ax < -kNav, ax < -kNav ? -ax : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, ax > kNav, ax > kNav ? ax : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ay < -kNav, ay < -kNav ? -ay : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ay > kNav, ay > kNav ? ay : 0.0f);

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    io.AddKeyEvent(ImGuiKey_UpArrow, ks[SDL_SCANCODE_UP] != 0);
    io.AddKeyEvent(ImGuiKey_DownArrow, ks[SDL_SCANCODE_DOWN] != 0);
    io.AddKeyEvent(ImGuiKey_LeftArrow, ks[SDL_SCANCODE_LEFT] != 0);
    io.AddKeyEvent(ImGuiKey_RightArrow, ks[SDL_SCANCODE_RIGHT] != 0);
    io.AddKeyEvent(ImGuiKey_Enter, ks[SDL_SCANCODE_RETURN] != 0 || ks[SDL_SCANCODE_KP_ENTER] != 0);
    {   /* Backspace and Esc go back: Esc arrives as an event (menuPostBack), held for two frames so the menu sees a press */
        static int escPulse = 0;
        if (SDL_AtomicSet(&s_back, 0)) escPulse = 2;
        io.AddKeyEvent(ImGuiKey_Backspace, ks[SDL_SCANCODE_BACKSPACE] != 0 || escPulse > 0);
        if (escPulse > 0) escPulse--;
    }
    io.AddKeyEvent(ImGuiKey_PageUp, ks[SDL_SCANCODE_PAGEUP] != 0);
    io.AddKeyEvent(ImGuiKey_PageDown, ks[SDL_SCANCODE_PAGEDOWN] != 0);

    feedMouse(io, W, H);
}

void feedMouse(ImGuiIO& io, float W, float H) {
    int mx = 0, my = 0;
    const Uint32 mb = SDL_GetMouseState(&mx, &my);
    SDL_Window* win = SDL_GetMouseFocus();
    int ww = 0, wh = 0;
    if (win) {
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww > 0 && wh > 0) io.AddMousePosEvent((float)mx * W / (float)ww, (float)my * H / (float)wh);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    io.AddMouseButtonEvent(0, (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);
}

}  // namespace

extern "C" void menuPostBack(void) { SDL_AtomicSet(&s_back, 1); }

extern "C" int menuNewActive(void) {   /* the menu is this one: the C-era overlay no longer draws or reads input */
    return 1;
}

extern "C" int coopModalActive(void);
extern "C" int coopModalSel(void);
extern "C" void coopModalPointer(int row, int click);

/* The Mods screen's contents, from the game's side (missions_screen.c), once per frame. */
static void modsView(menu::ModsView* v) {
    const int n = modsScreenCount();
    v->sel = modsScreenSel();
    v->focus = modsScreenFocus();
    v->btn = modsScreenButton();
    v->chosen = modsScreenChosen();
    for (int e = 0; e < n; e++) {
        menu::ModsView::Entry en;
        en.name = modsScreenName(e);
        en.author = modsScreenAuthor(e);
        en.desc = modsScreenDescription(e);
        en.progress = modsScreenProgress(e);
        en.affects = modsScreenAffects(e);
        en.thumb = modsScreenThumbId(e, -1);
        if (e == v->sel) {   /* the missions of the highlighted one only */
            for (int m = 0; m < modsScreenMissions(e); m++) {
                menu::ModsView::Mission ms;
                ms.name = modsScreenMissionName(e, m);
                ms.done = modsScreenMissionDone(e, m);
                ms.state = modsScreenMissionState(e, m);
                ms.thumb = modsScreenThumbId(e, m);
                en.missions.push_back(ms);
            }
        }
        v->entries.push_back(en);
    }
}

extern "C" int portCreditsView(const MissionCredits** credits, double* seconds);

/* The scripted credits (credits.show) on screen now: the lines in view, where they are and how opaque. */
static void creditsView(menu::CreditsView* v) {
    const MissionCredits* c = nullptr;
    double t = 0.0;
    if (!portCreditsView(&c, &t)) return;
    v->view = MHCR_VIEW;
    v->backdrop = mhcrBackdrop(c, t);
    for (int i = 0; i < c->n; i++) {
        const MhcrEntry* e = &c->e[i];
        if (e->kind == MHCR_E_GAP || !mhcrVisible(c, e, t)) continue;
        menu::CreditsView::Line ln;
        ln.left = e->left;
        ln.right = e->right;
        ln.pair = e->kind == MHCR_E_PAIR;
        ln.style = e->style;
        ln.align = e->align;
        ln.y = mhcrScreenY(c, e, t);
        ln.h = e->h;
        ln.alpha = mhcrAlpha(c, e, t);
        v->lines.push_back(ln);
    }
}

static void runFrame(int mode) {   /* 0 the menu, 1 the wheel, 2 the device assignment, 3 the co-op player count, 4 the Mods screen, 5 the scripted credits */
    const bool wheel = mode == 1;
    if (!s_ctx) init();
    const float W = (float)gfx_current_dimensions.width, H = (float)gfx_current_dimensions.height;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(W, H);
    const Uint64 now = SDL_GetTicks64();
    io.DeltaTime = s_lastTicks ? (float)(now - s_lastTicks) / 1000.0f : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f || io.DeltaTime > 0.25f) io.DeltaTime = 1.0f / 60.0f;
    s_lastTicks = now;
    if (mode == 3 || mode == 4) feedMouse(io, W, H);
    if (mode == 4) {
        menu::setOwner(0);   /* player 1 drives */
        menu::setPadFamily(inputPadFamily(0));
    }
    if (mode == 0) {
        menu::setOwner(inputOverlayOwnerSeat());
        optionsMenuSetSeat(inputOverlayOwnerSeat());
        menu::setContext(optionsMenuFileSelect() != 0, optionsMenuInStage() != 0, optionsMenuUpdateAvailable() != 0, optionsMenuProfileAvailable() != 0);
        menu::setUpdate(optionsMenuUpdateState(), optionsMenuUpdateTag(), optionsMenuUpdateNotes(), optionsMenuVersion(),
                        optionsMenuUpdateInstallable() != 0, optionsMenuUpdateBusy() != 0, optionsMenuUpdateStatus());
        feedInput(io, W, H);
        io.AddMouseWheelEvent(0.0f, (float)optionsMenuWheel());   /* the mouse wheel: the lists and the release text scroll */
    }
    if (menu::fontsFrame(W) || modsScreenThumbRev() != s_thumbRev) buildAtlas();   /* before NewFrame: the atlas is locked during a frame */
    ImGui::NewFrame();
    if (mode == 3) {   /* the mouse picks a row too: hovering moves the selection, a click starts */
        int row = -1;
        bool click = false;
        menu::drawCoopCount(W, H, coopModalSel(), &row, &click);
        coopModalPointer(row, click ? 1 : 0);
        menu::drawPointer(inputCrosshairCursorOn() != 0, W, H);
    } else if (mode == 4) {   /* the Mods screen: the pad is read by the game (missions_screen.c), the mouse is passed on */
        menu::ModsView view;
        modsView(&view);
        int hover = -1;
        bool click = false;
        menu::drawModsScreen(W, H, view, &hover, &click);
        modsScreenPointer(hover, click ? 1 : 0);
        menu::drawPointer(inputCrosshairCursorOn() != 0, W, H);
    } else if (mode == 5) {   /* the scripted credits, over the game */
        menu::CreditsView view;
        creditsView(&view);
        menu::drawCredits(W, H, view);
    } else if (mode == 2) {   /* who plays with which device */
        char an[4][48];
        int astep = 0, acount = 0;
        std::vector<std::string> list;
        if (portAssignState(an, &astep, &acount)) {
            for (int i = 0; i < 4; i++) list.push_back(an[i]);
            menu::drawAssign(W, H, list, astep, acount);
        }
    } else if (mode == 0) {
        menu::draw(W, H);
        menu::drawPointer(inputCrosshairCursorOn() != 0, W, H);
    } else {   /* the Modern weapon / gadget wheel, over the running game */
        for (int seat = 0; seat < 4; seat++) {   /* every player with a wheel open, in that player's own picture */
            char names[16][24];
            int sel = -1, kind = 0;
            const int n = portWheelState(seat, names, 16, &sel, &kind);
            if (n <= 0) continue;
            float x0, y0, x1, y1;
            portSeatRegion(seat, &x0, &y0, &x1, &y1);
            std::vector<std::string> list;
            for (int i = 0; i < n; i++) list.push_back(names[i]);
            menu::drawWheel(W, H, seat, x0, y0, x1, y1, kind == 2 ? "Gadgets" : "Weapons", list, sel);
        }
    }
    ImGui::Render();

    s_vtx.clear();
    s_idx.clear();
    s_cmd.clear();
    ImDrawData* dd = ImGui::GetDrawData();
    for (int n = 0; dd && n < dd->CmdListsCount; n++) {
        const ImDrawList* cl = dd->CmdLists[n];
        const uint32_t vbase = (uint32_t)s_vtx.size(), ibase = (uint32_t)s_idx.size();
        for (int i = 0; i < cl->VtxBuffer.Size; i++) {
            const ImDrawVert& v = cl->VtxBuffer[i];
            s_vtx.push_back({ v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col });
        }
        for (int i = 0; i < cl->IdxBuffer.Size; i++) s_idx.push_back(vbase + cl->IdxBuffer[i]);
        for (int c = 0; c < cl->CmdBuffer.Size; c++) {
            const ImDrawCmd* cmd = &cl->CmdBuffer[c];
            if (cmd->UserCallback) continue;
            s_cmd.push_back({ cmd->ClipRect.x, cmd->ClipRect.y, cmd->ClipRect.z, cmd->ClipRect.w, ibase + cmd->IdxOffset,
                              cmd->ElemCount });
        }
    }
    gfx_ui_submit(s_vtx.data(), (uint32_t)s_vtx.size(), s_idx.data(), (uint32_t)s_idx.size(), s_cmd.data(),
                  (uint32_t)s_cmd.size());
}

extern "C" void menuUiFrame(void) { runFrame(0); }
extern "C" void menuWheelFrame(void) { runFrame(1); }
extern "C" void menuAssignFrame(void) { runFrame(2); }
extern "C" void menuCoopCountFrame(void) { runFrame(3); }

extern "C" {
int portDevMode(void);
int optionsMenuGet(const char* key, double* out);
void optionsMenuSet(const char* key, double value);
int optionsMenuResCount(void);
int optionsMenuAudioCount(void);
const char* optionsMenuAudioLabel(int i);
int optionsMenuAssetPackRows(char (*names)[64], char (*authors)[64], int* on, int max);
int optionsMenuAssetPackAct(int row, int what);
void optionsMenuSetSeat(int seat);
void optionsMenuResLabel(int i, char* out, int n);
const char* optionsMenuBindText(const char* key, int slot);
void optionsMenuBindBegin(const char* key, int slot);
int optionsMenuBindActive(void);
void optionsMenuAction(const char* key);
}

static bool bindGet(const char* key, double* out) { return optionsMenuGet(key, out) != 0; }
static void bindSet(const char* key, double value) { optionsMenuSet(key, value); }
static std::string keyText(const char* key, int slot) { return optionsMenuBindText(key, slot); }
static void keyBegin(const char* key, int slot) { optionsMenuBindBegin(key, slot); }
static bool keyActive() { return optionsMenuBindActive() != 0; }
static void doAction(const char* key) { optionsMenuAction(key); }

/* "hfov": the horizontal view angle that a vertical one gives at the window's aspect ratio (the picture's, right now) */
static const char* readout(const char* name, double cfg) {
    static char buf[32];
    buf[0] = 0;
    if (!strcmp(name, "hfov")) snprintf(buf, sizeof buf, "(%d deg horizontal)", (int)(portFovHorizDegrees((int)(cfg + 0.5)) + 0.5f));
    return buf;
}

/* The asset-pack list: one row per pack found, the enabled ones first in their order (the game keeps the pending edits). */
static int packRows(std::vector<menu::PackRow>* out) {
    static char names[64][64], authors[64][64];
    static int on[64];
    const int n = optionsMenuAssetPackRows(names, authors, on, 64);
    out->clear();
    for (int i = 0; i < n; i++) {
        menu::PackRow r;
        r.name = names[i];
        r.author = authors[i];
        r.on = on[i] != 0;
        out->push_back(r);
    }
    return n;
}
static int packAct(int row, int what) { return optionsMenuAssetPackAct(row, what); }

extern "C" void menuBindGame(void) {   /* the menu reads and writes the game's values */
    std::vector<std::string> res;
    for (int i = 0; i < optionsMenuResCount(); i++) {
        char b[32];
        optionsMenuResLabel(i, b, sizeof b);
        res.push_back(b);
    }
    if (!res.empty()) menu::setOptionLabels("res", res);
    std::vector<std::string> dev;
    for (int i = 0; i < optionsMenuAudioCount(); i++) dev.push_back(optionsMenuAudioLabel(i));
    if (!dev.empty()) menu::setOptionLabels("adev", dev);
    menu::setPackList(packRows, packAct);
    menu::setBinding(bindGet, bindSet);
    menu::setKeyBinding(keyText, keyBegin, keyActive);
    menu::setActionHandler(doAction);
    menu::setReadout(readout);
    menu::setTint(portCrosshairTint);
    menu::setPicture(portCrosshairPicture);
}

extern "C" int menuTakeCloseRequest(void) {
    return menu::takeCloseRequest() ? 1 : 0;
}

extern "C" void menuUiClose(void) {
    optionsMenuAssetPackDiscard();   /* the menu is closed: unapplied asset-pack edits are dropped */
    if (modsScreenActive()) {   /* the idle branch of the overlay's emit: the Mods screen is up over the mode select */
        runFrame(4);
        return;
    }
    {   /* the scripted credits (credits.show) are on screen */
        const MissionCredits* c = nullptr;
        double t = 0.0;
        if (portCreditsView(&c, &t)) {
            runFrame(5);
            return;
        }
    }
    menu::showRoot();
    gfx_ui_submit(nullptr, 0, nullptr, 0, nullptr, 0);
}
