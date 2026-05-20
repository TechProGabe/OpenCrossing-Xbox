#include "ef_effect_control.h"
#include "m_common_data.h"

static void eASC_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1);
static void eASC_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg);
static void eASC_mv(eEC_Effect_c* effect, GAME* game);
static void eASC_dw(eEC_Effect_c* effect, GAME* game);

eEC_PROFILE_c iam_ef_ase_ch = {
    // clang-format off
    &eASC_init,
    &eASC_ct,
    &eASC_mv,
    &eASC_dw,
    eEC_IMMEDIATE_DEATH,
    eEC_NO_CHILD_ID,
    eEC_DEFAULT_DEATH_DIST,
    // clang-format on
};
extern Gfx ef_ase01_00_modelT[];

static f32 eASC_scale_table[] = {
    0.00027000002f, 0.00027000002f, 0.00027000002f, 0.00054000004f, 0.0010800001f,0.0016200001f,
    0.0021600001f,0.0027f,0.00135f,0.00054000004f,
};

static void eASC_DecideASCeDirect(xyz_t* vec){
    eEC_CLIP->random_first_speed_proc(vec,2.7f, 180.0f, 0.0f);
} 

static void eASC_SetAseGravity(xyz_t* acc) {
    acc->x = 0.0f;
    acc->y = -0.057499997f;
    acc->z = 0.0f;
}

static void eASC_init(xyz_t pos, int prio, s16 angle, GAME* game, u16 item_name, s16 arg0, s16 arg1) {
    eEC_CLIP->make_effect_proc(eEC_EFFECT_ASE_CH,pos,NULL,game,NULL,item_name,prio,0,0);
}

static void eASC_ct(eEC_Effect_c* effect, GAME* game, void* ct_arg) {
    effect->timer = 20;
    eASC_DecideASCeDirect(&effect->velocity);
    eASC_SetAseGravity(&effect->acceleration);
    effect->scale.x = 0.0027f;
    effect->scale.y = 0.0027f;   
    effect->scale.z = 0.0027f;

}

static void eASC_mv(eEC_Effect_c* effect, GAME* game) {
    f32 dt = (f32)game->graph->dt_num_60fps_frames;
    effect->velocity.x += effect->acceleration.x * dt;
    effect->velocity.y += effect->acceleration.y * dt;
    effect->velocity.z += effect->acceleration.z * dt;
    effect->position.x += effect->velocity.x * dt;
    effect->position.y += effect->velocity.y * dt;
    effect->position.z += effect->velocity.z * dt;
}

static void eASC_dw(eEC_Effect_c* effect, GAME* game) {
    xyz_t* pos = &effect->position;
    xyz_t* scale = &effect->scale;
    f32 k = (20.0f - effect->lifetime) * 0.5f;
    int i, j;
    f32 frac, s;
    GRAPH* graph = game->graph;

    if (k < 0.0f) k = 0.0f;
    if (k > 9.0f) k = 9.0f;
    i = (int)k;
    if (i > 9) i = 9;
    j = (i < 9) ? i + 1 : i;
    frac = k - (f32)i;
    s = eASC_scale_table[i] + (eASC_scale_table[j] - eASC_scale_table[i]) * frac;

    effect->scale.x = s;
    effect->scale.y = s;
    effect->scale.z = s;
    
    OPEN_DISP(game->graph);
    
    eEC_CLIP->auto_matrix_xlu_proc(game, pos, scale); 
    gSPDisplayList(NEXT_POLY_XLU_DISP, ef_ase01_00_modelT);

    CLOSE_DISP(game->graph);
}
