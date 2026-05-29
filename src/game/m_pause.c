#include "m_pause.h"
#include "m_common_data.h"
#include "m_controller.h"

void Pause_ct(pause_t* pause) {
    pause->timer = 0.0f;
    pause->enabled = 0;
}

int Pause_proc(pause_t* pause, pad_t* pad) {
    if (CHECK_BTN_ALL(pad->now.button, BUTTON_R) && CHECK_BTN_ALL(pad->on.button, BUTTON_DDOWN)) {
        pause->enabled = !pause->enabled;
    }

    if (CHECK_BTN_ALL(pad->now.button, BUTTON_Z) && CHECK_BTN_ALL(pad->now.button, BUTTON_R) &&
        !CHECK_BTN_ALL(pad->on.button, BUTTON_R)) {
        pause->timer += gamePT->graph->dt_num_60fps_frames;
    }

    if ((!pause->enabled) || (CHECK_BTN_ALL(pad->now.button, BUTTON_Z) &&
                              (CHECK_BTN_ALL(pad->on.button, BUTTON_R) ||
                               (CHECK_BTN_ALL(pad->now.button, BUTTON_R) && (pause->timer > 8.0f))))) {
        pause->timer = 0.0f;
        return 1;
    }

    return 0;
}
