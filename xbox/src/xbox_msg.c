/* xbox_msg.c — the game's Game Boy Advance and e-Reader talk, fitted to a
 * console that can't connect either (the user's brief, 2026-10-29).
 *
 *   xbox_msg_override  our own text for a few messages (msg_overrides.inc,
 *                      from tools/xbox/make_msg_overrides.py): Kapp'n's GBA
 *                      lines and the e-Reader machine
 *   xbox_msg_patch     takes the GBA choices out of a menu as it loads
 *                      ("Advance Play" for NES games, "Design Advance" and
 *                      the Able Sisters' "Other things" submenu, which is
 *                      all GBA and e-Reader); the disc's own text is edited
 *                      in memory, so none of it is in the repository
 *   xbox_choice_unmap  the index the game's code expects for the choice the
 *                      player picked (m_choice_main.c_inc): the SETNEXTMSGn
 *                      codes and every handler keep the original numbering
 *   xbox_ereader_gift  Super Mario Bros. once per player for asking the
 *                      e-Reader machine (ac_pterminal.c)
 * Called from src/ under TARGET_XBOX (docs/patches.md). Kill switch:
 * -DXBOX_MSG_OVERRIDES=0 (all of it). */
#include <stdio.h>
#include <string.h>
#include "xbox_io.h"

#ifndef XBOX_MSG_OVERRIDES
#define XBOX_MSG_OVERRIDES 1
#endif

int mFont_CodeSize_idx_get(unsigned char* str, int idx);

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

/* ---- menus without the GBA ---- */

/* SETSELSTR2..6: 0x7F, the command, two bytes per choice's string number */
static const unsigned char k_selstr_cmd[7] = { 0, 0, 0x16, 0x17, 0x18, 0x79, 0x7A };

#define SEL_ADVANCE_PLAY   0x0244   /* NES: "Advance Play" */
#define SEL_DESIGN_ADVANCE 0x01EB   /* Able Sisters: "Design Advance" */
#define SEL_OTHER_THINGS   0x01B3   /* "Other things": GBA / e-Reader, at the Able Sisters only */
#define SEL_DESIGN         0x01E9   /* "Design a pattern" */
#define SEL_SAVE_DESIGN    0x01EA   /* "Save a pattern" */

static int s_drop_pos = -1;   /* the choice taken out of the menu on screen, or -1 */

static int sel_id(const unsigned char* args, int k) { return args[2 * k] << 8 | args[2 * k + 1]; }

static int dropped(const unsigned char* args, int n, int k) {
    int id = sel_id(args, k), j;
    if (id == SEL_ADVANCE_PLAY || id == SEL_DESIGN_ADVANCE) return 1;
    if (id == SEL_OTHER_THINGS)
        for (j = 0; j < n; j++)
            if (sel_id(args, j) == SEL_DESIGN || sel_id(args, j) == SEL_SAVE_DESIGN) return 1;
    return 0;
}

void xbox_msg_patch(unsigned char* buf, unsigned* size_p) {
    int i = 0, size = (int)*size_p;
    if (!XBOX_MSG_OVERRIDES) return;
    while (i < size) {
        int step;
        if (buf[i] == 0x7F && i + 1 < size) {
            int n, k;
            for (n = 2; n <= 6 && k_selstr_cmd[n] != buf[i + 1]; n++) {}
            if (n <= 6) {
                s_drop_pos = -1;   /* a new menu: nothing taken out unless below */
                for (k = 0; k < n && n > 2; k++)
                    if (dropped(buf + i + 2, n, k)) {
                        int at = i + 2 + 2 * k;
                        memmove(buf + at, buf + at + 2, (size_t)(size - at - 2));
                        size -= 2;
                        buf[i + 1] = k_selstr_cmd[n - 1];
                        s_drop_pos = k;
                        break;
                    }
            }
        }
        step = mFont_CodeSize_idx_get(buf, i);
        i += step > 0 ? step : 1;
    }
    *size_p = (unsigned)size;
}

int xbox_choice_unmap(int idx) {
    return s_drop_pos >= 0 && idx >= s_drop_pos ? idx + 1 : idx;
}

/* ---- the e-Reader machine's apology ---- */

#define GIFT_INI XBOX_UDATA_DIR "ereader.ini"

/* 1 if this player (town and player ids) hasn't had the gift; claim = 1
 * records that they have */
int xbox_ereader_gift(unsigned land_id, unsigned player_id, int claim) {
    char want[32], line[64];
    FILE* f;
    int seen = 0;
    snprintf(want, sizeof want, "gift = %04x %04x", land_id & 0xFFFF, player_id & 0xFFFF);
    if ((f = fopen(GIFT_INI, "r")) != NULL) {
        while (!seen && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            seen = strcmp(line, want) == 0;
        }
        fclose(f);
    }
    if (seen || !claim) return !seen;
    if ((f = fopen(GIFT_INI, "a")) != NULL) {
        fprintf(f, "%s\n", want);
        fclose(f);
    }
    xbox_logf("[MSG] e-Reader machine: Super Mario Bros. for %04x %04x\n", land_id & 0xFFFF, player_id & 0xFFFF);
    return 1;
}
