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

typedef struct {
    int stick_deadzone; /* left stick radial dead zone, percent 0-60 */
    int rumble;         /* motor strength, percent 0-100 (0 = off) */
    int video_720p;     /* 1 = 1280x720 output; needs a restart */
    int widescreen;     /* XBOX_WS_*: 16:9 picture (anamorphic at 480) */
    int gpu_overlap;    /* hidden (settings.ini only): 0 = drain the GPU at present,
                         * for A/B tests on hardware; read once at GPU init */
} XboxSettings;

extern XboxSettings g_xbox_settings;
/* what this boot runs with: the output mode is fixed at GPU init */
extern XboxSettings g_xbox_settings_boot;

/* 1 when the dashboard allows 720p on this AV pack (component cable,
 * "720p" ticked in the dashboard's video settings) */
int xbox_video_720p_allowed(void);
/* 1 when this boot actually runs at 720p (xbox_nv2a.c decides at init) */
extern int g_xbox_video_720p;
/* 480i / 480p / 720p: what the encoder is putting out now */
const char* xbox_video_mode_name(void);
/* 1 when s asks for a 16:9 picture (Auto follows the dashboard) */
int xbox_widescreen_wanted(const XboxSettings* s);
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
