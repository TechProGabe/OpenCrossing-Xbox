#include "ef_effect_control.h"

#include "m_common_data.h"

extern Gfx ef_hirameki01_den_modelT[];

static void eHiramekiD_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eHiramekiD_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eHiramekiD_mv(eEC_Effect_c* effect, GAME* game);
static void eHiramekiD_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_hirameki_den = {
    // clang-format off
    &eHiramekiD_init,
    &eHiramekiD_ct,
    &eHiramekiD_mv,
    &eHiramekiD_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
    // clang-format on
};

static u8 eHiramekiD_01f_primR_envRG[2] = { 0, 255 };
static u8 eHiramekiD_01f_primGB[2] = { 100, 255 };

static void eHiramekiD_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    if (eEC_CLIP != NULL) {
        eEC_CLIP->effect_make_proc(eEC_EFFECT_HIRAMEKI_HIKARI, pos, prio, angle, game, item_name, arg0, arg1);
    }

    pos.y += 24.0f;
    eEC_CLIP->make_effect_proc(eEC_EFFECT_HIRAMEKI_DEN, pos, NULL, game, NULL, item_name, prio, 0, 0);
}

static void eHiramekiD_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->timer = 72;
    effect->offset.x = effect->offset.y = effect->offset.z = 0.0f;
    effect->scale.x = effect->scale.y = effect->scale.z = 0.007f;
    effect->effect_specific[0] = 0; /* sfx-fired flag */
}

static void eHiramekiD_mv(eEC_Effect_c* effect, GAME* game) {
    if (effect->effect_specific[0] == 0) {
        sAdo_OngenTrgStart(0x2E, &effect->position);
        effect->effect_specific[0] = 1;
    }
}

static void eHiramekiD_dw(eEC_Effect_c* effect, GAME* game) {
    xyz_t* position = &effect->position;
    xyz_t* offset = &effect->offset;
    xyz_t* scale = &effect->scale;
    f32 t = 72.0f - effect->lifetime;
    u8 prim_r, prim_g, prim_b, prim_a;
    u8 env_r, env_g;

    prim_a = (int)eEL_CalcAdjust_F(t, 64.0f, 72.0f, 255.0f, 0.0f);

    if (t < 4.0f) {
        f32 k = t * 0.5f;
        if (k < 0.0f) k = 0.0f;
        if (k > 1.0f) k = 1.0f;
        {
            int i = (int)k;
            if (i > 1) i = 1;
            int j = (i < 1) ? i + 1 : i;
            f32 frac = k - (f32)i;
            u8 p_r = (u8)(eHiramekiD_01f_primR_envRG[i] + (eHiramekiD_01f_primR_envRG[j] - eHiramekiD_01f_primR_envRG[i]) * frac);
            u8 p_gb = (u8)(eHiramekiD_01f_primGB[i] + (eHiramekiD_01f_primGB[j] - eHiramekiD_01f_primGB[i]) * frac);
            prim_r = p_r;
            env_r = p_r;
            prim_g = p_gb;
            prim_b = p_gb;
            env_g = p_r;
        }
    } else {
        prim_r = 255;
        prim_g = 255;
        prim_b = 100;
        env_r = 100;
        env_g = 50;
    }

    OPEN_DISP(game->graph);

    eEC_CLIP->auto_matrix_xlu_offset_proc(game, position, scale, offset);
    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, 255, prim_r, prim_g, prim_b, prim_a);
    gDPSetEnvColor(NEXT_POLY_XLU_DISP, env_r, env_g, 0, 255);
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_hirameki01_den_modelT);

    CLOSE_DISP(game->graph);
}
