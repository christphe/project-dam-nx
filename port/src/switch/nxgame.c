/*
 * nxgame.c -- Switch-only gameplay feedback options (project-dam).
 *
 * Game.BloodOnHit: the puff a bullet makes on a guard's body is, in the
 * original game, the same pale-yellow puff as on a wall: g_HitReactionTable
 * (src/game/chr.c) asks bullet_spark_create for puff type 1 on every body
 * part, and type 1 is { 0xFF, 0xFF, 0xC8 } in g_BulletSparkColors
 * (src/game/glass2.c). The game already has a red puff, type 2 { 0xFF, 0, 0 },
 * with the same image and lifetime (bullet_sparks_init's default branch), that
 * nothing uses. With the option on, the body parts ask for type 2, so a hit on
 * a guard reads as a hit; the gun and the hat keep type 1. Off restores the
 * table as the game ships it. Port-side data only: no game code changes.
 */
#include "platform.h"

#if defined(PLATFORM_SWITCH)

#include "game/chr.h"
#include "config.h"
#include "system.h"

#define PUFF_YELLOW 1   /* g_BulletSparkColors[1], the game's body and wall puff */
#define PUFF_RED    2   /* g_BulletSparkColors[2], unused by the game */
#define HIT_FIRST_BODY_PART 1      /* HIT_LEFT_FOOT */
#define HIT_LAST_BODY_PART  0xF    /* HIT_CHEST; HIT_GUN 0x64 and HIT_HAT 0x6E stay yellow */

/* The game's type 2 is pure red, which reads as paint rather than blood.
 * Nothing in the game uses type 2, so the port tones it down to a dark
 * blood red; the gun and hat puffs and every wall puff are unaffected. */
#define BLOOD_R 0x8C
#define BLOOD_G 0x0A
#define BLOOD_B 0x0A

extern struct rgba_u8 g_BulletSparkColors[8];   /* src/game/glass2.c */

static int s_bloodOnHit = 1;

void nxGameApply(void)
{
    const int puff = s_bloodOnHit ? PUFF_RED : PUFF_YELLOW;
    g_BulletSparkColors[PUFF_RED].r = BLOOD_R;
    g_BulletSparkColors[PUFF_RED].g = BLOOD_G;
    g_BulletSparkColors[PUFF_RED].b = BLOOD_B;
    for (int i = 0; g_HitReactionTable[i].hitpart != -1; i++) {
        const s32 part = g_HitReactionTable[i].hitpart;
        if (part >= HIT_FIRST_BODY_PART && part <= HIT_LAST_BODY_PART &&
            g_HitReactionTable[i].impactPuffCount != 0) {
            g_HitReactionTable[i].impactPuffCount = puff;
        }
    }
}

PD_CONSTRUCTOR static void nxGameRegisterConfig(void)
{
    configRegisterInt("Game.BloodOnHit", &s_bloodOnHit, 0, 1);
}

#endif /* PLATFORM_SWITCH */
