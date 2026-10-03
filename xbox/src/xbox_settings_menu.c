/* xbox_settings_menu.c — the Options page on the Xbox. Replaces
 * pc/src/pc_settings_menu.c (not built) behind the same pc_settings_menu_*
 * API, so the title screen's Options (ac_animal_logo.c) and the pause menu's
 * Settings (pc_pause_menu.c) drive it unchanged.
 *
 * Differences from the PC page: no window/resolution/MSAA/VSync rows (the
 * Xbox has one screen); the video output and widescreen rows; the left stick
 * dead zone is the Xbox radial one (xbox_pad_axis.c) with a live readout; a
 * rumble row; the bindings page is controller-only, with Xbox button names.
 * Layout and navigation follow upstream so both menus feel the same.
 *
 * Only the game font's glyphs render (see glyph notes in pc_settings_menu.c):
 * no '/', '+', '*', '[' in any string here. */
#include "pc_settings_menu.h"
#include "pc_settings.h"
#include "pc_keybindings.h"
#include "pc_menu_util.h"
#include "pc_text_draw.h"
#include "xbox_settings.h"

#include "graph.h"
#include "m_font.h"
#include "m_rcp.h"
#include "main.h" /* SCREEN_WIDTH_F */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern int g_pc_paused;
int xbox_left_stick_pct(void);        /* xbox_pad_axis.c */
void xbox_rumble_preview(int pct);    /* xbox_pad_axis.c */

enum {
    ITEM_OUTPUT,
    ITEM_WIDESCREEN,
    ITEM_TEXTURE_FILTERING,
    ITEM_MASTER_VOLUME,
    ITEM_STICK_DEADZONE,
    ITEM_CSTICK_DEADZONE,
    ITEM_RUMBLE,
    ITEM_BINDINGS,
    ITEM_RESETTI,
    ITEM_SHOP_VISITOR,
    ITEM_BORDERLESS_ACRES,
    ITEM_NES_ASPECT,
    ITEM_FPS_COUNTER,
};

typedef struct {
    const char* label;
    int         id;
} Item;

static const Item tab_video_items[] = {
#if XBOX_WIDESCREEN   /* both need pc_gx.c's 16:9 path (CMake XBOX_WIDESCREEN) */
    { "Output",         ITEM_OUTPUT },
    { "Widescreen",     ITEM_WIDESCREEN },
#endif
    { "Texture filter", ITEM_TEXTURE_FILTERING },
    { "FPS counter",    ITEM_FPS_COUNTER },
};
static const Item tab_audio_items[] = {
    { "Master volume", ITEM_MASTER_VOLUME },
};
static const Item tab_controls_items[] = {
    { "Stick deadzone",   ITEM_STICK_DEADZONE },
    { "C-stick deadzone", ITEM_CSTICK_DEADZONE },
    { "Rumble",           ITEM_RUMBLE },
    { "Buttons",          ITEM_BINDINGS },
};
static const Item tab_gameplay_items[] = {
    { "Resetti",          ITEM_RESETTI },
    { "Shop upgrade",     ITEM_SHOP_VISITOR },
    { "Borderless acres", ITEM_BORDERLESS_ACRES },
    { "NES aspect",       ITEM_NES_ASPECT },
};

typedef struct {
    const char* name;
    const Item* items;
    int         count;
} Tab;

#define TAB_ITEMS(a) (a), (int)(sizeof(a) / sizeof((a)[0]))
static const Tab s_tabs[] = {
    { "Video",    TAB_ITEMS(tab_video_items) },
    { "Audio",    TAB_ITEMS(tab_audio_items) },
    { "Controls", TAB_ITEMS(tab_controls_items) },
    { "Gameplay", TAB_ITEMS(tab_gameplay_items) },
};
#define TAB_COUNT ((int)(sizeof(s_tabs) / sizeof(s_tabs[0])))
enum { TAB_CONTROLS = 2 };

typedef enum {
    SUB_SETTINGS,
    SUB_CONFIRM_BACK,
    SUB_CONFIRM_RESTART,
    SUB_BINDINGS,
} SubPage;

static int     s_active;
static SubPage s_sub = SUB_SETTINGS;
static int     s_tab;
static int     s_sel = -1;  /* -1 = the tab row */
static int     s_back_sel;    /* 0 = Keep editing, 1 = Discard */
static int     s_restart_sel; /* 0 = Restart, 1 = Later */

/* pending edits, committed on Apply */
static PCSettings   s_pending;
static XboxSettings s_xpending;
static int          s_dirty;

/* ---- bindings page (controller only) ---- */
typedef struct {
    const char* label;
    int off;   /* into PCPadBindings */
} BindRow;
#define BROW(lbl, f) { lbl, (int)offsetof(PCPadBindings, f) }
static const BindRow s_bind_rows[] = {
    BROW("A", a), BROW("B", b), BROW("X", x), BROW("Y", y), BROW("Start", start),
    BROW("Z", z), BROW("L", l), BROW("R", r),
    BROW("D-Pad Up", dpad_up), BROW("D-Pad Down", dpad_down),
    BROW("D-Pad Left", dpad_left), BROW("D-Pad Right", dpad_right),
};
#define BIND_ROW_COUNT    ((int)(sizeof(s_bind_rows) / sizeof(s_bind_rows[0])))
#define BIND_VISIBLE      9
#define BIND_IDX_DEFAULTS BIND_ROW_COUNT
#define BIND_IDX_BACK     (BIND_ROW_COUNT + 1)

static int s_bind_sel, s_bind_scroll;
static int s_capture;        /* waiting for the next button */
static int s_capture_grace;  /* frames pad-polling hosts stay blocked after capture */

static PCPadCode* bind_slot(int row) {
    return (PCPadCode*)((char*)&g_pc_padbindings + s_bind_rows[row].off);
}

/* rebinding swaps with whichever action already used the button */
static void bind_assign(int row, PCPadCode code) {
    PCPadCode* dst = bind_slot(row);
    int i;
    if (code >= 0)
        for (i = 0; i < BIND_ROW_COUNT; i++) {
            PCPadCode* other = bind_slot(i);
            if (i != row && *other == code) { *other = *dst; break; }
        }
    *dst = code;
}

/* Duke/S names: SDL's shoulders are the White and Black buttons */
static const char* pad_name(PCPadCode code) {
    if (code < 0) return "-";
    if (code & PC_PAD_AXIS_BIT) {
        switch (code & 0xFF) {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  return "L trigger";
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return "R trigger";
        }
        return "?";
    }
    switch (code) {
        case SDL_CONTROLLER_BUTTON_A:             return "A";
        case SDL_CONTROLLER_BUTTON_B:             return "B";
        case SDL_CONTROLLER_BUTTON_X:             return "X";
        case SDL_CONTROLLER_BUTTON_Y:             return "Y";
        case SDL_CONTROLLER_BUTTON_BACK:          return "Back";
        case SDL_CONTROLLER_BUTTON_START:         return "Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L-Stick click";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R-Stick click";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "White";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "Black";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "D-Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "D-Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "D-Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "D-Right";
    }
    return "?";
}

static void bind_fix_scroll(void) {
    if (s_bind_sel >= BIND_ROW_COUNT) return;
    if (s_bind_sel < s_bind_scroll) s_bind_scroll = s_bind_sel;
    if (s_bind_sel >= s_bind_scroll + BIND_VISIBLE) s_bind_scroll = s_bind_sel - BIND_VISIBLE + 1;
}

static void capture_finish(int changed) {
    s_capture = 0;
    s_capture_grace = 15;
    if (changed) pc_keybindings_save();
}

/* ---- pending state ---- */

/* Output: 480i, then 480p and 720p where the dashboard allows them. 480p
 * is progressive = 1 without 720p; 720p keeps progressive as it was, for
 * when it can't start; 480i changes progressive only where 480p exists. */
enum { OUT_480I, OUT_480P, OUT_720P, OUT_N };
static int out_get(const XboxSettings* s) {
    if (s->video_720p) return OUT_720P;
    return s->progressive && xbox_video_480p_allowed() ? OUT_480P : OUT_480I;
}
static int out_allowed(int o) {
    return o == OUT_480I || (o == OUT_480P && xbox_video_480p_allowed()) ||
           (o == OUT_720P && xbox_video_720p_allowed());
}
static void out_set(XboxSettings* s, int o) {
    s->video_720p = o == OUT_720P;
    if (o == OUT_480P) s->progressive = 1;
    else if (o == OUT_480I && xbox_video_480p_allowed()) s->progressive = 0;
}

static int restart_pending(void) {
    return out_get(&s_xpending) != out_get(&g_xbox_settings);
}

/* the running output differs from the saved choice: shown until a restart */
static int restart_outstanding(void) {
    return out_get(&g_xbox_settings) != out_get(&g_xbox_settings_boot);
}

static void recompute_dirty(void) {
    s_dirty = memcmp(&s_pending, &g_pc_settings, sizeof s_pending) != 0 ||
              memcmp(&s_xpending, &g_xbox_settings, sizeof s_xpending) != 0;
}

static int step_clamp(int v, int step, int dir, int lo, int hi) {
    v += dir > 0 ? step : -step;
    return v < lo ? lo : v > hi ? hi : v;
}

static void item_cycle(int id, int dir) {
    switch (id) {
        case ITEM_OUTPUT: {
            int o = out_get(&s_xpending), k;
            for (k = 0; k < OUT_N; k++) {
                o = (o + (dir > 0 ? 1 : OUT_N - 1)) % OUT_N;
                if (out_allowed(o)) break;
            }
            out_set(&s_xpending, o);
            break;
        }
        case ITEM_FPS_COUNTER:
            s_xpending.fps_counter = !s_xpending.fps_counter;
            break;
        case ITEM_WIDESCREEN: /* Off -> On -> Auto */
            s_xpending.widescreen = (s_xpending.widescreen + (dir > 0 ? 1 : 2)) % 3;
            break;
        case ITEM_TEXTURE_FILTERING:
            s_pending.texture_filtering = !s_pending.texture_filtering;
            break;
        case ITEM_MASTER_VOLUME:
            s_pending.master_volume = step_clamp(s_pending.master_volume, 10, dir, 0, 100);
            break;
        case ITEM_STICK_DEADZONE:
            s_xpending.stick_deadzone = step_clamp(s_xpending.stick_deadzone, 2, dir, 0, 60);
            break;
        case ITEM_CSTICK_DEADZONE:
            s_pending.cstick_deadzone = step_clamp(s_pending.cstick_deadzone, 2, dir, 0, 40);
            break;
        case ITEM_RUMBLE:
            s_xpending.rumble = step_clamp(s_xpending.rumble, 25, dir, 0, 100);
            xbox_rumble_preview(s_xpending.rumble);
            break;
        case ITEM_RESETTI:
            s_pending.disable_resetti = !s_pending.disable_resetti;
            break;
        case ITEM_SHOP_VISITOR:
            s_pending.disable_shop_visitor_req = !s_pending.disable_shop_visitor_req;
            break;
        case ITEM_BORDERLESS_ACRES:
            s_pending.borderless_acres = !s_pending.borderless_acres;
            break;
        case ITEM_NES_ASPECT:
            s_pending.nes_aspect = !s_pending.nes_aspect;
            break;
    }
    recompute_dirty();
}

static void item_format(int id, char* buf, size_t n) {
    switch (id) {
        case ITEM_OUTPUT: {
            static const char* const names[OUT_N] = { "480i", "480p", "720p" };
            snprintf(buf, n, "< %s >", names[out_get(&s_xpending)]);
            break;
        }
        case ITEM_FPS_COUNTER:
            snprintf(buf, n, "%s", s_xpending.fps_counter ? "< On >" : "< Off >");
            break;
        case ITEM_WIDESCREEN:
            if (s_xpending.widescreen == XBOX_WS_AUTO)
                snprintf(buf, n, "< Auto, %s >", xbox_widescreen_wanted(&s_xpending) ? "16:9" : "4:3");
            else
                snprintf(buf, n, "< %s >", s_xpending.widescreen ? "16:9" : "4:3");
            break;
        case ITEM_TEXTURE_FILTERING:
            snprintf(buf, n, "%s", s_pending.texture_filtering ? "< On >" : "< Off >");
            break;
        case ITEM_MASTER_VOLUME:
            snprintf(buf, n, "< %d%% >", s_pending.master_volume);
            break;
        case ITEM_STICK_DEADZONE:
            snprintf(buf, n, "< %d%% >", s_xpending.stick_deadzone);
            break;
        case ITEM_CSTICK_DEADZONE:
            snprintf(buf, n, "< %d%% >", s_pending.cstick_deadzone);
            break;
        case ITEM_RUMBLE:
            if (s_xpending.rumble > 0) snprintf(buf, n, "< %d%% >", s_xpending.rumble);
            else snprintf(buf, n, "< Off >");
            break;
        case ITEM_BINDINGS:
            snprintf(buf, n, "Edit...");
            break;
        case ITEM_RESETTI:
            snprintf(buf, n, "%s", s_pending.disable_resetti ? "< Disabled >" : "< Enabled >");
            break;
        case ITEM_SHOP_VISITOR:
            snprintf(buf, n, "%s", s_pending.disable_shop_visitor_req ? "< Singleplayer >" : "< Multiplayer >");
            break;
        case ITEM_BORDERLESS_ACRES:
            snprintf(buf, n, "%s", s_pending.borderless_acres ? "< On >" : "< Off >");
            break;
        case ITEM_NES_ASPECT:
            snprintf(buf, n, "%s", s_pending.nes_aspect ? "< 4:3 >" : "< Stretch >");
            break;
        default:
            buf[0] = '\0';
            break;
    }
}

static int item_changed(int id) {
    switch (id) {
        case ITEM_OUTPUT:            return out_get(&s_xpending) != out_get(&g_xbox_settings);
        case ITEM_FPS_COUNTER:       return s_xpending.fps_counter != g_xbox_settings.fps_counter;
        case ITEM_WIDESCREEN:        return s_xpending.widescreen != g_xbox_settings.widescreen;
        case ITEM_TEXTURE_FILTERING: return s_pending.texture_filtering != g_pc_settings.texture_filtering;
        case ITEM_MASTER_VOLUME:     return s_pending.master_volume != g_pc_settings.master_volume;
        case ITEM_STICK_DEADZONE:    return s_xpending.stick_deadzone != g_xbox_settings.stick_deadzone;
        case ITEM_CSTICK_DEADZONE:   return s_pending.cstick_deadzone != g_pc_settings.cstick_deadzone;
        case ITEM_RUMBLE:            return s_xpending.rumble != g_xbox_settings.rumble;
        case ITEM_RESETTI:           return s_pending.disable_resetti != g_pc_settings.disable_resetti;
        case ITEM_SHOP_VISITOR:      return s_pending.disable_shop_visitor_req != g_pc_settings.disable_shop_visitor_req;
        case ITEM_BORDERLESS_ACRES:  return s_pending.borderless_acres != g_pc_settings.borderless_acres;
        case ITEM_NES_ASPECT:        return s_pending.nes_aspect != g_pc_settings.nes_aspect;
    }
    return 0;
}

static int cur_item_count(void) { return s_tabs[s_tab].count; }
static int idx_apply(void)      { return cur_item_count(); }
static int idx_back(void)       { return cur_item_count() + 1; }

static void apply_pending(void) {
    int restart;
    if (!s_dirty) return;
    restart = restart_pending();
    g_pc_settings = s_pending;
    g_xbox_settings = s_xpending;
    pc_settings_apply();    /* frame limit + borderless acres; no window here */
    xbox_settings_apply();  /* widescreen */
    pc_settings_save();
    s_dirty = 0;
    printf("[SETTINGS] applied\n");
    if (restart && restart_outstanding()) {
        s_sub = SUB_CONFIRM_RESTART;
        s_restart_sel = 1;
    }
}

/* ======================================================================
 * API (pc_settings_menu.h)
 * ====================================================================== */

void pc_settings_menu_enter(void) {
    s_pending = g_pc_settings;
    s_xpending = g_xbox_settings;
    s_dirty = 0;
    s_tab = 0;
    s_sel = -1;
    s_sub = SUB_SETTINGS;
    s_capture = 0;
    s_capture_grace = 0;
    s_active = 1;
}

int pc_settings_menu_active(void) { return s_active; }

int pc_settings_menu_nav_up(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture && s_bind_sel > 0) s_bind_sel--;
        bind_fix_scroll();
        return 1;
    }
    if (s_sub != SUB_SETTINGS) return 1;
    if (s_sel == 0) s_sel = -1;
    else if (s_sel > 0) s_sel--;
    return 1;
}

int pc_settings_menu_nav_down(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture && s_bind_sel < BIND_IDX_BACK) s_bind_sel++;
        bind_fix_scroll();
        return 1;
    }
    if (s_sub != SUB_SETTINGS) return 1;
    if (s_sel == -1) s_sel = 0;
    else if (s_sel < idx_back()) s_sel++;
    return 1;
}

static int nav_horizontal(int dir) {
    if (!s_active) return 0;
    switch (s_sub) {
        case SUB_BINDINGS: return 1;
        case SUB_CONFIRM_BACK: s_back_sel ^= 1; return 1;
        case SUB_CONFIRM_RESTART: s_restart_sel ^= 1; return 1;
        default: break;
    }
    if (s_sel == -1) {
        if (dir < 0 && s_tab > 0) s_tab--;
        else if (dir > 0 && s_tab < TAB_COUNT - 1) s_tab++;
    } else if (s_sel < cur_item_count()) {
        int id = s_tabs[s_tab].items[s_sel].id;
        if (id != ITEM_BINDINGS) item_cycle(id, dir);
    }
    return 1;
}

int pc_settings_menu_nav_left(void)  { return nav_horizontal(-1); }
int pc_settings_menu_nav_right(void) { return nav_horizontal(+1); }

int pc_settings_menu_confirm(void) {
    if (!s_active) return 0;
    switch (s_sub) {
        case SUB_BINDINGS:
            if (s_capture) return 1;
            if (s_bind_sel < BIND_ROW_COUNT) {
                s_capture = 1;
            } else if (s_bind_sel == BIND_IDX_DEFAULTS) {
                pc_keybindings_reset_defaults();
                pc_keybindings_save();
            } else {
                s_sub = SUB_SETTINGS;
            }
            return 1;
        case SUB_CONFIRM_BACK:
            if (s_back_sel == 1) { s_active = 0; return 0; }   /* Discard */
            s_sub = SUB_SETTINGS;
            return 1;
        case SUB_CONFIRM_RESTART:
            if (s_restart_sel == 0) xbox_restart();   /* does not return */
            s_sub = SUB_SETTINGS;
            return 1;
        default: break;
    }
    if (s_sel == -1) {
        s_sel = 0;
    } else if (s_sel < cur_item_count()) {
        int id = s_tabs[s_tab].items[s_sel].id;
        if (id == ITEM_BINDINGS) {
            s_sub = SUB_BINDINGS;
            s_bind_sel = s_bind_scroll = 0;
            s_capture = 0;
        } else {
            item_cycle(id, +1);
        }
    } else if (s_sel == idx_apply()) {
        apply_pending();
    } else if (s_sel == idx_back()) {
        if (s_dirty) {
            s_sub = SUB_CONFIRM_BACK;
            s_back_sel = 0;
            return 1;
        }
        s_active = 0;
        return 0;
    }
    return 1;
}

int pc_settings_menu_cancel(void) {
    if (!s_active) return 0;
    switch (s_sub) {
        case SUB_BINDINGS:
            if (s_capture) capture_finish(0);
            else s_sub = SUB_SETTINGS;
            return 1;
        case SUB_CONFIRM_BACK:
        case SUB_CONFIRM_RESTART:
            s_sub = SUB_SETTINGS;
            return 1;
        default: break;
    }
    if (s_dirty) {
        s_sub = SUB_CONFIRM_BACK;
        s_back_sel = 0;
        return 1;
    }
    s_active = 0;
    return 0;
}

void pc_settings_menu_tick(void) {
    if (s_active && s_capture_grace > 0) s_capture_grace--;
}

int pc_settings_menu_capture_active(void) { return s_active && s_capture; }

int pc_settings_menu_capture_blocking(void) { return s_active && (s_capture || s_capture_grace > 0); }

int pc_settings_menu_handle_capture_event(const SDL_Event* e) {
    int row = s_bind_sel;
    if (!pc_settings_menu_capture_active()) return 0;
    if (row >= BIND_ROW_COUNT) { s_capture = 0; return 1; }
    switch (e->type) {
        case SDL_CONTROLLERBUTTONDOWN:
            /* Back opens the pause menu everywhere else: here it cancels */
            if (e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { capture_finish(0); return 1; }
            bind_assign(row, (PCPadCode)e->cbutton.button);
            capture_finish(1);
            return 1;
        case SDL_CONTROLLERAXISMOTION:
            if (e->caxis.value > 16000 && (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                                           e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
                bind_assign(row, PC_PAD_AXIS_BIT | e->caxis.axis);
                capture_finish(1);
            }
            return 1;
    }
    return 0;
}

/* ======================================================================
 * Drawing (320x240 font space, as upstream)
 * ====================================================================== */

static void value_colors(int selected, int changed, int* r, int* g, int* b, int* a) {
    if (changed) {
        *r = 255; *g = 215; *b = 90;
        *a = selected ? 255 : 200;
    } else {
        pc_menu_row_colors(selected, r, g, b, a);
    }
}

static void draw_tab_row(struct game_s* game, f32 y) {
    int widths[TAB_COUNT], total = 0, t;
    const int gap_px = 18;
    f32 x;
    for (t = 0; t < TAB_COUNT; t++) {
        widths[t] = pc_text_width(s_tabs[t].name);
        total += widths[t];
    }
    total += gap_px * (TAB_COUNT - 1);
    x = (SCREEN_WIDTH_F - (f32)total) * 0.5f;
    for (t = 0; t < TAB_COUNT; t++) {
        int active = t == s_tab, on_row = s_sel == -1, r, g, b, a;
        if (active && on_row) { r = 255; g = 235; b = 120; a = 255; }
        else if (active)      { r = 255; g = 255; b = 255; a = 230; }
        else                  { r = 160; g = 160; b = 160; a = 180; }
        pc_text_draw(game, s_tabs[t].name, x, y, r, g, b, a, (active && on_row) ? PC_MENU_SCALE_SELECTED : 1.0f);
        x += (f32)widths[t] + (f32)gap_px;
    }
}

static void draw_settings_page(struct game_s* game) {
    const Tab* tab = &s_tabs[s_tab];
    const f32 lx = 70.0f, vx = 200.0f, y0 = 78.0f, line_h = 15.0f;
    int r, g, b, a, i, max_items = 0, t;
    f32 apy, bky, hint_y;
    const char* hint = NULL;

    pc_menu_draw_centered(game, "- Options -", 30.0f, 255, 255, 255, 255, 1.0f);
    draw_tab_row(game, 50.0f);

    for (i = 0; i < tab->count; i++) {
        const Item* it = &tab->items[i];
        int selected = s_sel == i;
        char value[64];
        f32 y = y0 + i * line_h, s = selected ? PC_MENU_SCALE_SELECTED : 1.0f;
        item_format(it->id, value, sizeof value);
        pc_menu_row_colors(selected, &r, &g, &b, &a);
        pc_menu_draw_left(game, it->label, lx, y, r, g, b, a, s);
        value_colors(selected, item_changed(it->id), &r, &g, &b, &a);
        pc_menu_draw_left(game, value, vx, y, r, g, b, a, s);
    }

    for (t = 0; t < TAB_COUNT; t++)
        if (s_tabs[t].count > max_items) max_items = s_tabs[t].count;

    apy = y0 + max_items * line_h + 10.0f;
    {
        int sel_apply = s_sel == idx_apply();
        if (sel_apply && s_dirty)  { r = 120; g = 255; b = 140; a = 255; }
        else if (sel_apply)        { r = 160; g = 160; b = 160; a = 220; }
        else if (s_dirty)          { r = 120; g = 220; b = 140; a = 200; }
        else                       { r = 120; g = 120; b = 120; a = 160; }
        pc_menu_draw_centered(game, "Apply", apy, r, g, b, a, sel_apply ? PC_MENU_SCALE_SELECTED : 1.0f);
    }
    bky = apy + line_h;
    pc_menu_row_colors(s_sel == idx_back(), &r, &g, &b, &a);
    pc_menu_draw_centered(game, "Back", bky, r, g, b, a, s_sel == idx_back() ? PC_MENU_SCALE_SELECTED : 1.0f);

    /* one hint line under Back: the most relevant thing for this tab */
    hint_y = bky + line_h + 6.0f;
    if (s_tab == TAB_CONTROLS) {
        /* live stick readout: set the dead zone just above where it rests */
        char buf[64];
        int pct = xbox_left_stick_pct(), moves = pct >= s_xpending.stick_deadzone;
        snprintf(buf, sizeof buf, "Left stick now %d%% (%s)", pct, moves ? "moves" : "ignored");
        if (moves) pc_menu_draw_centered(game, buf, hint_y, 140, 230, 150, 230, 1.0f);
        else       pc_menu_draw_centered(game, buf, hint_y, 170, 170, 170, 210, 1.0f);
        return;
    }
    if (s_tab == 0 && s_xpending.video_720p && !xbox_video_720p_allowed())
        hint = "720p is off in the dashboard";
    else if (s_tab == 0 && s_xpending.video_720p && g_xbox_settings_boot.video_720p && !g_xbox_video_720p)
        hint = "720p failed to start: see boot.log";
    else if (s_tab == 0 && g_xbox_video_720p && s_xpending.widescreen != XBOX_WS_ON)
        hint = "720p is always 16:9";
    else if (restart_outstanding())
        hint = "Restart to change the output";
    if (hint) pc_menu_draw_centered(game, hint, hint_y, 255, 195, 85, 230, 1.0f);
}

static void draw_back_confirm_page(struct game_s* game) {
    pc_menu_draw_centered(game, "- Discard changes? -", 80.0f, 255, 255, 255, 255, 1.0f);
    pc_menu_draw_centered(game, "You have unapplied changes.", 115.0f, 230, 230, 230, 255, 1.0f);
    pc_menu_draw_two_choice(game, "Keep editing", "Discard", s_back_sel, 160.0f);
}

static void draw_restart_page(struct game_s* game) {
    pc_menu_draw_centered(game, "- Restart now? -", 70.0f, 255, 255, 255, 255, 1.0f);
    pc_menu_draw_centered(game, "The new output starts after a restart.", 100.0f, 230, 230, 230, 255, 1.0f);
    if (g_pc_paused)
        pc_menu_draw_centered(game, "Unsaved progress will be lost!", 118.0f, 255, 160, 110, 255, 1.0f);
    pc_menu_draw_two_choice(game, "Restart", "Later", s_restart_sel, 160.0f);
}

static void draw_bindings_page(struct game_s* game) {
    const f32 lx = 70.0f, px = 180.0f, y0 = 62.0f, line_h = 13.0f;
    int r, g, b, a, i;

    pc_menu_draw_centered(game, "- Buttons -", 28.0f, 255, 255, 255, 255, 1.0f);
    pc_menu_draw_left(game, "Action", lx, 46.0f, 150, 150, 150, 200, 1.0f);
    pc_menu_draw_left(game, "Controller", px, 46.0f, 150, 150, 150, 200, 1.0f);

    for (i = 0; i < BIND_VISIBLE; i++) {
        int row = s_bind_scroll + i, sel;
        char buf[48];
        f32 y = y0 + i * line_h;
        if (row >= BIND_ROW_COUNT) break;
        sel = s_bind_sel == row;
        pc_menu_row_colors(sel, &r, &g, &b, &a);
        pc_menu_draw_left(game, s_bind_rows[row].label, lx, y, r, g, b, a, 1.0f);
        if (sel && s_capture) {
            snprintf(buf, sizeof buf, "<press>");
            r = 255; g = 140; b = 90; a = 255;
        } else {
            snprintf(buf, sizeof buf, sel ? "<%s>" : "%s", pad_name(*bind_slot(row)));
        }
        pc_menu_draw_left(game, buf, px, y, r, g, b, a, 1.0f);
    }
    if (s_bind_scroll > 0) pc_menu_draw_left(game, "...", 40.0f, y0, 180, 180, 180, 200, 1.0f);
    if (s_bind_scroll + BIND_VISIBLE < BIND_ROW_COUNT)
        pc_menu_draw_left(game, "...", 40.0f, y0 + (BIND_VISIBLE - 1) * line_h, 180, 180, 180, 200, 1.0f);

    pc_menu_row_colors(s_bind_sel == BIND_IDX_DEFAULTS, &r, &g, &b, &a);
    pc_menu_draw_centered(game, "Restore Defaults", 186.0f, r, g, b, a,
                          s_bind_sel == BIND_IDX_DEFAULTS ? PC_MENU_SCALE_SELECTED : 1.0f);
    pc_menu_row_colors(s_bind_sel == BIND_IDX_BACK, &r, &g, &b, &a);
    pc_menu_draw_centered(game, "Back", 200.0f, r, g, b, a, s_bind_sel == BIND_IDX_BACK ? PC_MENU_SCALE_SELECTED : 1.0f);

    if (s_capture)
        pc_menu_draw_centered(game, "Press a button (Back cancels)", 218.0f, 255, 195, 85, 230, 1.0f);
    else if (s_bind_sel < BIND_ROW_COUNT)
        pc_menu_draw_centered(game, "A to rebind", 218.0f, 150, 150, 150, 180, 1.0f);
}

void pc_settings_menu_draw(struct game_s* game, int with_dim_backdrop) {
    if (!s_active || !game || !game->graph) return;
    if (with_dim_backdrop) pc_menu_dim_rect(game->graph, 180);
    switch (s_sub) {
        case SUB_SETTINGS:        draw_settings_page(game); break;
        case SUB_CONFIRM_BACK:    draw_back_confirm_page(game); break;
        case SUB_CONFIRM_RESTART: draw_restart_page(game); break;
        case SUB_BINDINGS:        draw_bindings_page(game); break;
    }
}
