#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_settings_menu.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"

#include "m_font.h"
#include "m_rcp.h"
#include "graph.h"
#include "main.h"       /* SCREEN_WIDTH_F */

#include <stdio.h>
#include <string.h>

int g_pc_paused = 0;

/* Pausing is blocked while either is set: main menu (set by ac_animal_logo)
 * or NES running (its own pause, and pausing it crashes the game). */
int g_pc_title_main_menu_visible = 0;
int g_pc_nes_active = 0;

/* Drains the keys still held from confirming Resume so they don't leak into play. */
int g_pc_pause_input_drain = 0;

/* Pause menu pages */
typedef enum {
    PAGE_MAIN = 0,
    PAGE_SETTINGS = 1,
    PAGE_CONFIRM_QUIT = 2,
} PauseMenuPage;

#define MAIN_ITEM_COUNT     3
#define CONFIRM_ITEM_COUNT  2

static PauseMenuPage cur_page = PAGE_MAIN;
static int main_sel = 0;    /* 0=Resume, 1=Settings, 2=Quit Game */
static int confirm_sel = 0; /* 0=No (default), 1=Yes */

void pc_pause_menu_toggle(void) {
    if (!g_pc_paused && (g_pc_title_main_menu_visible || g_pc_nes_active)) return;

    g_pc_paused = !g_pc_paused;
    if (g_pc_paused) {
        cur_page = PAGE_MAIN;
        main_sel = 0;
    } else {
        g_pc_pause_input_drain = 1;
    }
}

/* Input */

static void main_activate(void) {
    switch (main_sel) {
        case 0: /* Resume */
            pc_pause_menu_toggle();
            break;
        case 1: /* Settings */
            cur_page = PAGE_SETTINGS;
            pc_settings_menu_enter();
            break;
        case 2: /* Quit Game -> confirm page (default to No) */
            cur_page = PAGE_CONFIRM_QUIT;
            confirm_sel = 0;
            break;
    }
}

static void confirm_activate(void) {
    if (confirm_sel == 1) {
        printf("[PAUSE] Quit confirmed\n");
        g_pc_running = 0;
    } else {
        cur_page = PAGE_MAIN;
        main_sel = 0;
    }
}

int pc_pause_menu_handle_event(const SDL_Event* e) {
    if (!g_pc_paused) return 0;
    if (e->type != SDL_KEYDOWN) return 0;
    if (e->key.repeat) return 1;

    SDL_Keycode k = e->key.keysym.sym;

    /* Settings page is driven by pc_settings_menu. */
    if (cur_page == PAGE_SETTINGS) {
        switch (k) {
            case SDLK_UP:
            case SDLK_w:     pc_settings_menu_nav_up();   return 1;
            case SDLK_DOWN:
            case SDLK_s:     pc_settings_menu_nav_down(); return 1;
            case SDLK_LEFT:
            case SDLK_a:     pc_settings_menu_nav_left(); return 1;
            case SDLK_RIGHT:
            case SDLK_d:     pc_settings_menu_nav_right(); return 1;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (!pc_settings_menu_confirm()) cur_page = PAGE_MAIN;
                return 1;
            case SDLK_ESCAPE:
                if (!pc_settings_menu_cancel()) cur_page = PAGE_MAIN;
                return 1;
            default: return 1;
        }
    }

    /* Pages owned here (Main, Quit-confirm). */
    int item_count = (cur_page == PAGE_MAIN) ? MAIN_ITEM_COUNT : CONFIRM_ITEM_COUNT;
    int* sel       = (cur_page == PAGE_MAIN) ? &main_sel : &confirm_sel;

    switch (k) {
        case SDLK_UP:
        case SDLK_w:
        case SDLK_LEFT:
        case SDLK_a:
            *sel = (*sel + item_count - 1) % item_count;
            return 1;
        case SDLK_DOWN:
        case SDLK_s:
        case SDLK_RIGHT:
        case SDLK_d:
            *sel = (*sel + 1) % item_count;
            return 1;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (cur_page == PAGE_MAIN)            main_activate();
            else if (cur_page == PAGE_CONFIRM_QUIT) confirm_activate();
            return 1;
        case SDLK_ESCAPE:
            if (cur_page == PAGE_CONFIRM_QUIT) cur_page = PAGE_MAIN;
            else                                pc_pause_menu_toggle();
            return 1;
        default:
            return 1; /* swallow all keys while paused */
    }
}

/* Drawing */

static void draw_main_page(struct game_s* game) {
    static const char* items[MAIN_ITEM_COUNT] = { "Resume", "Settings", "Quit Game" };

    pc_menu_draw_centered(game, "- Paused -", 80.0f, 255, 255, 255, 255, 1.0f);

    f32 y = 110.0f;
    f32 line_h = 18.0f;
    for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
        int r, g, b, a;
        int selected = (i == main_sel);
        pc_menu_row_colors(selected, &r, &g, &b, &a);
        pc_menu_draw_centered(game, items[i], y + i * line_h, r, g, b, a,
                              selected ? PC_MENU_SCALE_SELECTED : 1.0f);
    }
}

static void draw_confirm_page(struct game_s* game) {
    pc_menu_draw_centered(game, "- Quit Game -", 80.0f, 255, 255, 255, 255, 1.0f);
    pc_menu_draw_centered(game, "Are you sure you want to quit?",
                          115.0f, 230, 230, 230, 255, 1.0f);
    pc_menu_draw_two_choice(game, "No", "Yes", confirm_sel, 150.0f);
}

void pc_pause_menu_draw(struct game_s* game) {
    if (!g_pc_paused || game == NULL || game->graph == NULL) return;

    /* Font ortho projection + identity modelview, else our screen-space quads
     * inherit whatever projection the scene ended its frame with. */
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    pc_settings_menu_tick();

    if (cur_page == PAGE_SETTINGS) {
        /* Settings module owns its own dim backdrop. */
        pc_settings_menu_draw(game, /*with_dim_backdrop=*/1);
        /* The user may have closed it from inside (Back). */
        if (!pc_settings_menu_active()) cur_page = PAGE_MAIN;
    } else {
        pc_menu_dim_rect(game->graph, 180);
        if (cur_page == PAGE_MAIN)              draw_main_page(game);
        else if (cur_page == PAGE_CONFIRM_QUIT) draw_confirm_page(game);
    }

    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}
