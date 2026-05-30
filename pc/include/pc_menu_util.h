/* Shared drawing helpers for the PC pause menu and settings menu. */
#ifndef PC_MENU_UTIL_H
#define PC_MENU_UTIL_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;
struct graph_s;

#define PC_MENU_SCALE_SELECTED 1.15f

/* Full-screen translucent black backdrop, drawn into NOW_FONT_DISP. */
void pc_menu_dim_rect(struct graph_s* graph, int alpha);

/* Yellow when selected, grey otherwise. */
void pc_menu_row_colors(int selected, int* r, int* g, int* b, int* a);

/* Horizontally centered text at scale. */
void pc_menu_draw_centered(struct game_s* game, const char* s, f32 y,
                           int r, int g, int b, int a, f32 scale);

void pc_menu_draw_left(struct game_s* game, const char* s, f32 x, f32 y,
                       int r, int g, int b, int a, f32 scale);

void pc_menu_draw_two_choice(struct game_s* game, const char* left,
                             const char* right, int sel, f32 y);

#ifdef __cplusplus
}
#endif

#endif /* PC_MENU_UTIL_H */
