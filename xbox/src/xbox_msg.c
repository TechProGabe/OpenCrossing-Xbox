/* xbox_msg.c — our own text for the few game messages that talk about a
 * Game Boy Advance (msg_overrides.inc, from tools/xbox/make_msg_overrides.py).
 * mMsg_LoadMsgData (src/game/m_msg_main.c_inc) asks here after reading a
 * message from the disc; a hit replaces it. Kill switch: -DXBOX_MSG_OVERRIDES=0. */
#include <string.h>

#ifndef XBOX_MSG_OVERRIDES
#define XBOX_MSG_OVERRIDES 1
#endif

typedef struct { int index; const unsigned char* data; unsigned size; } MsgOverride;
#include "msg_overrides.inc"

/* the message's new length in buf, or 0 to keep the disc's */
int xbox_msg_override(int index, unsigned char* buf, int cap) {
    unsigned i;
    if (!XBOX_MSG_OVERRIDES) return 0;
    for (i = 0; i < sizeof k_msg_overrides / sizeof k_msg_overrides[0]; i++)
        if (k_msg_overrides[i].index == index && (int)k_msg_overrides[i].size <= cap) {
            memcpy(buf, k_msg_overrides[i].data, k_msg_overrides[i].size);
            return (int)k_msg_overrides[i].size;
        }
    return 0;
}
