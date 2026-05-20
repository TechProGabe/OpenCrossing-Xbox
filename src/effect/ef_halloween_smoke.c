#include "ef_effect_control.h"

#include "m_common_data.h"

extern Gfx ef_haro01_00_modelT[];
extern u8 ef_dust01_0[];
extern u8 ef_dust01_1[];
extern u8 ef_dust01_2[];
extern u8 ef_dust01_3[];

static u8* eHalloween_texture_table[] = {
    ef_dust01_0,
    ef_dust01_1,
    ef_dust01_2,
    ef_dust01_3,
};

// clang-format off
static u8 eHalloween_2tile_texture_idx[][2] = {
    {0, 0},
    {0, 1},
    {1, 1},
    {1, 2},
    {2, 2},
    {2, 3},
    {3, 3},
    {3, 3},
    {3, 3},
};
// clang-format on

// clang-format off
static u8 eHalloween_prim_f[] = {
    0,
    128,
    255,
    128,
    0,
    128,
    255,
    128,
    0,
};
// clang-format on

static void eHalloween_Smoke_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eHalloween_Smoke_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eHalloween_Smoke_mv(eEC_Effect_c* effect, GAME* game);
static void eHalloween_Smoke_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_halloween_smoke = {
    // clang-format off
    &eHalloween_Smoke_init,
    &eHalloween_Smoke_ct,
    &eHalloween_Smoke_mv,
    &eHalloween_Smoke_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
    // clang-format on
};

static void eHalloween_Smoke_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_HALLOWEEN_SMOKE, pos, NULL, game, &angle, item_name, prio, arg0, arg1);
}

static void eHalloween_Smoke_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    s16 angle = *(s16*)ct_arg;

    effect->timer = 18;

    (effect->scale).x = (effect->scale).y = (effect->scale).z = 0.01f;

    (effect->velocity).y = 0.25f;
    (effect->velocity).x = 0.0f * sin_s(angle);
    (effect->velocity).z = 0.0f * cos_s(angle);

    (effect->acceleration).y = 0.075f;
    (effect->acceleration).x = (effect->acceleration).z = 0.0f;
}

static void eHalloween_Smoke_mv(eEC_Effect_c* effect, GAME* game) {
    f32 dt = (f32)game->graph->dt_num_60fps_frames;
    effect->velocity.x += effect->acceleration.x * dt;
    effect->velocity.y += effect->acceleration.y * dt;
    effect->velocity.z += effect->acceleration.z * dt;
    effect->position.x += effect->velocity.x * dt;
    effect->position.y += effect->velocity.y * dt;
    effect->position.z += effect->velocity.z * dt;
}

static void eHalloween_Smoke_dw(eEC_Effect_c* effect, GAME* game) {
    f32 k = (18.0f - effect->lifetime) * 0.5f;
    int i, j;
    f32 frac;
    int tile_idx0, tile_idx1;
    u8 prim_f;

    if (k < 0.0f) k = 0.0f;
    if (k > 8.0f) k = 8.0f;
    i = (int)k;
    if (i > 8) i = 8;
    j = (i < 8) ? i + 1 : i;
    frac = k - (f32)i;
    tile_idx0 = eHalloween_2tile_texture_idx[i][0];
    tile_idx1 = eHalloween_2tile_texture_idx[i][1];
    prim_f = (u8)(eHalloween_prim_f[i] + (eHalloween_prim_f[j] - eHalloween_prim_f[i]) * frac);

    OPEN_DISP(game->graph);

    eEC_CLIP->auto_matrix_xlu_proc(game, &effect->position, &effect->scale);

    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_1_TXT_SEG, eHalloween_texture_table[tile_idx0]);
    gSPSegment(NEXT_POLY_XLU_DISP, ANIME_2_TXT_SEG, eHalloween_texture_table[tile_idx1]);
    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, prim_f, 200, 0, 255, 120);
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_haro01_00_modelT);

    CLOSE_DISP(game->graph);
}
