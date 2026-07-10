/* Shared settings page used by both the in-game pause
 * menu and the title-screen Options menu. */

#ifndef PC_SETTINGS_MENU_H
#define PC_SETTINGS_MENU_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* Snapshot g_pc_settings into the pending buffer, reset selection.
 * Call when opening the menu from either host (pause or title). */
void pc_settings_menu_enter(void);

/* 1 while the menu (settings page or res-confirm sub-page) is active. */
int  pc_settings_menu_active(void);

/* Navigation. Each returns 1 if the menu should stay open, 0 if the caller
 * should close (user picked Back from the settings page). */
int  pc_settings_menu_nav_up(void);
int  pc_settings_menu_nav_down(void);
int  pc_settings_menu_nav_left(void);
int  pc_settings_menu_nav_right(void);
int  pc_settings_menu_confirm(void);
int  pc_settings_menu_cancel(void); /* Esc/B - reverts on res-confirm, else closes */

/* Call once per frame while the revert menu is active. Auto-reverts if the 15s expires. */
void pc_settings_menu_tick(void);

/* Keybinding capture (bindings editor page). While capture is active the
 * host must feed raw SDL events here instead of translating them to nav
 * calls; pc_main routes key/mouse/controller events based on _active. */
int  pc_settings_menu_capture_active(void);
int  pc_settings_menu_handle_capture_event(const SDL_Event* e); /* 1 = consumed */

/* Like _active but stays up a few frames after a capture completes, so
 * pad-polling hosts (title menu) don't react to the just-pressed input. */
int  pc_settings_menu_capture_blocking(void);

/* Draw into NOW_FONT_DISP. with_dim_backdrop=1 dims the full screen behind
 * the menu (pause-menu style); =0 draws directly on top (title-screen
 * style). Font projection must already be loaded by the caller. */
void pc_settings_menu_draw(struct game_s* game, int with_dim_backdrop);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_MENU_H */
