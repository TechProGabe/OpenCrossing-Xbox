/* xbox_nv2a.h — the NV2A backend behind pc_gx.c (xbox/src/xbox_nv2a.c) and
 * its TEV -> register-combiner compiler (xbox/src/xbox_tev_rc.c). */
#ifndef XBOX_NV2A_H
#define XBOX_NV2A_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define XRC_MAX_TEV     3
#define XRC_MAX_STAGES  8

/* One TEV stage, exactly as pc_gx.c uploads it to the uber shader. */
typedef struct {
    int cin[4], ain[4];       /* GXTevColorArg / GXTevAlphaArg a,b,c,d */
    int cop, aop;             /* 0 add, 1 sub (compare modes -> add) */
    int cbias, cscale, abias, ascale;
    int cclamp, aclamp;
    int cout, aout;           /* 0 PREV 1 REG0 2 REG1 3 REG2 */
    int kcsel, kasel;
    int use_tex;
} XTevStage;

typedef struct {
    int nstages;
    XTevStage st[XRC_MAX_TEV];
    int fog_on;
} XTevCfg;

/* Where a combiner constant comes from (resolved per draw). */
enum {
    XREF_NONE = 0,
    XREF_TEVREG_RGB,   /* param: 0 PREV .. 3 REG2 */
    XREF_TEVREG_A,
    XREF_KONST_C,      /* param: kcsel */
    XREF_KONST_A,      /* param: kasel */
    XREF_FOG_RGB,
};
#define XREF(t, p) ((uint16_t)(((t) << 8) | ((p) & 0xFF)))

typedef struct {
    int nstages;                        /* NV2A general combiner stages used */
    uint32_t cicw[XRC_MAX_STAGES], cocw[XRC_MAX_STAGES];
    uint32_t aicw[XRC_MAX_STAGES], aocw[XRC_MAX_STAGES];
    uint32_t cw0, cw1;                  /* final combiner */
    /* per stage: C0.rgb, C0.a, C1.rgb, C1.a ; final: C0.rgb, C0.a, C1.rgb, C1.a */
    uint16_t cref[XRC_MAX_STAGES][4];
    uint16_t fref[4];
    int approximated;                   /* 1 if something did not fit */
} XRcProg;

void xbox_tev_compile(const XTevCfg* cfg, XRcProg* out);

/* backend entry points used by xbox_main.c */
int  xbox_nv2a_init(void);
void xbox_nv2a_present(void);
int  xbox_gl_nv2a_load(void);
extern int g_xbox_fbdump_every;   /* 0 = off; N = dump every Nth frame to COM1 */

#ifdef __cplusplus
}
#endif
#endif
