#include "pc_profiler.h"

int g_pc_profile_enabled = 0;
int g_pc_profile_interval = 120;

typedef struct {
    double timers_ms[PC_PROF_TIMER_COUNT];
    int draws;
    int flushes;
    int shader_switches;
    int uniforms;
    int uniform_skips;
    int uniform_lookups;
    int texture_binds;
    int buffer_uploads;
    size_t buffer_upload_bytes;
    int state_changes;
    int dirty_groups[16];
    int vertices;
    int indices;
    int emu64_cmds;
    int emu64_tris;
    int emu64_vtx_cmds;
    int emu64_dl_cmds;
    int cull_visible;
    int cull_rejected;
    double frame_ms;
    int audio_fill;
} PCProfilerFrame;

static PCProfilerFrame s_frame;
static PCProfilerFrame s_accum;
static PCProfilerFrame s_peak;
static int s_frames = 0;
static int s_have_frame = 0;
static Uint64 s_freq = 0;

static const char* s_timer_names[PC_PROF_TIMER_COUNT] = {
    "gx_begin", "dl_replay", "gx_flush", "buf_upload", "uniforms", "uniform_lookup",
    "tex_bind", "shader_switch", "gl_state", "draw_submit", "poll", "swap", "pace",
    "game_logic", "emu64_task", "texobj"
};

static const char* s_dirty_names[16] = {
    "proj", "modelview", "tev_colors", "tev_stages", "swap", "konst", "alpha",
    "lighting", "texgen", "textures", "indirect", "fog", "depth", "color_mask",
    "cull", "blend"
};

static double pc_profiler_ticks_to_ms(Uint64 ticks) {
    if (!s_freq) s_freq = SDL_GetPerformanceFrequency();
    return (double)ticks * 1000.0 / (double)s_freq;
}

void pc_profiler_begin_frame(void) {
    if (!g_pc_profile_enabled) return;
    memset(&s_frame, 0, sizeof(s_frame));
    s_have_frame = 1;
}

Uint64 pc_profiler_begin_timer(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return 0;
    return SDL_GetPerformanceCounter();
}

void pc_profiler_add_time(PCProfilerTimer timer, Uint64 start) {
    if (!g_pc_profile_enabled || !s_have_frame || start == 0 || timer < 0 || timer >= PC_PROF_TIMER_COUNT) return;
    s_frame.timers_ms[timer] += pc_profiler_ticks_to_ms(SDL_GetPerformanceCounter() - start);
}

void pc_profiler_add_count_draw(int vertices, int indices) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.draws++;
    s_frame.vertices += vertices;
    s_frame.indices += indices;
}

void pc_profiler_add_count_flush(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.flushes++;
}

void pc_profiler_add_count_shader_switch(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.shader_switches++;
}

void pc_profiler_add_count_uniform(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.uniforms++;
}

void pc_profiler_add_count_uniform_skip(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.uniform_skips++;
}

void pc_profiler_add_count_uniform_lookup(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.uniform_lookups++;
}

void pc_profiler_add_count_texture_bind(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.texture_binds++;
}

void pc_profiler_add_count_buffer_upload(size_t bytes) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.buffer_uploads++;
    s_frame.buffer_upload_bytes += bytes;
}

void pc_profiler_add_count_state_change(void) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    s_frame.state_changes++;
}

void pc_profiler_add_dirty_mask(unsigned int dirty) {
    if (!g_pc_profile_enabled || !s_have_frame) return;
    for (int i = 0; i < 16; i++) {
        if (dirty & (1u << i)) s_frame.dirty_groups[i]++;
    }
}

static void pc_profiler_accum_frame(void) {
    for (int i = 0; i < PC_PROF_TIMER_COUNT; i++) {
        s_accum.timers_ms[i] += s_frame.timers_ms[i];
    }
    s_accum.draws += s_frame.draws;
    s_accum.flushes += s_frame.flushes;
    s_accum.shader_switches += s_frame.shader_switches;
    s_accum.uniforms += s_frame.uniforms;
    s_accum.uniform_skips += s_frame.uniform_skips;
    s_accum.uniform_lookups += s_frame.uniform_lookups;
    s_accum.texture_binds += s_frame.texture_binds;
    s_accum.buffer_uploads += s_frame.buffer_uploads;
    s_accum.buffer_upload_bytes += s_frame.buffer_upload_bytes;
    s_accum.state_changes += s_frame.state_changes;
    for (int i = 0; i < 16; i++) {
        s_accum.dirty_groups[i] += s_frame.dirty_groups[i];
    }
    s_accum.vertices += s_frame.vertices;
    s_accum.indices += s_frame.indices;
    s_accum.emu64_cmds += s_frame.emu64_cmds;
    s_accum.emu64_tris += s_frame.emu64_tris;
    s_accum.emu64_vtx_cmds += s_frame.emu64_vtx_cmds;
    s_accum.emu64_dl_cmds += s_frame.emu64_dl_cmds;
    s_accum.cull_visible += s_frame.cull_visible;
    s_accum.cull_rejected += s_frame.cull_rejected;
    s_accum.frame_ms += s_frame.frame_ms;
    s_accum.audio_fill += s_frame.audio_fill;

    if (s_frame.frame_ms > s_peak.frame_ms) {
        s_peak = s_frame;
    }
}

static void pc_profiler_print_report(void) {
    double n = (double)s_frames;
    double avg_frame = s_accum.frame_ms / n;
    double fps = avg_frame > 0.0 ? 1000.0 / avg_frame : 0.0;
    double submit_ms = s_accum.timers_ms[PC_PROF_TIMER_GX_FLUSH] / n;
    double poll_ms = s_accum.timers_ms[PC_PROF_TIMER_POLL_EVENTS] / n;
    double swap_ms = s_accum.timers_ms[PC_PROF_TIMER_SWAP] / n;
    double pace_ms = s_accum.timers_ms[PC_PROF_TIMER_PACE] / n;
    double non_submit_ms = avg_frame - submit_ms - poll_ms - swap_ms - pace_ms;

    if (non_submit_ms < 0.0) non_submit_ms = 0.0;

    printf("[PROFILE] frames=%d avg=%.3fms %.1ffps peak=%.3fms draws=%.1f flushes=%.1f verts=%.0f idx=%.0f\n",
           s_frames, avg_frame, fps, s_peak.frame_ms,
           (double)s_accum.draws / n, (double)s_accum.flushes / n,
           (double)s_accum.vertices / n, (double)s_accum.indices / n);
    printf("[PROFILE] cpu_other=%.3fms gx_flush=%.3fms poll=%.3fms swap=%.3fms pace=%.3fms audio_fill=%.0f\n",
           non_submit_ms, submit_ms, poll_ms, swap_ms, pace_ms, (double)s_accum.audio_fill / n);

    printf("[PROFILE] timers");
    for (int i = 0; i < PC_PROF_TIMER_COUNT; i++) {
        printf(" %s=%.3f", s_timer_names[i], s_accum.timers_ms[i] / n);
    }
    printf("\n");

    printf("[PROFILE] gl calls/state per frame: uniforms=%.1f skipped=%.1f lookups=%.1f tex_binds=%.1f buf_uploads=%.1f %.1fKB shader_switch=%.1f state=%.1f cmds=%.1f tris=%.1f vtxcmd=%.1f dl=%.1f cull=%.1f/%.1f\n",
           (double)s_accum.uniforms / n,
           (double)s_accum.uniform_skips / n,
           (double)s_accum.uniform_lookups / n,
           (double)s_accum.texture_binds / n,
           (double)s_accum.buffer_uploads / n,
           (double)s_accum.buffer_upload_bytes / n / 1024.0,
           (double)s_accum.shader_switches / n,
           (double)s_accum.state_changes / n,
           (double)s_accum.emu64_cmds / n,
           (double)s_accum.emu64_tris / n,
           (double)s_accum.emu64_vtx_cmds / n,
           (double)s_accum.emu64_dl_cmds / n,
           (double)s_accum.cull_visible / n,
           (double)s_accum.cull_rejected / n);

    printf("[PROFILE] dirty groups per frame:");
    for (int i = 0; i < 16; i++) {
        double count = (double)s_accum.dirty_groups[i] / n;
        if (count > 0.0) printf(" %s=%.1f", s_dirty_names[i], count);
    }
    printf("\n");
}

void pc_profiler_end_frame(double frame_ms, int audio_fill) {
    if (!g_pc_profile_enabled || !s_have_frame) return;

    s_frame.frame_ms = frame_ms;
    s_frame.audio_fill = audio_fill;
    s_frame.emu64_cmds = pc_emu64_frame_cmds;
    s_frame.emu64_tris = pc_emu64_frame_tri_cmds;
    s_frame.emu64_vtx_cmds = pc_emu64_frame_vtx_cmds;
    s_frame.emu64_dl_cmds = pc_emu64_frame_dl_cmds;
    s_frame.cull_visible = pc_emu64_frame_cull_visible;
    s_frame.cull_rejected = pc_emu64_frame_cull_rejected;
    pc_profiler_accum_frame();
    s_frames++;
    s_have_frame = 0;

    if (s_frames >= g_pc_profile_interval) {
        pc_profiler_print_report();
        memset(&s_accum, 0, sizeof(s_accum));
        memset(&s_peak, 0, sizeof(s_peak));
        s_frames = 0;
    }
}
