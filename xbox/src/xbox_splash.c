/* xbox_splash.c — the boot title card and the "no disc image" screen.
 *
 * Drawn with the CPU straight into the linear framebuffer nxdk's
 * XVideoSetMode() hands back, BEFORE the GPU backend takes the display — the
 * same trick as the Dreamcast port (dc_main.c): no GPU state, no textures, no
 * assets, nothing to undo afterwards.
 *
 *   xbox_splash_show()       "TechProGabe Presents..." on a navy gradient,
 *                            fading in; holds XBOX_SPLASH_MS (any button skips)
 *   xbox_splash_progress(f)  load bar under the title, 0..1
 *   xbox_splash_error(...)   full-screen error card; never returns
 *
 * Font: unscii-16 (public domain), the 8x16 face nxdk's debug console uses.
 * Kill switch: -DXBOX_NO_SPLASH. Duration: -DXBOX_SPLASH_MS=<n>. */
#include <hal/video.h>
#include <windows.h>
#include <string.h>
#include <SDL.h>
#include "xbox_io.h"
#include "xbox_splash.h"
#include "xbox_fbdump.h"

#ifndef XBOX_SPLASH_MS
#define XBOX_SPLASH_MS 2000
#endif

#define SPLASH_TEXT "TechProGabe Presents..."
#define SCR_W 640
#define SCR_H 480

static const unsigned char s_font[] = {
#include <hal/font_unscii_16.h>
};
#define GLYPH_W 8
#define GLYPH_H 16

static unsigned int* s_fb;
static int s_ready;

static unsigned int rgb(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

/* Dark navy at the top fading to near-black: same palette as the DC card. */
static unsigned int bg_at(int y) {
    int t = (y * 255) / (SCR_H - 1);
    return rgb(6 + (8 * (255 - t)) / 255, 10 + (12 * (255 - t)) / 255, 28 + (24 * (255 - t)) / 255);
}

static void fill_bg(int y0, int y1) {
    int x, y;
    for (y = y0; y < y1; y++) {
        unsigned int c = bg_at(y);
        unsigned int* row = s_fb + y * SCR_W;
        for (x = 0; x < SCR_W; x++) row[x] = c;
    }
}

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

static int init_fb(void) {
    if (s_ready) return 1;
    if (!XVideoSetMode(SCR_W, SCR_H, 32, REFRESH_DEFAULT)) return 0;
    s_fb = (unsigned int*)XVideoGetFB();
    if (!s_fb) return 0;
    s_ready = 1;
    return 1;
}

/* Any controller button — used to skip the hold. SDL's gamecontroller
 * subsystem may not be up yet; then we simply wait out the timer. */
static int any_button(void) {
    int i, b;
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) return 0;
    SDL_GameControllerUpdate();
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!gc) continue;
        for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
            if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) return 1;
    }
    return 0;
}

#define TITLE_ZOOM 3
#define TITLE_Y    ((SCR_H - GLYPH_H * TITLE_ZOOM) / 2 - 24)
#define BAR_X      120
#define BAR_W      400
#define BAR_H      8
#define BAR_Y      (TITLE_Y + GLYPH_H * TITLE_ZOOM + 32)

void xbox_splash_show(void) {
#ifndef XBOX_NO_SPLASH
    DWORD t0;
    int step;
    if (!init_fb()) return;
    fill_bg(0, SCR_H);
    /* fade in over ~500 ms: redraw only the title band */
    for (step = 0; step <= 16; step++) {
        int v = 40 + (215 * step) / 16;
        fill_bg(TITLE_Y, TITLE_Y + GLYPH_H * TITLE_ZOOM);
        draw_centered(SPLASH_TEXT, TITLE_Y, TITLE_ZOOM, rgb(v, v, v));
        XVideoWaitForVBlank();
        Sleep(30);
    }
    xbox_logf("[XBOX] splash: %s\n", SPLASH_TEXT);
#ifdef XBOX_SPLASH_DUMP
    xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    t0 = GetTickCount();
    while (GetTickCount() - t0 < XBOX_SPLASH_MS) {
        if (any_button()) break;
        Sleep(16);
    }
#endif
}

void xbox_splash_progress(float f) {
#ifndef XBOX_NO_SPLASH
    int x, y, fill;
    if (!s_ready) return;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    fill = (int)(f * BAR_W);
    for (y = BAR_Y; y < BAR_Y + BAR_H; y++) {
        unsigned int* row = s_fb + y * SCR_W;
        for (x = 0; x < BAR_W; x++) row[BAR_X + x] = x < fill ? rgb(120, 200, 110) : rgb(30, 40, 60);
    }
#endif
}

void xbox_splash_error(const char* title, const char* const* lines) {
    int y, i;
    xbox_logf("[XBOX] FATAL: %s\n", title);
    for (i = 0; lines && lines[i]; i++) xbox_logf("[XBOX]   %s\n", lines[i]);
    if (init_fb()) {
        fill_bg(0, SCR_H);
        draw_centered("OpenCrossing", 56, 2, rgb(120, 200, 110));
        draw_centered(title, 120, 2, rgb(255, 110, 100));
        y = 190;
        for (i = 0; lines && lines[i]; i++, y += 22)
            draw_centered(lines[i], y, 1, rgb(225, 230, 240));
#ifdef XBOX_SPLASH_DUMP
        xbox_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    }
    for (;;) Sleep(1000);
}
