#include "ef_effect_control.h"

#include "m_common_data.h"

static void eNebo_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eNebo_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eNebo_mv(eEC_Effect_c* effect, GAME* game);
static void eNebo_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_neboke = {
    // clang-format off
    &eNebo_init,
    &eNebo_ct,
    &eNebo_mv,
    &eNebo_dw,
    eEC_IMMEDIATE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
    // clang-format on
};

static void eNebo_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_NEBOKE, pos, NULL, game, &angle, item_name, prio, 0, 0);
}

static void eNebo_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->timer = 112;
    effect->effect_specific[0] = *(s16*)ct_arg;
    effect->effect_specific[1] = 0; /* phase counter for NORMAL state */
    effect->effect_specific[2] = 0; /* phase counter for CONTINUOUS state */
}

static void eNebo_mv(eEC_Effect_c* effect, GAME* game) {
    f32 t;

    eEC_CLIP->set_continious_env_proc(effect, 112, 120);

    if (effect->state == eEC_STATE_NORMAL) {
        t = 112.0f - effect->lifetime;
        if (effect->effect_specific[1] < 1 && t >= 16.0f) {
            eEC_CLIP->effect_make_proc(eEC_EFFECT_NEBOKE_AKUBI, effect->position, effect->prio,
                                       effect->effect_specific[0], game, (u16)effect->item_name, 0, 0);
            effect->effect_specific[1] = 1;
        }
        if (effect->effect_specific[1] < 2 && t >= 44.0f) {
            eEC_CLIP->effect_make_proc(eEC_EFFECT_NEBOKE_AKUBI, effect->position, effect->prio,
                                       effect->effect_specific[0], game, (u16)effect->item_name, 1, 0);
            effect->effect_specific[1] = 2;
        }
    } else if (effect->state == eEC_STATE_CONTINUOUS) {
        t = 120.0f - effect->lifetime;
        if (effect->effect_specific[2] < 1 && t >= 46.0f) {
            eEC_CLIP->effect_make_proc(eEC_EFFECT_NEBOKE_AWA, effect->position, effect->prio,
                                       effect->effect_specific[0], game, (u16)effect->item_name, 1, 0);
            effect->effect_specific[2] = 1;
        }
        if (effect->effect_specific[2] < 2 && t >= 74.0f) {
            eEC_CLIP->effect_make_proc(eEC_EFFECT_NEBOKE_AWA, effect->position, effect->prio,
                                       effect->effect_specific[0], game, (u16)effect->item_name, 1, 0);
            effect->effect_specific[2] = 2;
        }
    }
}

static void eNebo_dw(eEC_Effect_c* effect, GAME* game) {
    return;
}
