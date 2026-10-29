/* xbox_splash.c — the boot screen, the first boot's disc-check letter and
 * the error / question cards.
 *
 * Drawn with the CPU straight into the linear 640x480 framebuffer nxdk's
 * XVideoSetMode() hands back, BEFORE the GPU backend takes the display: no
 * GPU state, nothing to undo afterwards. The painted layer is one of four
 * 256-colour pictures appended to default.xbe (tools/xbox/xbe_payload.py,
 * drawn by tools/xbox/make_splash.py from tools/xbox/splash/letter.html):
 *   SPWE  welcome back: every boot ("Loading your town..." and the bar)
 *   SPLE  the letter, ten empty stamp boxes: the first boot's disc check
 *   SPST  the same letter, every box stamped: a box is copied over from it
 *         as each file passes
 *   SPBL  blank paper: error and question cards
 * Without them (no Pillow at build time) the paper is a plain fill.
 *
 * Cheap on purpose (the boot reads the disc image meanwhile): a picture is
 * read once (300 KB) and blitted once; after that a low-priority thread
 * wakes ~30 times a second and redraws only what changed: the bar's filled
 * span, a text line, a stamp box. The bar eases towards the boot's progress
 * (never slower than a second for its width) and xbox_splash_finish() waits
 * for it to fill and hold before the game takes the screen.
 *
 *   xbox_splash_show()        welcome screen, fades in; holds XBOX_SPLASH_MS
 *                             (any button skips); reads BACK (safe video)
 *   xbox_splash_progress(f)   the bar's target, 0..1
 *   xbox_splash_letter()      switch to the letter (first boot's check)
 *   xbox_splash_text(a, b)    the two status lines (NULL keeps a line)
 *   xbox_splash_stamp(i, ok)  box i stamped, or crossed out in red
 *   xbox_splash_finish()      bar to 100%, held, thread stopped
 *   xbox_splash_ask / _error  cards on blank paper
 *
 * Font: unscii-16 (public domain), the 8x16 face nxdk's debug console uses.
 * Kill switch: -DXBOX_NO_SPLASH. Duration: -DXBOX_SPLASH_MS=<n>. */
#include <hal/video.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "xbox_io.h"
#include "xbox_splash.h"
#include "xbox_payload.h"
#include "xbox_fbdump.h"
#include "xbox_settings.h"

#ifndef XBOX_SPLASH_MS
#define XBOX_SPLASH_MS 1500
#endif

#define SCR_W 640
#define SCR_H 480

static const unsigned char s_font[] = {
#include <hal/font_unscii_16.h>
};
#define GLYPH_W 8
#define GLYPH_H 16

/* where letter.html leaves room for the live layer */
#define STAMP_X0   98
#define STAMP_DX   47
#define STAMP_Y    296
#define STAMP_W    40
#define STAMP_H    48
#define STAMPS     10
#define LINE_X     98
#define LINE_A_Y   356
#define LINE_B_Y   378
#define LINE_W     (542 - LINE_X)
#define BAR_X      98
#define BAR_W      444
#define BAR_Y      412
#define BAR_H      14
#define HINT_Y     458
#define PAPER_X    40
#define PAPER_Y    112
#define PAPER_W    560
#define PAPER_H    340

static unsigned int* s_fb;
static int s_ready;

static unsigned int rgb(int r, int g, int b) {
    return 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}
#define INK    rgb(58, 74, 107)
#define MUTED  rgb(123, 138, 168)
#define GREEN  rgb(63, 155, 44)
#define RED    rgb(208, 74, 58)
#define PAPER  rgb(255, 250, 240)
#define SKY    rgb(143, 199, 232)

/* ---- the painted layer ---- */

typedef struct {
    unsigned pal[256];
    unsigned char* px;   /* SCR_W * SCR_H indices, or NULL: plain paper */
} Picture;

static Picture s_bg, s_stamped;

static void pic_free(Picture* p) {
    free(p->px);
    p->px = NULL;
}

static int pic_load(Picture* p, const char* tag) {
    unsigned off, size, head[2];
    HANDLE h;
    int ok = 0;
    pic_free(p);
    if (!xbox_payload_find(tag, &off, &size) || size != 8 + 1024 + SCR_W * SCR_H) return 0;
    h = xbox_payload_open();
    if (h == INVALID_HANDLE_VALUE) return 0;
    p->px = (unsigned char*)malloc(SCR_W * SCR_H);
    if (p->px && xbox_payload_read(h, off, head, 8) && head[0] == (SCR_W | SCR_H << 16) &&
        xbox_payload_read(h, off + 8, p->pal, 1024) && xbox_payload_read(h, off + 8 + 1024, p->px, SCR_W * SCR_H)) {
        int i;
        for (i = 0; i < 256; i++) p->pal[i] |= 0xFF000000u;
        ok = 1;
    }
    CloseHandle(h);
    if (!ok) pic_free(p);
    return ok;
}

/* the painted pixel, or the plain stand-in: sky around white paper */
static unsigned int pic_at(const Picture* p, int x, int y) {
    if (p->px) return p->pal[p->px[y * SCR_W + x]];
    return x >= PAPER_X && x < PAPER_X + PAPER_W && y >= PAPER_Y && y < PAPER_Y + PAPER_H ? PAPER : SKY;
}

/* copy a rectangle of a picture to the screen (restores what text covered) */
static void pic_blit(const Picture* p, int x0, int y0, int w, int h) {
    int x, y;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > SCR_W) w = SCR_W - x0;
    if (y0 + h > SCR_H) h = SCR_H - y0;
    for (y = y0; y < y0 + h; y++) {
        unsigned int* row = s_fb + y * SCR_W;
        if (p->px) {
            const unsigned char* src = p->px + y * SCR_W;
            for (x = x0; x < x0 + w; x++) row[x] = p->pal[src[x]];
        } else {
            for (x = x0; x < x0 + w; x++) row[x] = pic_at(p, x, y);
        }
    }
}

/* the whole screen at brightness k/16 (fade in) */
static void pic_blit_dim(const Picture* p, int k) {
    int x, y;
    for (y = 0; y < SCR_H; y++) {
        unsigned int* row = s_fb + y * SCR_W;
        for (x = 0; x < SCR_W; x++) {
            unsigned c = pic_at(p, x, y);
            row[x] = 0xFF000000u | ((c & 0xFF00FFu) * (unsigned)k >> 4 & 0xFF00FFu) |
                     ((c & 0x00FF00u) * (unsigned)k >> 4 & 0x00FF00u);
        }
    }
}

/* ---- text ---- */

static void draw_text(const char* s, int x, int y, int zoom, unsigned int col) {
    for (; *s; s++, x += GLYPH_W * zoom) {
        const unsigned char* g = s_font + (unsigned char)*s * GLYPH_H;
        int gy, gx, zy, zx;
        for (gy = 0; gy < GLYPH_H; gy++) {
            for (gx = 0; gx < GLYPH_W; gx++) {
                if (!(g[gy] & (0x80 >> gx))) continue;
                for (zy = 0; zy < zoom; zy++) {
                    int py = y + gy * zoom + zy;
                    if (py < 0 || py >= SCR_H) continue;
                    for (zx = 0; zx < zoom; zx++) {
                        int px = x + gx * zoom + zx;
                        if (px >= 0 && px < SCR_W) s_fb[py * SCR_W + px] = col;
                    }
                }
            }
        }
    }
}

static int text_w(const char* s, int zoom) { return (int)strlen(s) * GLYPH_W * zoom; }

static void draw_centered(const char* s, int y, int zoom, unsigned int col) {
    draw_text(s, (SCR_W - text_w(s, zoom)) / 2, y, zoom, col);
}

/* a hint under the paper, on a cream tag with a brown rim (white on the
 * blue stripes was hard to read on a TV) */
#define HINT_PAD 8
static void draw_hint(const char* s) {
    int w = text_w(s, 1), x0 = (SCR_W - w) / 2 - HINT_PAD, x1 = (SCR_W + w) / 2 + HINT_PAD;
    int y0 = HINT_Y - 3, y1 = HINT_Y + GLYPH_H + 2, x, y;
    pic_blit(&s_bg, 0, y0, SCR_W, y1 - y0 + 1);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int edge = y == y0 || y == y1 || x == x0 || x == x1;
            int corner = (x == x0 || x == x1) && (y == y0 || y == y1);
            if (!corner) s_fb[y * SCR_W + x] = edge ? rgb(138, 85, 38) : PAPER;
        }
    draw_text(s, (SCR_W - w) / 2, HINT_Y, 1, INK);
}

static int init_fb(void) {
    if (s_ready) return 1;
    if (!xbox_video_set_480()) return 0;   /* 480i when progressive = 0 (safe video) */
    s_fb = (unsigned int*)XVideoGetFB();
    if (!s_fb) return 0;
    s_ready = 1;
    return 1;
}

/* ---- the live layer: one thread draws, the boot only sets state ---- */

enum { MODE_WELCOME, MODE_LETTER };

static SDL_Thread* s_thread;
static SDL_mutex* s_lock;
static volatile int s_run;
static volatile int s_mode = MODE_WELCOME, s_mode_drawn = -1;
static volatile float s_target;      /* the boot's progress */
static float s_shown;                /* what the bar shows (thread only) */
static int s_bar_drawn = -1;         /* filled pixels drawn (thread only) */
static volatile int s_bar_on = 1;    /* hidden while the letter's stamps are the progress */
static volatile int s_stamps, s_stamps_drawn;
static volatile char s_stamp_bad[STAMPS];
static char s_line[2][80];
static volatile int s_line_gen[2];
static int s_line_drawn[2] = { -1, -1 };

static void draw_line(int i) {
    char buf[80];
    int y = i ? LINE_B_Y : LINE_A_Y;
    SDL_LockMutex(s_lock);
    memcpy(buf, s_line[i], sizeof buf);
    s_line_drawn[i] = s_line_gen[i];
    SDL_UnlockMutex(s_lock);
    pic_blit(&s_bg, LINE_X, y, LINE_W, GLYPH_H);
    draw_text(buf, LINE_X, y, 1, i ? MUTED : INK);
}

/* the track: a 2 px brown rim with clipped corners, cream inside, the
 * filled part in green stripes */
static void draw_bar(int fill) {
    int x, y;
    for (y = 0; y < BAR_H; y++) {
        unsigned int* row = s_fb + (BAR_Y + y) * SCR_W + BAR_X;
        int edge_y = y < 2 || y >= BAR_H - 2;
        for (x = 0; x < BAR_W; x++) {
            int cx = x < 3 ? 3 - x : x >= BAR_W - 3 ? x - (BAR_W - 4) : 0;
            int cy = y < 3 ? 3 - y : y >= BAR_H - 3 ? y - (BAR_H - 4) : 0;
            if (cx + cy > 3)
                row[x] = pic_at(&s_bg, BAR_X + x, BAR_Y + y);   /* the rounded corner */
            else if (edge_y || x < 2 || x >= BAR_W - 2 || cx + cy == 3)
                row[x] = rgb(138, 85, 38);
            else if (x < fill)
                row[x] = ((x + y) / 5) & 1 ? rgb(124, 194, 67) : rgb(143, 209, 79);
            else
                row[x] = rgb(233, 220, 192);
        }
    }
}

/* the skip hint only while the check runs (the letter, bar hidden) */
static int s_hint_drawn = -1;

static void update_hint(void) {
    int skip = s_mode == MODE_LETTER && !s_bar_on;
    if (skip == s_hint_drawn) return;
    draw_hint(skip ? "B: skip the check (it runs again next time)  -  Hold BACK for 480i" : "Hold BACK for 480i");
    s_hint_drawn = skip;
}

static void draw_mode(void) {
    pic_blit(&s_bg, 0, 0, SCR_W, SCR_H);
    s_hint_drawn = -1;
    update_hint();
    s_mode_drawn = s_mode;
    s_bar_drawn = -1;
    s_line_drawn[0] = s_line_drawn[1] = -1;
    s_stamps_drawn = 0;
}

static int splash_thread(void* arg) {
    (void)arg;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    while (s_run) {
        float t = s_target, step;
        int i, fill;
        if (s_mode_drawn != s_mode) draw_mode();
        update_hint();
        for (i = 0; i < 2; i++)
            if (s_line_drawn[i] != s_line_gen[i]) draw_line(i);
        while (s_stamps_drawn < s_stamps && s_stamps_drawn < STAMPS) {
            int x0 = STAMP_X0 + STAMP_DX * s_stamps_drawn;
            if (s_stamp_bad[s_stamps_drawn]) {
                /* a file that differs: a red cross in its empty box */
                int k, d;
                for (k = 10; k < STAMP_W - 10; k++)
                    for (d = 0; d < 3; d++) {
                        s_fb[(STAMP_Y + 4 + k + d) * SCR_W + x0 + k] = RED;
                        s_fb[(STAMP_Y + 4 + k + d) * SCR_W + x0 + STAMP_W - 1 - k] = RED;
                    }
            } else {
                pic_blit(&s_stamped, x0, STAMP_Y, STAMP_W, STAMP_H);
            }
            s_stamps_drawn++;
#ifdef XBOX_SPLASH_DUMP
            if (s_stamps_drawn == 6) xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
        }
        /* ease: a seventh of the way a frame, at least a second's width */
        step = (t - s_shown) / 7;
        if (step < 1.0f / 30) step = 1.0f / 30;
        s_shown = s_shown + step > t ? t : s_shown + step;
        fill = s_bar_on ? (int)(s_shown * BAR_W) : -2;
        if (fill != s_bar_drawn) {
            if (s_bar_on) draw_bar(fill);
            else pic_blit(&s_bg, BAR_X, BAR_Y, BAR_W, BAR_H);
            s_bar_drawn = fill;
        }
        Sleep(33);
    }
    return 0;
}

static void thread_start(void) {
    if (s_thread || !s_ready) return;
    if (!s_lock) s_lock = SDL_CreateMutex();
    s_run = 1;
    s_thread = SDL_CreateThread(splash_thread, "Splash", NULL);
    if (!s_thread) s_run = 0;
}

static void thread_stop(void) {
    if (!s_thread) return;
    s_run = 0;
    SDL_WaitThread(s_thread, NULL);
    s_thread = NULL;
}

/* ---- controllers ---- */

/* Controllers during the splash: any button skips the hold, and BACK held
 * as it ends is safe video (xbox_settings_safe_video). SDL's gamecontroller
 * subsystem is started before the splash (xbox_main.c); pads are opened
 * here as USB enumerates them and closed once the splash is done, and a
 * pad's buttons read as down only from the update after its open. */
#define MAX_PADS 4
static SDL_GameController* s_pads[MAX_PADS];

static void pads_open(void) {
    int i;
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) return;
    SDL_GameControllerUpdate();
    for (i = 0; i < SDL_NumJoysticks() && i < MAX_PADS; i++)
        if (!s_pads[i] && SDL_IsGameController(i)) s_pads[i] = SDL_GameControllerOpen(i);
}

/* button b down on any open pad; b < 0: any button */
static int pad_down(int b) {
    int i, k;
    for (i = 0; i < MAX_PADS; i++) {
        if (!s_pads[i]) continue;
        for (k = b < 0 ? 0 : b; k < (b < 0 ? SDL_CONTROLLER_BUTTON_MAX : b + 1); k++)
            if (SDL_GameControllerGetButton(s_pads[i], (SDL_GameControllerButton)k)) return 1;
    }
    return 0;
}

static void pads_close(void) {
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (s_pads[i]) {
            SDL_GameControllerClose(s_pads[i]);
            s_pads[i] = NULL;
        }
}

/* wait up to `ms` for the hold, polling the pads; 1 if a button ended it */
static int pads_wait(DWORD ms, int stop_on_button) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        pads_open();
        if (stop_on_button && pad_down(-1)) return 1;
        Sleep(16);
    }
    return 0;
}

int g_xbox_force_disc_check;

static int triggers_down(void) {
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (s_pads[i] && SDL_GameControllerGetAxis(s_pads[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000 &&
            SDL_GameControllerGetAxis(s_pads[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
            return 1;
    return 0;
}

/* BACK held now (one more update after the last open); and Y with both
 * triggers, the full disc check on demand (nothing on screen mentions it) */
static void read_safe_video(void) {
    pads_open();
    SDL_GameControllerUpdate();
    g_xbox_safe_video_held = pad_down(SDL_CONTROLLER_BUTTON_BACK);
    g_xbox_force_disc_check = pad_down(SDL_CONTROLLER_BUTTON_Y) && triggers_down();
    pads_close();
}

int xbox_splash_button(int b) {
    pads_open();
    SDL_GameControllerUpdate();
    return pad_down(b);
}

/* ---- the boot ---- */

void xbox_splash_show(void) {
#ifndef XBOX_NO_SPLASH
    int k;
    if (!init_fb()) {
        pads_wait(1500, 0);   /* no splash: still give USB time for safe video */
        read_safe_video();
        return;
    }
    pic_load(&s_bg, "SPWE");
    /* fade in over ~250 ms; no XVideoWaitForVBlank(): it hooks the GPU IRQ,
     * and pb_init() then fails with -4 when the NV2A backend starts */
    for (k = 2; k <= 16; k += 2) {
        pic_blit_dim(&s_bg, k);
        Sleep(16);
    }
    xbox_splash_text("Loading your town...", "");
    draw_mode();
    thread_start();
    xbox_logf("[XBOX] splash: welcome (%s)\n", s_bg.px ? "painted" : "plain");
#ifdef XBOX_SPLASH_DUMP
    Sleep(100);
    xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    pads_wait(XBOX_SPLASH_MS, 1);
#else
    pads_wait(1500, 0);   /* no splash: still give USB time for safe video */
#endif
    read_safe_video();
}

void xbox_splash_progress(float f) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    if (f > s_target) s_target = f;   /* the bar never goes back */
}

void xbox_splash_text(const char* a, const char* b) {
    const char* s[2] = { a, b };
    int i;
    if (!s_lock) s_lock = SDL_CreateMutex();
    SDL_LockMutex(s_lock);
    for (i = 0; i < 2; i++)
        if (s[i]) {
            strncpy(s_line[i], s[i], sizeof s_line[i] - 1);
            s_line[i][sizeof s_line[i] - 1] = 0;
            s_line_gen[i]++;
        }
    SDL_UnlockMutex(s_lock);
}

void xbox_splash_letter(int on) {
    if (!s_thread) return;
    if (on) {
        /* the pictures are swapped with the thread stopped; ~0.6 MB, freed
         * when the check is done */
        thread_stop();
        if (pic_load(&s_bg, "SPLE")) pic_load(&s_stamped, "SPST");
        s_stamps = 0;
        memset((void*)s_stamp_bad, 0, sizeof s_stamp_bad);
        s_bar_on = 0;
        s_mode = MODE_LETTER;
        thread_start();
    } else {
        /* the stamps stay; the bar comes back under them for the rest of the load */
        s_bar_on = 1;
    }
}

void xbox_splash_stamp(int i, int ok) {
    if (i < 0 || i >= STAMPS) return;
    s_stamp_bad[i] = (char)!ok;
    s_stamps = i + 1;   /* files are checked in order: every box before i is done */
}

void xbox_splash_finish(void) {
    DWORD t0 = GetTickCount();
    if (!s_thread) return;
    xbox_splash_progress(1.0f);
    s_bar_on = 1;
    xbox_splash_text("Here we go!", "");
    /* the bar fills (a second at most) and holds a moment, then the game */
    while (s_bar_drawn < BAR_W && GetTickCount() - t0 < 3000) Sleep(16);
    Sleep(300);
#ifdef XBOX_SPLASH_DUMP
    xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    thread_stop();
    pic_free(&s_bg);
    pic_free(&s_stamped);
    xbox_logf("[XBOX] splash: done\n");
}

/* The GPU backend is about to change the video mode (720p): XVideoSetMode
 * frees this framebuffer. Forget it, so nothing draws there and an error
 * screen sets up its own 640x480 mode again. */
void xbox_splash_release(void) {
    thread_stop();
    s_ready = 0;
    s_fb = NULL;
}

/* ---- cards ---- */

static void draw_card(const char* title, const char* const* lines, unsigned int title_col) {
    int y, i;
    thread_stop();
    if (!pic_load(&s_bg, "SPBL")) pic_free(&s_bg);
    pic_blit(&s_bg, 0, 0, SCR_W, SCR_H);
    draw_centered(title, PAPER_Y + 30, 2, title_col);
    y = PAPER_Y + 80;
    for (i = 0; lines && lines[i]; i++, y += 19)   /* 11 lines fit above the footer */
        draw_centered(lines[i], y, 1, INK);
}

int xbox_splash_ask(const char* title, const char* const* lines) {
    int i, a;
    xbox_logf("[XBOX] ask: %s\n", title);
    for (i = 0; lines && lines[i]; i++) xbox_logf("[XBOX]   %s\n", lines[i]);
    if (!init_fb()) return 1;   /* nothing to ask on: play */
    draw_card(title, lines, rgb(214, 120, 30));
    draw_centered("A: play anyway        B: quit", PAPER_Y + PAPER_H - 40, 1, GREEN);
    draw_hint("Hold BACK for 480i");
#ifdef XBOX_SPLASH_DUMP
    xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    /* a button held from before the card doesn't answer it */
    while (xbox_splash_button(SDL_CONTROLLER_BUTTON_A) || xbox_splash_button(SDL_CONTROLLER_BUTTON_B)) Sleep(16);
    for (;;) {
        if ((a = xbox_splash_button(SDL_CONTROLLER_BUTTON_A)) || xbox_splash_button(SDL_CONTROLLER_BUTTON_B)) break;
        Sleep(16);
    }
    pads_close();
    xbox_logf("[XBOX] answer: %s\n", a ? "A (play)" : "B (quit)");
    /* back to the welcome screen for the rest of the load */
    pic_load(&s_bg, "SPWE");
    s_mode = MODE_WELCOME;
    s_mode_drawn = -1;
    s_bar_on = 1;
    xbox_splash_text("Loading your town...", "");
    thread_start();
    return a;
}

void xbox_splash_error(const char* title, const char* const* lines) {
    int i;
    xbox_watchdog_disable();
    xbox_logf("[XBOX] FATAL: %s\n", title);
    for (i = 0; lines && lines[i]; i++) xbox_logf("[XBOX]   %s\n", lines[i]);
    if (init_fb()) {
        draw_card(title, lines, RED);
        draw_centered("Fix it, then restart the Xbox.", PAPER_Y + PAPER_H - 40, 1, MUTED);
#ifdef XBOX_SPLASH_DUMP
        xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    }
    for (;;) Sleep(1000);
}
