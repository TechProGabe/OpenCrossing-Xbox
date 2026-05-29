#include "ef_effect_control.h"
#include "m_common_data.h"
#include "m_debug.h"

// Furo Yuge == Bath Steam

#define EFFECT_STAGE1 41
#define EFFECT_FADEOUT 3
#define EFFECT_LIFETIME (EFFECT_STAGE1 + EFFECT_FADEOUT)

static void eFuro_Yuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eFuro_Yuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eFuro_Yuge_mv(eEC_Effect_c* effect, GAME* game);
static void eFuro_Yuge_dw(eEC_Effect_c* effect, GAME* game);

// clang-format off

extern Gfx ef_dust01_0[], ef_dust01_1[], ef_dust01_2[], ef_dust01_3[], ef_dust01_modelT[];

Gfx* eFuro_Yuge_texture_table[] = { 
    ef_dust01_0, 
    ef_dust01_1, 
    ef_dust01_2, 
    ef_dust01_3 
};

eEC_PROFILE_c iam_ef_furo_yuge = {
    &eFuro_Yuge_init,
    &eFuro_Yuge_ct,
    &eFuro_Yuge_mv,
    &eFuro_Yuge_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_IGNORE_DEATH_DIST,
};

u8 eFuro_Yuge_2tile_texture_idx[][2] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 1 },
    { 0, 1 },
    { 0, 1 },
    { 1, 1 },
    { 1, 2 },
    { 1, 2 },
    { 1, 2 },
    { 2, 2 },
    { 2, 3 },
    { 2, 3 },
    { 2, 3 },
    { 3, 3 },
    { 3, 3 },
    { 3, 3 },
    { 3, 3 },
    { 3, 3 }
};

u8 eFuro_Yuge_prim_f[] = { 
    0,
    0,
    0,
    0,
    0,
    0,
    64,
    128,
    192,
    0,
    64,
    128,
    192,
    0,
    64,
    128,
    192,
    0,
    0,
    0,
    0,
    0
};

// clang-format on

static void eFuro_Yuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_FURO_YUGE, pos, NULL, game, NULL, item_name, prio, arg0, arg1);
}

static void eFuro_Yuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    qrand();
    effect->position.y += GETREG(TAKREG, 0x34) + 1.f;
    effect->scale.x = effect->scale.y = effect->scale.z = GETREG(TAKREG, 0x37) * 0.0001f + 0.001f;
    effect->timer = EFFECT_LIFETIME;
    effect->acceleration = ZeroVec;
    effect->acceleration.y = GETREG(TAKREG, 0x32) * 0.001f + 0.05f;
    effect->velocity = ZeroVec;
    effect->position.x += RANDOM2_F(effect->arg0);
    effect->position.z += RANDOM2_F(effect->arg0);
}

static void eFuro_Yuge_mv(eEC_Effect_c* effect, GAME* game) {
    f32 dt = (f32)game->graph->dt_num_60fps_frames;
    f32 decay = powf(GETREG(TAKREG, 0x39) * 0.001f + 0.9f, dt);
    effect->velocity.x += effect->acceleration.x * dt;
    effect->velocity.y += effect->acceleration.y * dt;
    effect->velocity.z += effect->acceleration.z * dt;
    effect->position.x += effect->velocity.x * dt;
    effect->position.y += effect->velocity.y * dt;
    effect->position.z += effect->velocity.z * dt;
    effect->velocity.y *= decay;
}

static void eFuro_Yuge_dw(eEC_Effect_c* effect, GAME* game) {
    int opacity;
    f32 t = (f32)EFFECT_LIFETIME - effect->lifetime;
    f32 k = t * 0.5f;
    int i, j;
    f32 frac;
    int max_idx = (EFFECT_LIFETIME / 2) - 1;
    int texIdx1, texIdx2;
    u8 prim_f;

    if (k < 0.0f) k = 0.0f;
    if (k > (f32)max_idx) k = (f32)max_idx;
    i = (int)k;
    if (i > max_idx) i = max_idx;
    j = (i < max_idx) ? i + 1 : i;
    frac = k - (f32)i;
    texIdx1 = eFuro_Yuge_2tile_texture_idx[i][0];
    texIdx2 = eFuro_Yuge_2tile_texture_idx[i][1];
    prim_f = (u8)(eFuro_Yuge_prim_f[i] + (eFuro_Yuge_prim_f[j] - eFuro_Yuge_prim_f[i]) * frac);

    effect->scale.x =
        eEL_CalcAdjust_F(t, 0.0f, (f32)EFFECT_LIFETIME, GETREG(TAKREG, 0x37) * 0.0001f + 0.0015f,
                         GETREG(TAKREG, 0x38) * 0.0001f + 0.009f);
    effect->scale.y = effect->scale.z = effect->scale.x;

    if (t < (f32)EFFECT_STAGE1) {
        opacity = (int)eEL_CalcAdjust_F(t, 0.0f, (f32)EFFECT_STAGE1, GETREG(TAKREG, 0x35) + 50.f,
                                        GETREG(TAKREG, 0x36) + 80.f);
    } else {
        opacity = (int)eEL_CalcAdjust_F(t, (f32)EFFECT_STAGE1, (f32)EFFECT_LIFETIME,
                                        GETREG(TAKREG, 0x36) + 80.f, 0.0f);
    }

    OPEN_DISP(game->graph);
    eEC_CLIP->auto_matrix_xlu_proc(game, &effect->position, &effect->scale);
    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_1_TXT_SEG, eFuro_Yuge_texture_table[texIdx1]);
    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_2_TXT_SEG, eFuro_Yuge_texture_table[texIdx2]);
    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, prim_f, 255, 255, 255, opacity);
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_dust01_modelT);
    CLOSE_DISP(game->graph);
}
