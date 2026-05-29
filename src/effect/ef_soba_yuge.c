#include "ef_effect_control.h"

#include "m_common_data.h"
#include "m_debug.h"
#include "sys_math3d.h"

extern Gfx ef_dust01_modelT[];
extern Gfx ef_dust01_stew_modelT[];

extern u8 ef_dust01_0[];
extern u8 ef_dust01_1[];
extern u8 ef_dust01_2[];
extern u8 ef_dust01_3[];

static u8* eSoba_Yuge_texture_table[] = {
    ef_dust01_0,
    ef_dust01_1,
    ef_dust01_2,
    ef_dust01_3,
};

static void eSoba_Yuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eSoba_Yuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eSoba_Yuge_mv(eEC_Effect_c* effect, GAME* game);
static void eSoba_Yuge_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_soba_yuge = {
    // clang-format off
    &eSoba_Yuge_init,
    &eSoba_Yuge_ct,
    &eSoba_Yuge_mv,
    &eSoba_Yuge_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_IGNORE_DEATH_DIST,
    // clang-format on
};

typedef struct {
    u8 tex0;
    u8 tex1;
} eSoba_Yuge_2tile_c;

// clang-format off
static eSoba_Yuge_2tile_c eSoba_Yuge_2tile_texture_idx[] = {
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 1},
    {0, 1},
    {0, 1},
    {1, 1},
    {1, 2},
    {1, 2},
    {1, 2},
    {2, 2},
    {2, 3},
    {2, 3},
    {2, 3},
    {3, 3},
    {3, 3},
    {3, 3},
    {3, 3},
    {3, 3},
};

static u8 eSoba_Yuge_prim_f[] = { 
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

static void eSoba_Yuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_SOBA_YUGE, pos, NULL, game, NULL, item_name, prio, arg0, arg1);
}

static void eSoba_Yuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    s16 rand = qrand();

    effect->position.y += GETREG(TAKREG, 0x34) + 1.0f;

    effect->scale.x = effect->scale.y = effect->scale.z = GETREG(TAKREG, 0x37) * 0.0001f + 0.001f;

    effect->timer = 44;

    effect->acceleration = ZeroVec;
    effect->acceleration.y = GETREG(TAKREG, 0x32) * 0.001f + 0.017f;

    effect->velocity = ZeroVec;

    effect->position.x += effect->arg0 * sin_s(rand);
    effect->position.z += effect->arg0 * cos_s(rand);
}

static void eSoba_Yuge_mv(eEC_Effect_c* effect, GAME* game) {
    f32 dt = (f32)game->graph->dt_num_60fps_frames;
    f32 decay = powf(GETREG(TAKREG, 0x39) * 0.001f + 0.95f, dt);
    effect->velocity.x += effect->acceleration.x * dt;
    effect->velocity.y += effect->acceleration.y * dt;
    effect->velocity.z += effect->acceleration.z * dt;
    effect->position.x += effect->velocity.x * dt;
    effect->position.y += effect->velocity.y * dt;
    effect->position.z += effect->velocity.z * dt;
    effect->velocity.y *= decay;
}

static void eSoba_Yuge_dw(eEC_Effect_c* effect, GAME* game) {
    int alpha;
    f32 t = 44.0f - effect->lifetime;
    f32 k = t * 0.5f;
    int i, j;
    f32 frac;
    int texIdx1, texIdx2;
    u8 prim_f;

    if (k < 0.0f) k = 0.0f;
    if (k > 21.0f) k = 21.0f;
    i = (int)k;
    if (i > 21) i = 21;
    j = (i < 21) ? i + 1 : i;
    frac = k - (f32)i;
    texIdx1 = eSoba_Yuge_2tile_texture_idx[i].tex0;
    texIdx2 = eSoba_Yuge_2tile_texture_idx[i].tex1;
    prim_f = (u8)(eSoba_Yuge_prim_f[i] + (eSoba_Yuge_prim_f[j] - eSoba_Yuge_prim_f[i]) * frac);

    if (effect->arg1 == 0) {
        effect->scale.x = eEL_CalcAdjust_F(t, 0.0f, 44.0f, GETREG(TAKREG, 0x37) * 0.0001f + 0.001f,
                                           GETREG(TAKREG, 0x38) * 0.0001f + 0.005f);
    } else {
        effect->scale.x = eEL_CalcAdjust_F(t, 0.0f, 44.0f, 0.001f, 0.01f);
    }
    effect->scale.y = effect->scale.z = effect->scale.x;

    if (effect->arg1 == 0) {
        alpha = (int)eEL_CalcAdjust_F(t, 0.0f, 44.0f, GETREG(TAKREG, 0x35) + 130.0f, GETREG(TAKREG, 0x36) + 10.0f);
    } else {
        alpha = (int)eEL_CalcAdjust_F(t, 0.0f, 44.0f, GETREG(TAKREG, 0x35) + 190.0f, GETREG(TAKREG, 0x36) + 10.0f);
    }

    OPEN_DISP(game->graph);

    eEC_CLIP->auto_matrix_xlu_proc(game, &effect->position, &effect->scale);

    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_1_TXT_SEG, eSoba_Yuge_texture_table[texIdx1]);
    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_2_TXT_SEG, eSoba_Yuge_texture_table[texIdx2]);

    if (effect->arg1 == 0) {
        gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, prim_f, 255, 255, 255, alpha);
    } else {
        gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, prim_f, 255, 200, 130, alpha);
    }

    if (effect->arg1 == 0) {
        gSPDisplayList(NEXT_POLY_XLU_DISP, ef_dust01_modelT);
    } else {
        gSPDisplayList(NEXT_POLY_XLU_DISP, ef_dust01_stew_modelT);
    }

    CLOSE_DISP(game->graph);
}
