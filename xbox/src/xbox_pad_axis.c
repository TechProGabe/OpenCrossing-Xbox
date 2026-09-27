/* xbox_pad_axis.c — stick reads for pc_pad.c on real hardware.
 *
 * pc_pad.c is compiled with SDL_GameControllerGetAxis renamed to
 * xbox_controller_axis (xbox/CMakeLists.txt), so pc/ stays untouched.
 *
 * First hardware playtest: "joystick keeps going in random directions
 * sometimes". nxdk's SDL xbox joystick driver copies each XID report into a
 * shared buffer from the USB completion path with no lock against the reader
 * (SDL_xboxjoystick.c int_read_callback / SDL_XBOX_JoystickUpdate), so a read
 * can mix two reports. Whatever the source, a single-frame spike is filtered
 * here: each axis returns the median of its last three reads (PADRead reads
 * each axis once per frame, so +1 frame of latency). Kill switch:
 * -DXBOX_PAD_MEDIAN=0.
 *
 * Diagnostics: a left-stick swing of more than ~120 degrees in one frame while
 * held past half tilt is logged with the raw values to
 * E:\UDATA\4f430001\input.log (first 64 events), so the next playtest says
 * whether spikes are the cause. */
#include <windows.h>
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include "xbox_io.h"

#ifndef XBOX_PAD_MEDIAN
#define XBOX_PAD_MEDIAN 1
#endif

void xbox_flush_file(HANDLE h);
unsigned int xbox_frame_count(void);

static Sint16 s_hist[SDL_CONTROLLER_AXIS_MAX][3];
static int s_n[SDL_CONTROLLER_AXIS_MAX];
static Sint16 s_prev_lx, s_prev_ly, s_raw_lx;
static int s_events;

static Sint16 med3(Sint16 a, Sint16 b, Sint16 c) {
    if (a > b) { Sint16 t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}

static void log_swing(Sint16 lx, Sint16 ly) {
    static HANDLE h = INVALID_HANDLE_VALUE;
    char line[160];
    DWORD w;
    int n;
    if (s_events >= 64) return;
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileA(XBOX_UDATA_DIR "input.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    n = snprintf(line, sizeof line, "frame %u: left stick %d,%d -> %d,%d (raw, pre-filter)\r\n", xbox_frame_count(),
                 s_prev_lx, s_prev_ly, lx, ly);
    WriteFile(h, line, (DWORD)n, &w, NULL);
    xbox_flush_file(h);
    s_events++;
}

static void check_swing(Sint16 lx, Sint16 ly) {
    const float half = 16384.0f;
    float m0 = sqrtf((float)s_prev_lx * s_prev_lx + (float)s_prev_ly * s_prev_ly);
    float m1 = sqrtf((float)lx * lx + (float)ly * ly);
    if (m0 > half && m1 > half) {
        float dot = ((float)s_prev_lx * lx + (float)s_prev_ly * ly) / (m0 * m1);
        if (dot < -0.5f) log_swing(lx, ly);   /* > 120 degrees in one frame */
    }
    s_prev_lx = lx;
    s_prev_ly = ly;
}

Sint16 xbox_controller_axis(SDL_GameController* gc, SDL_GameControllerAxis axis) {
    Sint16 v = SDL_GameControllerGetAxis(gc, axis);
    Sint16* h;
    if ((unsigned)axis >= SDL_CONTROLLER_AXIS_MAX) return v;
    if (axis == SDL_CONTROLLER_AXIS_LEFTX) s_raw_lx = v;
    if (axis == SDL_CONTROLLER_AXIS_LEFTY) check_swing(s_raw_lx, v);
    if (!XBOX_PAD_MEDIAN) return v;
    h = s_hist[axis];
    h[0] = h[1];
    h[1] = h[2];
    h[2] = v;
    if (s_n[axis] < 3) {
        s_n[axis]++;
        return v;
    }
    return med3(h[0], h[1], h[2]);
}
