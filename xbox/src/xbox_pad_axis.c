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
 * whether spikes are the cause.
 *
 * Stick trace: the last 10 s of stick reads (mapped controller axes AND the
 * raw SDL joystick axes 0-5) are kept in a ring; clicking the left stick (L3,
 * unused by the game) writes them to E:\UDATA\4f430001\stick.log. The
 * playtester clicks right after the character goes the wrong way. */
#include <windows.h>
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include "xbox_io.h"
#include "pc_settings.h"

#ifndef XBOX_PAD_MEDIAN
#define XBOX_PAD_MEDIAN 0   /* hardware traces showed no spikes; costs a frame */
#endif
#ifndef XBOX_STICK_DZ
#define XBOX_STICK_DZ 37    /* radial %: playtest pad rests up to 29-35% off centre */
#endif
#ifndef XBOX_STICK_SNAPBACK
#define XBOX_STICK_SNAPBACK 1
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

#define TRACE_N 600
typedef struct { unsigned frame; Sint16 lx, ly, j[6]; } Trace;
static Trace s_trace[TRACE_N];
static unsigned s_trace_pos, s_dumps;

static void trace_add(SDL_GameController* gc, Sint16 lx, Sint16 ly) {
    SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
    Trace* t = &s_trace[s_trace_pos++ % TRACE_N];
    int i;
    t->frame = xbox_frame_count();
    t->lx = lx;
    t->ly = ly;
    for (i = 0; i < 6; i++) t->j[i] = js ? SDL_JoystickGetAxis(js, i) : 0;
}

static void trace_dump(void) {
    char name[64];
    HANDLE h;
    unsigned i, n = s_trace_pos < TRACE_N ? s_trace_pos : TRACE_N;
    if (s_dumps >= 20) return;
    snprintf(name, sizeof name, XBOX_UDATA_DIR "stick%u.log", s_dumps++);
    h = CreateFileA(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    for (i = s_trace_pos - n; i != s_trace_pos; i++) {
        const Trace* t = &s_trace[i % TRACE_N];
        char line[128];
        DWORD w;
        int len = snprintf(line, sizeof line, "%u  L %6d %6d  raw %6d %6d %6d %6d %6d %6d\r\n", t->frame, t->lx, t->ly,
                           t->j[0], t->j[1], t->j[2], t->j[3], t->j[4], t->j[5]);
        WriteFile(h, line, (DWORD)len, &w, NULL);
    }
    xbox_flush_file(h);
    CloseHandle(h);
    xbox_logf("[PAD] stick trace -> %s\n", name);
}

/* Worn Duke/S sticks (measured on the playtest pad, stick*.log 2026-09-27):
 * rest at (-5136,-2617), 18% off centre, past pc_pad.c's 12% per-axis
 * deadzone -> creeps left; and after a full push + release the spring
 * overshoots to -9000..-12500 (up to 38%) the other way for ~10 frames ->
 * the character lurches backwards. Smooth data, no spikes. So:
 *  - radial deadzone (max of XBOX_STICK_DZ and settings stick_deadzone) on
 *    the stick vector, zeroing both axes inside it and rescaling the rest so
 *    the whole tilt range past it still maps onto walk..run;
 *  - snap-back suppression: within 12 frames of being held past 70% in some
 *    direction, a reading more than 90 degrees away and under 50% is the
 *    spring, not the player: neutral. A real reversal passes 50% at once. */
static Sint16 s_out_ly;
static float s_hold_x, s_hold_y;   /* direction of the last strong push */
static unsigned s_hold_frame;

static void shape_left(Sint16 lx, Sint16 ly, Sint16* ox, Sint16* oy) {
    int dzp = g_pc_settings.stick_deadzone;
    float dz = (float)(dzp > XBOX_STICK_DZ ? dzp : XBOX_STICK_DZ) * 327.67f;
    float x = lx, y = ly, m = sqrtf(x * x + y * y);
    unsigned f = xbox_frame_count();
    *ox = *oy = 0;
    if (m > 0.7f * 32767.0f) {
        s_hold_x = x / m;
        s_hold_y = y / m;
        s_hold_frame = f;
    }
    if (m < dz) return;
    if (XBOX_STICK_SNAPBACK && f - s_hold_frame <= 12 && m < 0.5f * 32767.0f &&
        (x * s_hold_x + y * s_hold_y) / m < 0.0f)
        return;
    {
        /* rescale [dz, full] onto [just past pc_pad.c's 12% per-axis
         * deadzone, full] so gentle tilts (tiptoeing) still register */
        const float lo = 0.13f * 32767.0f;
        float k = (lo + (m > 32767.0f ? 32767.0f - dz : m - dz) / (32767.0f - dz) * (32767.0f - lo)) / m;
        *ox = (Sint16)(x * k);
        *oy = (Sint16)(y * k);
    }
}

Sint16 xbox_controller_axis(SDL_GameController* gc, SDL_GameControllerAxis axis) {
    Sint16 v = SDL_GameControllerGetAxis(gc, axis);
    /* pc_pad.c reads LEFTX then LEFTY back to back: shape the pair on X */
    if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
        Sint16 ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY), ox;
        s_raw_lx = v;
        check_swing(v, ly);
        trace_add(gc, v, ly);
        {
            static int l3_was;
            int l3 = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK);
            if (l3 && !l3_was) trace_dump();
            l3_was = l3;
        }
        shape_left(v, ly, &ox, &s_out_ly);
        v = ox;
    } else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
        v = s_out_ly;
    }
    if ((unsigned)axis >= SDL_CONTROLLER_AXIS_MAX || !XBOX_PAD_MEDIAN) return v;
    Sint16* h = s_hist[axis];
    h[0] = h[1];
    h[1] = h[2];
    h[2] = v;
    if (s_n[axis] < 3) {
        s_n[axis]++;
        return v;
    }
    return med3(h[0], h[1], h[2]);
}
