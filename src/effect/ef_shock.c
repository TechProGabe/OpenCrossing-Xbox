#include "ef_effect_control.h"

#include "m_common_data.h"

extern Gfx ef_shock01_00_modelT[];

static void eSK_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eSK_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eSK_mv(eEC_Effect_c* effect, GAME* game);
static void eSK_dw(eEC_Effect_c* effect, GAME* game);

// clang-format off
eEC_PROFILE_c iam_ef_shock = {
    &eSK_init,
    &eSK_ct,
    &eSK_mv,
    &eSK_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
};

static rgba_t eSK_prim_table[] = {
    { 255, 0, 0, 200 },
    { 255, 128, 0, 227 },
    { 255, 255, 0, 255 },
    { 255, 191, 0, 191 },
    { 255, 128, 0, 128 },
    { 255, 63, 0, 63 },
    { 255, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
};

static f32 eSK_scale_table[] = {
    0.019f, 0.02375f, 0.028499999f, 0.026124999f, 0.02375f, 0.021374999f, 0.019f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};
// clang-format on

typedef struct {
    xyz_t velocity;
    xyz_t acceleration;
    f32 scale;
} eSK_dt_c;

static void eSK_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    xyz_t ofs;
    eSK_dt_c data;

    data.velocity.x = 0.0f;
    data.velocity.y = 1.0f;
    data.velocity.z = 0.0f;

    data.acceleration.x = 0.0f;
    data.acceleration.y = -0.1f;
    data.acceleration.z = 0.0f;

    data.scale = 1.0f;

    ofs.x = 0.0f;
    ofs.y = 4.0f;
    ofs.z = 6.0f;

    eEC_CLIP->make_effect_proc(eEC_EFFECT_SHOCK, pos, &ofs, game, &data, item_name, prio, 0, 0);
}

static void eSK_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    eSK_dt_c* data = (eSK_dt_c*)ct_arg;

    effect->scale.x = data->scale;
    effect->scale.y = data->scale;
    effect->scale.z = data->scale;

    effect->timer = 14;

    effect->acceleration = data->acceleration;

    effect->velocity = data->velocity;
    effect->effect_specific[0] = 0; /* SE-played flag */
}

static void eSK_mv(eEC_Effect_c* effect, GAME* game) {
    if (effect->effect_specific[0] == 0) {
        sAdo_OngenTrgStart(0x2d, &effect->position);
        effect->effect_specific[0] = 1;
    }
}

static void eSK_dw(eEC_Effect_c* effect, GAME* game) {
    xyz_t* pos = &effect->position;
    xyz_t* scale = &effect->scale;
    xyz_t* ofs = &effect->offset;
    f32 k = (14.0f - effect->lifetime) * 0.5f;
    int i, j;
    f32 frac, s_val;
    if (k < 0.0f) k = 0.0f;
    if (k > 6.0f) k = 6.0f;
    i = (int)k;
    if (i > 6) i = 6;
    j = (i < 6) ? i + 1 : i;
    frac = k - (f32)i;
    s_val = eSK_scale_table[i] + (eSK_scale_table[j] - eSK_scale_table[i]) * frac;

    scale->x = s_val;
    scale->y = s_val;
    scale->z = s_val;

    OPEN_DISP(game->graph);

    eEC_CLIP->auto_matrix_xlu_offset_proc(game, pos, scale, ofs);

    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, 128,
                    (u8)(eSK_prim_table[i].r + (eSK_prim_table[j].r - eSK_prim_table[i].r) * frac),
                    (u8)(eSK_prim_table[i].g + (eSK_prim_table[j].g - eSK_prim_table[i].g) * frac),
                    (u8)(eSK_prim_table[i].b + (eSK_prim_table[j].b - eSK_prim_table[i].b) * frac),
                    (u8)(eSK_prim_table[i].a + (eSK_prim_table[j].a - eSK_prim_table[i].a) * frac));
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_shock01_00_modelT);

    CLOSE_DISP(game->graph);
}
