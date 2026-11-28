#ifndef PORT_MISSION_HOST_CREDITS_H
#define PORT_MISSION_HOST_CREDITS_H

/* Scripted credits (#165): the model behind credits.show. A script gives the lines; this header lays them out and says
 * which of them are on screen at a given time, so the layout, the scroll and the end are unit-tested without the game
 * (tests/test_mission_credits.c). The host (port/src/missions_host_cinema.c) owns one MissionCredits and advances its
 * clock; the drawing is the menu's (port/src/menu/menu_ui.cpp, drawCredits), over every player's view at once.
 *
 * Everything is in "lines", not pixels, so the credits look the same at any resolution: one line is 1/MHCR_VIEW of the
 * screen height. A text line is 1 line high, a heading and a title more. The first line enters at the bottom edge at
 * t = 0 and the credits end when the last one has left through the top. */

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MHCR_MAX_ENTRIES 400
#define MHCR_TEXT        96       /* bytes of one text, with the NUL */
#define MHCR_VIEW        24.0f    /* the screen's height in lines */
#define MHCR_FADE        2.0f     /* lines over which a line fades in at the bottom and out at the top */
#define MHCR_SPEED_MIN   0.2f
#define MHCR_SPEED_MAX   10.0f
#define MHCR_SPEED_DEF   1.5f     /* lines per second */

enum { MHCR_E_LINE = 0, MHCR_E_PAIR, MHCR_E_GAP };            /* what an entry is */
enum { MHCR_ST_TEXT = 0, MHCR_ST_HEADING, MHCR_ST_TITLE };    /* a line's style */
enum { MHCR_AL_CENTER = 0, MHCR_AL_LEFT, MHCR_AL_RIGHT };     /* a line's alignment */

typedef struct MhcrEntry {
    int   kind;                   /* MHCR_E_* */
    int   style;                  /* MHCR_ST_* (a line) */
    int   align;                  /* MHCR_AL_* (a line) */
    float y;                      /* where it starts, in lines from the first line's top */
    float h;                      /* how high it is, in lines */
    char  left[MHCR_TEXT];        /* a line's text, or a pair's left column */
    char  right[MHCR_TEXT];       /* a pair's right column */
} MhcrEntry;

typedef struct MissionCredits {
    float speed;                  /* lines per second */
    int   black;                  /* 1: a black screen behind the lines; 0: the game's view stays visible */
    int   n;
    float total;                  /* the height of all the entries, in lines */
    MhcrEntry e[MHCR_MAX_ENTRIES];
} MissionCredits;

static inline void mhcrInit(MissionCredits *c)
{
    memset(c, 0, sizeof *c);
    c->speed = MHCR_SPEED_DEF;
}

static inline void mhcrSetSpeed(MissionCredits *c, double s)
{
    c->speed = s < MHCR_SPEED_MIN ? MHCR_SPEED_MIN : s > MHCR_SPEED_MAX ? MHCR_SPEED_MAX : (float)s;
}

/* A line's height by style. */
static inline float mhcrHeight(int style)
{
    return style == MHCR_ST_TITLE ? 2.6f : style == MHCR_ST_HEADING ? 1.7f : 1.0f;
}

static inline void mhcrCopy(char *dst, const char *src)
{
    size_t n = src ? strlen(src) : 0;
    if (n > MHCR_TEXT - 1) n = MHCR_TEXT - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static inline MhcrEntry *mhcrNext(MissionCredits *c, int kind, float h)
{
    MhcrEntry *e;
    if (c->n >= MHCR_MAX_ENTRIES) return NULL;
    e = &c->e[c->n++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->y = c->total;
    e->h = h;
    c->total += h;
    return e;
}

/* One entry each; 0 when the list is full. A gap is `lines` empty lines (0 to 50). */
static inline int mhcrAddLine(MissionCredits *c, int style, int align, const char *text)
{
    MhcrEntry *e = mhcrNext(c, MHCR_E_LINE, mhcrHeight(style));
    if (!e) return 0;
    e->style = style;
    e->align = align;
    mhcrCopy(e->left, text);
    return 1;
}

static inline int mhcrAddPair(MissionCredits *c, const char *left, const char *right)
{
    MhcrEntry *e = mhcrNext(c, MHCR_E_PAIR, 1.0f);
    if (!e) return 0;
    e->style = MHCR_ST_TEXT;
    mhcrCopy(e->left, left);
    mhcrCopy(e->right, right);
    return 1;
}

static inline int mhcrAddGap(MissionCredits *c, double lines)
{
    float h = lines < 0 ? 0.0f : lines > 50 ? 50.0f : (float)lines;
    return mhcrNext(c, MHCR_E_GAP, h) != NULL;
}

/* How long the credits last: the first line enters at the bottom, the last one leaves through the top. */
static inline double mhcrDuration(const MissionCredits *c)
{
    return c->n ? ((double)MHCR_VIEW + c->total) / c->speed : 0.0;
}

static inline int mhcrFinished(const MissionCredits *c, double t) { return t >= mhcrDuration(c); }

/* Where an entry's top is on the screen at time t, in lines from the screen's top (negative: above it). */
static inline float mhcrScreenY(const MissionCredits *c, const MhcrEntry *e, double t)
{
    return MHCR_VIEW - (float)(t * c->speed) + e->y;
}

/* Is any part of the entry on screen? */
static inline int mhcrVisible(const MissionCredits *c, const MhcrEntry *e, double t)
{
    float y = mhcrScreenY(c, e, t);
    return y < MHCR_VIEW && y + e->h > 0.0f;
}

/* A line's opacity: it fades in over the last MHCR_FADE lines at the bottom and out over the first ones at the top. */
static inline float mhcrAlpha(const MissionCredits *c, const MhcrEntry *e, double t)
{
    float y = mhcrScreenY(c, e, t) + e->h * 0.5f;
    float a = 1.0f;
    if (y > MHCR_VIEW - MHCR_FADE) a = (MHCR_VIEW - y) / MHCR_FADE;
    if (y < MHCR_FADE && (y / MHCR_FADE) < a) a = y / MHCR_FADE;
    return a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
}

/* The black backdrop's opacity: 1 s in at the start, 1 s out at the end (0 when the backdrop is the game's view). */
static inline float mhcrBackdrop(const MissionCredits *c, double t)
{
    double d = mhcrDuration(c), a;
    if (!c->black) return 0.0f;
    a = t < 1.0 ? t : 1.0;
    if (d - t < 1.0 && d - t < a) a = d - t;
    return a < 0.0 ? 0.0f : a > 1.0 ? 1.0f : (float)a;
}

#ifdef __cplusplus
}
#endif

#endif
