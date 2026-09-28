/* xbox_settings.c — the [Xbox] section of settings.ini, and leaving the game.
 *
 * pc_settings.c is compiled with pc_settings_load/save renamed to *_pc
 * (xbox/CMakeLists.txt). The wrappers here run the PC port's loader/writer
 * unchanged, then read/append the [Xbox] section. The PC parser skips keys it
 * doesn't know, and its writer rewrites the whole file, so the section is
 * appended after every PC save.
 *
 * Migration: before settings.ini owned it, the left stick dead zone lived in
 * controller.ini. The first boot without an [Xbox] dead zone takes that file's
 * value (the playtest console keeps its tuned 43%), then settings.ini wins.
 * controller.ini is left on disk, unread. The same first boot turns the shop
 * upgrade's visitor requirement off (pc_settings_load). */
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_platform.h"
#include "pc_settings.h"
#include "xbox_io.h"
#include "xbox_settings.h"

#ifndef XBOX_STICK_DZ
#define XBOX_STICK_DZ 43   /* radial %: playtest pad rests up to 41% off centre */
#endif

void pc_settings_load_pc(void);
void pc_settings_save_pc(void);
int xbox_controller_ini_deadzone(void);   /* xbox_pad_axis.c, -1 if none */
void xbox_watchdog_disable(void);

/* 4:3 by default: 16:9 draws more of the town (hor+), which costs frame time.
 * GPU overlap off until it is measured on hardware (gpu_overlap = 1 tries it). */
XboxSettings g_xbox_settings = { XBOX_STICK_DZ, 100, 0, XBOX_WS_OFF, 0 };
XboxSettings g_xbox_settings_boot = { XBOX_STICK_DZ, 100, 0, XBOX_WS_OFF, 0 };

static const char k_file[] = "settings.ini";
static DWORD encoder_settings(void);

enum { HAVE_DZ = 1, HAVE_RUMBLE = 2, HAVE_720P = 4, HAVE_WS = 8, HAVE_OVERLAP = 16, HAVE_ALL = 31 };

static int parse_int(const char* s, int lo, int hi, int* out) {
    char* end;
    long v = strtol(s, &end, 10);
    if (end == s || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

/* reads [Xbox] keys into g_xbox_settings; returns the HAVE_* found */
static int read_xbox_section(void) {
    char line[256];
    int in_xbox = 0, have = 0;
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
            if (parse_int(val, 0, 1, &g_xbox_settings.gpu_overlap)) have |= HAVE_OVERLAP;
        }
    }
    fclose(f);
    return have;
}

static void append_xbox_section(void) {
    FILE* f = fopen(k_file, "a");
    if (!f) {
        xbox_logf("[Settings] could not append [Xbox] to %s\n", k_file);
        return;
    }
    fprintf(f, "\n[Xbox]\n");
    fprintf(f, "# Left stick dead zone in percent (0-60). 43 suits a worn controller whose\n");
    fprintf(f, "# stick doesn't centre; one in good shape feels better at 15-20.\n");
    fprintf(f, "xbox_stick_deadzone = %d\n", g_xbox_settings.stick_deadzone);
    fprintf(f, "\n# Rumble strength in percent (0 = off)\n");
    fprintf(f, "rumble = %d\n", g_xbox_settings.rumble);
    fprintf(f, "\n# Video output: 0 = standard (480i, or 480p when the dashboard allows it),\n");
    fprintf(f, "# 1 = 720p (component cable + 720p enabled in the dashboard). Needs a restart.\n");
    fprintf(f, "video_720p = %d\n", g_xbox_settings.video_720p);
    fprintf(f, "\n# Widescreen: 0 = 4:3, 1 = 16:9, 2 = follow the dashboard setting\n");
    fprintf(f, "widescreen = %d\n", g_xbox_settings.widescreen);
    fprintf(f, "\n# Testing: 1 = the next frame's game logic runs while the GPU draws,\n");
    fprintf(f, "# 0 = wait for the GPU at the end of every frame (the old way). Needs a restart.\n");
    fprintf(f, "gpu_overlap = %d\n", g_xbox_settings.gpu_overlap);
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
    if (!(have & HAVE_DZ)) {
        int dz = xbox_controller_ini_deadzone();
        if (dz >= 0) {
            g_xbox_settings.stick_deadzone = dz;
            xbox_logf("[Settings] stick dead zone %d%% taken over from controller.ini\n", dz);
        }
    }
    if (have != HAVE_ALL) pc_settings_save();   /* write the missing keys once */
    g_xbox_settings_boot = g_xbox_settings;
    xbox_logf("[Settings] Xbox: stick dead zone %d%%, rumble %d%%, 720p %d, widescreen %d, gpu overlap %d "
              "(encoder %08x)\n",
              g_xbox_settings.stick_deadzone, g_xbox_settings.rumble, g_xbox_settings.video_720p,
              g_xbox_settings.widescreen, g_xbox_settings.gpu_overlap, (unsigned)encoder_settings());
    xbox_settings_apply();
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

const char* xbox_video_mode_name(void) {
    DWORD enc = encoder_settings();
    if (g_xbox_video_720p) return "720p";
    /* nxdk's mode table picks 480p for 640x480 whenever the dashboard allows
     * it on a component (HDTV) AV pack */
    if ((enc & 0xFF) == AV_PACK_HDTV && (enc & VIDEO_MODE_480P)) return "480p";
    return "480i";
}

int xbox_widescreen_wanted(const XboxSettings* s) {
    if (s->widescreen == XBOX_WS_AUTO) return (encoder_settings() & VIDEO_WIDESCREEN) != 0;
    return s->widescreen == XBOX_WS_ON;
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
    pc_audio_shutdown();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    usbh_core_deinit();
    xbox_watchdog_disable();
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

/* D: is the XBE's own folder (nxdk automount); relaunch the same file name */
void xbox_restart(void) {
    char path[300];
    const ANSI_STRING* img = &XeImageFileName[0];
    const char* base = img->Buffer;
    int i;
    for (i = 0; i < img->Length; i++)
        if (img->Buffer[i] == '\\') base = img->Buffer + i + 1;
    snprintf(path, sizeof path, "D:\\%.*s", (int)(img->Length - (base - img->Buffer)), base);
    xbox_logf("[XBOX] restart: %s\n", path);
    leave_game();
    XLaunchXBE(path);
    xbox_logf("[XBOX] restart failed, going to the dashboard\n");
    XLaunchXBE(NULL);
    launch_failed();
}
