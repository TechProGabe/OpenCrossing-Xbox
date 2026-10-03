/* xbox_settings_menu.c — the Options page on the Xbox. Replaces
 * pc/src/pc_settings_menu.c (not built) behind the same pc_settings_menu_*
 * API, so the title screen's Options (ac_animal_logo.c) and the pause menu's
 * Settings (pc_pause_menu.c) drive it unchanged.
 *
 * Differences from the PC page: no window/resolution/MSAA/VSync rows (the
 * Xbox has one screen); the video output and widescreen rows; the left stick
 * dead zone is the Xbox radial one (xbox_pad_axis.c) with a live readout; a
 * rumble row; the bindings page is controller-only, with Xbox button names.
 * Navigation follows upstream so both menus feel the same.
 *
 * The page is drawn like the game's own windows: a cream sheet with a wood
 * border and a green name tag, ruled like a notebook, the game font in brown
 * ink, a help line for the selected row at the bottom (Melee-X's menu). The
 * shapes are untextured, vertex-coloured triangles in the font display list
 * (no textures, no allocations beyond the frame's own arena), drawn in the
 * game's 320x240 font space: 640x480 logical, a centred 4:3 at 16:9.
 *
 * Only the game font's glyphs render (see glyph notes in pc_settings_menu.c):
 * no '/', '+', '*', '[', ';', '#' in any string here. */
#include "pc_settings_menu.h"
#include "pc_settings.h"
#include "pc_keybindings.h"
#include "pc_text_draw.h"
#include "xbox_settings.h"

#include "game.h"
#include "graph.h"
#include "m_font.h"
#include "m_rcp.h"
#include "main.h" /* SCREEN_WIDTH_F */
#include "THA_GA.h"
#include "libforest/gbi_extensions.h" /* G_TRIN_INDEPEND */

#include <math.h>
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
    ITEM_SCREENSHOTS,
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
    { "Screenshots (R-Stick)", ITEM_SCREENSHOTS },
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
static int     s_logged_room; /* display list headroom logged for this visit */

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
        case ITEM_SCREENSHOTS:
            s_xpending.screenshots = !s_xpending.screenshots;
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

/* the value alone: the page draws the arrows */
static void item_format(int id, char* buf, size_t n) {
    switch (id) {
        case ITEM_OUTPUT: {
            static const char* const names[OUT_N] = { "480i", "480p", "720p" };
            snprintf(buf, n, "%s", names[out_get(&s_xpending)]);
            break;
        }
        case ITEM_FPS_COUNTER:
            snprintf(buf, n, "%s", s_xpending.fps_counter ? "On" : "Off");
            break;
        case ITEM_SCREENSHOTS:
            snprintf(buf, n, "%s", s_xpending.screenshots ? "On" : "Off");
            break;
        case ITEM_WIDESCREEN:
            if (s_xpending.widescreen == XBOX_WS_AUTO)
                snprintf(buf, n, "Auto, %s", xbox_widescreen_wanted(&s_xpending) ? "16:9" : "4:3");
            else
                snprintf(buf, n, "%s", s_xpending.widescreen ? "16:9" : "4:3");
            break;
        case ITEM_TEXTURE_FILTERING:
            snprintf(buf, n, "%s", s_pending.texture_filtering ? "On" : "Off");
            break;
        case ITEM_MASTER_VOLUME:
            snprintf(buf, n, "%d%%", s_pending.master_volume);
            break;
        case ITEM_STICK_DEADZONE:
            snprintf(buf, n, "%d%%", s_xpending.stick_deadzone);
            break;
        case ITEM_CSTICK_DEADZONE:
            snprintf(buf, n, "%d%%", s_pending.cstick_deadzone);
            break;
        case ITEM_RUMBLE:
            if (s_xpending.rumble > 0) snprintf(buf, n, "%d%%", s_xpending.rumble);
            else snprintf(buf, n, "Off");
            break;
        case ITEM_BINDINGS:
            snprintf(buf, n, "Edit...");
            break;
        case ITEM_RESETTI:
            snprintf(buf, n, "%s", s_pending.disable_resetti ? "Disabled" : "Enabled");
            break;
        case ITEM_SHOP_VISITOR:
            snprintf(buf, n, "%s", s_pending.disable_shop_visitor_req ? "Singleplayer" : "Multiplayer");
            break;
        case ITEM_BORDERLESS_ACRES:
            snprintf(buf, n, "%s", s_pending.borderless_acres ? "On" : "Off");
            break;
        case ITEM_NES_ASPECT:
            snprintf(buf, n, "%s", s_pending.nes_aspect ? "4:3" : "Stretch");
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
        case ITEM_SCREENSHOTS:       return s_xpending.screenshots != g_xbox_settings.screenshots;
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
    s_logged_room = 0;
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
        case SUB_BINDINGS:
            /* Restore Defaults and Back sit side by side */
            if (!s_capture && s_bind_sel == BIND_IDX_DEFAULTS && dir > 0) s_bind_sel = BIND_IDX_BACK;
            else if (!s_capture && s_bind_sel == BIND_IDX_BACK && dir < 0) s_bind_sel = BIND_IDX_DEFAULTS;
            return 1;
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
    } else if (s_sel == idx_apply() && dir > 0) {   /* Apply and Back sit side by side */
        s_sel = idx_back();
    } else if (s_sel == idx_back() && dir < 0) {
        s_sel = idx_apply();
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
 * Drawing kit: shapes and text in the font display list, 320x240 font space
 * ====================================================================== */

extern void mFont_gppSetMode(Gfx** gfx_pp);   /* m_font.c */

typedef struct { u8 r, g, b, a; } Col;

/* the game's paper, wood and leaf colours */
static const Col k_backdrop  = {  34,  22,  10, 150 };
static const Col k_shadow    = {  40,  24,   8, 105 };
static const Col k_paper_top = { 255, 251, 236, 255 };
static const Col k_paper_bot = { 247, 236, 206, 255 };
static const Col k_wood      = { 150, 102,  58, 255 };
static const Col k_rule      = { 230, 214, 178, 255 };
static const Col k_ink       = {  84,  56,  30, 255 };
static const Col k_ink_soft  = { 128,  98,  66, 255 };
static const Col k_ink_faint = { 178, 154, 118, 255 };
static const Col k_cream     = { 255, 250, 232, 255 };
static const Col k_leaf_top  = { 128, 194,  82, 255 };
static const Col k_leaf_bot  = {  88, 154,  52, 255 };
static const Col k_leaf_edge = {  58, 110,  38, 255 };
static const Col k_tan_top   = { 240, 226, 192, 255 };
static const Col k_tan_bot   = { 226, 208, 168, 255 };
static const Col k_tan_edge  = { 194, 168, 124, 255 };
static const Col k_hl_top    = { 255, 242, 166, 255 };
static const Col k_hl_bot    = { 255, 216, 116, 255 };
static const Col k_hl_edge   = { 228, 150,  52, 255 };
static const Col k_changed   = { 212,  82,  34, 255 };   /* a value not applied yet */
static const Col k_warn      = { 196,  60,  34, 255 };
static const Col k_go        = {  70, 146,  44, 255 };   /* the stick moves the player */
static const Col k_zone      = { 236, 196, 170, 255 };   /* the dead zone on the stick meter */

/* Shapes collect into one vertex load (the 5-bit indices reach 32) and go
 * out as one triangle list when it fills or before any text. */
#define UI_MAX_V 32
#define UI_MAX_T 48
#define UI_ARC   4   /* segments per rounded corner */

static struct {
    struct game_s* game;
    int nv, nt;
    s16 x[UI_MAX_V], y[UI_MAX_V];
    Col c[UI_MAX_V];
    u8  t[UI_MAX_T][3];
    unsigned anim;   /* frames drawn: the cursor arrows bob */
} s_ui;

/* untextured, vertex colour, blended, no Z: undone by the next text draw
 * (mFont_gppSetMode) and at the end of the page */
static Gfx s_shape_mode[] = {
    gsDPPipeSync(),
    gsSPTexture(0, 0, 0, G_TX_RENDERTILE, G_OFF),
    gsSPClearGeometryMode(G_ZBUFFER | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                          G_TEXTURE_GEN_LINEAR | G_LOD),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};

/* gSPNTriangles 5-bit packets, as pc_text_draw.c builds them: the first
 * carries 3 triangles, each next one 4 */
static void ui_trin(Gfx** pp, int n, const u8 (*t)[3]) {
    Gfx* g = *pp;
    int v[12], k, j;
    for (j = 0; j < 9; j++) v[j] = j / 3 < n ? t[j / 3][j % 3] : 0;
    g->words.w1 = ((unsigned)(v[0] & 0x1F) << 4) | ((unsigned)(v[1] & 0x1F) << 9) |
                  ((unsigned)(v[2] & 0x1F) << 14) | ((unsigned)(v[3] & 0x1F) << 19) |
                  ((unsigned)(v[4] & 0x1F) << 24) | ((unsigned)(v[5] & 0x07) << 29);
    g->words.w0 = ((unsigned)((v[5] >> 3) & 0x03) << 0) | ((unsigned)(v[6] & 0x1F) << 2) |
                  ((unsigned)(v[7] & 0x1F) << 7) | ((unsigned)(v[8] & 0x1F) << 12) |
                  ((unsigned)((n - 1) & 0x7F) << 17) | ((unsigned)G_TRIN_INDEPEND << 24);
    g++;
    for (k = 3; k < n; k += 4) {
        for (j = 0; j < 12; j++) v[j] = k + j / 3 < n ? t[k + j / 3][j % 3] : 0;
        g->words.w1 = ((unsigned)(v[0] & 0x1F) << 4) | ((unsigned)(v[1] & 0x1F) << 9) |
                      ((unsigned)(v[2] & 0x1F) << 14) | ((unsigned)(v[3] & 0x1F) << 19) |
                      ((unsigned)(v[4] & 0x1F) << 24) | ((unsigned)(v[5] & 0x07) << 29);
        g->words.w0 = ((unsigned)((v[5] >> 3) & 0x03) << 0) | ((unsigned)(v[6] & 0x1F) << 2) |
                      ((unsigned)(v[7] & 0x1F) << 7) | ((unsigned)(v[8] & 0x1F) << 12) |
                      ((unsigned)(v[9] & 0x1F) << 17) | ((unsigned)(v[10] & 0x1F) << 22) |
                      ((unsigned)(v[11] & 0x1F) << 27);
        g++;
    }
    *pp = g;
}

static void ui_flush(void) {
    int nv = s_ui.nv, nt = s_ui.nt, i;
    GRAPH* graph;
    Vtx* vtx;
    Gfx* gfx;
    s_ui.nv = s_ui.nt = 0;
    if (nv == 0 || nt == 0 || !s_ui.game) return;
    graph = s_ui.game->graph;
    vtx = GRAPH_ALLOC_TYPE(graph, Vtx, nv);
    for (i = 0; i < nv; i++) {
        Vtx_t* v = &vtx[i].v;
        v->ob[0] = s_ui.x[i];
        v->ob[1] = s_ui.y[i];
        v->ob[2] = 0;
        v->flag = 1;   /* as the font's vertices: no shared matrix */
        v->tc[0] = v->tc[1] = 0;
        v->cn[0] = s_ui.c[i].r;
        v->cn[1] = s_ui.c[i].g;
        v->cn[2] = s_ui.c[i].b;
        v->cn[3] = s_ui.c[i].a;
    }
    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    gSPDisplayList(gfx++, s_shape_mode);
    gSPVertex(gfx++, vtx, nv, 0);
    ui_trin(&gfx, nt, (const u8 (*)[3])s_ui.t);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
}

static void ui_room(int nv, int nt) {
    if (s_ui.nv + nv > UI_MAX_V || s_ui.nt + nt > UI_MAX_T) ui_flush();
}

/* a vertex at font-space x,y (the font's ortho: centred, y up, 16 units a pixel) */
static int ui_v(f32 x, f32 y, Col c) {
    int i = s_ui.nv++;
    s_ui.x[i] = (s16)floorf((x - SCREEN_WIDTH_F * 0.5f) * mFont_SCALE_F + 0.5f);
    s_ui.y[i] = (s16)floorf((SCREEN_HEIGHT_F * 0.5f - y) * mFont_SCALE_F + 0.5f);
    s_ui.c[i] = c;
    return i;
}

static void ui_t(int a, int b, int c) {
    u8* t = s_ui.t[s_ui.nt++];
    t[0] = (u8)a;
    t[1] = (u8)b;
    t[2] = (u8)c;
}

static Col col_mix(Col a, Col b, f32 t) {
    Col c;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    c.r = (u8)(a.r + (b.r - a.r) * t);
    c.g = (u8)(a.g + (b.g - a.g) * t);
    c.b = (u8)(a.b + (b.b - a.b) * t);
    c.a = (u8)(a.a + (b.a - a.a) * t);
    return c;
}

static void ui_quad(f32 x0, f32 y0, f32 x1, f32 y1, Col top, Col bot) {
    int a, b, c, d;
    ui_room(4, 2);
    a = ui_v(x0, y0, top);
    b = ui_v(x1, y0, top);
    c = ui_v(x1, y1, bot);
    d = ui_v(x0, y1, bot);
    ui_t(a, b, c);
    ui_t(a, c, d);
}

static void ui_tri(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy, Col col) {
    int a, b, c;
    ui_room(3, 1);
    a = ui_v(ax, ay, col);
    b = ui_v(bx, by, col);
    c = ui_v(cx, cy, col);
    ui_t(a, b, c);
}

/* a rounded rectangle, top colour to bottom colour: a fan around its centre */
static void ui_rrect(f32 x0, f32 y0, f32 x1, f32 y1, f32 r, Col top, Col bot) {
    static const f32 k_cos[UI_ARC + 1] = { 1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f };
    const int n = 4 * (UI_ARC + 1);
    f32 w = x1 - x0, h = y1 - y0;
    int center, first, q, k;
    if (w <= 0.0f || h <= 0.0f) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.5f) {
        ui_quad(x0, y0, x1, y1, top, bot);
        return;
    }
    ui_room(n + 1, n);
    center = ui_v((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, col_mix(top, bot, 0.5f));
    first = s_ui.nv;
    for (q = 0; q < 4; q++)
        for (k = 0; k <= UI_ARC; k++) {
            f32 c = k_cos[k] * r, s = k_cos[UI_ARC - k] * r, px, py;
            switch (q) {
                case 0:  px = x0 + r - c; py = y0 + r - s; break;   /* top left */
                case 1:  px = x1 - r + s; py = y0 + r - c; break;   /* top right */
                case 2:  px = x1 - r + c; py = y1 - r + s; break;   /* bottom right */
                default: px = x0 + r - s; py = y1 - r + c; break;   /* bottom left */
            }
            ui_v(px, py, col_mix(top, bot, (py - y0) / h));
        }
    for (k = 0; k < n; k++) ui_t(center, first + k, first + (k + 1) % n);
}

/* a rounded box with an edge e wide */
static void ui_box(f32 x0, f32 y0, f32 x1, f32 y1, f32 r, f32 e, Col edge, Col top, Col bot) {
    ui_rrect(x0, y0, x1, y1, r, edge, edge);
    ui_rrect(x0 + e, y0 + e, x1 - e, y1 - e, r > e ? r - e : 0.0f, top, bot);
}

/* a cursor arrow pointing left (dir < 0) or right, tip at x */
static void ui_arrow(f32 x, f32 cy, int dir, Col c) {
    f32 b = dir < 0 ? x + 6.0f : x - 6.0f;
    ui_tri(x, cy, b, cy - 5.0f, b, cy + 5.0f, c);
}

/* 0..2 units and back over 32 frames */
static f32 ui_bob(void) {
    unsigned t = s_ui.anim & 31;
    return (f32)(t < 16 ? t : 32 - t) * (2.0f / 16.0f);
}

/* the whole screen, 16:9 included (pc_menu_dim_rect with a colour) */
static void ui_backdrop(GRAPH* graph, Col c) {
    Gfx* gfx;
    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH);
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
        G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT |
        G_TF_POINT | G_TT_NONE | G_TL_TILE | G_TD_CLAMP |
        G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
        G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, c.r, c.g, c.b, c.a);
    gfx = gfx_gSPTextureRectangle1(gfx, 0, 0, SCREEN_WIDTH << 2, SCREEN_HEIGHT << 2, 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
}

/* ---- text: the game font, cells 16 units tall ---- */

static f32 ui_text_w(const char* s, f32 scale) { return (f32)pc_text_width(s) * scale; }

static void ui_text(const char* s, f32 x, f32 y, Col c, f32 scale) {
    ui_flush();   /* shapes drawn so far go under the text */
    pc_text_draw(s_ui.game, s, x, y, c.r, c.g, c.b, c.a, scale);
}

static void ui_text_c(const char* s, f32 cx, f32 y, Col c, f32 scale) {
    ui_text(s, cx - ui_text_w(s, scale) * 0.5f, y, c, scale);
}

/* centred, shrunk to fit max_w (not below 0.75), on the same middle line */
static void ui_text_fit(const char* s, f32 cx, f32 y, Col c, f32 max_w) {
    f32 w = ui_text_w(s, 1.0f), sc = 1.0f;
    if (w > max_w && w > 0.0f) {
        sc = max_w / w;
        if (sc < 0.75f) sc = 0.75f;
    }
    ui_text_c(s, cx, y + (1.0f - sc) * 8.0f, c, sc);
}

/* ---- the game's window parts ---- */

#define SHEET_X0 18.0f
#define SHEET_X1 302.0f
#define SHEET_CX 160.0f
#define INNER_W  256.0f   /* text room inside the sheet */

/* a sheet of paper in a wood frame, with a soft shadow */
static void ui_sheet(f32 x0, f32 y0, f32 x1, f32 y1) {
    ui_rrect(x0 + 3.0f, y0 + 4.0f, x1 + 3.0f, y1 + 4.0f, 14.0f, k_shadow, k_shadow);
    ui_box(x0, y0, x1, y1, 14.0f, 3.0f, k_wood, k_paper_top, k_paper_bot);
}

/* the green name tag over a sheet's top edge, as on the game's speech bubbles */
static void ui_name_tag(const char* s, f32 x, f32 y) {
    f32 w = ui_text_w(s, 1.0f) + 26.0f;
    ui_rrect(x + 2.0f, y + 3.0f, x + w + 2.0f, y + 22.0f, 9.5f, k_shadow, k_shadow);
    ui_box(x, y, x + w, y + 19.0f, 9.5f, 2.0f, k_leaf_edge, k_leaf_top, k_leaf_bot);
    ui_text(s, x + 13.0f, y + 1.0f, k_cream, 1.0f);
}

/* a ruled line across the sheet */
static void ui_rule(f32 y) {
    ui_quad(30.0f, y, SHEET_X1 - 12.0f, y + 1.0f, k_rule, k_rule);
}

enum { BTN_PLAIN, BTN_GO, BTN_OFF };

/* a pill button, 18 tall; sel: the cursor is on it */
static void ui_button(const char* s, f32 cx, f32 y, f32 w, int sel, int style) {
    f32 x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y1 = y + 18.0f;
    Col text = style == BTN_GO ? k_cream : style == BTN_OFF ? k_ink_faint : k_ink;
    if (sel) ui_rrect(x0 - 2.5f, y - 2.5f, x1 + 2.5f, y1 + 2.5f, 11.5f, k_hl_edge, k_hl_edge);
    if (style == BTN_GO)
        ui_box(x0, y, x1, y1, 9.0f, 2.0f, k_leaf_edge, k_leaf_top, k_leaf_bot);
    else if (sel)
        ui_box(x0, y, x1, y1, 9.0f, 2.0f, k_hl_edge, k_hl_top, k_hl_bot);
    else
        ui_box(x0, y, x1, y1, 9.0f, 2.0f, k_tan_edge, k_tan_top, k_tan_bot);
    if (sel && style == BTN_OFF) text = k_ink_soft;
    ui_text_c(s, cx, y + 0.5f, text, 1.0f);
}

/* two buttons side by side under the middle; sel: 0 left, 1 right, else none */
static void ui_button_pair(const char* l, int lstyle, const char* r, int rstyle, int sel, f32 y) {
    const f32 gap = 14.0f;
    f32 lw = ui_text_w(l, 1.0f) + 24.0f, rw = ui_text_w(r, 1.0f) + 24.0f, x;
    if (lw < 66.0f) lw = 66.0f;
    if (rw < 66.0f) rw = 66.0f;
    x = SHEET_CX - (lw + gap + rw) * 0.5f;
    ui_button(l, x + lw * 0.5f, y, lw, sel == 0, lstyle);
    ui_button(r, x + lw + gap + rw * 0.5f, y, rw, sel == 1, rstyle);
}

/* the selected row's band */
static void ui_row_band(f32 y, f32 h) {
    ui_box(26.0f, y, SHEET_X1 - 8.0f, y + h, h * 0.5f, 1.5f, k_hl_edge, k_hl_top, k_hl_bot);
}

/* ======================================================================
 * Pages
 * ====================================================================== */

#define TAG_X    30.0f
#define TAG_Y    15.0f
#define SHEET_Y0 26.0f
#define SHEET_Y1 224.0f
#define TABS_Y   40.0f
#define ROWS_Y   64.0f
#define ROW_H    17.0f
#define ROW_SLOTS 5   /* rows on the sheet: the longest tab must fit */
#define FITS(a) (sizeof(a) / sizeof((a)[0]) <= ROW_SLOTS)
_Static_assert(FITS(tab_video_items) && FITS(tab_audio_items) && FITS(tab_controls_items) &&
               FITS(tab_gameplay_items), "a tab has more rows than the sheet");
#undef FITS
#define LABEL_X  38.0f
#define VALUE_CX 236.0f
#define BTN_Y    156.0f
#define FOOT_Y   179.0f   /* rule; help line under it, status line under that */

/* what the selected row does, for the help line */
static const char* item_help(int id) {
    switch (id) {
        case ITEM_OUTPUT:            return "The video mode: used after a restart";
        case ITEM_WIDESCREEN:
            if (s_xpending.widescreen == XBOX_WS_AUTO) return "Auto follows the dashboard's setting";
            return "16:9 shows more of the town, but slower";
        case ITEM_TEXTURE_FILTERING: return "Smooth textures, or sharp pixels when Off";
        case ITEM_FPS_COUNTER:       return "Frames per second, in a screen corner";
        case ITEM_SCREENSHOTS:       return "Click the right stick to save a screenshot";
        case ITEM_MASTER_VOLUME:     return "Music and sound volume";
        case ITEM_STICK_DEADZONE:    return "Raise it if you walk without touching it";
        case ITEM_CSTICK_DEADZONE:   return "The right stick's dead zone";
        case ITEM_RUMBLE:            return "How strongly the controller shakes";
        case ITEM_BINDINGS:          return "Choose what each button does";
        case ITEM_RESETTI:           return "Mr. Resetti visits if you skip saving";
        case ITEM_SHOP_VISITOR:      return "Singleplayer: no visitor for Nookington's";
        case ITEM_BORDERLESS_ACRES:  return "Walk on between acres without a stop";
        case ITEM_NES_ASPECT:        return "NES games: 4:3 with bars, or stretched";
    }
    return "";
}

static void draw_tab_row(void) {
    const f32 gap = 5.0f, pad = 14.0f, h = 18.0f;
    f32 widths[TAB_COUNT], total = 0.0f, x, x_start;
    int t, focused = s_sel == -1;
    for (t = 0; t < TAB_COUNT; t++) {
        widths[t] = ui_text_w(s_tabs[t].name, 1.0f) + pad;
        total += widths[t];
    }
    total += gap * (TAB_COUNT - 1);
    x = x_start = SHEET_CX - total * 0.5f;
    for (t = 0; t < TAB_COUNT; t++) {
        f32 x1 = x + widths[t];
        if (t == s_tab) {
            if (focused) ui_rrect(x - 2.5f, TABS_Y - 2.5f, x1 + 2.5f, TABS_Y + h + 2.5f, h * 0.5f + 2.5f,
                                  k_hl_edge, k_hl_edge);
            ui_box(x, TABS_Y, x1, TABS_Y + h, h * 0.5f, 2.0f, k_leaf_edge, k_leaf_top, k_leaf_bot);
        } else {
            ui_box(x, TABS_Y, x1, TABS_Y + h, h * 0.5f, 2.0f, k_tan_edge, k_tan_top, k_tan_bot);
        }
        x = x1 + gap;
    }
    if (focused) {
        f32 bob = ui_bob();
        if (s_tab > 0) ui_arrow(x_start - 10.0f - bob, TABS_Y + h * 0.5f, -1, k_wood);
        if (s_tab < TAB_COUNT - 1) ui_arrow(x_start + total + 10.0f + bob, TABS_Y + h * 0.5f, +1, k_wood);
    }
    x = x_start;
    for (t = 0; t < TAB_COUNT; t++) {
        ui_text_c(s_tabs[t].name, x + widths[t] * 0.5f, TABS_Y + 0.5f, t == s_tab ? k_cream : k_ink_soft, 1.0f);
        x += widths[t] + gap;
    }
}

/* the Controls tab's free slot: where the left stick is now against the dead zone */
static void draw_stick_meter(f32 y) {
    const f32 x0 = 112.0f, x1 = SHEET_X1 - 14.0f, cy = y + ROW_H * 0.5f;
    int pct = xbox_left_stick_pct(), dz = s_xpending.stick_deadzone;
    f32 w = x1 - x0, xd = x0 + w * (f32)dz / 100.0f, xs = x0 + w * (f32)(pct > 100 ? 100 : pct) / 100.0f;
    ui_box(x0 - 1.5f, cy - 5.0f, x1 + 1.5f, cy + 5.0f, 5.0f, 1.5f, k_tan_edge, k_tan_top, k_tan_bot);
    if (xd > x0) ui_quad(x0, cy - 3.5f, xd, cy + 3.5f, k_zone, k_zone);
    if (xs > x0 + 0.5f) {
        Col c = pct >= dz ? k_go : k_ink_faint;
        ui_quad(x0, cy - 2.5f, xs, cy + 2.5f, c, c);
    }
    ui_quad(xd - 0.75f, cy - 7.0f, xd + 0.75f, cy + 7.0f, k_wood, k_wood);
    ui_text("Left stick", LABEL_X, y - 0.5f, k_ink_faint, 1.0f);
}

static void draw_settings_page(struct game_s* game) {
    const Tab* tab = &s_tabs[s_tab];
    int i, sel_btn;
    const char* help = "";
    const char* status = NULL;
    Col status_col = k_warn;
    char buf[64];
    (void)game;

    ui_sheet(SHEET_X0, SHEET_Y0, SHEET_X1, SHEET_Y1);
    ui_name_tag("Options", TAG_X, TAG_Y);
    draw_tab_row();

    for (i = 0; i <= ROW_SLOTS; i++) ui_rule(ROWS_Y + i * ROW_H - 1.0f);
    ui_rule(FOOT_Y);

    for (i = 0; i < tab->count && i < ROW_SLOTS; i++) {
        const Item* it = &tab->items[i];
        int selected = s_sel == i, changed = item_changed(it->id);
        f32 y = ROWS_Y + i * ROW_H, vw;
        char value[32];
        item_format(it->id, value, sizeof value);
        vw = ui_text_w(value, 1.0f);
        if (selected) {
            ui_row_band(y, ROW_H - 1.0f);
            if (it->id != ITEM_BINDINGS) {
                f32 half = (vw < 44.0f ? 22.0f : vw * 0.5f) + 13.0f, bob = ui_bob();
                ui_arrow(VALUE_CX - half - bob, y + 8.0f, -1, k_wood);
                ui_arrow(VALUE_CX + half + bob, y + 8.0f, +1, k_wood);
            }
            help = item_help(it->id);
        }
        if (changed) ui_rrect(29.0f, y + 5.5f, 34.0f, y + 10.5f, 2.5f, k_changed, k_changed);
        ui_text(it->label, LABEL_X, y - 0.5f, selected ? k_ink : k_ink_soft, 1.0f);
        ui_text(value, VALUE_CX - vw * 0.5f, y - 0.5f, changed ? k_changed : selected ? k_ink : k_ink_soft, 1.0f);
    }
    if (s_tab == TAB_CONTROLS && tab->count < ROW_SLOTS) draw_stick_meter(ROWS_Y + tab->count * ROW_H);

    sel_btn = s_sel == idx_apply() ? 0 : s_sel == idx_back() ? 1 : -1;
    ui_button_pair("Apply", s_dirty ? BTN_GO : BTN_OFF, "Back", BTN_PLAIN, sel_btn, BTN_Y);

    /* help line: the selected row; status line: what needs attention */
    if (s_sel == -1) help = "Left and Right turn the pages";
    else if (sel_btn == 0) help = s_dirty ? "Keep the changes marked in orange" : "Nothing changed yet";
    else if (sel_btn == 1) help = s_dirty ? "Leave: you can still keep the changes" : "Back to the menu";

    if (s_tab == TAB_CONTROLS) {
        /* live stick readout: set the dead zone just above where it rests */
        int pct = xbox_left_stick_pct(), moves = pct >= s_xpending.stick_deadzone;
        snprintf(buf, sizeof buf, "Left stick now %d%% (%s)", pct, moves ? "moves" : "ignored");
        status = buf;
        status_col = moves ? k_go : k_ink_faint;
    } else if (s_tab == 0 && s_xpending.video_720p && !xbox_video_720p_allowed())
        status = "720p is off in the dashboard";
    else if (s_tab == 0 && s_xpending.video_720p && g_xbox_settings_boot.video_720p && !g_xbox_video_720p)
        status = "720p failed to start: see boot.log";
    else if (s_tab == 0 && g_xbox_video_720p && s_xpending.widescreen != XBOX_WS_ON)
        status = "720p is always 16:9";
    else if (restart_outstanding())
        status = "Restart to change the output";
    else if (s_sel >= 0 && s_sel < tab->count && tab->items[s_sel].id == ITEM_SCREENSHOTS) {
        status = "Saved as shotNN.bmp in UDATA";
        status_col = k_ink_faint;
    }
    if (!status) {
        status = s_sel == -1 ? "A: open the page     B: back" :
                 sel_btn >= 0 ? "A: choose     B: back" : "Left, Right: change     B: back";
        status_col = k_ink_faint;
    }
    ui_text_fit(help, SHEET_CX, FOOT_Y + 3.0f, k_ink_soft, INNER_W);
    ui_text_fit(status, SHEET_CX, FOOT_Y + 20.0f, status_col, INNER_W);
}

/* a speech bubble for the yes/no questions, its tail at the bottom left */
static void draw_bubble(const char* title, f32 y0, f32 y1) {
    const f32 x0 = 36.0f, x1 = 284.0f;
    ui_rrect(x0 + 3.0f, y0 + 4.0f, x1 + 3.0f, y1 + 4.0f, 16.0f, k_shadow, k_shadow);
    ui_tri(88.0f, y1 - 6.0f, 116.0f, y1 - 6.0f, 80.0f, y1 + 17.0f, k_wood);
    ui_box(x0, y0, x1, y1, 16.0f, 3.0f, k_wood, k_paper_top, k_paper_bot);
    ui_tri(91.5f, y1 - 6.0f, 112.0f, y1 - 6.0f, 83.5f, y1 + 11.5f, k_paper_bot);
    ui_name_tag(title, x0 + 12.0f, y0 - 11.0f);
}

static void draw_back_confirm_page(struct game_s* game) {
    (void)game;
    draw_bubble("Discard changes?", 76.0f, 168.0f);
    ui_text_c("You have unapplied changes.", SHEET_CX, 100.0f, k_ink, 1.0f);
    ui_button_pair("Keep editing", BTN_PLAIN, "Discard", BTN_PLAIN, s_back_sel, 134.0f);
}

static void draw_restart_page(struct game_s* game) {
    (void)game;
    draw_bubble("Restart now?", 76.0f, 168.0f);
    ui_text_c("The new output needs a restart.", SHEET_CX, 92.0f, k_ink, 1.0f);
    if (g_pc_paused) ui_text_c("Unsaved progress will be lost!", SHEET_CX, 109.0f, k_warn, 1.0f);
    ui_button_pair("Restart", BTN_PLAIN, "Later", BTN_PLAIN, s_restart_sel, 136.0f);
}

#define BIND_ROWS_Y 56.0f
#define BIND_ROW_H  14.0f
#define BIND_PAD_X  172.0f

static void draw_bindings_page(struct game_s* game) {
    const f32 y1 = 226.0f;
    const char* help;
    int i, sel_btn;
    (void)game;

    ui_sheet(SHEET_X0, SHEET_Y0, SHEET_X1, y1);
    ui_name_tag("Buttons", TAG_X, TAG_Y);
    for (i = 0; i <= BIND_VISIBLE; i++) ui_rule(BIND_ROWS_Y + i * BIND_ROW_H - 1.0f);

    for (i = 0; i < BIND_VISIBLE; i++) {
        int row = s_bind_scroll + i, sel;
        f32 y = BIND_ROWS_Y + i * BIND_ROW_H;
        if (row >= BIND_ROW_COUNT) break;
        sel = s_bind_sel == row;
        if (sel) ui_row_band(y, BIND_ROW_H - 1.0f);
        ui_text(s_bind_rows[row].label, LABEL_X, y - 2.5f, sel ? k_ink : k_ink_soft, 1.0f);
        if (sel && s_capture)
            ui_text("Press a button", BIND_PAD_X, y - 2.5f, k_changed, 1.0f);
        else
            ui_text(pad_name(*bind_slot(row)), BIND_PAD_X, y - 2.5f, sel ? k_ink : k_ink_soft, 1.0f);
    }
    /* more rows above or below */
    if (s_bind_scroll > 0) {
        f32 y = BIND_ROWS_Y + 3.0f;
        ui_tri(SHEET_X1 - 22.0f, y + 8.0f, SHEET_X1 - 12.0f, y + 8.0f, SHEET_X1 - 17.0f, y + 2.0f, k_wood);
    }
    if (s_bind_scroll + BIND_VISIBLE < BIND_ROW_COUNT) {
        f32 y = BIND_ROWS_Y + (BIND_VISIBLE - 1) * BIND_ROW_H + 3.0f;
        ui_tri(SHEET_X1 - 22.0f, y, SHEET_X1 - 12.0f, y, SHEET_X1 - 17.0f, y + 6.0f, k_wood);
    }

    ui_text("Action", LABEL_X, TABS_Y - 1.0f, k_ink_faint, 1.0f);
    ui_text("Controller", BIND_PAD_X, TABS_Y - 1.0f, k_ink_faint, 1.0f);

    sel_btn = s_bind_sel == BIND_IDX_DEFAULTS ? 0 : s_bind_sel == BIND_IDX_BACK ? 1 : -1;
    ui_button_pair("Restore Defaults", BTN_PLAIN, "Back", BTN_PLAIN, sel_btn,
                   BIND_ROWS_Y + BIND_VISIBLE * BIND_ROW_H + 3.0f);

    if (s_capture) help = "Press a button (Back cancels)";
    else if (sel_btn == 0) help = "Back to the standard buttons";
    else if (sel_btn == 1) help = "Done: the buttons are saved";
    else help = "A to rebind: it swaps with the old button";
    ui_text_fit(help, SHEET_CX, y1 - 22.0f, s_capture ? k_changed : k_ink_soft, INNER_W);
}

void pc_settings_menu_draw(struct game_s* game, int with_dim_backdrop) {
    GRAPH* graph;
    Gfx* gfx;
    if (!s_active || !game || !game->graph) return;
    graph = game->graph;
    s_ui.game = game;
    s_ui.nv = s_ui.nt = 0;
    s_ui.anim++;
    if (with_dim_backdrop) {
        Col c = k_backdrop;
        if (s_sub == SUB_CONFIRM_BACK || s_sub == SUB_CONFIRM_RESTART) c.a = 190;   /* the question stands out */
        ui_backdrop(graph, c);
    }
    switch (s_sub) {
        case SUB_SETTINGS:        draw_settings_page(game); break;
        case SUB_CONFIRM_BACK:    draw_back_confirm_page(game); break;
        case SUB_CONFIRM_RESTART: draw_restart_page(game); break;
        case SUB_BINDINGS:        draw_bindings_page(game); break;
    }
    ui_flush();
    /* leave the font's mode set, as every other font draw does */
    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    mFont_gppSetMode(&gfx);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
    if (!s_logged_room) {
        s_logged_room = 1;
        printf("[MENU] Options drawn: font list %d bytes free, poly %d\n",
               THA_GA_getFreeBytes(&graph->font_thaga), THA_GA_getFreeBytes(&graph->polygon_opaque_thaga));
    }
}
