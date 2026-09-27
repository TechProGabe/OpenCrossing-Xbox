/* xbox_tev_rc.c — GameCube TEV configuration -> NV2A register combiners.
 *
 * A TEV stage computes, per colour and alpha independently,
 *     out = clamp((d OP ((1-c)*a + c*b) + bias) * scale)
 * and an NV2A general combiner stage computes  AB + CD  with per-input
 * mappings (invert, negate, half-bias, expand) and an output shift/bias.
 * So each TEV stage becomes one NV2A stage when the lerp collapses (a, b or c
 * is ZERO/ONE — the common modulate/replace/decal configs) and two when it
 * does not (stage A: lerp -> scratch, stage B: d +/- scratch).
 *
 * Register plan (NV2A names -> TEV):
 *   R0 (spare0)            PREV. The final combiner reads it.
 *   R1, T3, V1.rgb         REG0..REG2 once written; scratch otherwise.
 *                          V1.a carries the fog factor from the vertex program.
 *   T0..T2                 the texture sampled by TEV stage 0..2.
 *   V0 (diffuse)           RASC/RASA (colour channel 0, lit per vertex).
 *   C0/C1 per stage        KONST and not-yet-written TEV registers, resolved
 *                          per draw from the XREF_* tags (xbox_nv2a.c).
 * Anything that does not fit is approximated and flagged (out->approximated)
 * so the caller can count it; see docs/renderer.md. */
#include <string.h>
#include "xbox_nv2a.h"

/* combiner sources */
enum { S_ZERO = 0, S_C0 = 1, S_C1 = 2, S_FOG = 3, S_V0 = 4, S_V1 = 5,
       S_T0 = 8, S_T1 = 9, S_T2 = 10, S_T3 = 11, S_R0 = 12, S_R1 = 13 };
/* input mappings */
enum { M_UID = 0, M_UINV = 1, M_EXPN = 2, M_EXPNEG = 3, M_HBN = 4, M_HBNEG = 5,
       M_SID = 6, M_SNEG = 7 };
/* output ops */
enum { OP_NOSHIFT = 0, OP_NOSHIFT_BIAS = 1, OP_SHL1 = 2, OP_SHL1_BIAS = 3,
       OP_SHL2 = 4, OP_SHR1 = 6 };

typedef struct {
    uint8_t src, alpha, map;
    uint16_t cref;   /* constant request (XREF_*), resolved to S_C0/S_C1 later */
} Op;

static const Op OP_ZERO = { S_ZERO, 0, M_UID, 0 };
static const Op OP_ONE  = { S_ZERO, 0, M_UINV, 0 };
static const Op OP_HALF = { S_ZERO, 0, M_HBNEG, 0 };

static int is_zero(Op o) { return o.src == S_ZERO && !o.cref && o.map == M_UID; }
static int is_one(Op o)  { return o.src == S_ZERO && !o.cref && o.map == M_UINV; }
static int same(Op a, Op b) { return a.src == b.src && a.alpha == b.alpha && a.map == b.map && a.cref == b.cref; }

static Op op_inv(Op o) {
    if (o.src == S_ZERO && !o.cref) {
        if (o.map == M_UID) return OP_ONE;
        if (o.map == M_UINV) return OP_ZERO;
        return o; /* 1 - 0.5 = 0.5 */
    }
    if (o.map == M_UID) o.map = M_UINV;
    else if (o.map == M_UINV) o.map = M_UID;
    return o;
}

/* -x, or returns 0 if the mapping can't express it */
static int op_neg(Op o, Op* out) {
    if (o.src == S_ZERO && !o.cref) {
        if (o.map == M_UID) { *out = o; return 1; }
        if (o.map == M_UINV) { *out = o; out->map = M_EXPN; return 1; }   /* 2*0-1 = -1 */
        if (o.map == M_HBNEG) { *out = o; out->map = M_HBN; return 1; }   /* 0-0.5 */
        return 0;
    }
    if (o.map == M_UID) { *out = o; out->map = M_SNEG; return 1; }
    return 0;
}

/* ---------------- register allocation state ---------------- */
typedef struct {
    /* where TEV reg r (0 PREV..3 REG2) lives, per channel: 0 = still its constant */
    uint8_t rgb[4], a[4];
    /* texture presence per TEV stage */
    int use_tex[XRC_MAX_TEV];
} Alloc;

static const uint8_t k_pool_rgb[] = { S_R1, S_T3, S_V1 };
static const uint8_t k_pool_a[]   = { S_R1, S_T3 };   /* V1.a = fog */

static int hw_used(const Alloc* al, int alpha, uint8_t hw) {
    int r;
    for (r = 0; r < 4; r++)
        if ((alpha ? al->a[r] : al->rgb[r]) == hw) return 1;
    return 0;
}

static uint8_t pick_free(const Alloc* al, int alpha, uint8_t avoid) {
    const uint8_t* pool = alpha ? k_pool_a : k_pool_rgb;
    int n = alpha ? (int)sizeof k_pool_a : (int)sizeof k_pool_rgb;
    int i;
    for (i = 0; i < n; i++)
        if (pool[i] != avoid && !hw_used(al, alpha, pool[i])) return pool[i];
    return 0;
}

/* TEV register read -> operand */
static Op reg_op(const Alloc* al, int reg, int want_alpha, int alpha_portion) {
    Op o = OP_ZERO;
    uint8_t hw = want_alpha ? al->a[reg] : al->rgb[reg];
    if (hw) {
        o.src = hw;
        o.alpha = (uint8_t)(want_alpha ? 1 : 0);
    } else {
        o.cref = want_alpha ? XREF(XREF_TEVREG_A, reg) : XREF(XREF_TEVREG_RGB, reg);
        o.alpha = (uint8_t)(want_alpha || alpha_portion);
    }
    (void)alpha_portion;
    return o;
}

static Op color_arg(const Alloc* al, int s, int arg, const XTevStage* ts) {
    Op o = OP_ZERO;
    switch (arg) {
        case 0: return reg_op(al, 0, 0, 0);   /* CPREV */
        case 1: return reg_op(al, 0, 1, 0);   /* APREV */
        case 2: return reg_op(al, 1, 0, 0);   /* C0 */
        case 3: return reg_op(al, 1, 1, 0);   /* A0 */
        case 4: return reg_op(al, 2, 0, 0);
        case 5: return reg_op(al, 2, 1, 0);
        case 6: return reg_op(al, 3, 0, 0);
        case 7: return reg_op(al, 3, 1, 0);
        case 8: case 9:                       /* TEXC / TEXA */
            if (!al->use_tex[s]) return OP_ONE;
            o.src = (uint8_t)(S_T0 + s);
            o.alpha = (uint8_t)(arg == 9);
            return o;
        case 10: case 11:                     /* RASC / RASA */
            o.src = S_V0;
            o.alpha = (uint8_t)(arg == 11);
            return o;
        case 12: return OP_ONE;
        case 13: return OP_HALF;
        case 14:
            o.cref = XREF(XREF_KONST_C, ts->kcsel);
            return o;
        default: return OP_ZERO;
    }
}

static Op alpha_arg(const Alloc* al, int s, int arg, const XTevStage* ts) {
    Op o = OP_ZERO;
    switch (arg) {
        case 0: return reg_op(al, 0, 1, 1);
        case 1: return reg_op(al, 1, 1, 1);
        case 2: return reg_op(al, 2, 1, 1);
        case 3: return reg_op(al, 3, 1, 1);
        case 4:
            if (!al->use_tex[s]) return OP_ONE;
            o.src = (uint8_t)(S_T0 + s);
            o.alpha = 1;
            return o;
        case 5:
            o.src = S_V0;
            o.alpha = 1;
            return o;
        case 6:
            o.cref = XREF(XREF_KONST_A, ts->kasel);
            o.alpha = 1;
            return o;
        default: return OP_ZERO;
    }
}

/* ---------------- one portion (rgb or alpha) of one TEV stage -------------- */
typedef struct { Op a, b; } Term;

typedef struct {
    int n;            /* 1 or 2 NV2A stages */
    Term t[2][2];     /* [nv2a stage][AB, CD] */
    uint8_t dst[2];   /* destination register per NV2A stage */
    uint8_t op[2];
} Portion;

static int emit_portion(Op a, Op b, Op c, Op d, int sub, int bias, int scale,
                        uint8_t dst, uint8_t scratch, Portion* p, int* approx) {
    Term lerp[2];
    int nl = 0, i;
    Term terms[4];
    int nt = 0;
    int shift;

    /* lerp = a*(1-c) + b*c, collapsed */
    if (is_zero(c) || same(a, b)) {
        if (!is_zero(a)) { lerp[nl].a = a; lerp[nl].b = OP_ONE; nl++; }
    } else if (is_one(c)) {
        if (!is_zero(b)) { lerp[nl].a = b; lerp[nl].b = OP_ONE; nl++; }
    } else {
        if (!is_zero(a)) { lerp[nl].a = a; lerp[nl].b = op_inv(c); nl++; }
        if (!is_zero(b)) { lerp[nl].a = b; lerp[nl].b = c; nl++; }
    }

    switch (scale) {
        case 1: shift = 2; break;   /* x2 */
        case 2: shift = 4; break;   /* x4 */
        case 3: shift = 6; break;   /* /2 */
        default: shift = 0; break;
    }

    /* try single stage: terms = [-]lerp..., d, bias */
    {
        int ok = 1;
        for (i = 0; i < nl; i++) {
            Term t = lerp[i];
            if (sub) {
                Op n;
                if (op_neg(t.a, &n)) t.a = n;
                else if (op_neg(t.b, &n)) t.b = n;
                else { ok = 0; break; }
            }
            terms[nt++] = t;
        }
        if (ok && !is_zero(d)) { terms[nt].a = d; terms[nt].b = OP_ONE; nt++; }
        if (ok) {
            int op = shift;
            if (bias == 2 && (shift == 0 || shift == 2)) op = shift + 1;        /* -0.5 via output bias */
            else if (bias == 1) { terms[nt].a = OP_HALF; terms[nt].b = OP_ONE; nt++; }
            else if (bias == 2) { terms[nt].a = OP_HALF; terms[nt].b = OP_ONE;
                                  op_neg(terms[nt].a, &terms[nt].a); nt++; }
            if (nt <= 2) {
                p->n = 1;
                p->t[0][0] = nt > 0 ? terms[0] : (Term){ OP_ZERO, OP_ZERO };
                p->t[0][1] = nt > 1 ? terms[1] : (Term){ OP_ZERO, OP_ZERO };
                p->dst[0] = dst;
                p->op[0] = (uint8_t)op;
                return 1;
            }
        }
    }

    /* two stages: scratch = lerp (+/-0.5 folded if it fits); dst = d +/- scratch */
    if (!scratch) { *approx = 1; scratch = dst; }
    p->n = 2;
    p->t[0][0] = nl > 0 ? lerp[0] : (Term){ OP_ZERO, OP_ZERO };
    p->t[0][1] = nl > 1 ? lerp[1] : (Term){ OP_ZERO, OP_ZERO };
    p->dst[0] = scratch;
    p->op[0] = OP_NOSHIFT;
    {
        int op = shift;
        Op s = { scratch, 0, M_UID, 0 };
        if (bias == 2 && (shift == 0 || shift == 2)) op = shift + 1;
        else if (bias && nl < 2) {
            /* result = d +/- (lerp -/+ 0.5): fold the half into stage A */
            Term h = { OP_HALF, OP_ONE };
            int want_neg = (bias == 1) == (sub != 0);   /* +0.5 under sub, or -0.5 under add */
            if (want_neg) op_neg(h.a, &h.a);
            p->t[0][1] = h;
        } else if (bias) {
            *approx = 1;
        }
        if (sub) s.map = M_SNEG;
        p->t[1][0] = (Term){ d, OP_ONE };
        p->t[1][1] = (Term){ s, OP_ONE };
        p->dst[1] = dst;
        p->op[1] = (uint8_t)op;
    }
    return 2;
}

/* ---------------- constant slot allocation per NV2A stage ---------------- */
static uint8_t alloc_const(uint16_t slots[4], uint16_t ref, int want_alpha_slot, int* approx) {
    /* slots: 0 C0.rgb 1 C0.a 2 C1.rgb 3 C1.a */
    int base = want_alpha_slot ? 1 : 0;
    int k;
    for (k = 0; k < 2; k++)
        if (slots[base + 2 * k] == ref) return (uint8_t)(k ? S_C1 : S_C0);
    for (k = 0; k < 2; k++)
        if (slots[base + 2 * k] == 0) { slots[base + 2 * k] = ref; return (uint8_t)(k ? S_C1 : S_C0); }
    *approx = 1;
    return S_ZERO;
}

static uint32_t enc_in(Op o) {
    return (uint32_t)(o.src & 0xF) | ((uint32_t)(o.alpha & 1) << 4) | ((uint32_t)(o.map & 7) << 5);
}

static void resolve(Op* o, uint16_t slots[4], int alpha_portion, int* approx) {
    if (!o->cref) return;
    {
        /* rgb portion reading a 3-component constant uses the rgb slot; anything
         * read with the alpha flag (alpha portion, or A0/APREV/KONST-alpha in the
         * rgb portion) uses the alpha slot */
        int use_alpha_slot = alpha_portion || o->alpha;
        o->src = alloc_const(slots, o->cref, use_alpha_slot, approx);
        o->alpha = (uint8_t)use_alpha_slot;
        o->cref = 0;
    }
}

static uint32_t make_icw(Term ab, Term cd) {
    return (enc_in(ab.a) << 24) | (enc_in(ab.b) << 16) | (enc_in(cd.a) << 8) | enc_in(cd.b);
}

static uint32_t make_ocw(uint8_t dst, uint8_t op) {
    /* cd_dst = 0, ab_dst = 0, sum_dst = dst, op */
    return ((uint32_t)(dst & 0xF) << 8) | ((uint32_t)(op & 7) << 15);
}

void xbox_tev_compile(const XTevCfg* cfg, XRcProg* out) {
    Alloc al;
    int s, n = 0;

    memset(out, 0, sizeof *out);
    memset(&al, 0, sizeof al);
    for (s = 0; s < XRC_MAX_TEV; s++) al.use_tex[s] = s < cfg->nstages && cfg->st[s].use_tex;

    for (s = 0; s < cfg->nstages && s < XRC_MAX_TEV; s++) {
        const XTevStage* ts = &cfg->st[s];
        Portion pc, pa;
        uint8_t cdst, adst, cscr, ascr;
        int k, groups;

        /* destinations (PREV is pinned to R0) */
        cdst = ts->cout == 0 ? S_R0 : al.rgb[ts->cout];
        if (!cdst) cdst = pick_free(&al, 0, 0);
        if (!cdst) { cdst = S_R1; out->approximated = 1; }
        adst = ts->aout == 0 ? S_R0 : al.a[ts->aout];
        if (!adst) adst = pick_free(&al, 1, 0);
        if (!adst) { adst = S_R1; out->approximated = 1; }
        cscr = pick_free(&al, 0, cdst);
        ascr = pick_free(&al, 1, adst);

        memset(&pc, 0, sizeof pc);
        memset(&pa, 0, sizeof pa);
        emit_portion(color_arg(&al, s, ts->cin[0], ts), color_arg(&al, s, ts->cin[1], ts),
                     color_arg(&al, s, ts->cin[2], ts), color_arg(&al, s, ts->cin[3], ts),
                     ts->cop == 1, ts->cbias, ts->cscale, cdst, cscr, &pc, &out->approximated);
        emit_portion(alpha_arg(&al, s, ts->ain[0], ts), alpha_arg(&al, s, ts->ain[1], ts),
                     alpha_arg(&al, s, ts->ain[2], ts), alpha_arg(&al, s, ts->ain[3], ts),
                     ts->aop == 1, ts->abias, ts->ascale, adst, ascr, &pa, &out->approximated);

        /* pack: the destination write of each portion lands in the group's last
         * NV2A stage, so every read in this TEV stage sees pre-stage values */
        groups = pc.n > pa.n ? pc.n : pa.n;
        if (n + groups > XRC_MAX_STAGES) { out->approximated = 1; break; }
        for (k = 0; k < groups; k++) {
            int ci = pc.n == groups ? k : k - (groups - pc.n);
            int ai = pa.n == groups ? k : k - (groups - pa.n);
            uint16_t* slots = out->cref[n];
            Term cab = { OP_ZERO, OP_ZERO }, ccd = cab, aab = cab, acd = cab;
            uint8_t cd = 0, ad = 0, cop = 0, aop = 0;
            if (ci >= 0) { cab = pc.t[ci][0]; ccd = pc.t[ci][1]; cd = pc.dst[ci]; cop = pc.op[ci]; }
            if (ai >= 0) { aab = pa.t[ai][0]; acd = pa.t[ai][1]; ad = pa.dst[ai]; aop = pa.op[ai]; }
            resolve(&cab.a, slots, 0, &out->approximated);
            resolve(&cab.b, slots, 0, &out->approximated);
            resolve(&ccd.a, slots, 0, &out->approximated);
            resolve(&ccd.b, slots, 0, &out->approximated);
            resolve(&aab.a, slots, 1, &out->approximated);
            resolve(&aab.b, slots, 1, &out->approximated);
            resolve(&acd.a, slots, 1, &out->approximated);
            resolve(&acd.b, slots, 1, &out->approximated);
            /* alpha-portion inputs must select the alpha channel (else blue) */
            aab.a.alpha = aab.b.alpha = acd.a.alpha = acd.b.alpha = 1;
            if (is_zero(aab.a) || aab.a.src == S_ZERO) aab.a.alpha = 1;
            out->cicw[n] = make_icw(cab, ccd);
            out->cocw[n] = make_ocw(cd, cop);
            out->aicw[n] = make_icw(aab, acd);
            out->aocw[n] = make_ocw(ad, aop);
            n++;
        }

        /* record the writes */
        if (ts->cout != 0) al.rgb[ts->cout] = cdst; else al.rgb[0] = S_R0;
        if (ts->aout != 0) al.a[ts->aout] = adst; else al.a[0] = S_R0;
    }
    if (n == 0) {
        /* no TEV stage: pass PREV constant through */
        out->cicw[0] = 0;
        out->cocw[0] = 0;
        out->aicw[0] = 0;
        out->aocw[0] = 0;
        n = 1;
    }
    out->nstages = n;

    /* final combiner: rgb = A*B + (1-A)*C + D ; alpha = G */
    {
        Op prev_rgb = { S_ZERO, 0, M_UID, 0 }, prev_a = prev_rgb;
        Op A = OP_ZERO, B = OP_ZERO, D = OP_ZERO;
        if (al.rgb[0]) prev_rgb.src = al.rgb[0];
        else { prev_rgb.src = S_C1; out->fref[2] = XREF(XREF_TEVREG_RGB, 0); }
        if (al.a[0]) { prev_a.src = al.a[0]; prev_a.alpha = 1; }
        else { prev_a.src = S_C1; prev_a.alpha = 1; out->fref[3] = XREF(XREF_TEVREG_A, 0); }
        if (cfg->fog_on) {
            A.src = S_V1; A.alpha = 1;          /* fog factor */
            B.src = S_C0;                        /* fog colour */
            out->fref[0] = XREF(XREF_FOG_RGB, 0);
        }
        /* final-combiner inputs only take the invert bit (bit 5), not a mapping */
        out->cw0 = ((uint32_t)(A.src | (A.alpha << 4)) << 24) |
                   ((uint32_t)(B.src | (B.alpha << 4)) << 16) |
                   ((uint32_t)(prev_rgb.src | (prev_rgb.alpha << 4)) << 8) |
                   (uint32_t)(D.src | (D.alpha << 4));
        out->cw1 = ((uint32_t)(prev_a.src | (prev_a.alpha << 4)) << 8) | 0x80 /* specular clamp */;
    }
}
