#ifndef PC_KEYBINDINGS_H
#define PC_KEYBINDINGS_H

#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_mouse.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PCInputCode: values 0..SDL_NUM_SCANCODES-1 are keyboard scancodes.
   Values with PC_INPUT_MOUSE_BIT set are mouse buttons (low bits = SDL button index). */
typedef int PCInputCode;

#define PC_INPUT_MOUSE_BIT   0x10000
#define PC_INPUT_MOUSE1      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_LEFT)    /* left click */
#define PC_INPUT_MOUSE2      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_RIGHT)   /* right click */
#define PC_INPUT_MOUSE3      (PC_INPUT_MOUSE_BIT | SDL_BUTTON_MIDDLE)  /* middle click */

/* PCPadCode: -1 = unbound, 0..SDL_CONTROLLER_BUTTON_MAX-1 = controller button.
   Values with PC_PAD_AXIS_BIT set are trigger axes read as buttons
   (low bits = SDL_GameControllerAxis, positive direction). */
typedef int PCPadCode;

#define PC_PAD_NONE      (-1)
#define PC_PAD_AXIS_BIT  0x100

#define PC_PAD_AXIS_PRESS 12800

typedef struct {
    /* buttons */
    PCInputCode a;
    PCInputCode b;
    PCInputCode x;
    PCInputCode y;
    PCInputCode start;
    PCInputCode z;
    PCInputCode l;
    PCInputCode r;

    /* main stick */
    PCInputCode stick_up;
    PCInputCode stick_down;
    PCInputCode stick_left;
    PCInputCode stick_right;

    /* C-stick */
    PCInputCode cstick_up;
    PCInputCode cstick_down;
    PCInputCode cstick_left;
    PCInputCode cstick_right;

    /* D-pad */
    PCInputCode dpad_up;
    PCInputCode dpad_down;
    PCInputCode dpad_left;
    PCInputCode dpad_right;
} PCKeybindings;

/* Gamepad button bindings. Sticks are hardwired to the analog sticks
   (deadzone lives in settings.ini); everything else is remappable. */
typedef struct {
    PCPadCode a;
    PCPadCode b;
    PCPadCode x;
    PCPadCode y;
    PCPadCode start;
    PCPadCode z;
    PCPadCode l;
    PCPadCode r;
    PCPadCode dpad_up;
    PCPadCode dpad_down;
    PCPadCode dpad_left;
    PCPadCode dpad_right;
} PCPadBindings;

extern PCKeybindings  g_pc_keybindings;
extern PCPadBindings  g_pc_padbindings;

void pc_keybindings_load(void);
void pc_keybindings_save(void);
void pc_keybindings_reset_defaults(void); /* keyboard + gamepad, does not save */

const char* pc_input_code_name(PCInputCode code);
const char* pc_pad_code_name(PCPadCode code);

#ifdef __cplusplus
}
#endif

#endif /* PC_KEYBINDINGS_H */
