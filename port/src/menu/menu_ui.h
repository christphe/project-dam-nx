#ifndef MENU_UI_H
#define MENU_UI_H

/* The menu's interface, written against Dear ImGui only. This file and menu_ui.cpp know nothing about the
 * game: tools_pc/menuview runs them on their own, the game draws the same frames through gfx_ui_*
 * (port/fast3d/gfx_ui.h). What the menu shows comes from menu.json (menu_model.h); the look is the one
 * validated in the design artifact. */
#include <stddef.h>
#include <string>

#include <string>
#include <vector>

namespace menu {

/* Call once after the ImGui context exists and before the first frame. The font data must stay valid
 * (ImGui keeps pointers to it): the harness reads the TTFs from disk, the game embeds them. */
void init(const void* bodyTtf, size_t bodyLen, const void* headTtf, size_t headLen);

/* Each text size is drawn from a font baked at that pixel size. Call fontsFrame(width) before NewFrame: true when
 * the atlas must be rebuilt (a new size was asked for, or the window changed); then fontsBuild() (which clears the
 * atlas: add custom rects after it) and upload the texture again. */
bool fontsFrame(float W);
void fontsBuild();

/* Loads (or reloads) the content. Values already set are kept for the controls that still exist. On failure
 * returns false with the reason in *err; the menu then shows it instead of the content. */
bool setData(const char* json, size_t len, std::string* err);

void setHandheld(bool handheld);   /* "handheld" in expressions: a small screen off the Switch (GE_HANDHELD, #131) */
void setConsole(bool console);   /* "console" in expressions: no keyboard/mouse (a run-time setting, not the platform) */
void setPadFamily(int fam);   /* the menu pad's family, 0 Xbox, 1 PlayStation, 2 Nintendo (input.c PADFAM_*): the button names and icons follow */
void setPlatform(bool pc);   /* which defaults and which "platform ==" rules apply */
/* The link to the game's values: get fills the config value of a control's key (false = no such value), set stores
 * one. Without it the menu keeps its own values (the viewer). */
void setBinding(bool (*get)(const char* key, double* out), void (*set)(const char* key, double value));
void setOptionLabels(const char* id, const std::vector<std::string>& labels);   /* a list only the game knows (resolutions) */
/* Key and button capture is the game's: text of a slot ("" = none), begin a capture, is one running. */
void setKeyBinding(std::string (*text)(const char* key, int slot), void (*begin)(const char* key, int slot), bool (*active)());
/* An action control was activated: the game does it (centre the window, reset a section...). */
void setReadout(const char* (*f)(const char* name, double cfg));   /* a note beside a slider value, computed by the game */
void setPicture(int (*f)(int style, const unsigned char** texels));   /* the crosshair sprite, for the preview */
void setTint(void (*f)(int* r, int* g, int* b));   /* the crosshair colour in use, for the preview */
void setActionHandler(void (*handler)(const char* key));
/* The asset-pack list (a "packlist" control): one row per pack, the enabled ones first in their order. rows fills up to max rows
 * and returns how many; act does what was asked of a row (0 toggle, 1 move up, 2 move down) and returns the row the pack is on now. */
struct PackRow { std::string name, author; bool on = false; };
void setPackList(int (*rows)(std::vector<PackRow>* out), int (*act)(int row, int what));
/* The weapon / gadget wheel of the Modern control style, drawn over the game: the item names, the picked one (-1 = none). */
void drawWheel(float W, float H, int seat, float x0, float y0, float x1, float y1, const char* title, const std::vector<std::string>& names, int sel);
/* The "press a button" screen of a multiplayer set-up: what each player has so far, whose turn it is. */
void drawAssign(float W, float H, const std::vector<std::string>& names, int step, int count);
/* The scripted credits (credits.show), over the game. Every line carries where it is: `y` is its top and `h` its height in
 * screen lines (`view` lines make the screen's height, so the picture is the same at any resolution), `alpha` its
 * opacity. style 0 text, 1 heading, 2 title; align 0 centre, 1 left, 2 right. A pair is a role (left of the centre line,
 * right-aligned) and a name (right of it). `backdrop` is the opacity of a black screen behind them (0: none). */
struct CreditsView {
    struct Line { std::string left, right; bool pair = false; int style = 0, align = 0; float y = 0, h = 1, alpha = 1; };
    std::vector<Line> lines;
    float backdrop = 0, view = 24;
};
void drawCredits(float W, float H, const CreditsView& v);
/* The co-op player count (2, 3 or 4) asked from the mode menu: `sel` 0..2 is the highlighted row. */
void drawCoopCount(float W, float H, int sel, int* hover, bool* click);   /* hover: the row the mouse moved onto (-1 none) */
/* The Mods screen (the mode select's MODS row): what it shows, filled by the game each frame. Entry 0 is GoldenEye. */
struct ModsView {
    struct Mission { std::string name, done; int state = 0, thumb = -1; };   /* state: 0 locked, 1 available, 2 completed */
    struct Entry { std::string name, author, desc, progress; int affects = 0, thumb = -1; std::vector<Mission> missions; };   /* affects: 1 story, 2 multiplayer */
    std::vector<Entry> entries;
    int sel = 0, focus = 0, btn = 1, chosen = 0;   /* the highlighted entry, 0 list / 1 buttons, the focused button (0 Back, 1 Play), the entry in use */
};
void setThumbImage(int id, float u0, float v0, float u1, float v1);   /* a mod picture in the atlas (uv of its corners); id < 0 clears all */
/* hover: the card (0..n-1) or button (1000 Back, 1001 Play this mod) the mouse moved onto, -1 none; click: pressed on it */
void drawModsScreen(float W, float H, const ModsView& v, int* hover, bool* click);
void showRoot();   /* the next frame shows the first menu (Resume / Options / Restart / Quit) */
void debugGoto(int tab, int entry, int focus);   /* tests: open on this tab and entry (focus -1 = the list) */
void setUpdate(int state, const char* tag, const char* notes, const char* version, bool installable, bool busy, const char* status);   /* the update check: its state, the release found, its text, this build's version */
void setContext(bool fileSelect, bool inStage, bool updateAvailable, bool profile);   /* where the game is: the rules "fileselect" and "stage" read it */
/* where the gamepad drawing sits in the texture (uv of its corners) and its width over its height */
void setPadImage(float u0, float v0, float u1, float v1, float aspect);
void setIconImage(int icon, float u0, float v0, float u1, float v1);   /* a kMenuIcon* (menu_icons.h) in the atlas */
void setOwner(int seat);     /* the player whose pad drives the menu (0-based; -1 = not shown): named in the header */
void setDev(bool dev);       /* dev mode: the controls marked "dev" show up */
bool isPc();
bool devOn();

/* One frame of the menu, filling a W x H pixel target. Call between ImGui::NewFrame and ImGui::Render. */
void draw(float W, float H);
void drawPointer(bool crosshair, float W, float H);   /* the crosshair that replaces the system pointer (Input.CrosshairCursor) */

/* True once, when the player asked to close the menu (B while on the list of entries). The game polls it. */
bool takeCloseRequest();

/* A one-line description of the navigation state (tab, entry, focus, value), for the harness self-test. */
const char* debugState();

}  // namespace menu

#endif
