/* In game pause menu, because at this point it feels weird to not have it. */
#ifndef PC_PAUSE_MENU_H
#define PC_PAUSE_MENU_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct game_s; /* matches typedef in game_h.h */

extern int g_pc_paused;

/* Pausing is blocked while either of these is set (main menu / NES running). */
extern int g_pc_title_main_menu_visible; /* set by ac_animal_logo */
extern int g_pc_nes_active;

/* Toggle pause. ESC handler in pc_main calls this. No-op if pausing is blocked. */
void pc_pause_menu_toggle(void);

/* Forward an SDL event for menu navigation. Returns 1 if consumed. No-op if not paused. */
int  pc_pause_menu_handle_event(const SDL_Event* e);

/* Append the pause overlay to the game's font display list, from graph_main
 * after game_main() returns. No-op if not paused. */
void pc_pause_menu_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif /* PC_PAUSE_MENU_H */
