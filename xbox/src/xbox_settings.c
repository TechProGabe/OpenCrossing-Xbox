/* xbox_settings.c — the [Xbox] section of settings.ini, and leaving the game.
 *
 * pc_settings.c is compiled with pc_settings_load/save renamed to *_pc
 * (xbox/CMakeLists.txt). The wrappers here run the PC port's loader/writer
 * unchanged, then read/append the [Xbox] section. The PC parser skips keys it
 * doesn't know, and its writer rewrites the whole file, so the section is
 * appended after every PC save.
 *
 * The first boot without an [Xbox] section turns the shop upgrade's visitor
 * requirement off (pc_settings_load). An old controller.ini (the first
 * playtest builds kept the dead zone there) is no longer read: a fresh
 * settings.ini took its 43% over the 40% default. */
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_settings.h"
#include "xbox_io.h"
#include "xbox_settings.h"
#include "xbox_diag.h"   /* XBOX_RTC_SHIM */

#ifndef XBOX_STICK_DZ
#define XBOX_STICK_DZ 40   /* radial %: suits worn controllers, and AC needs no fine aim (was 43) */
#endif
#ifndef XBOX_CSTICK_DZ
#define XBOX_CSTICK_DZ 40  /* per-axis %: past the game's own C-button threshold, worn sticks too */
#endif

void pc_settings_load_pc(void);
void pc_settings_save_pc(void);
void xbox_watchdog_disable(void);

/* Video by default as Melee-X picks it (xhw_video.c, its settings.c
 * defaults): 720p where the dashboard allows it on this AV pack, else 480p
 * where it allows that, else 480i; 16:9 when the dashboard is set to
 * widescreen (720p always is). video_720p = 1 with progressive = 1 is that
 * Auto; video_720p = 0 pins 480, progressive = 0 pins 480i, widescreen 0/1
 * pin 4:3 / 16:9. -DXBOX_VIDEO_AUTO=0 is the kill switch: the old defaults
 * (480, 4:3) and no version-4 move. Explicit settings win either way.
 * GPU overlap on since the Melee-X backport (Melee-X runs it by default since
 * its v33); the backport's switches default to the new behaviour, and 0 in
 * settings.ini puts each one back (docs/backport.md). */
#ifndef XBOX_VIDEO_AUTO
#define XBOX_VIDEO_AUTO 1
#endif
/* one-time moves in pc_settings_load: 2 raised the C-stick dead zone,
 * 3 moved both sticks' old defaults (43% left, 30% C-stick) to 40%,
 * 4 moved the old video defaults (video_720p = 0, widescreen = 0) to Auto */
#define XBOX_OPT_VERSION 4
#ifndef XBOX_FPS_DEFAULT
#define XBOX_FPS_DEFAULT 0   /* the FPS counter's default; 1 for test builds */
#endif
#define XBOX_WS_DEFAULT (XBOX_VIDEO_AUTO ? XBOX_WS_AUTO : XBOX_WS_OFF)
#define XBOX_SETTINGS_DEFAULTS                                                                       \
    { XBOX_STICK_DZ, 100, XBOX_VIDEO_AUTO, XBOX_WS_DEFAULT, 1, 1, 1, 1, 1, 1, 32, 1, XBOX_OPT_VERSION, \
      XBOX_FPS_DEFAULT, 1, 1, 0, 0 }
XboxSettings g_xbox_settings = XBOX_SETTINGS_DEFAULTS;
XboxSettings g_xbox_settings_boot = XBOX_SETTINGS_DEFAULTS;

static const char k_file[] = "settings.ini";
static DWORD encoder_settings(void);

enum { HAVE_DZ = 1, HAVE_RUMBLE = 2, HAVE_720P = 4, HAVE_WS = 8, HAVE_OVERLAP = 16, HAVE_OPT = 32, HAVE_ALL = 63 };

/* the backport's switches: key, field, range */
static const struct { const char* key; int* v; int lo, hi; } k_opt_keys[] = {
    { "native_textures", &g_xbox_settings.native_tex, 0, 1 },
    { "texture_reuse", &g_xbox_settings.tex_reuse, 0, 1 },
    { "draw_skip", &g_xbox_settings.draw_skip, 0, 1 },
    { "vertex_cache_break", &g_xbox_settings.vb_cache_break, 0, 1 },
    { "strict_gpu_wait", &g_xbox_settings.strict_gpu_wait, 0, 1 },
    { "pushbuffer_kick_kb", &g_xbox_settings.pb_kick_kb, 4, 256 },
    { "audio_fix", &g_xbox_settings.audio_fix, 0, 1 },
    { "fps_counter", &g_xbox_settings.fps_counter, 0, 1 },
    { "progressive", &g_xbox_settings.progressive, 0, 1 },
    { "audio_priority", &g_xbox_settings.audio_priority, 0, 1 },
    { "screenshots", &g_xbox_settings.screenshots, 0, 1 },
#if XBOX_RTC_SHIM
    { "rtc_shim", &g_xbox_settings.rtc_shim, 0, 1 },   /* GitHub #3 (xbox_diag.c) */
#endif
};
#define N_OPT_KEYS ((int)(sizeof k_opt_keys / sizeof k_opt_keys[0]))

static int parse_int(const char* s, int lo, int hi, int* out) {
    char* end;
    long v = strtol(s, &end, 10);
    if (end == s || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

/* reads [Xbox] keys into g_xbox_settings; returns the HAVE_* found */
static int s_file_version;   /* opt_version the file had */

static int read_xbox_section(void) {
    char line[256];
    int in_xbox = 0, have = 0, overlap = 1, version = 0, opt_have = 0;
    FILE* f = fopen(k_file, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *eq, *key, *val, *e;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            in_xbox = strncmp(p, "[Xbox]", 6) == 0;
            continue;
        }
        if (!in_xbox || *p == '#' || *p == ';' || !(eq = strchr(p, '='))) continue;
        *eq = '\0';
        key = p;
        for (e = eq; e > key && (e[-1] == ' ' || e[-1] == '\t'); e--) e[-1] = '\0';
        val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (!strcmp(key, "xbox_stick_deadzone")) {
            if (parse_int(val, 0, 60, &g_xbox_settings.stick_deadzone)) have |= HAVE_DZ;
        } else if (!strcmp(key, "rumble")) {
            if (parse_int(val, 0, 100, &g_xbox_settings.rumble)) have |= HAVE_RUMBLE;
        } else if (!strcmp(key, "video_720p")) {
            if (parse_int(val, 0, 1, &g_xbox_settings.video_720p)) have |= HAVE_720P;
        } else if (!strcmp(key, "widescreen")) {
            if (parse_int(val, 0, 2, &g_xbox_settings.widescreen)) have |= HAVE_WS;
        } else if (!strcmp(key, "gpu_overlap")) {
            if (parse_int(val, 0, 1, &overlap)) have |= HAVE_OVERLAP;
        } else if (!strcmp(key, "opt_version")) {
            parse_int(val, 0, 1000, &version);
        } else {
            int k;
            for (k = 0; k < N_OPT_KEYS; k++)
                if (!strcmp(key, k_opt_keys[k].key) && parse_int(val, k_opt_keys[k].lo, k_opt_keys[k].hi, k_opt_keys[k].v))
                    opt_have++;
        }
    }
    fclose(f);
    /* Files from before the backport were written with gpu_overlap = 0, the
     * old default: that value was nobody's choice, so it gives way to the
     * new default once, and the save below writes opt_version. */
    if ((have & HAVE_OVERLAP) && version >= 1) g_xbox_settings.gpu_overlap = overlap;
    s_file_version = version;
    if (version >= XBOX_OPT_VERSION && opt_have == N_OPT_KEYS) have |= HAVE_OPT;
    g_xbox_settings.opt_version = XBOX_OPT_VERSION;
    return have;
}

/* Before the splash: the [Xbox] keys alone, for progressive, so a console
 * saved at 480i shows the splash and the no-disc screen at 480i too (the
 * full load, pc_settings_load, comes after them and reads it again). */
void xbox_settings_early(void) {
    read_xbox_section();
    g_xbox_settings_boot.progressive = g_xbox_settings.progressive;
}

static void append_xbox_section(void) {
    FILE* f = fopen(k_file, "a");
    if (!f) {
        xbox_logf("[Settings] could not append [Xbox] to %s\n", k_file);
        return;
    }
    fprintf(f, "\n[Xbox]\n");
    fprintf(f, "# Left stick dead zone in percent (0-60). 40 by default, for worn sticks that\n");
    fprintf(f, "# don't centre; 15-20 feels better on a controller in good shape.\n");
    fprintf(f, "xbox_stick_deadzone = %d\n", g_xbox_settings.stick_deadzone);
    fprintf(f, "\n# Rumble strength in percent (0 = off)\n");
    fprintf(f, "rumble = %d\n", g_xbox_settings.rumble);
    fprintf(f, "\n# Video output. video_720p = 1 with progressive = 1 is Auto, the best mode the\n");
    fprintf(f, "# dashboard allows: 720p (component cable + 720p enabled in the dashboard),\n");
    fprintf(f, "# else 480p, else 480i. 0 = stay at 480. Needs a restart.\n");
    fprintf(f, "video_720p = %d\n", g_xbox_settings.video_720p);
    fprintf(f, "\n# 480 output: 1 = 480p where the dashboard allows it, 0 = always 480i.\n");
    fprintf(f, "# Hold BACK while OpenCrossing starts for 480i (sets video_720p and this to 0).\n");
    fprintf(f, "progressive = %d\n", g_xbox_settings.progressive);
    fprintf(f, "\n# On-screen frame rate: 1 = on (Options > Video)\n");
    fprintf(f, "fps_counter = %d\n", g_xbox_settings.fps_counter);
    fprintf(f, "\n# Screenshots: 1 = clicking the right stick saves shot00.bmp, shot01.bmp...\n");
    fprintf(f, "# next to this file (handy for bug reports). Options > Video\n");
    fprintf(f, "screenshots = %d\n", g_xbox_settings.screenshots);
    fprintf(f, "\n# Widescreen: 0 = 4:3, 1 = 16:9, 2 = Auto (the dashboard's setting).\n");
    fprintf(f, "# 720p is always 16:9.\n");
    fprintf(f, "widescreen = %d\n", g_xbox_settings.widescreen);
    fprintf(f, "\n# Testing: 1 = the next frame's game logic runs while the GPU draws,\n");
    fprintf(f, "# 0 = wait for the GPU at the end of every frame (the old way). Needs a restart.\n");
    fprintf(f, "gpu_overlap = %d\n", g_xbox_settings.gpu_overlap);
    fprintf(f, "\n# Testing (docs/backport.md): each 1 is a speed or stability change from\n");
    fprintf(f, "# Melee-X; set one to 0 to go back to the old behaviour. Needs a restart.\n");
    fprintf(f, "native_textures = %d\n", g_xbox_settings.native_tex);
    fprintf(f, "texture_reuse = %d\n", g_xbox_settings.tex_reuse);
    fprintf(f, "draw_skip = %d\n", g_xbox_settings.draw_skip);
    fprintf(f, "vertex_cache_break = %d\n", g_xbox_settings.vb_cache_break);
    fprintf(f, "strict_gpu_wait = %d\n", g_xbox_settings.strict_gpu_wait);
    fprintf(f, "# 16 = the old value\n");
    fprintf(f, "pushbuffer_kick_kb = %d\n", g_xbox_settings.pb_kick_kb);
    fprintf(f, "audio_fix = %d\n", g_xbox_settings.audio_fix);
    fprintf(f, "audio_priority = %d\n", g_xbox_settings.audio_priority);
#if XBOX_RTC_SHIM
    fprintf(f, "\n# Troubleshooting build for GitHub #3: 1 = the Stalk Market date code\n");
    fprintf(f, "# (lbRTC_Sub_DD) runs a plain copy instead of the game's own. Needs a restart.\n");
    fprintf(f, "rtc_shim = %d\n", g_xbox_settings.rtc_shim);
#endif
    fprintf(f, "opt_version = %d\n", g_xbox_settings.opt_version);
    fclose(f);
}

void pc_settings_save(void) {
    pc_settings_save_pc();
    append_xbox_section();
}

void pc_settings_load(void) {
    int have;
    pc_settings_load_pc();
    have = read_xbox_section();
    if (have == 0) {
        /* No [Xbox] section: settings.ini is new, or from a build without
         * the Options menu, so its PC keys are defaults nobody picked. The
         * shop upgrade defaults to single player here: Nookington's needs a
         * shopper from another town, and on the Xbox that means copying a
         * second town into save/card_b over FTP. */
        g_pc_settings.disable_shop_visitor_req = 1;
    }
    /* Until version 4 the video defaults were video_720p = 0 and widescreen
     * = 0 (4:3), and the writer puts every key in the file: left as they
     * were, they were nobody's choice, so they move to Auto once (Melee-X's
     * defaults). progressive = 0 was a choice (480i in the menu, or safe
     * video), so that file keeps video_720p = 0. A 480p or 4:3 picked in an
     * older build can't be told from the default and moves too. */
    if (XBOX_VIDEO_AUTO && s_file_version < 4) {
        if ((have & HAVE_720P) && !g_xbox_settings.video_720p && g_xbox_settings.progressive) {
            xbox_logf("[Settings] video_720p 0 -> 1 (Auto: 720p where the dashboard allows it)\n");
            g_xbox_settings.video_720p = 1;
        }
        if ((have & HAVE_WS) && g_xbox_settings.widescreen == XBOX_WS_OFF) {
            xbox_logf("[Settings] widescreen 0 -> 2 (Auto: the dashboard's setting)\n");
            g_xbox_settings.widescreen = XBOX_WS_AUTO;
        }
    }
    /* AC turns the C-stick into the N64 C buttons past ~23% (29 of 127,
     * contreaddata.c), and the choice lists scroll on C-down: a worn right
     * stick resting a quarter off centre scrolled every list down by itself
     * (2026-10-03). 40% on the Xbox (30% at first), raised once for older files. */
    if (s_file_version < 2 && g_pc_settings.cstick_deadzone < XBOX_CSTICK_DZ) {
        xbox_logf("[Settings] C-stick dead zone %d%% -> %d%%\n", g_pc_settings.cstick_deadzone, XBOX_CSTICK_DZ);
        g_pc_settings.cstick_deadzone = XBOX_CSTICK_DZ;
    }
    /* Until 2026-10-03 the defaults were 43% (left) and 30% (C-stick): a
     * file still holding them never had them chosen, so they move to 40%
     * once. 40% suits the worn controllers most people have. */
    if (s_file_version >= 2 && s_file_version < 3 && g_pc_settings.cstick_deadzone == 30) {
        xbox_logf("[Settings] C-stick dead zone 30%% -> %d%%\n", XBOX_CSTICK_DZ);
        g_pc_settings.cstick_deadzone = XBOX_CSTICK_DZ;
    }
    if ((have & HAVE_DZ) && s_file_version < 3 && g_xbox_settings.stick_deadzone == 43) {
        xbox_logf("[Settings] left stick dead zone 43%% -> %d%%\n", XBOX_STICK_DZ);
        g_xbox_settings.stick_deadzone = XBOX_STICK_DZ;
    }
    if (have != HAVE_ALL) pc_settings_save();   /* write the missing keys once */
    g_xbox_settings_boot = g_xbox_settings;
    xbox_logf("[Settings] Xbox: stick dead zone %d%%, rumble %d%%, 720p %d, progressive %d, widescreen %d, "
              "gpu overlap %d, fps counter %d, screenshots %d (encoder %08x)\n",
              g_xbox_settings.stick_deadzone, g_xbox_settings.rumble, g_xbox_settings.video_720p,
              g_xbox_settings.progressive, g_xbox_settings.widescreen, g_xbox_settings.gpu_overlap,
              g_xbox_settings.fps_counter, g_xbox_settings.screenshots, (unsigned)encoder_settings());
    xbox_logf("[Settings] backport: native_textures %d texture_reuse %d draw_skip %d vertex_cache_break %d "
              "strict_gpu_wait %d pushbuffer_kick_kb %d audio_fix %d audio_priority %d\n",
              g_xbox_settings.native_tex, g_xbox_settings.tex_reuse, g_xbox_settings.draw_skip,
              g_xbox_settings.vb_cache_break, g_xbox_settings.strict_gpu_wait, g_xbox_settings.pb_kick_kb,
              g_xbox_settings.audio_fix, g_xbox_settings.audio_priority);
#if XBOX_RTC_SHIM
    xbox_logf("[Settings] diag: rtc_shim %d\n", g_xbox_settings.rtc_shim);
#endif
    xbox_settings_apply();
}

/* pc_vi.c is built with g_frame_limiter and g_pc_nes_active renamed
 * (xbox/CMakeLists.txt): the real ones (settings.ini max_fps, NES play) are
 * these, and its timer limiter sees what the policy below leaves it. */
u32 g_frame_limiter = 60;
extern u32 g_xbox_vi_frame_limit;   /* pc_vi.c's g_frame_limiter */
int g_xbox_vi_nes_pace;             /* pc_vi.c's g_pc_nes_active */

int xbox_vi_pace_policy(int vbl_ok) {
    int vbl = vbl_ok && (g_frame_limiter == 60 || g_pc_nes_active);
    g_xbox_vi_frame_limit = vbl ? 0 : g_frame_limiter;
    g_xbox_vi_nes_pace = vbl ? 0 : g_pc_nes_active;
    return vbl;
}

/* The encoder settings (AV pack, dashboard video flags) can't change while
 * the game runs; the Options page asks every frame, so read them once. */
static DWORD encoder_settings(void) {
    static DWORD s_enc;
    static int s_have;
    if (!s_have) {
        s_enc = XVideoGetEncoderSettings();
        s_have = 1;
    }
    return s_enc;
}

int g_xbox_safe_video_held;

/* Safe video (from Melee-X): BACK held on any controller as the splash ends
 * means the TV may not show the saved mode. This boot and the next run at
 * 480i: video_720p and progressive are saved as 0 (Output 480i, an explicit
 * choice that no default or migration undoes), and Options > Video > Output
 * goes back to Auto. */
void xbox_settings_safe_video(void) {
    if (!g_xbox_safe_video_held) return;
    g_xbox_settings.video_720p = 0;
    g_xbox_settings.progressive = 0;
    pc_settings_save();
    g_xbox_settings_boot = g_xbox_settings;
    xbox_logf("[VIDEO] BACK held at boot: 480i (video_720p and progressive saved as 0)\n");
}

/* 480p, and so a choice between 480i and 480p: the component (HDTV) pack
 * with 480p allowed in the dashboard, NTSC (nxdk has no PAL progressive
 * modes, and no 480i HDTV mode to force on other packs) */
int xbox_video_480p_allowed(void) {
    DWORD enc = encoder_settings();
    return (enc & VIDEO_MODE_480P) && (enc & VIDEO_ADAPTER_MASK) == AV_PACK_HDTV &&
           (enc & VIDEO_STANDARD_MASK) != 0x00000300 /* PAL */;
}

/* 640x480x32 for the splash, the error screens and the GPU: 480i where the
 * dashboard would give 480p but progressive = 0 (settings.ini, or safe
 * video). XVideoSetMode always picks 480p on an HDTV pack set to 480p, so
 * nxdk's own XVideoInit sets the 640x480i mode after it has recorded the
 * size for pbkit (Melee-X's set_mode_480). The splash reads progressive
 * from xbox_settings_early. */
void XVideoInit(DWORD dwMode, int width, int height, int bpp);   /* nxdk hal/video.c */
#define MODE_640x480I_HDTV 0x0801010du
int xbox_video_set_480(void) {
    if (!XVideoSetMode(640, 480, 32, REFRESH_DEFAULT)) return 0;
    if (!g_xbox_settings_boot.progressive && xbox_video_480p_allowed())
        XVideoInit(MODE_640x480I_HDTV, 640, 480, 32);
    return 1;
}

int xbox_video_720p_allowed(void) {
    static int s_allowed = -1;
    if (s_allowed < 0) {
        VIDEO_MODE vm;
        void* p = NULL;
        s_allowed = 0;
        while (XVideoListModes(&vm, 16, REFRESH_DEFAULT, &p))
            if (vm.width == 1280 && vm.height == 720) s_allowed = 1;
    }
    return s_allowed;
}

int xbox_widescreen_wanted(const XboxSettings* s) {
    if (s->widescreen == XBOX_WS_AUTO) return (encoder_settings() & VIDEO_WIDESCREEN) != 0;
    return s->widescreen == XBOX_WS_ON;
}

/* The output a boot with s runs, unless 720p fails to start (memory, GPU
 * init): the settings only allow a mode, the dashboard has to allow it too
 * (Melee-X's video_used, menu.c). 720p needs the 16:9 logical screen. */
int xbox_video_output(const XboxSettings* s) {
    if (XBOX_WIDESCREEN && s->video_720p && xbox_video_720p_allowed()) return XBOX_OUT_720P;
    return s->progressive && xbox_video_480p_allowed() ? XBOX_OUT_480P : XBOX_OUT_480I;
}

/* After GPU init: the mode this boot runs, what chose it, and the
 * dashboard's video flags (the EEPROM's, as the encoder reports them). */
void xbox_video_log(void) {
    static const char* const names[] = { "480i", "480p", "720p" };
    DWORD enc = encoder_settings();
    const XboxSettings* b = &g_xbox_settings_boot;
    int out = g_xbox_video_720p ? XBOX_OUT_720P : b->progressive && xbox_video_480p_allowed() ? XBOX_OUT_480P :
                                                                                                XBOX_OUT_480I;
    xbox_logf("[VIDEO] %s %s: output %s, widescreen %s (video_720p %d, progressive %d, widescreen %d)\n",
              names[out], g_pc_window_w > PC_SCREEN_WIDTH ? "16:9" : "4:3",
              b->video_720p && b->progressive ? "Auto" : names[xbox_video_output(b)],
              b->widescreen == XBOX_WS_AUTO ? "Auto" : b->widescreen == XBOX_WS_ON ? "16:9" : "4:3", b->video_720p,
              b->progressive, b->widescreen);
    xbox_logf("[VIDEO] dashboard: 480p %d, 720p %d, 1080i %d, widescreen %d, letterbox %d, AV pack %u, %s "
              "(encoder %08x; 480p usable %d, 720p usable %d)\n",
              (enc & VIDEO_MODE_480P) != 0, (enc & VIDEO_MODE_720P) != 0, (enc & VIDEO_MODE_1080I) != 0,
              (enc & VIDEO_WIDESCREEN) != 0, (enc & VIDEO_LETTERBOX) != 0, (unsigned)(enc & VIDEO_ADAPTER_MASK),
              (enc & VIDEO_STANDARD_MASK) == 0x00000300 ? "PAL" : "NTSC", (unsigned)enc, xbox_video_480p_allowed(),
              xbox_video_720p_allowed());
}

/* The game draws into a logical screen of g_pc_window_w x g_pc_window_h;
 * pc_gx.c (built with PC_ENHANCEMENTS) widens the 3D view (hor+) and
 * pillarboxes 2D art when it is wider than 4:3. xbox_nv2a.c scales the
 * logical screen onto the real framebuffer: 854x480 onto 640x480 is the
 * anamorphic squeeze a 16:9 TV undoes. 720p is always 16:9. */
void xbox_settings_apply(void) {
#if XBOX_WIDESCREEN
    int ws = g_xbox_video_720p || xbox_widescreen_wanted(&g_xbox_settings);
#else
    int ws = 0;
#endif
    g_pc_window_w = ws ? 854 : PC_SCREEN_WIDTH;
    g_pc_window_h = PC_SCREEN_HEIGHT;
}

void usbh_core_deinit(void);   /* nxdk libusbohci */

/* XLaunchXBE quick-reboots into the next XBE without resetting the devices,
 * so anything still doing DMA keeps going while the dashboard loads. pbkit
 * stops the GPU from its own shutdown notification; the rest is ours. Left
 * running, the AC97 loops its last buffer, and the USB host controller keeps
 * writing its frame counter and done queue into RAM every millisecond (SDL's
 * joystick quit leaves it on on purpose): the second quit in one power-on
 * hung on a looping sound and then showed error 21. */
static void leave_game(void) {
    xbox_bootlog_pump();   /* the quit line and what came before it */
    pc_audio_shutdown();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    usbh_core_deinit();
    xbox_watchdog_disable();
    xbox_bootlog_pump();
}

/* XLaunchXBE only returns when it couldn't set up the launch; the pad and
 * the sound are already down by then, so reboot rather than carry on */
static void launch_failed(void) {
    xbox_logf("[XBOX] launch failed, rebooting\n");
    HalReturnToFirmware(HalRebootRoutine);
}

void xbox_quit_to_dashboard(void) {
    xbox_logf("[XBOX] quit: back to the dashboard\n");
    leave_game();
    XLaunchXBE(NULL);
    launch_failed();
}

/* Relaunch this XBE by the kernel's own path for it
 * (\Device\Harddisk0\Partition6\...\default.xbe, \Device\CdRom0\...), as
 * Melee-X does (xhw_reboot_self): XLaunchXBE takes \Device\ paths as they
 * are, while the DOS path D:\default.xbe became \??\D:;default.xbe in the
 * launch data, the launch failed and the restart fell through to the
 * dashboard. */
void xbox_restart(void) {
    char path[300];
    const ANSI_STRING* img = &XeImageFileName[0];
    snprintf(path, sizeof path, "%.*s", (int)img->Length, img->Buffer);
    xbox_logf("[XBOX] restart: %s\n", path);
    leave_game();
    XLaunchXBE(path);
    xbox_logf("[XBOX] restart failed, going to the dashboard\n");
    XLaunchXBE(NULL);
    launch_failed();
}
