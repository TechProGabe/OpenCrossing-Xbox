#include "ef_effect_control.h"
#include "m_common_data.h"
#include "m_player_lib.h"

static void eHanabiSwitch_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eHanabiSwitch_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eHanabiSwitch_mv(eEC_Effect_c* effect, GAME* game);
static void eHanabiSwitch_dw(eEC_Effect_c* effect, GAME* game);

#define EFFECT_LIFETIME 300

eEC_PROFILE_c iam_ef_hanabi_switch = {
    // clang-format off
    &eHanabiSwitch_init,
    &eHanabiSwitch_ct,
    &eHanabiSwitch_mv,
    &eHanabiSwitch_dw,
    eEC_IMMEDIATE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_IGNORE_DEATH_DIST,
    // clang-format on
};

static void eHanabiSwitch_SearchLakePos(xyz_t* pos) {
    int bx, bz;
    xyz_t a = ZeroVec;
    *pos = ZeroVec;
    if (mFI_BlockKind2BkNum(&bx, &bz, mRF_BLOCKKIND_POOL) && mFI_BkNum2WposXZ(&a.x, &a.z, bx, bz)) {
        a.x += 320.f;
        a.y = 0.f;
        a.z += 320.f;
        pos->x = a.x;
        pos->y = mFI_BkNum2BaseHeight(bx, bz) + 20.f;
        pos->z = a.z;
    }
}

static void eHanabiSwitch_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_HANABI_SWITCH, pos, NULL, game, NULL, item_name, prio, 0, 0);
}

static void eHanabiSwitch_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->timer = 300;
    eHanabiSwitch_SearchLakePos(&effect->position);
    effect->offset = effect->position;
    effect->effect_specific[0] = 0; /* phase: 0 = pre-init, 1 = pos-captured, 2 = set-spawned */
}

static void eHanabiSwitch_mv(eEC_Effect_c* effect, GAME* game) {
    eEC_CLIP->set_continious_env_proc(effect, 300, 300);
    if (mEv_CheckTitleDemo() != mEv_TITLEDEMO_STAFFROLL) {
        f32 t = (f32)EFFECT_LIFETIME - effect->lifetime;
        if (effect->effect_specific[0] < 1 && eEC_CLIP->check_lookat_block_proc(effect->position)) {
            effect->offset = effect->position;
            effect->effect_specific[0] = 1;
        }
        if (effect->effect_specific[0] < 2 && t >= 40.0f) {
            effect->effect_specific[0] = 2;
            eEC_CLIP->effect_make_proc(eEC_EFFECT_HANABI_SET, effect->position, effect->prio, 0, game,
                                       (mActor_name_t)effect->item_name, 0, 0);
        }
        if (t < 240.0f && eEC_CLIP->check_lookat_block_proc(effect->position)) {
            effect->offset.y =
                eEL_CalcAdjust_F(t, 0.0f, 40.0f, effect->position.y, effect->position.y + 200.f);
            NPC_CLIP->set_attention_request_proc(aNPC_ATTENTION_TYPE_POSITION, NULL, &effect->offset);
        }
    }
}

static void eHanabiSwitch_dw(eEC_Effect_c* effect, GAME* game) {
    return;
}
