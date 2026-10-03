/* xbox_main.c — Xbox entry point. Replaces pc/src/pc_main.c (not compiled).
 *
 * Boot order (same as pc_main.c, docs/ref/dc/platform-api-boot-order.md):
 *   mount E:, UDATA dir -> XBE image range -> splash -> settings -> platform
 *   init -> disc -> assets -> ac_entry() -> boot_main() (never returns).
 * The disc image lives next to default.xbe (D:\); saves in E:\UDATA\. */
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <nxdk/mount.h>
#include "pc_platform.h"
#include "pc_gx_internal.h"
#include "pc_texture_pack.h"
#include "pc_settings.h"
#include "pc_keybindings.h"
#include "pc_assets.h"
#include "pc_disc.h"
#include "pc_typing.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_profiler.h"
#include "m_kankyo.h"
#include "xbox_io.h"
#include "xbox_splash.h"
#include "dirent.h"
#include "xbox_nv2a.h"
#include "xbox_settings.h"

SDL_Window*   g_pc_window = NULL;
SDL_GLContext g_pc_gl_context = NULL;
int           g_pc_running = 1;
int           g_pc_frame_limit_override = -1;
int           g_pc_speedhack_enabled = 0;
int           g_pc_verbose = 1;
int           g_xbox_verbose_noisy = 0;   /* g_pc_verbose for the files that log every second / slow frame (CMakeLists.txt); 1 = back on */
int           g_pc_time_override = -1;
int           g_pc_min_override = -1;
int           g_pc_sec_override = -1;
int           g_pc_date_month = -1;
int           g_pc_date_day = -1;
int           g_pc_date_year = -1;
int           g_pc_weather_override = -1;
int           g_pc_weather_intensity_override = mEnv_WEATHER_INTENSITY_HEAVY;
int           g_pc_window_w = PC_SCREEN_WIDTH;
int           g_pc_window_h = PC_SCREEN_HEIGHT;
int           g_pc_widescreen_stretch = 0;

/* pc_model_viewer.c is not built on Xbox. */
int g_pc_model_viewer = 0;
int g_pc_model_viewer_start = 0;
int g_pc_model_viewer_no_cull = 0;
void pc_model_viewer_init(void) {}
void pc_model_viewer_cleanup(void) {}

unsigned int pc_image_base = 0;
unsigned int pc_image_end  = 0;


/* ---- texture packs: not supported on Xbox (no spare RAM for HD textures) ---- */
void pc_texture_pack_init(void) {}
void pc_texture_pack_preload_all(void) {}
void pc_texture_pack_shutdown(void) {}
int  pc_texture_pack_active(void) { return 0; }
GLuint pc_texture_pack_lookup(const void* data, int data_size, int w, int h, unsigned int fmt,
                              const void* tlut_data, int tlut_entries, int tlut_is_be,
                              int* out_w, int* out_h) {
    (void)data; (void)data_size; (void)w; (void)h; (void)fmt;
    (void)tlut_data; (void)tlut_entries; (void)tlut_is_be; (void)out_w; (void)out_h;
    return 0;
}

void pc_platform_init(void) {
    /* the controllers are up already (main_body, for safe video): init them
     * once, so leave_game's SDL_QuitSubSystem still shuts them down */
    Uint32 pads = SDL_WasInit(SDL_INIT_GAMECONTROLLER) ? 0 : SDL_INIT_GAMECONTROLLER;
    if (SDL_Init(pads | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        xbox_logf("[XBOX] SDL_Init failed: %s\n", SDL_GetError());
    }
    if (!xbox_nv2a_init()) {
        static const char* const lines[] = { "The NV2A graphics backend failed to start.", "See the COM1 log for details.", NULL };
        xbox_splash_error("Graphics init failed", lines);
    }
    xbox_gl_nv2a_load();
    pc_gx_init();
}

/* Quit Game (title or pause menu) ends the game loop; src/main.c then calls
 * this and exit(0). nxdk's exit reboots, which relaunches a disc or shows
 * the boot animation, so go to the dashboard directly instead. Saves are
 * already flushed on close; xbox_quit_to_dashboard stops the sound and USB
 * before the launch (the quick reboot doesn't). */
void pc_platform_shutdown(void) {
    xbox_quit_to_dashboard();
}

/* the logical screen follows the widescreen setting (xbox_settings.c) */
void pc_platform_update_window_size(void) {
    xbox_settings_apply();
}

static volatile unsigned int s_frames;

unsigned int xbox_frame_count(void) { return s_frames; }

#ifdef XBOX_DBG_NES_TEST
/* test the NES screen path (pc_nes_fixnes.c -> xbox_nv2a.c blit): from frame
 * XBOX_DBG_NES_TEST, draw 120 frames of RGB565 colour bars over the game */
extern void pc_fixnes_render_frame(uint16_t* fb);
static void nes_test_frame(unsigned f) {
    static uint16_t fb[256 * 240];
    int x, y;
    if (f < XBOX_DBG_NES_TEST || f >= XBOX_DBG_NES_TEST + 120) return;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 256; x++) {
            unsigned bar = (unsigned)x / 32, v = (unsigned)y * 31 / 239;
            unsigned r = (bar & 1) ? v : 0, g = (bar & 2) ? v * 2 : 0, b = (bar & 4) ? v : 0;
            fb[y * 256 + x] = (uint16_t)(r | (g << 5) | (b << 11));   /* red in the low bits */
        }
    pc_fixnes_render_frame(fb);
}
#endif

void pc_platform_swap_buffers(void) {
#ifdef XBOX_DBG_NES_TEST
    nes_test_frame(s_frames);
#endif
    pc_gx_draw_pending();
    xbox_nv2a_present();
    s_frames++;
#ifdef XBOX_DBG_CRASH_FRAME
    /* test the exception reporter (xbox_crash.c) */
    if (s_frames == XBOX_DBG_CRASH_FRAME) *(volatile int*)4 = 1;
#endif
    /* a heartbeat for the boot: once a second until frame 120; after that
     * the watchdog's [BEAT] carries the frame count */
    if (s_frames <= 120 && s_frames % 60u == 0) xbox_logf("[XBOX] frame %u\n", s_frames);
    /* boot.log's per-line HDD flushes cost ~45 ms each on hardware: from
     * here on lines are queued and the watchdog writes them once a second
     * (xbox_io.c; -DXBOX_LOG_SESSION=0 closes boot.log here instead) */
    if (s_frames == 120) {
        xbox_logf("[XBOX] 120 frames up, boot.log continues buffered\n");
        xbox_bootlog_async();
    }
}

int pc_platform_poll_events(void) {
    SDL_Event event;

    pc_typing_update();
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERBUTTONDOWN:
                /* BACK is the pause menu here, so screenshots are on the right stick click */
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK && g_xbox_settings.screenshots &&
                    !pc_settings_menu_capture_active())
                    xbox_nv2a_shot();
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                    break;
                }
                if (g_pc_paused) {
                    pc_pause_menu_handle_event(&event);
                    break;
                }
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    pc_pause_menu_toggle();
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (pc_settings_menu_capture_active()) {
                    pc_settings_menu_handle_capture_event(&event);
                    break;
                }
                if (g_pc_paused) pc_pause_menu_handle_event(&event);
                break;
        }
    }
    return 1;
}

extern void ac_entry(void);
extern int boot_main(int argc, const char** argv);

/* XBE header: base address at +0x104, image size at +0x10C (the XBE is
 * mapped at 0x10000). seg2k0 treats this range as real pointers. */
static void read_image_range(void) {
    const unsigned char* xbe = (const unsigned char*)0x00010000;
    pc_image_base = *(const unsigned int*)(xbe + 0x104);
    pc_image_end = pc_image_base + *(const unsigned int*)(xbe + 0x10C);
}

static int ends_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcasecmp(s + a - b, suf) == 0;
}

/* Same scan pc_disc.c's find_disc_image() does for "."; done here first so
 * "no file at all" and "a file that isn't Animal Crossing" get different
 * messages. */
static int disc_image_present(char* name, size_t cap) {
    DIR* d = opendir(".");
    struct dirent* e;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        if (ends_ci(e->d_name, ".iso") || ends_ci(e->d_name, ".gcm") || ends_ci(e->d_name, ".ciso")) {
            snprintf(name, cap, "%s", e->d_name);
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

static void fatal_no_disc(void) {
    static const char* const lines[] = {
        "OpenCrossing needs your own copy of",
        "Animal Crossing for GameCube (USA, GAFE01).",
        "",
        "Put the disc image in the SAME FOLDER as default.xbe:",
        "",
        "    OpenCrossing\\default.xbe",
        "    OpenCrossing\\Animal Crossing.iso",
        "",
        "Accepted: .iso  .gcm  .ciso   (any filename)",
        "",
        "Burning a disc? Run make-xiso to pack both into one image.",
        "Then restart the Xbox.",
        NULL,
    };
    xbox_splash_error("No disc image found", lines);
}

static void fatal_bad_disc(const char* name) {
    static char l0[128];
    static const char* lines[] = {
        NULL,
        "",
        "is not an Animal Crossing (USA, GAFE01) GameCube image,",
        "or the file is damaged / only partly copied.",
        "",
        "Supported: GAFE01 Rev 0, as .iso, .gcm or .ciso.",
        "PAL, Japanese and e+ versions are not supported.",
        NULL,
    };
    snprintf(l0, sizeof l0, "\"%s\"", name);
    lines[0] = l0;
    xbox_splash_error("Wrong or damaged disc image", lines);
}

static void fatal_no_assets(void) {
    static const char* const lines[] = {
        "The disc image opened, but the game data inside it",
        "could not be read. Re-copy the image and try again.",
        NULL,
    };
    xbox_splash_error("Could not read game data", lines);
}

static int main_body(void* arg);

/* the whole game runs under the exception reporter (xbox_crash.c) */
int main(void) {
    return xbox_crash_guard(main_body, NULL);
}

static int main_body(void* arg) {
    char disc_name[260];
    (void)arg;

    /* 128 MB consoles run as 64 MB: the RAM above it is held before
     * anything else allocates (xbox_ramlock.c) */
    xbox_mem_lock64();
    xbox_logf("\n[XBOX] OpenCrossing-Xbox boot\n");

    if (!nxIsDriveMounted('E') && !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\"))
        xbox_logf("[XBOX] warning: could not mount E: (saves disabled)\n");
    CreateDirectoryA("E:\\UDATA", NULL);
    CreateDirectoryA(XBOX_UDATA_ROOT, NULL);
    xbox_bootlog_open();
    xbox_watchdog_start();

    xbox_mem_log("boot");
    xbox_mem_lock64_log();
    xbox_clock_check();   /* before anything reads a timer frequency */
    read_image_range();
    xbox_logf("[XBOX] image %08x-%08x\n", pc_image_base, pc_image_end);
    xbox_prof_start();   /* -DXBOX_PROF=1 builds only; this thread runs the game */

    /* controllers before the splash: BACK held as it ends is safe video
     * (480i, xbox_settings_safe_video); USB enumerates during the splash */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
        xbox_logf("[XBOX] SDL gamecontroller init failed: %s\n", SDL_GetError());
    xbox_settings_early();
    xbox_splash_show();

    if (!disc_image_present(disc_name, sizeof disc_name)) fatal_no_disc();
    xbox_logf("[XBOX] disc image: %s\n", disc_name);
    xbox_splash_progress(0.1f);

    pc_settings_load();
    xbox_settings_safe_video();
    pc_keybindings_load();
#ifdef XBOX_DBG_WEATHER
    /* test runs: force the weather (1 rain, 2 snow...; mEnv_WEATHER_*) */
    g_pc_weather_override = XBOX_DBG_WEATHER;
#endif

    /* Assets BEFORE the GPU backend: pc_assets_init() holds the compressed
     * (6 MB) and decompressed (15.6 MB) REL at once, then frees both. The
     * NV2A texture pool, vertex ring and framebuffers come after that peak. */
    if (!pc_disc_init()) fatal_bad_disc(disc_name);
    xbox_splash_progress(0.3f);
    xbox_mem_log("before assets");
    if (!pc_assets_init()) fatal_no_assets();
    xbox_mem_log("after assets");
    xbox_splash_progress(0.8f);

    xbox_logf("[XBOX] stage: nv2a init\n");
    pc_platform_init();
    xbox_settings_apply();   /* 720p is decided at GPU init: always 16:9 */
    xbox_mem_log("after nv2a init");
    xbox_splash_progress(1.0f);

    xbox_logf("[XBOX] stage: game entry\n");
    ac_entry();
    boot_main(0, NULL);   /* doesn't return: src/main.c quits through pc_platform_shutdown */
    return 0;
}
