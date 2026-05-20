#include "ef_effect_control.h"
#include "m_common_data.h"
#include "m_rcp.h"
#include "sys_matrix.h"

static void eGM_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eGM_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eGM_mv(eEC_Effect_c* effect, GAME* game);
static void eGM_dw(eEC_Effect_c* effect, GAME* game);

#define EFFECT_LIFETIME 72
#define EFFECT_STAGE2 64
#define EFFECT_STAGE1 50

// clang-format off
eEC_PROFILE_c iam_ef_gimonhu = {
    &eGM_init,
    &eGM_ct,
    &eGM_mv,
    &eGM_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
};

f32 eGM_scale_data[][2] = { 
    { 0.5f, 0.5f },
    { 0.5f, 1.2f },
    { 0.5f, 2.f },
    { 1.2f, 1.4f },
    { 2.f, 0.7f },
    { 1.5f, 0.8f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f },
    { 1.f, 1.f }
};

extern Gfx ef_gimonhu01_00_modelT[];

// clang-format on
static void eGM_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    const float delta = 9.58738e-05f; // not sure about this constant, close to 1e-4, but not quite
    xyz_t p = { 0.f, 15.f, 7.f };
    sMath_RotateY(&p, angle * delta);
    pos.x += p.x;
    pos.y += p.y;
    eEC_CLIP->make_effect_proc(eEC_EFFECT_GIMONHU, pos, NULL, game, NULL, item_name, prio, 0, 0);
}

static void eGM_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->timer = EFFECT_LIFETIME;
    effect->offset.x = effect->offset.y = effect->offset.z = 0.f;
    effect->effect_specific[0] = 0; /* SE-played flag */
}

static void eGM_mv(eEC_Effect_c* effect, GAME* game) {
    if (effect->effect_specific[0] == 0) {
        sAdo_OngenTrgStart(NA_SE_2F, &effect->position);
        effect->effect_specific[0] = 1;
    }
}

static void eGM_dw(eEC_Effect_c* effect, GAME* game) {
    f32 t = (f32)EFFECT_LIFETIME - effect->lifetime;
    f32 t_clamped = (t > (f32)EFFECT_STAGE1) ? (f32)EFFECT_STAGE1 : t;
    f32 k = t_clamped * 0.5f;
    int max_idx = EFFECT_STAGE1 / 2;
    int i, j;
    f32 frac;
    int v = (int)eEL_CalcAdjust_F(t, (f32)EFFECT_STAGE2, (f32)EFFECT_LIFETIME, 255.f, 0.f);

    if (k < 0.0f) k = 0.0f;
    if (k > (f32)max_idx) k = (f32)max_idx;
    i = (int)k;
    if (i > max_idx - 1) i = max_idx - 1;
    j = i + 1;
    frac = k - (f32)i;
    effect->scale.x = (eGM_scale_data[i][0] + (eGM_scale_data[j][0] - eGM_scale_data[i][0]) * frac) * 0.008f;
    effect->scale.y = (eGM_scale_data[i][1] + (eGM_scale_data[j][1] - eGM_scale_data[i][1]) * frac) * 0.008f;
    effect->scale.z = 0.008f;
    OPEN_DISP(game->graph);
    _texture_z_light_fog_prim_xlu(game->graph);
    eEC_CLIP->auto_matrix_xlu_offset_proc(game, &effect->position, &effect->scale, &effect->offset);
    gSPMatrix(NEXT_POLY_XLU_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, 128, 255, 255, 255, (u8)v);
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_gimonhu01_00_modelT);
    CLOSE_DISP(game->graph);
}
