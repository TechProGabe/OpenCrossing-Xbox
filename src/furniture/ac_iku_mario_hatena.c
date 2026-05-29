extern u16 int_iku_mario_hatena_a_pal[];
extern u16 int_iku_mario_hatena_b_pal[];
extern u16 int_iku_mario_hatena_c_pal[];

static u16* fIMH_palette_table[] = {
    // clang-format off
	int_iku_mario_hatena_a_pal,
	int_iku_mario_hatena_a_pal,
	int_iku_mario_hatena_a_pal,
	int_iku_mario_hatena_a_pal,
	int_iku_mario_hatena_b_pal,
	int_iku_mario_hatena_c_pal,
	int_iku_mario_hatena_b_pal,
    // clang-format on
};

#define fIMH_PALETTE_FRAMES (8.0f * ARRAY_COUNT(fIMH_palette_table))

extern Gfx int_iku_mario_hatena_model_a_model[];
extern Gfx int_iku_mario_hatena_model_b_model[];

static void fIMH_dw(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    int ctr_ofs = (int)ftr_actor->dynamic_work_f[0];

    OPEN_DISP(game->graph);

    gSPMatrix(NEXT_POLY_OPA_DISP, _Matrix_to_Mtx_new(game->graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_9,
               fIMH_palette_table[(ctr_ofs / 8) % ARRAY_COUNT(fIMH_palette_table)]);
    gSPDisplayList(NEXT_POLY_OPA_DISP, int_iku_mario_hatena_model_b_model);
    gSPDisplayList(NEXT_POLY_OPA_DISP, int_iku_mario_hatena_model_a_model);

    CLOSE_DISP(game->graph);
}

static void fIMH_mv(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    ftr_actor->dynamic_work_f[0] += (f32)game->graph->dt_num_60fps_frames;
    while (ftr_actor->dynamic_work_f[0] >= fIMH_PALETTE_FRAMES) {
        ftr_actor->dynamic_work_f[0] -= fIMH_PALETTE_FRAMES;
    }

    if (aFTR_CAN_PLAY_SE(ftr_actor)) {
        if (ftr_actor->switch_changed_flag == TRUE) {
            sAdo_OngenTrgStart(0x17F, &ftr_actor->position);
        }
    }
}

static aFTR_vtable_c fIMH_func = {
    NULL, &fIMH_mv, &fIMH_dw, NULL, NULL,
};

aFTR_PROFILE iam_iku_mario_hatena = {
    // clang-format off
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	40.0f,
	0.01f,
	aFTR_SHAPE_TYPEA,
	mCoBG_FTR_TYPEA,
	0,
	0,
	0,
	0,
	&fIMH_func,
    // clang-format on
};
