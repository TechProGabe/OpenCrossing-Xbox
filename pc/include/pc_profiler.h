/* pc_profiler.h - low-overhead frame profiler for PC performance work */
#ifndef PC_PROFILER_H
#define PC_PROFILER_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_PROF_TIMER_GX_BEGIN,
    PC_PROF_TIMER_DISPLAY_LIST,
    PC_PROF_TIMER_GX_FLUSH,
    PC_PROF_TIMER_BUFFER_UPLOAD,
    PC_PROF_TIMER_UNIFORM_UPLOAD,
    PC_PROF_TIMER_UNIFORM_LOOKUP,
    PC_PROF_TIMER_TEXTURE_BIND,
    PC_PROF_TIMER_SHADER_SWITCH,
    PC_PROF_TIMER_GL_STATE,
    PC_PROF_TIMER_DRAW_SUBMIT,
    PC_PROF_TIMER_POLL_EVENTS,
    PC_PROF_TIMER_SWAP,
    PC_PROF_TIMER_PACE,
    PC_PROF_TIMER_GAME_LOGIC,
    PC_PROF_TIMER_EMU64,
    PC_PROF_TIMER_TEXOBJ,
    PC_PROF_TIMER_DRAW_FINISH,
    PC_PROF_TIMER_AUDIO_FRAME,
    PC_PROF_TIMER_JW_FRAME,
    PC_PROF_TIMER_EFB_COPY,
    PC_PROF_TIMER_COUNT
} PCProfilerTimer;

extern int g_pc_profile_enabled;
extern int g_pc_profile_interval;

void pc_profiler_begin_frame(void);
Uint64 pc_profiler_begin_timer(void);
void pc_profiler_add_time(PCProfilerTimer timer, Uint64 start);
void pc_profiler_add_count_draw(int vertices, int indices);
void pc_profiler_add_count_flush(void);
void pc_profiler_add_count_shader_switch(void);
void pc_profiler_add_count_uniform(void);
void pc_profiler_add_count_uniform_skip(void);
void pc_profiler_add_count_uniform_lookup(void);
void pc_profiler_add_count_texture_bind(void);
void pc_profiler_add_count_buffer_upload(size_t bytes);
void pc_profiler_add_count_state_change(void);
void pc_profiler_add_dirty_mask(unsigned int dirty);
void pc_profiler_end_frame(double frame_ms, int audio_fill);

#ifdef __cplusplus
}
#endif

#endif /* PC_PROFILER_H */
