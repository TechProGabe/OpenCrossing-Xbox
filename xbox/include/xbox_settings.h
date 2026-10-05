/* xbox_settings.h — Xbox-only settings (xbox/src/xbox_settings.c).
 *
 * They live in the [Xbox] section of settings.ini, next to the PC port's own
 * keys. pc_settings.c is compiled with its load/save renamed (CMakeLists), and
 * xbox_settings.c wraps them, so pc/ stays untouched. */
#ifndef XBOX_SETTINGS_H
#define XBOX_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

/* Kill switch for widescreen/720p: -DXBOX_WIDESCREEN=0 builds pc_gx.c,
 * pc_gx_texture.c, m_actor.c and emu64.c without PC_ENHANCEMENTS again and
 * keeps the logical screen at 640x480 (CMakeLists). */
#ifndef XBOX_WIDESCREEN
#define XBOX_WIDESCREEN 1
#endif

enum { XBOX_WS_OFF = 0, XBOX_WS_ON = 1, XBOX_WS_AUTO = 2 };
enum { XBOX_OUT_480I, XBOX_OUT_480P, XBOX_OUT_720P };

typedef struct {
    int stick_deadzone; /* left stick radial dead zone, percent 0-60 */
    int rumble;         /* motor strength, percent 0-100 (0 = off) */
    int video_720p;     /* 1 = 1280x720 where the dashboard allows it (default;
                         * with progressive = 1 that is Output Auto), 0 = stay
                         * at 480; needs a restart */
    int widescreen;     /* XBOX_WS_*: 16:9 picture (anamorphic at 480); Auto by default */
    int gpu_overlap;    /* hidden (settings.ini only): 0 = drain the GPU at present,
                         * for A/B tests on hardware; read once at GPU init */
    /* Hidden switches for the Melee-X backport (docs/backport.md), each read
     * once at boot: 1 = the new behaviour (default), 0 = the old one, so a
     * regression on hardware is undone by editing settings.ini over FTP. */
    int native_tex;     /* textures in the smallest lossless NV2A format */
    int tex_reuse;      /* a re-upload of the same size rewrites the texture in place */
    int draw_skip;      /* per-draw rebuilds only for the state that changed */
    int vb_cache_break; /* BREAK_VERTEX_BUFFER_CACHE at each pushbuffer batch */
    int strict_gpu_wait;/* wait_idle also waits for PFIFO's CACHE1 and pusher */
    int pb_kick_kb;     /* pushbuffer kick size, KB (16 = the old 4096 words) */
    int audio_fix;      /* AC97: queue before the run bit, stuck/halt recovery */
    int opt_version;    /* 1 once the file has the keys above (migration) */
    int fps_counter;    /* on-screen frame rate (Options > Video), applies live */
    int progressive;    /* 0 = 480i even where the dashboard allows 480p; BACK
                         * held at boot sets it and video_720p to 0 (safe video) */
    int audio_priority; /* hidden: the audio producer thread above the game's */
    int screenshots;    /* 1: clicking the right stick saves shotNN.bmp to UDATA (live) */
    int rtc_shim;       /* XBOX_RTC_SHIM builds (GitHub #3, xbox_diag.c): 1 = lbRTC_Sub_DD
                         * runs a plain -O0 copy of its C instead of the game's code */
    int code_repair;    /* 1 = .text bytes patched after load are put back
                         * from default.xbe at boot (xbox_code_repair.c, GitHub #3) */
} XboxSettings;

extern XboxSettings g_xbox_settings;
/* what this boot runs with: the output mode is fixed at GPU init */
extern XboxSettings g_xbox_settings_boot;

/* 1 when the dashboard allows 720p on this AV pack (component cable,
 * "720p" ticked in the dashboard's video settings) */
int xbox_video_720p_allowed(void);
/* the same for 480p (component cable, NTSC: nxdk has no PAL progressive) */
int xbox_video_480p_allowed(void);
/* sets 640x480x32: 480i when progressive = 0 and the pack would give 480p */
int xbox_video_set_480(void);
/* BACK held when the splash ended: 480i, saved (xbox_splash.c sets it,
 * xbox_settings_safe_video applies it after the settings load) */
extern int g_xbox_safe_video_held;
void xbox_settings_safe_video(void);
/* the [Xbox] keys before the splash (its video mode follows progressive) */
void xbox_settings_early(void);
/* 1 when this boot actually runs at 720p (xbox_nv2a.c decides at init) */
extern int g_xbox_video_720p;
/* 1 when s asks for a 16:9 picture (Auto follows the dashboard) */
int xbox_widescreen_wanted(const XboxSettings* s);
/* XBOX_OUT_*: the output a boot with s runs, as the dashboard allows it
 * (720p can still fall back to 480 at GPU init) */
int xbox_video_output(const XboxSettings* s);
/* the [VIDEO] lines after GPU init: mode, aspect, settings, dashboard flags */
void xbox_video_log(void);
/* sets the game's logical screen (g_pc_window_w/h) for the widescreen setting */
void xbox_settings_apply(void);

/* Frame limiter policy (xbox_settings.c). Returns 1 when vblank pacing runs
 * this frame (vbl_ok = pacing built in and the GPU interrupt alive, and
 * max_fps is the default 60 or an NES game is running); pc_vi.c's timer is
 * then off, otherwise it gets max_fps and the NES flag as before. */
int xbox_vi_pace_policy(int vbl_ok);

/* leave the game: back to the dashboard / relaunch this XBE */
void xbox_quit_to_dashboard(void);
void xbox_restart(void);

#ifdef __cplusplus
}
#endif
#endif
