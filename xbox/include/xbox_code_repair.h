/* xbox_code_repair.h — .text in RAM checked against default.xbe at boot,
 * and any bytes something patched after load put back
 * (xbox/src/xbox_code_repair.c, GitHub #2/#3). */
#ifndef XBOX_CODE_REPAIR_H
#define XBOX_CODE_REPAIR_H
#ifdef __cplusplus
extern "C" {
#endif

/* kill switch: -DXBOX_CODE_REPAIR=0 builds without it (settings.ini
 * code_repair = 0 keeps the check and the log, without the writes) */
#ifndef XBOX_CODE_REPAIR
#define XBOX_CODE_REPAIR 1
#endif

#if XBOX_CODE_REPAIR
/* after the settings load, before the game's code runs */
void xbox_code_repair(void);
#else
static inline void xbox_code_repair(void) {}
#endif

#ifdef __cplusplus
}
#endif
#endif
