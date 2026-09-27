/* xbox_gx_tev.c — replaces pc/src/pc_gx_tev.c (TEV -> GLSL generator; there is
 * no GLSL on nxdk). pc_gx.c still asks for a "variant" per draw; until the NV2A
 * backend owns the draw path, hand back one variant that is never compiled. */
#include "pc_gx_internal.h"

int g_pc_uber_shader_only = 1;

static PCGXShaderVariant s_one;

void pc_gx_tev_seq_reset(void) {
    memset(s_one.uploaded_seq, 0xFF, sizeof(s_one.uploaded_seq));
    memset(g_gx.group_seq, 0, sizeof(g_gx.group_seq));
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) { return &s_one; }

void pc_gx_tev_init(void) {
    memset(&s_one, 0, sizeof s_one);
    s_one.used = 1;
    s_one.prog = 1;
    pc_gx_cache_uniform_locations(s_one.prog, &s_one.uloc);
    memset(s_one.uploaded_seq, 0xFF, sizeof(s_one.uploaded_seq));
}

void pc_gx_tev_shutdown(void) {}
