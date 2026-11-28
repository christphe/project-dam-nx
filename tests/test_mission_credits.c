/* The scripted credits' model (port/mission/mission_host_credits.h): the layout in lines, what is on screen at a given
 * time, the fades, the end, and the limits (text length, list size, speed, gap). No game, no Lua. */
#include "ge_test.h"
#include "mission_host_credits.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int near_(double a, double b) { return fabs(a - b) < 0.001; }

int main(void)
{
    MissionCredits *c = malloc(sizeof *c);   /* 85 KB: not on the stack */
    GE_TEST_BEGIN("mission credits");
    if (!c) return 1;

    /* layout: heights by style, y accumulates, gaps are empty lines */
    mhcrInit(c);
    GE_CHECK(near_(c->speed, MHCR_SPEED_DEF));
    GE_CHECK(mhcrAddLine(c, MHCR_ST_TITLE, MHCR_AL_CENTER, "Title"));
    GE_CHECK(mhcrAddGap(c, 1.5));
    GE_CHECK(mhcrAddLine(c, MHCR_ST_HEADING, MHCR_AL_LEFT, "Cast"));
    GE_CHECK(mhcrAddPair(c, "Agent", "Someone"));
    GE_CHECK(mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_RIGHT, "Thanks"));
    GE_CHECK_EQ_INT(c->n, 5);
    GE_CHECK(near_(c->e[0].y, 0.0) && near_(c->e[0].h, 2.6));
    GE_CHECK(near_(c->e[1].y, 2.6) && near_(c->e[1].h, 1.5) && c->e[1].kind == MHCR_E_GAP);
    GE_CHECK(near_(c->e[2].y, 4.1) && near_(c->e[2].h, 1.7));
    GE_CHECK(near_(c->e[3].y, 5.8) && c->e[3].kind == MHCR_E_PAIR);
    GE_CHECK_STR(c->e[3].left, "Agent");
    GE_CHECK_STR(c->e[3].right, "Someone");
    GE_CHECK(near_(c->e[4].y, 6.8) && c->e[4].align == MHCR_AL_RIGHT);
    GE_CHECK(near_(c->total, 7.8));

    /* the scroll: the first line enters at the bottom at t = 0, the last leaves through the top */
    mhcrSetSpeed(c, 2.0);
    GE_CHECK(near_(mhcrDuration(c), (MHCR_VIEW + 7.8) / 2.0));
    GE_CHECK(near_(mhcrScreenY(c, &c->e[0], 0.0), MHCR_VIEW));
    GE_CHECK(!mhcrVisible(c, &c->e[0], 0.0));                           /* just below the screen */
    GE_CHECK(mhcrVisible(c, &c->e[0], 0.1));
    GE_CHECK(!mhcrVisible(c, &c->e[4], 0.1));                           /* the last is still far below */
    GE_CHECK(near_(mhcrScreenY(c, &c->e[0], 5.0), MHCR_VIEW - 10.0));
    GE_CHECK(mhcrVisible(c, &c->e[0], 12.0));                           /* y = 0: top edge */
    GE_CHECK(!mhcrVisible(c, &c->e[0], 12.0 + 2.6 / 2.0 + 0.01));       /* it has left through the top */
    GE_CHECK(!mhcrFinished(c, mhcrDuration(c) - 0.01));
    GE_CHECK(mhcrFinished(c, mhcrDuration(c)));
    GE_CHECK(!mhcrVisible(c, &c->e[4], mhcrDuration(c) + 0.01));        /* nothing left at the end */

    /* the fades: full in the middle of the screen, nothing at the edges */
    mhcrInit(c);
    mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, "x");
    GE_CHECK(near_(mhcrAlpha(c, &c->e[0], 0.0), 0.0));                                  /* just below the screen */
    GE_CHECK(near_(mhcrAlpha(c, &c->e[0], 1.5 / c->speed), 0.5));                      /* its middle one line above the bottom edge: 1 / 2 */
    GE_CHECK(near_(mhcrAlpha(c, &c->e[0], (MHCR_VIEW - 11.5) / c->speed), 1.0));       /* its middle on the screen's middle line */
    GE_CHECK(near_(mhcrAlpha(c, &c->e[0], MHCR_VIEW / c->speed), 0.25));               /* its top on the screen's top edge: its middle is 0.5 / 2 */
    GE_CHECK(near_(mhcrAlpha(c, &c->e[0], (MHCR_VIEW + 5.0) / c->speed), 0.0));        /* gone through the top */

    /* the black backdrop: in over a second, out over the last second; none when the game's view stays */
    mhcrInit(c);
    mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, "x");
    GE_CHECK(near_(mhcrBackdrop(c, 0.5), 0.0));                          /* black = 0: no backdrop */
    c->black = 1;
    GE_CHECK(near_(mhcrBackdrop(c, 0.0), 0.0));
    GE_CHECK(near_(mhcrBackdrop(c, 0.5), 0.5));
    GE_CHECK(near_(mhcrBackdrop(c, 5.0), 1.0));
    GE_CHECK(near_(mhcrBackdrop(c, mhcrDuration(c) - 0.25), 0.25));
    GE_CHECK(near_(mhcrBackdrop(c, mhcrDuration(c) + 1.0), 0.0));

    /* an empty list lasts no time */
    mhcrInit(c);
    GE_CHECK(near_(mhcrDuration(c), 0.0));
    GE_CHECK(mhcrFinished(c, 0.0));

    /* limits: a text is cut to what fits, the speed and the gap are clamped, the list is bounded */
    {
        char big[300];
        memset(big, 'a', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        mhcrInit(c);
        mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, big);
        GE_CHECK_EQ_INT((int)strlen(c->e[0].left), MHCR_TEXT - 1);
        mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, NULL);              /* a missing text is empty, not a crash */
        GE_CHECK_STR(c->e[1].left, "");
    }
    mhcrSetSpeed(c, 0.0);
    GE_CHECK(near_(c->speed, MHCR_SPEED_MIN));
    mhcrSetSpeed(c, 1000.0);
    GE_CHECK(near_(c->speed, MHCR_SPEED_MAX));
    mhcrInit(c);
    mhcrAddGap(c, -3);
    mhcrAddGap(c, 500);
    GE_CHECK(near_(c->e[0].h, 0.0) && near_(c->e[1].h, 50.0));
    mhcrInit(c);
    for (int i = 0; i < MHCR_MAX_ENTRIES; i++) GE_CHECK(mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, "l"));
    GE_CHECK(!mhcrAddLine(c, MHCR_ST_TEXT, MHCR_AL_CENTER, "one too many"));
    GE_CHECK(!mhcrAddPair(c, "a", "b"));
    GE_CHECK(!mhcrAddGap(c, 1));
    GE_CHECK_EQ_INT(c->n, MHCR_MAX_ENTRIES);

    free(c);
    return GE_TEST_END();
}
