#include <ultra64.h>
#if defined(PORT)
#include "portaddr.h"
#else
#define PORT_N64PTR(T, x) ((T *)(x))
#endif
#ifdef PORT
#include <stdio.h>
#include <stdlib.h>
#endif
#include <memp.h>
#include <bondconstants.h>
#include <boss.h>
#include "math_atan2f.h"
#include "bondview_r.h"
#include "bondview.h"
#include "random.h"
#include "game/bondinv.h"
#include "game/chrai.h"
#include "game/front.h"
#include "game/gun.h"
#include "game/language.h"
#include "game/player.h"
#include "game/stan.h"


/**
 * Address 0x8002A780.
*/
struct coord3d default_start_position = { 0 };

u32 weaponLoadProjectileModels(ITEM_IDS modelid)
{
    s32 model;

    model = -1;
    switch(modelid)
    {
    case ITEM_THROWKNIFE:
        model = PROJECTILES_TYPE_KNIFE;
        break;
    case ITEM_GRENADELAUNCH:
        model = PROJECTILES_TYPE_GLAUNCH_ROUND;
        break;
    case ITEM_ROCKETLAUNCH:
        model = PROJECTILES_TYPE_ROCKET_ROUND;
        break;
    case ITEM_GRENADE:
        model = PROJECTILES_TYPE_GRENADE;
        break;
    case ITEM_TIMEDMINE:
        model = PROJECTILES_TYPE_TIMED_MINE;
        break;
    case ITEM_PROXIMITYMINE:
        model = PROJECTILES_TYPE_PROX_MINE;
        break;
    case ITEM_REMOTEMINE:
        model = PROJECTILES_TYPE_REMOTE_MINE;
        break;
    case ITEM_TANKSHELLS:
        model = PROJECTILES_TYPE_ROCKET_ROUND2;
        break;
    case ITEM_BOMBCASE:
        model = PROJECTILES_TYPE_BOMBCASE;
        break;
    case ITEM_PLASTIQUE:
        model = PROJECTILES_TYPE_PLASTIQUE;
        break;
    case ITEM_BUG:
        model = PROJECTILES_TYPE_BUG;
        break;
    case ITEM_MICROCAMERA:
        model = PROJECTILES_TYPE_MICROCAMERA;
    }

    if (-1 < model)
    {
        return modelLoad(model);
    }
    return 0;
}

void bondviewLoadSetupIntroSection(void)
{

#define FLOAT_INIT 0


#if defined(VERSION_EU)
#define FIELD_6C_FACTOR 0.20039999485f
#define FIELD_3B8_FACTOR 0.118799984455f
#else
#define FIELD_6C_FACTOR 0.170000016689f
#define FIELD_3B8_FACTOR 0.100000023842f
#endif

    // declarations

    struct coord3d start_pos;
    f32 start_look_angle;
    StandTile *start_stan;
    struct SetupIntroEmpty *intro_record;
    s32 set_starting_weapon;
    CreditsEntry *credits;
    s32 rand_pad_index;
    f32 stan_height;
    s32 i;
    struct SetupIntroItem *intro_item;
    struct SetupIntroWatch *intro_watch;
    struct SetupIntroCredits *intro_credits;
    s32 padding[5];

    // done with declarations

    start_pos = default_start_position;

    intro_record = (struct SetupIntroEmpty *)g_CurrentSetup.intro;
    g_isBondKIA = 0;
    g_bondviewForceDisarm = 0;
    resolution = 0;
    cameraBufferToggle = 0;
    cameraFrameCounter1 = 0;
    set_starting_weapon = 0;
    cameraFrameCounter2 = 0;
    start_look_angle = FLOAT_INIT;

    if (bossGetStageNum() == LEVELID_CUBA)
    {
        resolution = (s32)mempAllocBytesInBank(0x46EA0, MEMPOOL_STAGE);
        resolution = (resolution + 0x3f) & ~0x3F;
        cameraFrameCounter1 = 1;
    }

    camera_80036438 = 0;
    credits_state = 0;
    credits_pointer = NULL;
    g_ForceBondMoveOffset.f[0] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[1] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[2] = FLOAT_INIT;
    g_SurroundBondWithExplosionsFlag = 0;
    startpadcount = 0;
    g_PlayerIsInTank = 0;
    g_WorldTankProp = 0;
    g_PlayerTankProp = NULL;
    g_PlayerTankYOffset = FLOAT_INIT;
    g_TankSfxState[0] = NULL;
    g_TankSfxState[1] = NULL;
    g_TankTurnSpeed = FLOAT_INIT;
    g_TankOrientationAngle = FLOAT_INIT;
    tank_turret_unused_angle = FLOAT_INIT;
    g_TankTurretVerticalAngle = FLOAT_INIT;
    g_TankTurretVerticalAngleRelated = FLOAT_INIT;
    g_TankTurretOrientationAngleRad = FLOAT_INIT;
    g_TankTurretOrientationAngleDeg = FLOAT_INIT;
    tank_turret_turn_speed = FLOAT_INIT;
    g_BondCanEnterTank = 0;
    g_TankTurretAngle = FLOAT_INIT;
    g_TankTurretTurn = FLOAT_INIT;
    g_ExplodeTankOnDeathFlag = 0;
    is_timer_active = 1;
    g_PlayerInvincible = FALSE;
    g_CameraMode = 0;
    g_CameraAfterCinema = 0;
    camera_fade_active = 0;
    stop_time_flag = 0;
    camera_transition_timer = FLOAT_INIT;
    intro_camera_index = CAMERAMODE_INTRO;
    mission_timer = 0;
    watch_time_0 = 0;
    watch_transition_time = 0.9090909f;
    starting_weapon[GUNLEFT] = ITEM_UNARMED;
    starting_weapon[GUNRIGHT] = ITEM_UNARMED;

    if (intro_record != NULL)
    {
        while (intro_record->type != INTROTYPE_END)
        {
            switch (intro_record->type)
            {
                case INTROTYPE_SPAWN:
                {
                    if (g_CurrentSetup.pads != NULL
                        && (((struct SetupIntroSpawn*)intro_record)->is_demo_playback == 0))
                    {
                        g_Startpad[startpadcount] = &g_CurrentSetup.pads[((struct SetupIntroSpawn*)intro_record)->index];
                        startpadcount++;
                    }

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroSpawn));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroSpawn));
#endif
                }
                break;

                case INTROTYPE_ITEM:
                {
                    intro_item = (struct SetupIntroItem*)intro_record;

                    if (intro_item->is_demo_playback == 0)
                    {
                        weaponLoadProjectileModels(intro_item->item_right);

                        if (intro_item->item_left >= 0)
                        {
                            weaponLoadProjectileModels(intro_item->item_left);
                            bondinvAddDoublesInvItem(intro_item->item_right, intro_item->item_left);
                        }
                        else
                        {
                            bondinvAddInvItem(intro_item->item_right);
                        }

                        if (set_starting_weapon == 0)
                        {
                            starting_weapon[GUNRIGHT] = intro_item->item_right;

                            if(intro_item->item_left);

                            set_starting_weapon = 1;

                            if (intro_item->item_left >= 0)
                            {
                                starting_weapon[GUNLEFT] = intro_item->item_left;
                            }
                        }
                    }

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroItem));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroItem));
#endif
                }
                break;

                case INTROTYPE_AMMO:
                {
                    if (((struct SetupIntroAmmo*)intro_record)->is_demo_playback == 0)
                    {
                        give_cur_player_ammo(((struct SetupIntroAmmo*)intro_record)->ammo_type, ((struct SetupIntroAmmo*)intro_record)->ammo_amount);
                    }

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroAmmo));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroAmmo));
#endif
                }
                break;

                case INTROTYPE_SWIRL:
                {
                    /* the intro swirl is not read any more: the intro lives in the campaign mod's Luau */
#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroSwirl));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroSwirl));
#endif
                }
                break;

                case INTROTYPE_ANIM:
                {
#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroAnim));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroAnim));
#endif
                }
                break;

                case INTROTYPE_CUFF:
                {
                    g_CurrentPlayer->bondtype = ((struct SetupIntroCuff*)intro_record)->bondtype;

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCuff));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCuff));
#endif
                }
                break;

                case INTROTYPE_CAMERA:
                {
                    /* the fixed intro cameras are not read any more (see INTROTYPE_SWIRL) */
#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCamera));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCamera));
#endif
                }
                break;

                case INTROTYPE_WATCH:
                {
                    intro_watch = (struct SetupIntroWatch*)intro_record;

                    watch_time_0 = 0;

                    if (intro_watch->minutes > 0)
                    {
                        watch_time_0 += (intro_watch->minutes % 60) * (60*60);
                    }

                    if (intro_watch->hours > 0)
                    {
                        watch_time_0 += ((intro_watch->hours % 12) * (60*60*60));
                    }

                    if (watch_time_0);

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroWatch));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroWatch));
#endif
                }
                break;

                case INTROTYPE_CREDITS:
                {
                    intro_credits = (struct SetupIntroCredits*)intro_record;

                    // hack: bad address math
#ifdef PORT
                    credits = (CreditsEntry*)((uintptr_t)g_ptrStageSetupFile + (s32)intro_credits->unk04);
#else
                    credits = (CreditsEntry*)((s32)g_ptrStageSetupFile + (s32)intro_credits->unk04);
#endif
                    credits_pointer = credits;

                    // what is the point of this?
                    while (credits->TextId1 != 0 || credits->TextId2 != 0)
                    {
                        credits++;
                    }

#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroCredits));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroCredits));
#endif
                }
                break;

                default:
                {
                    #ifdef DEBUG
                        ossyncprintf("unknown bondstart type %d!\n",intro_record->type);
                    #endif
#ifdef PORT
                    intro_record = (struct SetupIntroEmpty*)((uintptr_t)intro_record + sizeof(struct SetupIntroEmpty));
#else
                    intro_record = (struct SetupIntroEmpty*)((s32)intro_record + sizeof(struct SetupIntroEmpty));
#endif
                }
                break;

            }
        }
    }


    bondinvAddInvItem(ITEM_FIST);
#ifdef PORT
    /* TEMP D303 (M-201): test-only start weapon, GE_STARTWEAPON=<ITEM_IDS>,
     * so headless repros can hold a specific gun. Inert unset. */
    {
        extern char *getenv(const char *name);
        extern int atoi(const char *s);
        const char *sw = getenv("GE_STARTWEAPON");
        if (sw) {
            bondinvAddInvItem(atoi(sw));
            {
                extern void add_ammo_to_weapon(ITEM_IDS weapon, s32 ammo);
                add_ammo_to_weapon(atoi(sw), 400);
            }
            starting_weapon[GUNRIGHT] = atoi(sw);
            set_starting_weapon = 1;
        }
    }
#endif

    if (set_starting_weapon == 0)
    {
        starting_weapon[GUNRIGHT] = ITEM_FIST;
    }

    g_CurrentPlayer->field_78 = FLOAT_INIT;
    g_CurrentPlayer->field_7C = -0.0001f;
    g_CurrentPlayer->field_80 = FLOAT_INIT;

    if (startpadcount > 0)
    {
        if ((GE_RULE_MULTI()) && (startpadcount > 0))
        {
            rand_pad_index = bondviewGetRandomSpawnPadIndex();
        }
        else
        {
            rand_pad_index = 0;
        }

#ifdef DEBUG
        assert(g_Startpad[rand_pad_index]->stan); //              (".\\ported\\bondview_r.cpp",0x171,"Assertion failed: g_Startpad[sp]->stan");
#endif

        start_pos.f[0] = g_Startpad[rand_pad_index]->pos.f[0];
        start_pos.f[2] = g_Startpad[rand_pad_index]->pos.f[2];


        start_stan = g_Startpad[rand_pad_index]->stan;
        GE_COOP_SPAWN_OFFSET(&start_pos.f[0], &start_pos.f[2], &start_stan, g_Startpad[rand_pad_index]->look.f[0], g_Startpad[rand_pad_index]->look.f[2]);

        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->eyeheight + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
        start_look_angle = M_TAU_F - atan2f(g_Startpad[rand_pad_index]->look.f[0], g_Startpad[rand_pad_index]->look.f[2]);
    }
    else
    {
        start_stan = sub_GAME_7F0AFB78(&start_pos.f[0], &start_pos.f[1], &start_pos.f[2], 30.0f);
        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->eyeheight + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
    }

    g_CurrentPlayer->vv_theta = (start_look_angle * 360.0f) / M_TAU_F;
    g_CurrentPlayer->stanHeight = stan_height;
    g_CurrentPlayer->field_6C = stan_height / FIELD_6C_FACTOR;
    change_player_pos_to_target(&g_CurrentPlayer->field_488, &start_pos, start_stan);
    g_CurrentPlayer->field_488.theta_transform.f[0] = -sinf(start_look_angle);
    g_CurrentPlayer->field_488.theta_transform.f[1] = FLOAT_INIT;
    g_CurrentPlayer->field_488.theta_transform.f[2] = cosf(start_look_angle);
    sub_GAME_7F089718(D_800364D0);
    dword_CODE_bss_80079DA0 = 0;


    for (i=0; i<BSS_80079DA8_LENGTH; i++)
    {
        dword_CODE_bss_80079DA4 = 0;
        dword_CODE_bss_80079DA8[i] = 0;
    }

    bondviewResetIntroCameraMessageDialogs();
    bondviewResetUpperTextDisplay();
    g_CurrentPlayer->prop = chrpropAllocate();
    g_CurrentPlayer->prop->obj = NULL;
    g_CurrentPlayer->prop->type = PROP_TYPE_VIEWER;

    g_CurrentPlayer->prop->pos.f[0] =
        g_CurrentPlayer->bondprevpos.f[0] = start_pos.f[0];

    g_CurrentPlayer->prop->pos.f[1] =
        g_CurrentPlayer->bondprevpos.f[1] = start_pos.f[1];

    g_CurrentPlayer->prop->pos.f[2] =
        g_CurrentPlayer->bondprevpos.f[2] = start_pos.f[2];

    g_CurrentPlayer->prop->stan = start_stan;

    chrpropActivate(g_CurrentPlayer->prop);
    chrpropEnable(g_CurrentPlayer->prop);
    g_CurrentPlayer->field_3B8.f[0] = (g_CurrentPlayer->field_488.pos.f[0] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[1] = (g_CurrentPlayer->field_488.pos.f[1] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[2] = (g_CurrentPlayer->field_488.pos.f[2] / FIELD_3B8_FACTOR);

    if (getPlayerCount() == 1)
    {
        bondviewSetCameraMode(CAMERAMODE_INTRO);
    }
    else
    {
        bondviewSetCameraMode(CAMERAMODE_MP);
    }

    g_bondviewBondDeathAnimationsCount = 0;
    while (g_bondviewBondDeathAnimations[g_bondviewBondDeathAnimationsCount] != 0)
    {
        g_bondviewBondDeathAnimationsCount++;
    }

    g_CurrentPlayer->startnewbonddie = TRUE;
    g_CurrentPlayer->redbloodfinished = FALSE;
    g_CurrentPlayer->deathanimfinished = FALSE;
    camera_mode = CAMERAMODE_NONE;
}
