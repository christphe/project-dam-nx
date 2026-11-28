#include "chrai.h"
#ifdef PORT
#include "drawdistgameplay.h"
#endif
#include "bg.h"
#include "bgfog.h"
#include "bondinv.h"
#include "bondview.h"
#include "cheat.h"
#include "chr.h"
#include "chraction.h"
#include "chraidata.h"
#include "file.h"
#include "gun.h"
#include "initanitable.h"
#include "language.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math_atan2f.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "model.h"
#include "mp_music.h"
#include "objecthandler.h"
#include "objective_status.h"
#include "player.h"
#include "propobj.h"
#include "stan.h"
#include <assert.h>
#include <bondaicommands.h>
#include <bondgame.h>
#include <bondtypes.h>
#include <boss.h>
#include <limits.h>
#include <music.h>
#include <random.h>
#include <snd.h>
#include <ultra64.h>
#if defined(PORT)
#include "portaddr.h"
#else
#define PORT_N64PTR(T, x) ((T *)(x))
#endif
#ifdef PORT
#include <stdlib.h>
#include "envflag.h"
#endif

// hack? used to match as called with 2 args, but decompiled code takes 1
extern s32 objectiveGetStatus_WEAK(s32 objectiveNum, s32);

// bss
/**
 * Address 0x80069B70.
 */
sfxRecord  sfx_related[SFX_RELATED_LEN];

/**
 * Play Audio in slot X from Prop or Pad
 * @param slot: Where audio is loaded
 */
void       audioPlayFromProp2(s32 slot)
{
    int        tempvol;
    sfxRecord *sfx = &sfx_related[slot]; // always added to stack anyway, cleaner to use
    int        clock_timer;

    if ((sfx->state) && (sndGetPlayingState(sfx->state)))
    {
        if (sfx->pos)
        {
            sfx->Volume = sub_GAME_7F0539E4(sfx->pos);
        }
        else
        {
            if (sfx->Obj && sfx->Obj->prop)
            {
                sfx->Volume = sub_GAME_7F0539E4(&sfx->Obj->runtime_pos);
            }
        }

        tempvol = sfx->Volume;
        if (sfx->sfxID >= 0)
        {
            clock_timer = g_ClockTimer;
            if (clock_timer < sfx->sfxID)
            {
                tempvol = (((sfx->Volume - sfx->Volume2) * clock_timer) / sfx->sfxID) + sfx->Volume2;
            }
            sfx->sfxID = sfx->sfxID - clock_timer;
        }
        if (lvlGetControlsLockedFlag() != 0)
        {
            tempvol = 0;
        }
        if (tempvol != sfx->Volume2)
        {
            sndCreatePostEvent(sfx->state, 8, tempvol);
            sfx->Volume2 = tempvol;
            return;
        }
        return;
    }
    sfx->Volume2 = 0;
}

/**
 Play All Sounds in all slots
*/
void loop_set_sound_effect_all_slots(void)
{
    int i;
    for (i = 0; i < SFX_RELATED_LEN; i++)
    {
        audioPlayFromProp2(i);
    }
}

/**
 * Load Audio Slot with sound and Play
 * @param slot: where to load sound
 * @param soundIndex: SFX_ID
 */
void audioPlayFromProp(s32 slot, s16 soundIndex)
{
    sfxRecord *sfx = NULL; // always added to stack anyway, cleaner to use
    //"Existing ai sound number %d!\n"
    if (slot >= 0 && slot < SFX_RELATED_LEN)
    {
        if (!sfx_related[slot].state || !sndGetPlayingState(sfx_related[slot].state))
        {
            sfx = &sfx_related[slot];

            sfx->Volume  = SHRT_MAX;
            sfx->Volume2 = SHRT_MAX;
            sfx->sfxID   = -1;
            sfx->pos     = NULL;
            sfx->Obj     = NULL;
        }
#ifdef DEBUG
        else
        {
            osSyncPrintf("Existing ai sound number %d!\n", slot);
        }
#endif
    }
    sndPlaySfx(g_musicSfxBufferPtr, soundIndex, sfx);
}

/**
 Stop All Sounds in all slots
*/
void sub_GAME_7F0349BC(s32 slot)
{
    if ((slot >= 0) && (slot < SFX_RELATED_LEN))
    {
        sndDeactivate(sfx_related[slot].state);
    }
}
AIRecord *ailistFindById(s32 ID)
{
    s32 i;

    if (!isGlobalAIListID(ID))
    {
        if (g_CurrentSetup.ailists)
        {
            for (i = 0; g_CurrentSetup.ailists[i].ailist; i++)
            {
                if (g_CurrentSetup.ailists[i].ID == ID)
                {
                    return g_CurrentSetup.ailists[i].ailist;
                }
            }
        }
    }
    else
    {
        for (i = 0; g_GlobalAILists[i].ailist; i++)
        {
            if (g_GlobalAILists[i].ID == ID)
            {
                return g_GlobalAILists[i].ailist;
            }
        }
    }
    return NULL;
}

PathRecord *pathFindById(s32 ID)
{
    int i;

    for (i = 0; g_CurrentSetup.patrolpaths[i].waypoints; i++)
    {
        if (ID == g_CurrentSetup.patrolpaths[i].ID)
        {
            return &g_CurrentSetup.patrolpaths[i];
        }
    }

    return NULL;
}
