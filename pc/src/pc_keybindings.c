#include "pc_keybindings.h"
#include "pc_platform.h"

static const PCKeybindings s_kb_defaults = {
    /* buttons */
    .a     = SDL_SCANCODE_SPACE,
    .b     = SDL_SCANCODE_LSHIFT,
    .x     = SDL_SCANCODE_X,
    .y     = SDL_SCANCODE_Y,
    .start = SDL_SCANCODE_RETURN,
    .z     = SDL_SCANCODE_Z,
    .l     = SDL_SCANCODE_Q,
    .r     = SDL_SCANCODE_E,

    /* main stick (WASD) */
    .stick_up    = SDL_SCANCODE_W,
    .stick_down  = SDL_SCANCODE_S,
    .stick_left  = SDL_SCANCODE_A,
    .stick_right = SDL_SCANCODE_D,

    /* C-stick (arrows) */
    .cstick_up    = SDL_SCANCODE_UP,
    .cstick_down  = SDL_SCANCODE_DOWN,
    .cstick_left  = SDL_SCANCODE_LEFT,
    .cstick_right = SDL_SCANCODE_RIGHT,

    /* D-pad (IJKL) */
    .dpad_up    = SDL_SCANCODE_I,
    .dpad_down  = SDL_SCANCODE_K,
    .dpad_left  = SDL_SCANCODE_J,
    .dpad_right = SDL_SCANCODE_L,
};

/* Back/Select is reserved for opening the PC pause menu, so it is not a
 * default here. Triggers: LB=L, RB=Z, RT=R (GC Z is a digital button). */
static const PCPadBindings s_pad_defaults = {
    .a     = SDL_CONTROLLER_BUTTON_A,
    .b     = SDL_CONTROLLER_BUTTON_B,
    .x     = SDL_CONTROLLER_BUTTON_X,
    .y     = SDL_CONTROLLER_BUTTON_Y,
    .start = SDL_CONTROLLER_BUTTON_START,
    .z     = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    .l     = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    .r     = PC_PAD_AXIS_BIT | SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    .dpad_up    = SDL_CONTROLLER_BUTTON_DPAD_UP,
    .dpad_down  = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    .dpad_left  = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    .dpad_right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
};

PCKeybindings g_pc_keybindings;
PCPadBindings g_pc_padbindings;

static const char* KEYBINDINGS_FILE = "keybindings.ini";

/* mapping table: ini key name -> offset into PCKeybindings */
typedef struct {
    const char* ini_key;
    int offset; /* byte offset into PCKeybindings */
} KeybindEntry;

#define KB_ENTRY(name, field) { name, offsetof(PCKeybindings, field) }

static const KeybindEntry s_entries[] = {
    KB_ENTRY("A",            a),
    KB_ENTRY("B",            b),
    KB_ENTRY("X",            x),
    KB_ENTRY("Y",            y),
    KB_ENTRY("Start",        start),
    KB_ENTRY("Z",            z),
    KB_ENTRY("L",            l),
    KB_ENTRY("R",            r),
    KB_ENTRY("Stick_Up",     stick_up),
    KB_ENTRY("Stick_Down",   stick_down),
    KB_ENTRY("Stick_Left",   stick_left),
    KB_ENTRY("Stick_Right",  stick_right),
    KB_ENTRY("CStick_Up",    cstick_up),
    KB_ENTRY("CStick_Down",  cstick_down),
    KB_ENTRY("CStick_Left",  cstick_left),
    KB_ENTRY("CStick_Right", cstick_right),
    KB_ENTRY("DPad_Up",      dpad_up),
    KB_ENTRY("DPad_Down",    dpad_down),
    KB_ENTRY("DPad_Left",    dpad_left),
    KB_ENTRY("DPad_Right",   dpad_right),
};

#define NUM_ENTRIES (sizeof(s_entries) / sizeof(s_entries[0]))

/* mapping table: ini key name -> offset into PCPadBindings */
#define PAD_ENTRY(name, field) { name, offsetof(PCPadBindings, field) }

static const KeybindEntry s_pad_entries[] = {
    PAD_ENTRY("Pad_A",          a),
    PAD_ENTRY("Pad_B",          b),
    PAD_ENTRY("Pad_X",          x),
    PAD_ENTRY("Pad_Y",          y),
    PAD_ENTRY("Pad_Start",      start),
    PAD_ENTRY("Pad_Z",          z),
    PAD_ENTRY("Pad_L",          l),
    PAD_ENTRY("Pad_R",          r),
    PAD_ENTRY("Pad_DPad_Up",    dpad_up),
    PAD_ENTRY("Pad_DPad_Down",  dpad_down),
    PAD_ENTRY("Pad_DPad_Left",  dpad_left),
    PAD_ENTRY("Pad_DPad_Right", dpad_right),
};

#define NUM_PAD_ENTRIES (sizeof(s_pad_entries) / sizeof(s_pad_entries[0]))

/* mouse button name table */
typedef struct {
    const char* name;
    PCInputCode code;
} MouseButtonEntry;

static const MouseButtonEntry s_mouse_buttons[] = {
    { "Mouse1", PC_INPUT_MOUSE1 },
    { "Mouse2", PC_INPUT_MOUSE2 },
    { "Mouse3", PC_INPUT_MOUSE3 },
};
#define NUM_MOUSE_BUTTONS (sizeof(s_mouse_buttons) / sizeof(s_mouse_buttons[0]))

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_end(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

/* get display name for an input code */
const char* pc_input_code_name(PCInputCode code) {
    if (code & PC_INPUT_MOUSE_BIT) {
        for (int i = 0; i < (int)NUM_MOUSE_BUTTONS; i++) {
            if (s_mouse_buttons[i].code == code)
                return s_mouse_buttons[i].name;
        }
        return "Unknown";
    }
    return SDL_GetScancodeName((SDL_Scancode)code);
}

/* short display name for a pad code (UI cells) */
const char* pc_pad_code_name(PCPadCode code) {
    if (code < 0) return "-";
    if (code & PC_PAD_AXIS_BIT) {
        switch (code & 0xFF) {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  return "LT";
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return "RT";
        }
        return "Axis?";
    }
    switch (code) {
        case SDL_CONTROLLER_BUTTON_A:             return "A";
        case SDL_CONTROLLER_BUTTON_B:             return "B";
        case SDL_CONTROLLER_BUTTON_X:             return "X";
        case SDL_CONTROLLER_BUTTON_Y:             return "Y";
        case SDL_CONTROLLER_BUTTON_BACK:          return "Back";
        case SDL_CONTROLLER_BUTTON_GUIDE:         return "Guide";
        case SDL_CONTROLLER_BUTTON_START:         return "Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "LS";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "RS";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "D-Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "D-Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "D-Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "D-Right";
    }
    {
        const char* s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)code);
        return s ? s : "Unknown";
    }
}

/* ini name for a pad code (SDL string names, "none" for unbound) */
static const char* pad_code_ini_name(PCPadCode code) {
    if (code < 0) return "none";
    if (code & PC_PAD_AXIS_BIT) {
        const char* s = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)(code & 0xFF));
        return s ? s : "none";
    }
    {
        const char* s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)code);
        return s ? s : "none";
    }
}

/* parse an input value string to a PCInputCode */
static PCInputCode parse_input_code(const char* value) {
    /* check mouse button names first (case-insensitive) */
    for (int i = 0; i < (int)NUM_MOUSE_BUTTONS; i++) {
        if (SDL_strcasecmp(value, s_mouse_buttons[i].name) == 0) {
            return s_mouse_buttons[i].code;
        }
    }

    /* fall back to SDL scancode */
    SDL_Scancode sc = SDL_GetScancodeFromName(value);
    if (sc != SDL_SCANCODE_UNKNOWN) {
        return (PCInputCode)sc;
    }

    return -1; /* invalid */
}

/* parse a pad value string; returns -2 on parse failure (-1 means unbound) */
static PCPadCode parse_pad_code(const char* value) {
    if (SDL_strcasecmp(value, "none") == 0) return PC_PAD_NONE;

    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(value);
    if (b != SDL_CONTROLLER_BUTTON_INVALID) return (PCPadCode)b;

    SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(value);
    if (ax == SDL_CONTROLLER_AXIS_TRIGGERLEFT || ax == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
        return PC_PAD_AXIS_BIT | (PCPadCode)ax;
    }

    return -2; /* invalid */
}

static void apply_keybind(const char* key, const char* value) {
    /* gamepad entries (Pad_ prefix) */
    for (int i = 0; i < (int)NUM_PAD_ENTRIES; i++) {
        if (strcmp(key, s_pad_entries[i].ini_key) == 0) {
            PCPadCode code = parse_pad_code(value);
            if (code < -1) {
                printf("[Keybindings] WARNING: unknown pad input '%s' for '%s'\n", value, key);
                return;
            }
            *(PCPadCode*)((char*)&g_pc_padbindings + s_pad_entries[i].offset) = code;
            return;
        }
    }

    /* keyboard entries */
    for (int i = 0; i < (int)NUM_ENTRIES; i++) {
        if (strcmp(key, s_entries[i].ini_key) == 0) {
            PCInputCode code = parse_input_code(value);
            if (code < 0) {
                printf("[Keybindings] WARNING: unknown key name '%s' for '%s'\n", value, key);
                return;
            }
            *(PCInputCode*)((char*)&g_pc_keybindings + s_entries[i].offset) = code;
            return;
        }
    }
    printf("[Keybindings] WARNING: unknown binding '%s'\n", key);
}

void pc_keybindings_reset_defaults(void) {
    g_pc_keybindings = s_kb_defaults;
    g_pc_padbindings = s_pad_defaults;
}

void pc_keybindings_save(void) {
    FILE* f = fopen(KEYBINDINGS_FILE, "w");
    if (!f) {
        printf("[Keybindings] Failed to write %s\n", KEYBINDINGS_FILE);
        return;
    }

    fprintf(f, "[Keyboard]\n");
    fprintf(f, "# Key names use SDL2 scancode names.\n");
    fprintf(f, "# Common names: Space, Left Shift, Right Shift, Left Ctrl, Right Ctrl,\n");
    fprintf(f, "#   Left Alt, Right Alt, Return, Escape, Tab, Backspace, Delete,\n");
    fprintf(f, "#   A-Z, 0-9, F1-F12, Up, Down, Left, Right, etc.\n");
    fprintf(f, "# Mouse buttons: Mouse1 (left), Mouse2 (right), Mouse3 (middle)\n");
    fprintf(f, "# For the full list, search the SDL2 scancode name table.\n");
    fprintf(f, "\n");
    fprintf(f, "# Buttons\n");

    for (int i = 0; i < (int)NUM_ENTRIES; i++) {
        PCInputCode code = *(PCInputCode*)((char*)&g_pc_keybindings + s_entries[i].offset);
        fprintf(f, "%s = %s\n", s_entries[i].ini_key, pc_input_code_name(code));

        /* blank line separators between sections */
        if (i == 7)  fprintf(f, "\n# Main Stick\n");
        if (i == 11) fprintf(f, "\n# C-Stick (Camera)\n");
        if (i == 15) fprintf(f, "\n# D-Pad\n");
    }

    fprintf(f, "\n[Gamepad]\n");
    fprintf(f, "# SDL2 game controller names: a, b, x, y, start, leftshoulder,\n");
    fprintf(f, "#   rightshoulder, lefttrigger, righttrigger, leftstick, rightstick,\n");
    fprintf(f, "#   dpup, dpdown, dpleft, dpright. Use 'none' to unbind.\n");
    fprintf(f, "# Sticks are fixed: left stick = movement, right stick = C-stick.\n");
    fprintf(f, "# The Back/Select button is reserved: it opens the pause menu.\n");

    for (int i = 0; i < (int)NUM_PAD_ENTRIES; i++) {
        PCPadCode code = *(PCPadCode*)((char*)&g_pc_padbindings + s_pad_entries[i].offset);
        fprintf(f, "%s = %s\n", s_pad_entries[i].ini_key, pad_code_ini_name(code));
    }

    fclose(f);
    printf("[Keybindings] Saved %s\n", KEYBINDINGS_FILE);
}

void pc_keybindings_load(void) {
    pc_keybindings_reset_defaults();

    FILE* f = fopen(KEYBINDINGS_FILE, "r");
    if (!f) {
        pc_keybindings_save();
        printf("[Keybindings] Created default %s\n", KEYBINDINGS_FILE);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        const char* p = skip_ws(line);

        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '[')
            continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = (char*)skip_ws(line);
        trim_end(key);
        char* value = (char*)skip_ws(eq + 1);
        trim_end(value);

        if (*key && *value) {
            apply_keybind(key, value);
        }
    }
    fclose(f);
    printf("[Keybindings] Loaded %s\n", KEYBINDINGS_FILE);
}
