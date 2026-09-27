/* xbox_autopad.c — scripted input for headless test runs (debug only).
 *
 * Build with XBOX_CMAKE_ARGS="-DXBOX_AUTOPAD=<frame>": CMake compiles
 * pc_pad.c with PADRead renamed to xbox_pad_read_real, and this PADRead
 * wraps it. From PADRead call <frame> on, every 30 calls it holds a button
 * for 4 calls: START every 8th press, A otherwise. That walks the title,
 * the file menu and Rover's train dialog without a human. Real pad input
 * still passes through. Not built into release XBEs. */
#ifdef XBOX_DBG_AUTOPAD
#include "pc_platform.h"
#include <dolphin/pad.h>

u32 xbox_pad_read_real(PADStatus* status);

u32 PADRead(PADStatus* status) {
    static u32 calls, presses;
    u32 r = xbox_pad_read_real(status);
    u32 t;
    calls++;
    if (calls < (u32)XBOX_DBG_AUTOPAD) return r;
    t = (calls - (u32)XBOX_DBG_AUTOPAD) % 30;
    if (t < 4) {
        u16 b = (presses % 8) == 7 ? PAD_BUTTON_START : PAD_BUTTON_A;
        status[0].err = PAD_ERR_NONE;
        status[0].button |= b;
        if (t == 3) {
            if ((presses % 16) == 0)
                printf("[AUTOPAD] call %u press %u (%s)\n", (unsigned)calls, (unsigned)presses,
                       b == PAD_BUTTON_START ? "START" : "A");
            presses++;
        }
    }
    return r;
}
#endif
