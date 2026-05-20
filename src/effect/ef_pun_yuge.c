#include "ef_effect_control.h"

#include "m_common_data.h"
#include "m_rcp.h"
#include "sys_matrix.h"

extern u8 ef_pun01_0_int_i4[];
extern u8 ef_pun01_1_int_i4[];
extern u8 ef_pun01_2_int_i4[];
extern u8 ef_pun01_3_int_i4[];
extern u8 ef_pun01_4_int_i4[];
extern Gfx ef_pun01_00_modelT[];

static u8* ePunYuge_yuge_texture_table[] = {
    ef_pun01_0_int_i4, ef_pun01_1_int_i4, ef_pun01_2_int_i4, ef_pun01_3_int_i4, ef_pun01_4_int_i4,
};

static void ePunYuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void ePunYuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void ePunYuge_mv(eEC_Effect_c* effect, GAME* game);
static void ePunYuge_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_pun_yuge = {
    // clang-format off
    &ePunYuge_init,
    &ePunYuge_ct,
    &ePunYuge_mv,
    &ePunYuge_dw,
    eEC_IGNORE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
    // clang-format on
};

static void ePunYuge_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    pos.y += 23.0f;
    eEC_CLIP->make_effect_proc(eEC_EFFECT_PUN_YUGE, pos, NULL, game, &arg0, item_name, prio, 0, 0);
}

static void ePunYuge_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->effect_specific[1] = *(s16*)ct_arg;
    effect->effect_specific[0] = eEC_CLIP->random_first_speed_proc(NULL, 1.5f, 16.0f, 0.0f);
    effect->offset.z = 10.0f;
    effect->timer = 26;
    sAdo_OngenTrgStart(NA_SE_PUN_YUGE, &effect->position);
}

static void ePunYuge_mv(eEC_Effect_c* effect, GAME* game) {
    f32 dt = (f32)game->graph->dt_num_60fps_frames;
    f32 t = 26.0f - effect->lifetime;

    if (t < 4.0f) {
        effect->offset.y += 1.5f * dt;
    }
}

typedef struct pun_yuge_tex_anim_s {
    u8 tex0;
    u8 tex1;
    u8 prim_f;
    u8 unused;
} ePunYuge_tex_anim_c;

static ePunYuge_tex_anim_c ePunYuge_texture_anime_idx[] = {
    { 0, 0, 0, 0 },   { 0, 0, 0, 0 }, { 0, 0, 0, 0 },   { 0, 1, 127, 0 }, { 1, 1, 255, 0 },
    { 1, 2, 127, 0 }, { 2, 2, 0, 0 }, { 2, 3, 127, 0 }, { 3, 3, 255, 0 }, { 3, 4, 127, 0 },
    { 4, 4, 0, 0 },   { 4, 4, 0, 0 }, { 4, 4, 0, 0 },
};

static u8 ePunYuge_prim_f_table[] = {
    0, 0, 0, 127, 255, 127, 0, 127, 255, 127, 0, 0, 0,
};

static void ePunYuge_dw(eEC_Effect_c* effect, GAME* game) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    f32 t = 26.0f - effect->lifetime;
    f32 k = t * 0.5f;
    int i, j;
    f32 frac;
    int anime_idx0, anime_idx1;
    int prim_f, prim_gb, prim_a, env_gb;

    if (k < 0.0f) k = 0.0f;
    if (k > 12.0f) k = 12.0f;
    i = (int)k;
    if (i > 12) i = 12;
    j = (i < 12) ? i + 1 : i;
    frac = k - (f32)i;
    anime_idx0 = ePunYuge_texture_anime_idx[i].tex0;
    anime_idx1 = ePunYuge_texture_anime_idx[i].tex1;
    prim_f = (int)(ePunYuge_prim_f_table[i] + (ePunYuge_prim_f_table[j] - ePunYuge_prim_f_table[i]) * frac);
    prim_gb = (u8)eEL_CalcAdjust_F(t, 0.0f, 8.0f, 200.0f, 255.0f);
    prim_a = (u8)eEL_CalcAdjust_F(t, 12.0f, 26.0f, 255.0f, 0.0f);
    env_gb = (u8)eEL_CalcAdjust_F(t, 0.0f, 8.0f, 0.0f, 255.0f);

    if (t <= 8.0f) {
        static const f32 scale_y_table[5] = { 0.00595f, 0.00833f, 0.014161f, 0.00833f, 0.00595f };
        f32 ks = (k < 4.0f) ? k : 4.0f;
        int si = (int)ks;
        f32 sf = ks - (f32)si;
        int sj = (si < 4) ? si + 1 : si;

        effect->scale.y = scale_y_table[si] + (scale_y_table[sj] - scale_y_table[si]) * sf;
        effect->scale.x = 0.00595f;
    } else {
        effect->scale.x = eEL_CalcAdjust_F(t, 10.0f, 26.0f, 0.00595f, 0.0119f);
        effect->scale.y = effect->scale.x;
    }

    OPEN_DISP(game->graph);

    _texture_z_light_fog_prim_xlu(game->graph);
    Matrix_translate(effect->position.x, effect->position.y, effect->position.z, MTX_LOAD);
    Matrix_mult(&play->billboard_matrix, MTX_MULT);
    Matrix_RotateZ(effect->effect_specific[0], MTX_MULT);
    Matrix_translate(effect->offset.x, effect->offset.y, effect->offset.z, MTX_MULT);
    Matrix_scale(effect->scale.x, effect->scale.y, effect->scale.z, MTX_MULT);

    gSPMatrix(NEXT_POLY_XLU_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPSegment(NEXT_POLY_XLU_DISP, G_MWO_SEGMENT_8, ePunYuge_yuge_texture_table[anime_idx0]);
    gSPSegment(NEXT_POLY_XLU_DISP, G_MWO_SEGMENT_9, ePunYuge_yuge_texture_table[anime_idx1]);
    gDPSetPrimColor(NEXT_POLY_XLU_DISP, 0, (u8)prim_f, 255, (u8)prim_gb, (u8)prim_gb, (u8)prim_a);
    gDPSetEnvColor(NEXT_POLY_XLU_DISP, 255, (u8)env_gb, (u8)env_gb, 255);
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_pun01_00_modelT);

    CLOSE_DISP(game->graph);
}
