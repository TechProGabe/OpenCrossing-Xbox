/* xbox_autopad.c — scripted input for headless test runs (debug only).
 *
 * Build with XBOX_CMAKE_ARGS="-DXBOX_AUTOPAD=<frame>": CMake compiles
 * pc_pad.c with PADRead renamed to xbox_pad_read_real, and this PADRead
 * wraps it. From PADRead call <frame> on, every 30 calls it holds a button
 * for 4 calls: START every 8th press, A otherwise. That walks the title,
 * the file menu and Rover's train dialog without a human. Real pad input
 * still passes through. Not built into release XBEs.
 *
 * -DXBOX_AUTOPAD=script instead plays D:\autopad.txt (stage it with
 * OCX_STAGE_EXTRA), one step per line, "#" comments:
 *   <call> <button> [hold]   hold a GC button from PADRead call <call> for
 *                            [hold] calls (default 4): A B X Y Z L R START
 *                            UP DOWN LEFT RIGHT
 *   <call> SDL <button>      push an SDL controller button press+release (the
 *                            pause menu and the rebinding page read those):
 *                            A B X Y BACK START WHITE BLACK UP DOWN LEFT RIGHT
 *                            RS (right stick click: screenshots)
 *   <call> STICK <x> <y> [hold]  hold the main stick at x,y (-100..100, up is
 *                            +y) for [hold] calls (default 4)
 *   <call> SHOT              one [FBDUMP] screenshot at the next present
 *   <call> LOG <text>        a log line (marks the step in serial.log)
 * A line starting "@480 " or "@720 " only counts when this boot runs at that
 * output, so one script can drive both sides of a video-mode restart.
 * Steps must be in call order. */
#ifdef XBOX_DBG_AUTOPAD
#include "pc_platform.h"
#include <dolphin/pad.h>

u32 xbox_pad_read_real(PADStatus* status);

#ifdef XBOX_DBG_AUTOPAD_SCRIPT
#include <stdlib.h>
#include <string.h>

extern int g_xbox_fbdump_once;   /* xbox_nv2a.c */
extern int g_xbox_video_720p;    /* xbox_nv2a.c */

typedef struct { u32 call; int kind; u32 arg, hold; int x, y; char text[48]; } Step;
enum { K_PAD, K_SDL, K_SHOT, K_LOG, K_STICK };
#define MAX_STEPS 1024   /* a 30-minute burn-in script is ~900 steps */
static Step s_steps[MAX_STEPS];
static int s_nsteps, s_next, s_loaded;
static u16 s_held;
static u32 s_held_until;
static s8 s_stick_x, s_stick_y;
static u32 s_stick_until;

static int pad_bit(const char* n, u32* out) {
    static const struct { const char* n; u32 b; } k[] = {
        { "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B }, { "X", PAD_BUTTON_X }, { "Y", PAD_BUTTON_Y },
        { "Z", PAD_TRIGGER_Z }, { "L", PAD_TRIGGER_L }, { "R", PAD_TRIGGER_R }, { "START", PAD_BUTTON_START },
        { "UP", PAD_BUTTON_UP }, { "DOWN", PAD_BUTTON_DOWN }, { "LEFT", PAD_BUTTON_LEFT },
        { "RIGHT", PAD_BUTTON_RIGHT },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcmp(n, k[i].n)) { *out = k[i].b; return 1; }
    return 0;
}

static int sdl_button(const char* n, u32* out) {
    static const struct { const char* n; u32 b; } k[] = {
        { "A", SDL_CONTROLLER_BUTTON_A }, { "B", SDL_CONTROLLER_BUTTON_B }, { "X", SDL_CONTROLLER_BUTTON_X },
        { "Y", SDL_CONTROLLER_BUTTON_Y }, { "BACK", SDL_CONTROLLER_BUTTON_BACK },
        { "START", SDL_CONTROLLER_BUTTON_START }, { "WHITE", SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
        { "BLACK", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER }, { "RS", SDL_CONTROLLER_BUTTON_RIGHTSTICK }, { "UP", SDL_CONTROLLER_BUTTON_DPAD_UP },
        { "DOWN", SDL_CONTROLLER_BUTTON_DPAD_DOWN }, { "LEFT", SDL_CONTROLLER_BUTTON_DPAD_LEFT },
        { "RIGHT", SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcmp(n, k[i].n)) { *out = k[i].b; return 1; }
    return 0;
}

static void load_script(void) {
    char line[128];
    FILE* f = fopen("D:\\autopad.txt", "r");
    s_loaded = 1;
    if (!f) {
        printf("[AUTOPAD] no D:\\autopad.txt\n");
        return;
    }
    while (s_nsteps < MAX_STEPS && fgets(line, sizeof line, f)) {
        Step* st = &s_steps[s_nsteps];
        char* tok;
        char* p = line;
        char* nl = strpbrk(line, "\r\n#");
        if (nl) *nl = '\0';
        if (!strncmp(p, "@480 ", 5) || !strncmp(p, "@720 ", 5)) {
            if ((p[1] == '7') != (g_xbox_video_720p != 0)) continue;
            p += 5;
        }
        if (!(tok = strtok(p, " \t"))) continue;
        memset(st, 0, sizeof *st);
        st->call = (u32)strtoul(tok, NULL, 10);
        if (!(tok = strtok(NULL, " \t"))) continue;
        if (!strcmp(tok, "SHOT")) {
            st->kind = K_SHOT;
        } else if (!strcmp(tok, "LOG")) {
            st->kind = K_LOG;
            tok = strtok(NULL, "");
            snprintf(st->text, sizeof st->text, "%s", tok ? tok : "");
        } else if (!strcmp(tok, "STICK")) {
            char* ty;
            st->kind = K_STICK;
            if (!(tok = strtok(NULL, " \t")) || !(ty = strtok(NULL, " \t"))) continue;
            st->x = atoi(tok);
            st->y = atoi(ty);
            tok = strtok(NULL, " \t");
            st->hold = tok ? (u32)strtoul(tok, NULL, 10) : 4;
        } else if (!strcmp(tok, "SDL")) {
            st->kind = K_SDL;
            if (!(tok = strtok(NULL, " \t")) || !sdl_button(tok, &st->arg)) continue;
        } else {
            st->kind = K_PAD;
            if (!pad_bit(tok, &st->arg)) continue;
            tok = strtok(NULL, " \t");
            st->hold = tok ? (u32)strtoul(tok, NULL, 10) : 4;
        }
        s_nsteps++;
    }
    if (s_nsteps == MAX_STEPS && fgets(line, sizeof line, f))
        printf("[AUTOPAD] script longer than %d steps: the rest is ignored\n", MAX_STEPS);
    fclose(f);
    printf("[AUTOPAD] script: %d steps\n", s_nsteps);
}

static void push_sdl(u32 b) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = (Uint8)b;
    e.cbutton.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    e.type = SDL_CONTROLLERBUTTONUP;
    e.cbutton.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

u32 PADRead(PADStatus* status) {
    static u32 calls;
    u32 r = xbox_pad_read_real(status);
    calls++;
    if (!s_loaded) load_script();
    while (s_next < s_nsteps && s_steps[s_next].call <= calls) {
        const Step* st = &s_steps[s_next++];
        switch (st->kind) {
            case K_PAD:
                s_held = (u16)st->arg;
                s_held_until = calls + st->hold;
                break;
            case K_SDL: push_sdl(st->arg); break;
            case K_STICK:
                s_stick_x = (s8)(st->x * 72 / 100);   /* GC stick reach ~72 */
                s_stick_y = (s8)(st->y * 72 / 100);
                s_stick_until = calls + st->hold;
                break;
            case K_SHOT: g_xbox_fbdump_once = 1; break;
            case K_LOG: printf("[AUTOPAD] call %u: %s\n", (unsigned)calls, st->text); break;
        }
    }
    if (calls < s_stick_until) {
        status[0].err = PAD_ERR_NONE;
        status[0].stickX = s_stick_x;
        status[0].stickY = s_stick_y;
    }
    if (s_held && calls < s_held_until) {
        status[0].err = PAD_ERR_NONE;
        status[0].button |= s_held;
    } else {
        s_held = 0;
    }
    return r;
}
#else
u32 PADRead(PADStatus* status) {
    static u32 calls, presses;
    u32 r = xbox_pad_read_real(status);
    u32 t;
    calls++;
    if (calls < (u32)XBOX_DBG_AUTOPAD) return r;
    t = (calls - (u32)XBOX_DBG_AUTOPAD) % 30;
    if (t < 4) {
        u16 b = (presses % 8) == 7 ? PAD_BUTTON_START : PAD_BUTTON_A;
        status[0].err = PAD_ERR_NONE;
        status[0].button |= b;
        if (t == 3) {
            if ((presses % 16) == 0)
                printf("[AUTOPAD] call %u press %u (%s)\n", (unsigned)calls, (unsigned)presses,
                       b == PAD_BUTTON_START ? "START" : "A");
            presses++;
        }
    }
    return r;
}
#endif
#endif
