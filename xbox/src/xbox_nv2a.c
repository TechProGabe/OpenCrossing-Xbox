/* xbox_nv2a.c — the OpenGL 3.3 subset pc_gx*.c use, implemented on the NV2A.
 *
 * pc/src/pc_gx.c stays upstream: it still sets uniforms and calls glDraw*.
 * glad's function pointers are pointed here (xbox_gl_nv2a_load). Uniforms land
 * in a table keyed by name; at each draw the table is turned into
 *   - vertex-program constants for xbox/shaders/gx.vsh (transform with the GL
 *     viewport folded in, per-vertex GC lighting, per-stage texgen, fog),
 *   - a register-combiner program compiled from the TEV uniforms
 *     (xbox_tev_rc.c, cached by config),
 *   - texture stages 0..2, fixed state (depth, blend, cull, masks, scissor,
 *     alpha test),
 * and the vertices are copied into a contiguous ring and drawn with
 * NV097_DRAW_ARRAYS. Textures are RGBA8 from pc_gx_texture.c, converted to
 * swizzled A8R8G8B8 in a sub-allocated contiguous pool; NPOT images are padded
 * to POT and the texcoords rescaled in the vertex program.
 * docs/renderer.md has the design and the known gaps. */
#include <hal/video.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <glad/gl.h>
#include "xbox_io.h"
#include "xbox_nv2a.h"
#include "xbox_fbdump.h"
#include "xbox_settings.h"
#include "xbox_splash.h"
#include <pbkit/pbkit_framebuffer.h>

/* The real framebuffer: 640x480, or 1280x720 when this boot runs at 720p.
 * pc_gx.c works in a logical screen of g_pc_window_w x g_pc_window_h (640x480,
 * or 854x480 for 16:9); gl_viewport/gl_scissor/gl_read_pixels scale between
 * the two (xbox_settings.c). */
static int s_fbw = 640, s_fbh = 480, s_fb_bpp = 32;
#define SCR_W s_fbw
#define SCR_H s_fbh
/* depth range of the zeta surface: Z24S8, or Z16 at 720p (video_select) */
static float s_zmax = 16777215.0f;
#define ZMAX s_zmax
extern unsigned int pb_DepthFmt;   /* settable: tools/xbox/patch_pbkit.py */

#define SETF(x) (*(const uint32_t*)&(x))

#ifndef XBOX_FBDUMP_EVERY
#define XBOX_FBDUMP_EVERY 0
#endif
int g_xbox_fbdump_every = XBOX_FBDUMP_EVERY;
int g_xbox_fbdump_once;   /* one screenshot at the next present (xbox_autopad.c) */
static uint32_t s_n_da, s_n_de, s_n_bd, s_n_clr, s_n_null, s_n_clr_frame;

/* ======================================================================
 * Uniform table
 * ====================================================================== */
typedef union { float f; int i; } UVal;

typedef struct {
    const char* name;
    int comps, count;
    UVal* v;
} Uniform;

enum {
    U_PROJ, U_MV, U_NRM, U_PREV, U_REG0, U_REG1, U_REG2, U_NUMST,
    U_CIN, U_AIN, U_COP, U_AOP, U_TCSRC, U_INDCFG, U_INDWRAP,
    U_KCOLOR, U_KSEL, U_ACTRL, U_AREFS, U_LCFG0, U_CHANCOL, U_LCFG1,
    U_LPOS, U_LCOL, U_TMEN, U_TMR0, U_TMR1, U_TGSRC, U_USETEX,
    U_TEX0, U_TEX1, U_TEX2, U_NUMIND, U_INDTEX0, U_INDTEX1, U_INDTEX2, U_INDTEX3,
    U_INDSCALE, U_INDM0, U_INDM1, U_FOGP, U_FOGEN, U_FOGCOL, U_BSC, U_OUT, U_SWAP, U_SWAPTBL,
    U_COUNT
};

static UVal s_uv[U_COUNT][16 * 4];
static Uniform s_u[U_COUNT] = {
    [U_PROJ] = {"u_projection", 16, 1}, [U_MV] = {"u_modelview", 16, 1}, [U_NRM] = {"u_normal_mtx", 9, 1},
    [U_PREV] = {"u_tev_prev", 4, 1}, [U_REG0] = {"u_tev_reg0", 4, 1}, [U_REG1] = {"u_tev_reg1", 4, 1},
    [U_REG2] = {"u_tev_reg2", 4, 1}, [U_NUMST] = {"u_num_tev_stages", 1, 1},
    [U_CIN] = {"u_tev_color_in", 4, 3}, [U_AIN] = {"u_tev_alpha_in", 4, 3},
    [U_COP] = {"u_tev_color_op", 1, 3}, [U_AOP] = {"u_tev_alpha_op", 1, 3},
    [U_TCSRC] = {"u_tev_tc_src", 1, 3}, [U_INDCFG] = {"u_tev_ind_cfg", 4, 3},
    [U_INDWRAP] = {"u_tev_ind_wrap", 3, 3}, [U_KCOLOR] = {"u_kcolor", 4, 4},
    [U_KSEL] = {"u_tev_ksel", 3, 3}, [U_ACTRL] = {"u_alpha_ctrl", 3, 1}, [U_AREFS] = {"u_alpha_refs", 2, 1},
    [U_LCFG0] = {"u_lighting_cfg0", 4, 1}, [U_CHANCOL] = {"u_chan_color", 4, 2},
    [U_LCFG1] = {"u_lighting_cfg1", 4, 1}, [U_LPOS] = {"u_light_pos", 3, 8}, [U_LCOL] = {"u_light_color", 4, 8},
    [U_TMEN] = {"u_texmtx_enable", 1, 2}, [U_TMR0] = {"u_texmtx_row0", 4, 2}, [U_TMR1] = {"u_texmtx_row1", 4, 2},
    [U_TGSRC] = {"u_texgen_src", 1, 2}, [U_USETEX] = {"u_use_texture", 1, 3},
    [U_TEX0] = {"u_texture0", 1, 1}, [U_TEX1] = {"u_texture1", 1, 1}, [U_TEX2] = {"u_texture2", 1, 1},
    [U_NUMIND] = {"u_num_ind_stages", 1, 1}, [U_INDTEX0] = {"u_ind_tex0", 1, 1},
    [U_INDTEX1] = {"u_ind_tex1", 1, 1}, [U_INDTEX2] = {"u_ind_tex2", 1, 1}, [U_INDTEX3] = {"u_ind_tex3", 1, 1},
    [U_INDSCALE] = {"u_ind_scale", 2, 4}, [U_INDM0] = {"u_ind_mtx_r0", 3, 3}, [U_INDM1] = {"u_ind_mtx_r1", 3, 3},
    [U_FOGP] = {"u_fog_params", 4, 1}, [U_FOGEN] = {"u_fog_enable", 1, 1}, [U_FOGCOL] = {"u_fog_color", 4, 1},
    [U_BSC] = {"u_tev_bsc", 4, 3}, [U_OUT] = {"u_tev_out", 4, 3}, [U_SWAP] = {"u_tev_swap", 2, 3},
    [U_SWAPTBL] = {"u_swap_table", 4, 4},
};

#define UI(u, e, c) (s_uv[u][(e) * s_u[u].comps + (c)].i)
#define UF(u, e, c) (s_uv[u][(e) * s_u[u].comps + (c)].f)

/* Per-draw skips (Melee-X renderer.md "CPU cost of the back end"). pc_gx.c
 * sets every uniform before every draw, mostly to the value it already has.
 * A setter that changes a value marks the groups that read it; draw() then
 * rebuilds only those: the TEV config and its combiner program (D_TEV), the
 * combiner constants (D_TEVK), and the vertex-constant blocks (projection,
 * modelview, material, lights, texgen). Textures bump s_tex_epoch whenever
 * what a unit samples can change. Kill switch: -DXBOX_DRAW_SKIP=0, or
 * draw_skip = 0 in settings.ini (everything rebuilt for every draw). */
#ifndef XBOX_DRAW_SKIP
#define XBOX_DRAW_SKIP 1
#endif
enum { D_TEV = 1, D_TEVK = 2, D_PROJ = 4, D_MV = 8, D_MAT = 16, D_LIGHT = 32, D_TEXGEN = 64, D_ALL = 127 };
static const uint8_t k_ugroup[U_COUNT] = {
    [U_PROJ] = D_PROJ, [U_MV] = D_MV, [U_NRM] = D_MV,
    [U_PREV] = D_TEVK, [U_REG0] = D_TEVK, [U_REG1] = D_TEVK, [U_REG2] = D_TEVK, [U_KCOLOR] = D_TEVK,
    [U_FOGCOL] = D_TEVK,
    [U_NUMST] = D_TEV, [U_CIN] = D_TEV, [U_AIN] = D_TEV, [U_COP] = D_TEV, [U_AOP] = D_TEV, [U_BSC] = D_TEV,
    [U_OUT] = D_TEV, [U_KSEL] = D_TEV, [U_USETEX] = D_TEV, [U_FOGEN] = D_TEV | D_MAT,
    [U_CHANCOL] = D_MAT, [U_LCFG0] = D_MAT, [U_FOGP] = D_MAT, [U_LCFG1] = D_MAT | D_LIGHT,
    [U_LPOS] = D_LIGHT, [U_LCOL] = D_LIGHT,
    [U_TCSRC] = D_TEXGEN, [U_TMEN] = D_TEXGEN, [U_TMR0] = D_TEXGEN, [U_TMR1] = D_TEXGEN, [U_TGSRC] = D_TEXGEN,
};
static uint32_t s_udirty = D_ALL;
static uint32_t s_tex_epoch = 1;

static GLint gl_get_uniform_location(GLuint prog, const GLchar* name) {
    int k;
    (void)prog;
    for (k = 0; k < U_COUNT; k++) {
        size_t n = strlen(s_u[k].name);
        if (strncmp(name, s_u[k].name, n) == 0) {
            if (name[n] == '\0') return k << 8;
            if (name[n] == '[') return (k << 8) | (atoi(name + n + 1) & 0xFF);
        }
    }
    return -1;
}

static UVal* uslot(GLint loc, int* room) {
    int k = loc >> 8, e = loc & 0xFF;
    if (loc < 0 || k >= U_COUNT || e >= s_u[k].count) return NULL;
    *room = (s_u[k].count - e) * s_u[k].comps;
    return &s_uv[k][e * s_u[k].comps];
}

/* floats are compared as bits: -0 and NaN payloads count as changes */
static void set_f(GLint loc, int comps, int count, const GLfloat* v) {
    int room, n, i, diff = 0;
    UVal* d = uslot(loc, &room);
    if (!d) return;
    n = comps * count;
    if (n > room) n = room;
    for (i = 0; i < n; i++) {
        UVal u;
        u.f = v[i];
        diff |= d[i].i ^ u.i;
        d[i].i = u.i;
    }
    if (diff) s_udirty |= k_ugroup[loc >> 8];
}

static void set_i(GLint loc, int comps, int count, const GLint* v) {
    int room, n, i, diff = 0;
    UVal* d = uslot(loc, &room);
    if (!d) return;
    n = comps * count;
    if (n > room) n = room;
    for (i = 0; i < n; i++) {
        diff |= d[i].i ^ v[i];
        d[i].i = v[i];
    }
    if (diff) s_udirty |= k_ugroup[loc >> 8];
}

static void u1i(GLint l, GLint a) { set_i(l, 1, 1, &a); }
static void u2i(GLint l, GLint a, GLint b) { GLint v[2] = {a, b}; set_i(l, 2, 1, v); }
static void u3i(GLint l, GLint a, GLint b, GLint c) { GLint v[3] = {a, b, c}; set_i(l, 3, 1, v); }
static void u4i(GLint l, GLint a, GLint b, GLint c, GLint d) { GLint v[4] = {a, b, c, d}; set_i(l, 4, 1, v); }
static void u1f(GLint l, GLfloat a) { set_f(l, 1, 1, &a); }
static void u2f(GLint l, GLfloat a, GLfloat b) { GLfloat v[2] = {a, b}; set_f(l, 2, 1, v); }
static void u3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { GLfloat v[3] = {a, b, c}; set_f(l, 3, 1, v); }
static void u4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { GLfloat v[4] = {a, b, c, d}; set_f(l, 4, 1, v); }
static void u1iv(GLint l, GLsizei n, const GLint* v) { set_i(l, 1, n, v); }
static void u2iv(GLint l, GLsizei n, const GLint* v) { set_i(l, 2, n, v); }
static void u3iv(GLint l, GLsizei n, const GLint* v) { set_i(l, 3, n, v); }
static void u4iv(GLint l, GLsizei n, const GLint* v) { set_i(l, 4, n, v); }
static void u1fv(GLint l, GLsizei n, const GLfloat* v) { set_f(l, 1, n, v); }
static void u2fv(GLint l, GLsizei n, const GLfloat* v) { set_f(l, 2, n, v); }
static void u3fv(GLint l, GLsizei n, const GLfloat* v) { set_f(l, 3, n, v); }
static void u4fv(GLint l, GLsizei n, const GLfloat* v) { set_f(l, 4, n, v); }

/* Stored row-major. pc_gx.c always passes transpose=GL_TRUE (row-major in). */
static void umat(GLint l, int dim, GLboolean transpose, const GLfloat* m) {
    GLfloat t[16];
    int r, c;
    if (transpose) { set_f(l, dim * dim, 1, m); return; }
    for (r = 0; r < dim; r++)
        for (c = 0; c < dim; c++) t[r * dim + c] = m[c * dim + r];
    set_f(l, dim * dim, 1, t);
}
static void um3(GLint l, GLsizei n, GLboolean tr, const GLfloat* m) { (void)n; umat(l, 3, tr, m); }
static void um4(GLint l, GLsizei n, GLboolean tr, const GLfloat* m) { (void)n; umat(l, 4, tr, m); }

/* ======================================================================
 * Contiguous memory: textures (pool) and vertices (ring)
 * ====================================================================== */
#ifndef XBOX_TEX_POOL_BYTES
#define XBOX_TEX_POOL_BYTES (8 * 1024 * 1024)
#endif
#ifndef XBOX_VTX_RING_BYTES
#define XBOX_VTX_RING_BYTES (1024 * 1024)
#endif
/* at 720p the framebuffers take ~3 MB more; the pool gives it back */
#ifndef XBOX_TEX_POOL_720P_BYTES
#define XBOX_TEX_POOL_720P_BYTES (5 * 1024 * 1024)
#endif
#define POOL_ALIGN 128
static uint32_t s_pool_bytes = XBOX_TEX_POOL_BYTES;

typedef struct Blk { uint32_t off, size; int free; struct Blk* next; } Blk;
static uint8_t* s_pool;
static Blk* s_blocks;
static uint32_t s_pool_used, s_pool_peak;

static void pool_init(void) {
    s_pool = (uint8_t*)MmAllocateContiguousMemoryEx(s_pool_bytes, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    s_blocks = (Blk*)calloc(1, sizeof(Blk));
    s_blocks->size = s_pool_bytes;
    s_blocks->free = 1;
}

/* Small textures are taken first-fit from the bottom of the pool, big ones
 * (the 640x480 screen grab pads to 2 MB) last-fit from the top, so the town's
 * hundreds of small textures can't chop up the space a big one needs. A failed
 * big allocation used to leave the inventory background white. */
#define POOL_BIG (256 * 1024)

static void* pool_alloc(uint32_t size) {
    Blk *b, *pick = NULL;
    size = (size + POOL_ALIGN - 1) & ~(uint32_t)(POOL_ALIGN - 1);
    for (b = s_blocks; b; b = b->next) {
        if (!b->free || b->size < size) continue;
        pick = b;
        if (size < POOL_BIG) break;   /* first fit; big ones keep the last fit */
    }
    if (!pick) return NULL;
    b = pick;
    if (b->size > size) {
        Blk* n = (Blk*)calloc(1, sizeof(Blk));
        if (!n) return NULL;
        n->free = 1;
        n->next = b->next;
        b->next = n;
        if (size < POOL_BIG) {
            n->off = b->off + size;
            n->size = b->size - size;
            b->size = size;
        } else {
            /* take the top of the hole: the free remainder stays below */
            n->off = b->off + b->size - size;
            n->size = size;
            b->size -= size;
            n->free = 0;
            s_pool_used += size;
            if (s_pool_used > s_pool_peak) s_pool_peak = s_pool_used;
            return s_pool + n->off;
        }
    }
    b->free = 0;
    s_pool_used += size;
    if (s_pool_used > s_pool_peak) s_pool_peak = s_pool_used;
    return s_pool + b->off;
}

static void pool_free(void* p) {
    Blk* b;
    uint32_t off;
    if (!p) return;
    off = (uint32_t)((uint8_t*)p - s_pool);
    for (b = s_blocks; b; b = b->next) {
        if (b->off == off && !b->free) {
            b->free = 1;
            s_pool_used -= b->size;
            break;
        }
    }
    /* coalesce */
    for (b = s_blocks; b && b->next;) {
        if (b->free && b->next->free) {
            Blk* n = b->next;
            b->size += n->size;
            b->next = n->next;
            free(n);
        } else {
            b = b->next;
        }
    }
}

/* compact vertex fed to gx.vsh */
typedef struct { float pos[3]; float nrm[3]; uint8_t col[4]; float tc[2]; } XVtx;   /* 36 B */
static XVtx* s_ring;
static uint32_t s_ring_cap, s_ring_pos;

/* ======================================================================
 * GL objects
 * ====================================================================== */
#define MAX_TEX 4096
typedef struct {
    int used;
    int w, h, pw, ph;
    void* mem;
    int wrap_s, wrap_t, min_f, mag_f;
    uint32_t nvfmt, bpp;   /* NV097_SET_TEXTURE_FORMAT_COLOR_SZ_*, bytes per texel */
    uint32_t drawn;        /* s_frame + 1 of the last frame that drew it, 0 = never */
} XTex;
static XTex s_tex[MAX_TEX];
static int s_tex_next = 1;
static void* s_deferred_free[4096];
static int s_ndeferred;
static int s_active_unit;
static GLuint s_bound[8];

static GLuint s_next_obj = 1;
static const void* s_array_data;     /* last glBufferData(GL_ARRAY_BUFFER) */
static GLuint s_program;             /* current program */
static GLuint s_uber_prog = 1;       /* xbox_gx_tev.c's single variant */

/* fixed state */
static struct {
    int depth_test, depth_func, depth_mask;
    int blend, sfac, dfac, beq;
    int cull, cull_face;
    int cmask;
    int scissor, sx, sy, sw, sh;
    int vx, vy, vw, vh;
    float dn, df;
    float clear_c[4];
    float clear_d;
} G = {
    0, GL_LESS, 1, 0, GL_ONE, GL_ZERO, GL_FUNC_ADD, 0, GL_BACK, 0x01010101,
    0, 0, 0, 640, 480, 0, 0, 640, 480, 0.0f, 1.0f, {0, 0, 0, 0}, 1.0f
};

/* ======================================================================
 * Push helpers
 * ====================================================================== */
static uint32_t* P;
/* NV_PCRTC_START: address of the framebuffer the CRTC is scanning out */
#define PCRTC_START_REG (*(volatile uint32_t*)0xFD600800)
#define PB_BEGIN() (P = pb_begin())
#define PB_END() pb_end(P)

static inline void put1(uint32_t m, uint32_t v);

/* Draws share one open pushbuffer block, closed only every s_pb_kick dwords
 * or before anything that must see the GPU caught up: every pb_end runs
 * pbkit's pb_cache_flush (sfence + write-combine flush + MMIO poll), and
 * with two blocks per draw that was ~400 flushes a frame and the second-
 * hottest function in the profile. 32 KB a kick since the Melee-X backport
 * (Melee-X measured each kick's flush at ~2% of its CPU at 16 KB);
 * settings.ini pushbuffer_kick_kb sets the size (16 = the old one). Kill
 * switch: -DXBOX_PB_KICK=0 (close after every draw). */
#ifndef XBOX_PB_KICK
#define XBOX_PB_KICK 1
#endif
static uint32_t s_pb_kick = 8192;   /* dwords; pushbuffer_kick_kb at init */
static int s_pb_open;
static uint32_t* s_pb_mark;

/* The NV2A caches vertex data by address, and a draw's fetch reads ahead of
 * its last vertex. Once a batch is kicked the GPU can run it before the CPU
 * writes the next vertices into the ring right behind it, and the next draw
 * then takes its first vertices from the stale read-ahead (Melee-X: black
 * wedges on the console only; xemu has no such cache). Every batch starts by
 * dropping that cache. Kill switch: -DXBOX_VB_CACHE_BREAK=0, or
 * vertex_cache_break = 0 in settings.ini. */
#ifndef XBOX_VB_CACHE_BREAK
#define XBOX_VB_CACHE_BREAK 1
#endif
static int s_vb_break = XBOX_VB_CACHE_BREAK;

static void pb_open(void) {
    if (s_pb_open) return;
    PB_BEGIN();
    s_pb_mark = P;
    s_pb_open = 1;
    if (s_vb_break) put1(NV097_BREAK_VERTEX_BUFFER_CACHE, 0);
}

static void pb_close(void) {
    if (!s_pb_open) return;
    PB_END();
    s_pb_open = 0;
}
/* pb_push1 inlined (subchannel 0 = 3D, one parameter): it is called a few
 * hundred thousand times a frame's worth of state and was an out-of-line call
 * into pbkit for every word pair */
static inline void put1(uint32_t m, uint32_t v) { P[0] = (1u << 18) | m; P[1] = v; P += 2; }
static inline void putf(uint32_t m, float v) { put1(m, SETF(v)); }

/* ---- pushbuffer budget ----
 * pbkit's pushbuffer (pb_size, 1 MB) has no overflow check: a frame that
 * pushes more than that writes past the end of it into whatever contiguous
 * memory follows and feeds the GPU garbage. Every frame restarts at the head
 * (frame_open), and a frame that gets within XBOX_PB_GUARD bytes of the end
 * drains the GPU and restarts at the head mid-frame. Kill switch:
 * -DXBOX_PB_GUARD=0. */
#define PB_BYTES (1024 * 1024)
#ifndef XBOX_PB_GUARD
#define XBOX_PB_GUARD (PB_BYTES - 128 * 1024)
#endif
static uint32_t* s_pb_base;          /* pb_Put right after the last pb_reset */
static uint32_t s_pb_peak;           /* most bytes pushed between two resets, this minute */
static uint32_t s_pb_rewinds;

/* pbkit's pb_begin() only returns pb_Put when DBG is off (it is) */
static uint32_t pb_used(void) {
    const uint32_t* p = s_pb_open ? P : pb_begin();
    return (uint32_t)((const uint8_t*)p - (const uint8_t*)s_pb_base);
}

static void pb_budget(void);

static void pb_note_peak(void) {
    uint32_t u = pb_used();
    if (u > s_pb_peak) s_pb_peak = u;
}

/* ======================================================================
 * GPU faults (tools/xbox/patch_pbkit.py)
 * ====================================================================== */
/* Called by the patched pbkit from its DPC: PGRAPH errors (kind 1), DMA
 * pusher errors (2), a DPC that didn't drain in 64 rounds (3), and interrupt-
 * time register waits that timed out (10-14). Only records; xbox_nv2a_present
 * logs at passive level. Sixteen storms without a frame presented in between
 * leave the GPU interrupt masked, so threads (and the watchdog) run again. */
volatile int ocx_pb_irq_off;
static volatile uint32_t s_gf_count, s_gf_storms, s_gf_last[5];
static uint32_t s_gf_logged;
static uint32_t s_draws, s_approx_draws, s_frame;
/* The first fault, with where the pusher was and which draw of which frame
 * the CPU had reached (later ones are usually its consequences), and PGRAPH
 * 0x400700-0x4008FC as they were then (surface, zeta, clip, limit and trap
 * state; decode against xemu's nv2a_regs). From Melee-X, where it found the
 * EFB-copy stalls. */
static volatile uint32_t s_gf_first[9], s_gf_first_logged, s_gf_regs[128];

void ocx_pb_gpu_fault(unsigned kind, unsigned a, unsigned b, unsigned c, unsigned d) {
    if (!s_gf_count) {
        int i;
        s_gf_first[0] = kind;
        s_gf_first[1] = a;
        s_gf_first[2] = b;
        s_gf_first[3] = c;
        s_gf_first[4] = d;
        s_gf_first[5] = *(volatile uint32_t*)(0xFD000000u + 0x3244);   /* DMA GET */
        s_gf_first[6] = *(volatile uint32_t*)(0xFD000000u + 0x3240);   /* DMA PUT */
        s_gf_first[7] = s_frame;
        s_gf_first[8] = s_draws;
        for (i = 0; i < 128; i++) s_gf_regs[i] = *(volatile uint32_t*)(0xFD400700u + (uint32_t)i * 4);
    }
    s_gf_last[0] = kind;
    s_gf_last[1] = a;
    s_gf_last[2] = b;
    s_gf_last[3] = c;
    s_gf_last[4] = d;
    s_gf_count++;
    if (kind == 3 && ++s_gf_storms >= 16) ocx_pb_irq_off = 1;
}


/* passive level only: logs, and reads the pushbuffer around the fault */
static void log_first_fault(void) {
    const uint32_t* w;
    int i;
    if (!s_gf_count || s_gf_first_logged) return;
    s_gf_first_logged = 1;
    xbox_logf("[NV2A] first GPU fault: kind %u %08x %08x %08x %08x, get %08x put %08x (pushbuffer %08x), "
              "frame %u draw %u\n",
              (unsigned)s_gf_first[0], (unsigned)s_gf_first[1], (unsigned)s_gf_first[2], (unsigned)s_gf_first[3],
              (unsigned)s_gf_first[4], (unsigned)s_gf_first[5], (unsigned)s_gf_first[6],
              (unsigned)((uint32_t)s_pb_base & 0x03FFFFFFu), (unsigned)s_gf_first[7], (unsigned)s_gf_first[8]);
    /* what the pusher had just read: 64 words before GET, 32 from it. The
     * pushbuffer is contiguous memory, mapped at 0x80000000 | physical. */
    w = (const uint32_t*)(0x80000000u | (s_gf_first[5] & 0x03FFFFFCu));
    if (MmIsAddressValid((PVOID)(w - 64)) && MmIsAddressValid((PVOID)(w + 31)))
        for (i = -64; i < 32; i += 8)
            xbox_logf("[NV2A]  first fault get%+d: %08x %08x %08x %08x %08x %08x %08x %08x\n", i * 4, w[i], w[i + 1],
                      w[i + 2], w[i + 3], w[i + 4], w[i + 5], w[i + 6], w[i + 7]);
    for (i = 0; i < 128; i += 8)
        xbox_logf("[NV2A]  first fault pgraph %06x: %08x %08x %08x %08x %08x %08x %08x %08x\n", 0x400700 + i * 4,
                  (unsigned)s_gf_regs[i], (unsigned)s_gf_regs[i + 1], (unsigned)s_gf_regs[i + 2],
                  (unsigned)s_gf_regs[i + 3], (unsigned)s_gf_regs[i + 4], (unsigned)s_gf_regs[i + 5],
                  (unsigned)s_gf_regs[i + 6], (unsigned)s_gf_regs[i + 7]);
}

static void gpu_fault_log(uint32_t frame) {
    uint32_t n = s_gf_count;
    s_gf_storms = 0;
    if (n == s_gf_logged) return;
    log_first_fault();
    if (s_gf_logged < 32 || (n >> 8) != (s_gf_logged >> 8))
        xbox_logf("[NV2A] GPU fault x%u (frame %u): kind %u %08x %08x %08x %08x%s\n", n, frame,
                  (unsigned)s_gf_last[0], (unsigned)s_gf_last[1], (unsigned)s_gf_last[2], (unsigned)s_gf_last[3],
                  (unsigned)s_gf_last[4], ocx_pb_irq_off ? " (GPU interrupt masked)" : "");
    s_gf_logged = n;
}

/* ======================================================================
 * Swizzle (Morton order for power-of-two images)
 * ====================================================================== */
static uint32_t swz_x[1024], swz_y[1024];
static int swz_w, swz_h;

static void swz_tables(int w, int h) {
    uint32_t xm = 0, ym = 0, bit = 1, mbit = 1;
    int done, i;
    if (w == swz_w && h == swz_h) return;
    do {
        done = 1;
        if (bit < (uint32_t)w) { xm |= mbit; mbit <<= 1; done = 0; }
        if (bit < (uint32_t)h) { ym |= mbit; mbit <<= 1; done = 0; }
        bit <<= 1;
    } while (!done);
    for (i = 0; i < w; i++) {
        uint32_t v = 0, m = 1, src = (uint32_t)i;
        uint32_t mask = xm;
        while (mask) { uint32_t low = mask & -mask; if (src & m) v |= low; m <<= 1; mask &= mask - 1; }
        swz_x[i] = v;
    }
    for (i = 0; i < h; i++) {
        uint32_t v = 0, m = 1, src = (uint32_t)i;
        uint32_t mask = ym;
        while (mask) { uint32_t low = mask & -mask; if (src & m) v |= low; m <<= 1; mask &= mask - 1; }
        swz_y[i] = v;
    }
    swz_w = w;
    swz_h = h;
}

static int pot(int v) { int p = 1; while (p < v) p <<= 1; return p; }

/* ======================================================================
 * GL entry points
 * ====================================================================== */
static int gl_noop(void) { return 0; }

static void gl_gen_objs(GLsizei n, GLuint* ids) { GLsizei i; for (i = 0; i < n; i++) ids[i] = s_next_obj++; }
static GLuint gl_create(void) { return ++s_next_obj; }

static void gl_getiv_status(GLuint o, GLenum pname, GLint* p) {
    (void)o;
    if (p) *p = (pname == GL_COMPILE_STATUS || pname == GL_LINK_STATUS) ? 1 : 0;
}
static void gl_get_integerv(GLenum pname, GLint* p) { (void)pname; if (p) *p = 0; }
static const GLubyte* gl_get_string(GLenum name) {
    return (const GLubyte*)(name == GL_VERSION ? "3.3 OpenCrossing-Xbox NV2A" : "OpenCrossing-Xbox NV2A");
}
static GLenum gl_fb_status(GLenum t) { (void)t; return GL_FRAMEBUFFER_COMPLETE; }

static void gl_use_program(GLuint p) { s_program = p; }

static void gl_gen_textures(GLsizei n, GLuint* ids) {
    GLsizei i;
    for (i = 0; i < n; i++) {
        int k, id = 0;
        for (k = 0; k < MAX_TEX - 1; k++) {
            int c = s_tex_next + k;
            if (c >= MAX_TEX) c = 1 + (c % (MAX_TEX - 1));
            if (!s_tex[c].used) { id = c; break; }
        }
        if (!id) { ids[i] = 0; continue; }
        memset(&s_tex[id], 0, sizeof s_tex[id]);
        s_tex[id].used = 1;
        s_tex[id].wrap_s = s_tex[id].wrap_t = GL_REPEAT;
        s_tex[id].min_f = s_tex[id].mag_f = GL_LINEAR;
        s_tex_next = id + 1;
        ids[i] = (GLuint)id;
    }
}

static void wait_idle(void);
static int s_frame_open;
static void frame_open(void);
/* The GPU may still read a texture this frame: free after the flip. If the
 * list is full, drain the GPU and free everything now rather than leak. */
static void release_deferred(void);
static void defer_free(void* p) {
    if (s_ndeferred >= (int)(sizeof s_deferred_free / sizeof s_deferred_free[0])) {
        wait_idle();
        release_deferred();
    }
    s_deferred_free[s_ndeferred++] = p;
}

static void gl_delete_textures(GLsizei n, const GLuint* ids) {
    GLsizei i;
    for (i = 0; i < n; i++) {
        GLuint id = ids[i];
        int u;
        if (!id || id >= MAX_TEX || !s_tex[id].used) continue;
        /* the GPU may still read it this frame: free after the flip */
        if (s_tex[id].mem) defer_free(s_tex[id].mem);
        s_tex[id].used = 0;
        s_tex[id].mem = NULL;
        for (u = 0; u < 8; u++) if (s_bound[u] == id) s_bound[u] = 0;
        s_tex_epoch++;
    }
}

static void gl_active_texture(GLenum t) { s_active_unit = (int)(t - GL_TEXTURE0) & 7; }
static void gl_bind_texture(GLenum target, GLuint id) {
    GLuint v = id < MAX_TEX ? id : 0;
    (void)target;
    if (s_bound[s_active_unit] != v) s_tex_epoch++;
    s_bound[s_active_unit] = v;
}

static void gl_tex_parameteri(GLenum target, GLenum pname, GLint v) {
    XTex* t;
    GLuint id = s_bound[s_active_unit];
    (void)target;
    if (!id || !s_tex[id].used) return;
    t = &s_tex[id];
    switch (pname) {
        case GL_TEXTURE_WRAP_S: t->wrap_s = v; break;
        case GL_TEXTURE_WRAP_T: t->wrap_t = v; break;
        case GL_TEXTURE_MIN_FILTER: t->min_f = v; break;
        case GL_TEXTURE_MAG_FILTER: t->mag_f = v; break;
    }
    s_tex_epoch++;
}

static uint32_t s_tex_fail, s_tex_recover;

static void release_deferred(void) {
    int i;
    for (i = 0; i < s_ndeferred; i++) pool_free(s_deferred_free[i]);
    s_ndeferred = 0;
}

/* A texture that doesn't fit is drawn untextured (white). Before that:
 * 1. drain the GPU and release this frame's deferred frees (a menu's new
 *    screen grab arrives in the same frame the old one is deleted);
 * 2. drop the whole texture cache (pc_gx_texture.c re-decodes on next use:
 *    one slow frame instead of a white one). */
extern void pc_gx_texture_shutdown(void);   /* drops the cache and pc_gx's bindings */
extern void pc_gx_texture_bind_cache_invalidate(void);
extern void pc_gx_efb_capture_cleanup(void);
static void* tex_alloc(uint32_t bytes, int w, int h) {
    void* p = pool_alloc(bytes);
    if (p) return p;
    wait_idle();
    release_deferred();
    p = pool_alloc(bytes);
    if (p) return p;
    s_tex_recover++;
    xbox_logf("[NV2A] texture pool full for %dx%d (%u KB used): dropping the texture cache\n", w, h,
              s_pool_used / 1024);
    pc_gx_texture_shutdown();
    pc_gx_texture_bind_cache_invalidate();
#if XBOX_WIDESCREEN
    /* pc_gx.c (PC_ENHANCEMENTS) keeps up to 4 full-res EFB copies as GL
     * textures, 2 MB each for a screen grab; the cache drop doesn't free
     * them. The game re-copies on its next GXCopyTex. */
    pc_gx_efb_capture_cleanup();
#endif
    release_deferred();
    return pool_alloc(bytes);
}

static void tex_image_2d(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                         GLenum fmt, GLenum type, const void* data);

static unsigned long long s_wait_ticks;   /* every wait_idle, for the [FRAME] line */

/* upload time without the GPU waits inside it (frame_open, tex_alloc's
 * recovery): those count as gpu wait */
static void gl_tex_image_2d(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                            GLenum fmt, GLenum type, const void* data) {
    unsigned long long t0 = xbox_ticks(), w0 = s_wait_ticks;
    tex_image_2d(target, level, ifmt, w, h, border, fmt, type, data);
    g_xfs.tex_ticks += xbox_ticks() - t0 - (s_wait_ticks - w0);
    g_xfs.tex_n++;
}

/* Native texture formats (Melee-X keeps GX textures in formats the NV2A
 * samples as they are; here only the RGBA8 that pc_gx_texture.c decodes
 * arrives, so each upload is classified instead). Every texel of the image
 * is checked, and the smallest format that holds all of them is used:
 *   grey, opaque              -> Y8        (1 byte)
 *   grey, alpha = grey        -> AY8       (1 byte: I4/I8)
 *   grey                      -> A8Y8      (2 bytes: IA4/IA8)
 *   opaque, 5/6/5-bit colour  -> R5G6B5    (2 bytes)
 *   alpha 0/255, 5-bit colour -> A1R5G5B5  (2 bytes: most RGB5A3/CI)
 *   all four 4-bit            -> A4R4G4B4  (2 bytes)
 *   anything else             -> A8R8G8B8  (4 bytes)
 * Grey and alpha are kept exactly. A 5- or 6-bit colour channel is accepted
 * when it is what pc_gx_texture.c's x * 255 / 31 (or a framebuffer read
 * back's bit replication) gives for some x; the NV2A expands x by bit
 * replication, as the GameCube does, so such a texel can come out 1/255
 * from the decoder's value. The NES screen (RGB565, red in the low bits)
 * goes to R5G6B5 as it is. Kill switch: -DXBOX_NATIVE_TEX=0, or
 * native_textures = 0 in settings.ini (everything A8R8G8B8, as before). */
#ifndef XBOX_NATIVE_TEX
#define XBOX_NATIVE_TEX 1
#endif
static int s_native_tex = XBOX_NATIVE_TEX;
#define FMT_Y8 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8
#define FMT_AY8 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8
#define FMT_A8Y8 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8
#define FMT_R5G6B5 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5
#define FMT_A1R5G5B5 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5
#define FMT_A4R4G4B4 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4
#define FMT_A8R8G8B8 NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8

/* q5[v]: the 5-bit x whose x * 255 / 31 or bit replication is v, else 0xFF;
 * q6 likewise for 6 bits. 4 bits: x * 255 / 15 and replication are both 17x. */
static uint8_t s_q5[256], s_q6[256];
static void quant_tables(void) {
    int x;
    memset(s_q5, 0xFF, sizeof s_q5);
    memset(s_q6, 0xFF, sizeof s_q6);
    for (x = 0; x < 32; x++) {
        s_q5[x * 255 / 31] = (uint8_t)x;
        s_q5[(x << 3) | (x >> 2)] = (uint8_t)x;
    }
    for (x = 0; x < 64; x++) {
        s_q6[x * 255 / 63] = (uint8_t)x;
        s_q6[(x << 2) | (x >> 4)] = (uint8_t)x;
    }
}

/* since boot: uploads by stored size (1, 2, 4 bytes a texel) and the bytes
 * the smaller formats saved over A8R8G8B8, for perf.log's minute line */
static uint32_t s_tex_by_bpp[5];
static unsigned long long s_tex_saved_bytes;   /* totals since boot */

/* src: w x h texels, `bpp` bytes each (4 RGBA8, 3 RGB8) */
static uint32_t classify(const uint8_t* src, int w, int h, int bpp) {
    enum { GREY = 1, AY = 2, OPAQUE = 4, A1 = 8, C5 = 16, G6 = 32, C4 = 64 };
    uint32_t ok = GREY | AY | OPAQUE | A1 | C5 | G6 | C4;
    int i, n = w * h;
    for (i = 0; i < n && ok; i++, src += bpp) {
        uint32_t r = src[0], g = src[1], b = src[2], a = bpp == 4 ? src[3] : 255;
        if (r != g || g != b) ok &= ~(uint32_t)(GREY | AY);
        if (a != r) ok &= ~(uint32_t)AY;
        if (a != 255) ok &= ~(uint32_t)OPAQUE;
        if (a != 255 && a != 0) ok &= ~(uint32_t)A1;
        if ((s_q5[r] | s_q5[b]) == 0xFF) ok &= ~(uint32_t)(C5 | G6);
        if (s_q5[g] == 0xFF) ok &= ~(uint32_t)C5;
        if (s_q6[g] == 0xFF) ok &= ~(uint32_t)G6;
        if (r % 17 || g % 17 || b % 17 || a % 17) ok &= ~(uint32_t)C4;
    }
    if ((ok & (GREY | OPAQUE)) == (GREY | OPAQUE)) return FMT_Y8;
    if ((ok & (GREY | AY)) == (GREY | AY)) return FMT_AY8;
    if (ok & GREY) return FMT_A8Y8;
    if ((ok & (OPAQUE | G6)) == (OPAQUE | G6)) return FMT_R5G6B5;
    if ((ok & (A1 | C5)) == (A1 | C5)) return FMT_A1R5G5B5;
    if (ok & C4) return FMT_A4R4G4B4;
    return FMT_A8R8G8B8;
}

static uint32_t fmt_bpp(uint32_t f) {
    switch (f) {
        case FMT_Y8: case FMT_AY8: return 1;
        case FMT_A8R8G8B8: return 4;
        default: return 2;
    }
}

/* one texel in format f from RGBA8 bytes */
static inline uint32_t texel(uint32_t f, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    switch (f) {
        case FMT_Y8: case FMT_AY8: return r;
        case FMT_A8Y8: return (a << 8) | r;
        case FMT_R5G6B5: return ((uint32_t)s_q5[r] << 11) | ((uint32_t)s_q6[g] << 5) | s_q5[b];
        case FMT_A1R5G5B5:
            return (a ? 0x8000u : 0) | ((uint32_t)s_q5[r] << 10) | ((uint32_t)s_q5[g] << 5) | s_q5[b];
        case FMT_A4R4G4B4: return ((a / 17) << 12) | ((r / 17) << 8) | ((g / 17) << 4) | (b / 17);
        default: return (a << 24) | (r << 16) | (g << 8) | b;
    }
}

/* A re-upload of the same size and format rewrites the texture where it is
 * (the NES screen every frame, EFB captures): no pool churn, no deferred
 * free. Only when the GPU can't be reading it: frame_open has drained last
 * frame's work, and this frame hasn't drawn it yet. Kill switch:
 * -DXBOX_TEX_REUSE=0, or texture_reuse = 0 in settings.ini. */
#ifndef XBOX_TEX_REUSE
#define XBOX_TEX_REUSE 1
#endif
static int s_tex_reuse = XBOX_TEX_REUSE;
static uint32_t s_tex_reused, s_tex_reused_total;   /* this [FRAME] window, since boot */

static void tex_image_2d(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                         GLenum fmt, GLenum type, const void* data) {
    GLuint id = s_bound[s_active_unit];
    XTex* t;
    int pw, ph, x, y, bpp;
    uint32_t nvfmt, tbpp;
    uint8_t* dst;
    const uint8_t* src = (const uint8_t*)data;
    (void)target; (void)ifmt; (void)border;
    /* pc_gx_texture.c uploads RGBA8; the NES screen (pc_nes_fixnes.c) is
     * RGB565 with red in the low bits. Anything else is uploaded white rather
     * than read with the wrong pixel size. */
    if (fmt == GL_RGBA && type == GL_UNSIGNED_BYTE) bpp = 4;
    else if (fmt == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5_REV) bpp = 2;
    else if (fmt == GL_RGB && type == GL_UNSIGNED_BYTE) bpp = 3;
    else { bpp = 4; src = NULL; }
    if (level != 0 || !id || !s_tex[id].used || w <= 0 || h <= 0 || w > 1024 || h > 1024) return;
    t = &s_tex[id];
    s_tex_epoch++;
    if (!s_native_tex || !src) nvfmt = FMT_A8R8G8B8;
    else if (bpp == 2) nvfmt = FMT_R5G6B5;
    else nvfmt = classify(src, w, h, bpp);
    tbpp = fmt_bpp(nvfmt);
    pw = pot(w);
    ph = pot(h);
    if (t->mem && s_tex_reuse && t->w == w && t->h == h && t->nvfmt == nvfmt) {
        frame_open();   /* with the GPU overlap, this drains last frame's draws */
        if (t->drawn == s_frame + 1) {   /* queued in this frame: can't touch it */
            defer_free(t->mem);
            t->mem = NULL;
        } else {
            s_tex_reused++;
            s_tex_reused_total++;
        }
    } else if (t->mem) {
        defer_free(t->mem);
        t->mem = NULL;
    }
    if (!t->mem) {
        t->mem = tex_alloc((uint32_t)(pw * ph) * tbpp, w, h);
        if (!t->mem) {
            if ((s_tex_fail++ & 255) == 0) xbox_logf("[NV2A] texture pool full (%u used), %dx%d dropped\n", s_pool_used, w, h);
            return;
        }
        s_tex_by_bpp[tbpp]++;
        s_tex_saved_bytes += (uint32_t)(pw * ph) * (4 - tbpp);
    }
    dst = (uint8_t*)t->mem;
    t->w = w; t->h = h; t->pw = pw; t->ph = ph;
    t->nvfmt = nvfmt;
    t->bpp = tbpp;
    t->drawn = 0;
    swz_tables(pw, ph);
    /* The NES screen, every frame: 256 wide, so no column padding, and x and
     * x + 1 (x even) are neighbours in the swizzled layout. Two texels a
     * word, red and blue swapped in place: 3 ms of a 19 ms NES frame on the
     * console in the per-texel loop below. */
    if (src && bpp == 2 && nvfmt == FMT_R5G6B5 && w == pw && !(w & 1) && !((uintptr_t)src & 3)) {
        uint32_t* d32 = (uint32_t*)dst;
        for (y = 0; y < ph; y++) {
            const uint32_t* row = (const uint32_t*)(src + (size_t)(y < h ? y : h - 1) * (size_t)w * 2);
            uint32_t yo = swz_y[y];
            for (x = 0; x < pw; x += 2) {
                uint32_t c = row[x >> 1];
                d32[(yo | swz_x[x]) >> 1] = (c & 0x07E007E0u) | ((c & 0x001F001Fu) << 11) | ((c >> 11) & 0x001F001Fu);
            }
        }
        return;
    }
    for (y = 0; y < ph; y++) {
        int sy = y < h ? y : h - 1;   /* pad by edge replication */
        uint32_t yo = swz_y[y];
        const uint8_t* row = src ? src + (size_t)sy * (size_t)w * (size_t)bpp : NULL;
        for (x = 0; x < pw; x++) {
            int sx = x < w ? x : w - 1;
            uint32_t v, k = yo | swz_x[x];
            if (row && bpp == 4) {
                const uint8_t* p = row + sx * 4;
                v = texel(nvfmt, p[0], p[1], p[2], p[3]);
            } else if (row && bpp == 2) {
                uint32_t c = (uint32_t)row[sx * 2] | ((uint32_t)row[sx * 2 + 1] << 8);
                uint32_t r = c & 31, g = (c >> 5) & 63, b = c >> 11;
                if (nvfmt == FMT_R5G6B5) v = (r << 11) | (g << 5) | b;
                else v = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
            } else if (row) {
                const uint8_t* p = row + sx * 3;
                v = texel(nvfmt, p[0], p[1], p[2], 255);
            } else {
                v = 0xFFFFFFFFu;
            }
            if (tbpp == 4) ((uint32_t*)dst)[k] = v;
            else if (tbpp == 2) ((uint16_t*)dst)[k] = (uint16_t)v;
            else dst[k] = (uint8_t)v;
        }
    }
}

static void gl_buffer_data(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)size; (void)usage;
    s_n_bd++;
    if (target == GL_ARRAY_BUFFER) s_array_data = data;
}

static void gl_enable_cap(GLenum cap, int on) {
    switch (cap) {
        case GL_DEPTH_TEST: G.depth_test = on; break;
        case GL_BLEND: G.blend = on; break;
        case GL_CULL_FACE: G.cull = on; break;
        case GL_SCISSOR_TEST: G.scissor = on; break;
    }
}
static void gl_enable(GLenum c) { gl_enable_cap(c, 1); }
static void gl_disable(GLenum c) { gl_enable_cap(c, 0); }
static void gl_depth_func(GLenum f) { G.depth_func = (int)f; }
static void gl_depth_mask(GLboolean m) { G.depth_mask = m ? 1 : 0; }
static void gl_blend_func(GLenum s, GLenum d) { G.sfac = (int)s; G.dfac = (int)d; }
static void gl_blend_equation(GLenum e) { G.beq = (int)e; }
static void gl_cull_face(GLenum f) { G.cull_face = (int)f; }
static void gl_color_mask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    G.cmask = (r ? NV097_SET_COLOR_MASK_RED_WRITE_ENABLE : 0) | (g ? NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE : 0) |
              (b ? NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE : 0) | (a ? NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE : 0);
}
/* v * num / den rounded to nearest, also for negative v (viewports that start
 * off-screen): C division truncates toward zero */
static int scale_round(int v, int num, int den) {
    long long n = (long long)v * num * 2 + den, d = (long long)den * 2;
    return (int)(n >= 0 ? n / d : -((-n + d - 1) / d));
}

/* logical (pc_gx) -> framebuffer pixels; edges are rounded so abutting
 * rectangles stay abutting */
static void to_fb(GLint x, GLint y, GLsizei w, GLsizei h, int* ox, int* oy, int* ow, int* oh) {
    extern int g_pc_window_w, g_pc_window_h;
    int lw = g_pc_window_w > 0 ? g_pc_window_w : 640, lh = g_pc_window_h > 0 ? g_pc_window_h : 480;
    int x0, y0, x1, y1;
    if (lw == SCR_W && lh == SCR_H) {
        *ox = x; *oy = y; *ow = w; *oh = h;
        return;
    }
    x0 = scale_round(x, SCR_W, lw);
    x1 = scale_round(x + w, SCR_W, lw);
    y0 = scale_round(y, SCR_H, lh);
    y1 = scale_round(y + h, SCR_H, lh);
    *ox = x0; *oy = y0; *ow = x1 - x0; *oh = y1 - y0;
}
static void gl_scissor(GLint x, GLint y, GLsizei w, GLsizei h) { to_fb(x, y, w, h, &G.sx, &G.sy, &G.sw, &G.sh); }
static void gl_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    int vx = G.vx, vy = G.vy, vw = G.vw, vh = G.vh;
    to_fb(x, y, w, h, &G.vx, &G.vy, &G.vw, &G.vh);
    if (vx != G.vx || vy != G.vy || vw != G.vw || vh != G.vh) s_udirty |= D_PROJ;
}
static void gl_depth_range(GLdouble n, GLdouble f) {
    if (G.dn != (float)n || G.df != (float)f) s_udirty |= D_PROJ;
    G.dn = (float)n;
    G.df = (float)f;
}
static void gl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    G.clear_c[0] = r; G.clear_c[1] = g; G.clear_c[2] = b; G.clear_c[3] = a;
}
static void gl_clear_depth(GLdouble d) { G.clear_d = (float)d; }


static void clear_rect(int* x, int* y, int* w, int* h) {
    if (G.scissor) {
        *x = G.sx; *w = G.sw; *h = G.sh;
        *y = SCR_H - (G.sy + G.sh);
    } else {
        *x = 0; *y = 0; *w = SCR_W; *h = SCR_H;
    }
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > SCR_W) *w = SCR_W - *x;
    if (*y + *h > SCR_H) *h = SCR_H - *y;
}

static uint8_t f2b(float f) { return (uint8_t)(f <= 0.0f ? 0 : f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f)); }

static void gl_clear(GLbitfield mask) {
    int x, y, w, h;
    frame_open();
    pb_budget();
    s_n_clr++;
    s_n_clr_frame++;
    clear_rect(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    pb_close();   /* pb_fill / pb_erase push their own blocks */
    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t c = ((uint32_t)f2b(G.clear_c[3]) << 24) | ((uint32_t)f2b(G.clear_c[0]) << 16) |
                     ((uint32_t)f2b(G.clear_c[1]) << 8) | f2b(G.clear_c[2]);
        /* A8R8G8B8: pb_fill converts it to the surface's format itself (at
         * 720p R5G6B5). Converting here as well made every 16-bit clear
         * colour near black (Melee-X found it). */
        pb_fill(x, y, w, h, c);
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        if (pb_DepthFmt == NV097_SET_SURFACE_FORMAT_ZETA_Z16) {
            /* pb_erase_depth_stencil_buffer's 0xffffff00 is Z24S8; in Z16 its
             * low half (0xff00) would clear to 0.996, not the far plane */
            uint32_t* p = pb_begin();
            p = pb_push1(p, NV097_SET_CLEAR_RECT_HORIZONTAL, (uint32_t)((x + w - 1) << 16) | (uint32_t)x);
            p = pb_push1(p, NV097_SET_CLEAR_RECT_VERTICAL, (uint32_t)((y + h - 1) << 16) | (uint32_t)y);
            p = pb_push1(p, NV097_SET_ZSTENCIL_CLEAR_VALUE, 0xFFFFFFFFu);
            p = pb_push1(p, NV097_CLEAR_SURFACE, NV097_CLEAR_SURFACE_Z);
            pb_end(p);
        } else {
            pb_erase_depth_stencil_buffer(x, y, w, h);
        }
    }
}

/* pb_busy only compares the pusher's GET with PUT and reads PGRAPH's status:
 * methods already fetched into PFIFO's CACHE1 but not yet handed to PGRAPH
 * pass as idle whenever PGRAPH is between two of them. The callers free or
 * rewrite memory the GPU reads right after this (deferred textures, the
 * vertex ring at each frame and mid-frame, texture memory rewritten in
 * place), so idle also means CACHE1 empty and the pusher stopped, seen twice
 * (Melee-X, which relies on it for the same things). A GPU that stops
 * fetching would otherwise hang here with nothing in the log: after 2 s,
 * report once where the FIFO stopped. Kill switch: -DXBOX_STRICT_IDLE=0, or
 * strict_gpu_wait = 0 in settings.ini (pb_busy alone, as before). */
#ifndef XBOX_STRICT_IDLE
#define XBOX_STRICT_IDLE 1
#endif
static int s_strict_idle = XBOX_STRICT_IDLE;

static int gpu_quiet(void) {
    volatile const uint32_t* r = (volatile const uint32_t*)0xFD000000u;
    return !pb_busy() && (r[0x3214 / 4] & 0x10) && !(r[0x3220 / 4] & 0x10) && !r[0x400700 / 4];
}

static int gpu_busy(void) {
    if (!s_strict_idle) return pb_busy();
    return !gpu_quiet() || !gpu_quiet();
}

static void report_gpu_stall(void) {
    volatile const uint32_t* g = (volatile const uint32_t*)0xFD400000u;
    uint32_t get = *(volatile uint32_t*)(0xFD000000u + 0x3244), put = *(volatile uint32_t*)(0xFD000000u + 0x3240);
    const uint32_t* w = (const uint32_t*)(0x80000000u | (get & 0x03FFFFFCu));
    xbox_logf("[NV2A] GPU stalled: get %08x put %08x dma_state %08x pgraph %08x, faults %u (last kind %u %08x %08x "
              "%08x %08x)\n",
              get, put, *(volatile uint32_t*)(0xFD000000u + 0x3228), g[0x700 / 4], (unsigned)s_gf_count,
              (unsigned)s_gf_last[0], (unsigned)s_gf_last[1], (unsigned)s_gf_last[2], (unsigned)s_gf_last[3],
              (unsigned)s_gf_last[4]);
    log_first_fault();
    /* GET can be anywhere once the pusher has run off into other data */
    if (MmIsAddressValid((PVOID)(w - 8)) && MmIsAddressValid((PVOID)(w + 7))) {
        xbox_logf("[NV2A]  at get-32: %08x %08x %08x %08x %08x %08x %08x %08x\n", w[-8], w[-7], w[-6], w[-5], w[-4],
                  w[-3], w[-2], w[-1]);
        xbox_logf("[NV2A]  at get:    %08x %08x %08x %08x %08x %08x %08x %08x\n", w[0], w[1], w[2], w[3], w[4], w[5],
                  w[6], w[7]);
    } else {
        xbox_logf("[NV2A]  get is outside mapped memory (pushbuffer at %08x)\n",
                  (unsigned)((uint32_t)s_pb_base & 0x03FFFFFFu));
    }
    xbox_logf("[NV2A]  pgraph intr %08x nsource %08x trapped %08x data %08x surface %08x | clear %08x %08x "
              "window %08x %08x | frame %u draws %u\n",
              g[0x100 / 4], g[0x108 / 4], g[0x704 / 4], g[0x708 / 4], g[0x710 / 4], g[0x1864 / 4], g[0x1868 / 4],
              g[0x1A44 / 4], g[0x1A64 / 4], s_frame, s_draws);
}

static void wait_idle(void) {
    unsigned long long t0 = xbox_ticks();
    int reported = 0;
    pb_close();
    while (gpu_busy()) {
        if (!reported && xbox_ticks() - t0 > 2 * xbox_ticks_per_sec()) {
            report_gpu_stall();
            reported = 1;
        }
    }
    s_wait_ticks += xbox_ticks() - t0;
}

/* x, y, w, h are logical (pc_gx) pixels; each samples the framebuffer pixel
 * under its centre, so a 720p or 16:9 screen reads back at logical size */
static void gl_read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* out) {
    extern int g_pc_window_w, g_pc_window_h;
    int lw = g_pc_window_w > 0 ? g_pc_window_w : 640, lh = g_pc_window_h > 0 ? g_pc_window_h : 480;
    const uint8_t* fb;
    uint32_t pitch;
    int r, c;
    uint8_t* o = (uint8_t*)out;
    (void)fmt; (void)type;
    frame_open();
    wait_idle();
    fb = (const uint8_t*)pb_back_buffer();
    pitch = pb_back_buffer_pitch();
    /* GL: origin bottom-left, rows bottom-up */
    for (r = 0; r < h; r++) {
        int ly = y + r, sy = SCR_H - 1 - (int)(((long long)ly * 2 + 1) * SCR_H / (2 * lh));
        for (c = 0; c < w; c++) {
            int lx = x + c, sx = (int)(((long long)lx * 2 + 1) * SCR_W / (2 * lw));
            uint8_t* d = o + ((size_t)r * w + c) * 4;
            if (lx < 0 || lx >= lw || ly < 0 || ly >= lh || sx >= SCR_W || sy < 0) {
                d[0] = d[1] = d[2] = 0; d[3] = 255;
                continue;
            }
            if (s_fb_bpp == 16) {
                uint32_t v = *(const uint16_t*)(fb + (size_t)sy * pitch + (size_t)sx * 2);
                uint32_t r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
                d[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
                d[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                d[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
                d[3] = 255;
            } else {
                const uint8_t* p = fb + (size_t)sy * pitch + (size_t)sx * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3];
            }
        }
    }
}

/* ======================================================================
 * Draw
 * ====================================================================== */
static uint32_t s_shadow_vc[64 * 4];      /* last uploaded vertex constants 96.. */
static int s_vc_valid;
static XRcProg s_rc_cur;
static int s_rc_valid;
static uint32_t s_rc_consts[XRC_MAX_STAGES][2], s_rc_fconsts[2];

typedef struct { uint32_t hash; XTevCfg cfg; XRcProg prog; } RcEntry;
#define RC_CACHE 256
static RcEntry s_rc_cache[RC_CACHE];
static int s_rc_count, s_rc_last = -1;

static uint32_t cfg_hash(const XTevCfg* c) {
    const uint32_t* w = (const uint32_t*)c;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof *c / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

static float kfrac(int sel) { return (float)(8 - sel) / 8.0f; }

static void konst_c(int sel, float out[3]) {
    int ki, ch;
    if (sel <= 7) { out[0] = out[1] = out[2] = kfrac(sel); return; }
    if (sel <= 11) { out[0] = out[1] = out[2] = 0; return; }
    if (sel <= 15) { out[0] = UF(U_KCOLOR, sel - 12, 0); out[1] = UF(U_KCOLOR, sel - 12, 1); out[2] = UF(U_KCOLOR, sel - 12, 2); return; }
    ki = (sel - 16) & 3;
    ch = ((sel - 16) >> 2) & 3;
    out[0] = out[1] = out[2] = UF(U_KCOLOR, ki, ch);
}

static float konst_a(int sel) {
    if (sel <= 7) return kfrac(sel);
    if (sel <= 15) return 0.0f;
    return UF(U_KCOLOR, (sel - 16) & 3, ((sel - 16) >> 2) & 3);
}

static void ref_val(uint16_t ref, float* rgb, float* a) {
    int t = ref >> 8, p = ref & 0xFF;
    static const int regu[4] = { U_PREV, U_REG0, U_REG1, U_REG2 };
    switch (t) {
        case XREF_TEVREG_RGB: rgb[0] = UF(regu[p & 3], 0, 0); rgb[1] = UF(regu[p & 3], 0, 1); rgb[2] = UF(regu[p & 3], 0, 2); break;
        case XREF_TEVREG_A: *a = UF(regu[p & 3], 0, 3); break;
        case XREF_KONST_C: konst_c(p, rgb); break;
        case XREF_KONST_A: *a = konst_a(p); break;
        case XREF_FOG_RGB: rgb[0] = UF(U_FOGCOL, 0, 0); rgb[1] = UF(U_FOGCOL, 0, 1); rgb[2] = UF(U_FOGCOL, 0, 2); break;
    }
}

static uint32_t pack_const(uint16_t rgb_ref, uint16_t a_ref) {
    float rgb[3] = {0, 0, 0}, a = 0;
    if (rgb_ref) ref_val(rgb_ref, rgb, &a);
    if (a_ref) ref_val(a_ref, rgb, &a);
    if (a_ref && !rgb_ref) rgb[0] = rgb[1] = rgb[2] = 0;
    if (rgb_ref && !a_ref) a = 0;
    /* a request for the alpha slot might carry its value in rgb when it is a
     * broadcast of an rgb constant; ref_val writes the right field per type */
    return ((uint32_t)f2b(a) << 24) | ((uint32_t)f2b(rgb[0]) << 16) | ((uint32_t)f2b(rgb[1]) << 8) | f2b(rgb[2]);
}

/* n bytes (a multiple of 4) equal: the shim's per-draw compares, inline.
 * memcmp stays a call into xbox_mem.c even as __builtin_memcmp (the
 * prelude's macro): under -ffreestanding clang doesn't expand it. These
 * callers made it ~6% of a busy 720p town frame on the console. */
static inline int words_eq(const void* a, const void* b, size_t n) {
    const uint32_t* x = (const uint32_t*)a;
    const uint32_t* y = (const uint32_t*)b;
    uint32_t d = 0;
    size_t i;
    for (i = 0; i < n / 4; i++) d |= x[i] ^ y[i];
    return d == 0;
}
_Static_assert(sizeof(XTevCfg) % 4 == 0 && sizeof(XRcProg) % 4 == 0, "words_eq compares whole words");

/* consecutive draws usually share a TEV config: check the last hit first,
 * then the hashes (a full compare only on a hash match) */
static const XRcProg* rc_lookup(const XTevCfg* cfg) {
    int k;
    uint32_t h = cfg_hash(cfg);
    if (s_rc_last >= 0 && s_rc_cache[s_rc_last].hash == h &&
        words_eq(&s_rc_cache[s_rc_last].cfg, cfg, sizeof *cfg))
        return &s_rc_cache[s_rc_last].prog;
    for (k = 0; k < s_rc_count; k++)
        if (s_rc_cache[k].hash == h && words_eq(&s_rc_cache[k].cfg, cfg, sizeof *cfg)) {
            s_rc_last = k;
            return &s_rc_cache[k].prog;
        }
    k = s_rc_count < RC_CACHE ? s_rc_count++ : (int)(s_draws % RC_CACHE);
    s_rc_last = k;
    s_rc_cache[k].hash = h;
    s_rc_cache[k].cfg = *cfg;
    xbox_tev_compile(cfg, &s_rc_cache[k].prog);
#ifdef XBOX_DBG_RC_TEX
    {   /* debug: every draw outputs its stage-0 texture (or the raster colour) */
        XRcProg* p = &s_rc_cache[k].prog;
        int tex = cfg->st[0].use_tex;
        memset(p, 0, sizeof *p);
        p->nstages = 1;
        p->cicw[0] = tex ? 0x08200000u : 0x04200000u;
        p->aicw[0] = tex ? 0x18301010u : 0x14301010u;
        p->cocw[0] = p->aocw[0] = 0x00000c00u;
        p->cw0 = 0x00000c00u;
        p->cw1 = 0x00001c80u;
    }
#endif
#ifdef XBOX_DBG_TEVLOG
    {
        const XRcProg* p = &s_rc_cache[k].prog;
        int s, i;
        xbox_logf("[TEV] prog %d: %d tev stages fog %d -> %d rc stages%s\n", k, cfg->nstages, cfg->fog_on,
                  p->nstages, p->approximated ? " (approx)" : "");
        for (s = 0; s < cfg->nstages; s++) {
            const XTevStage* t = &cfg->st[s];
            xbox_logf("[TEV]   s%d c(%d %d %d %d)op%d b%d s%d ->%d  a(%d %d %d %d)op%d ->%d  k%d/%d tex%d\n", s,
                      t->cin[0], t->cin[1], t->cin[2], t->cin[3], t->cop, t->cbias, t->cscale, t->cout, t->ain[0],
                      t->ain[1], t->ain[2], t->ain[3], t->aop, t->aout, t->kcsel, t->kasel, t->use_tex);
        }
        for (i = 0; i < p->nstages; i++)
            xbox_logf("[TEV]   rc%d cicw %08x cocw %08x aicw %08x aocw %08x cref %x %x %x %x\n", i, p->cicw[i],
                      p->cocw[i], p->aicw[i], p->aocw[i], p->cref[i][0], p->cref[i][1], p->cref[i][2], p->cref[i][3]);
        xbox_logf("[TEV]   final cw0 %08x cw1 %08x fref %x %x %x %x\n", p->cw0, p->cw1, p->fref[0], p->fref[1],
                  p->fref[2], p->fref[3]);
    }
#endif
    return &s_rc_cache[k].prog;
}

static void build_tev_cfg(XTevCfg* c) {
    int s, n = UI(U_NUMST, 0, 0);
    memset(c, 0, sizeof *c);
    if (n < 1) n = 1;
    if (n > XRC_MAX_TEV) n = XRC_MAX_TEV;
    c->nstages = n;
    for (s = 0; s < n; s++) {
        XTevStage* t = &c->st[s];
        int k;
        for (k = 0; k < 4; k++) { t->cin[k] = UI(U_CIN, s, k); t->ain[k] = UI(U_AIN, s, k); }
        t->cop = UI(U_COP, s, 0) == 1;
        t->aop = UI(U_AOP, s, 0) == 1;
        t->cbias = UI(U_BSC, s, 0); t->cscale = UI(U_BSC, s, 1);
        t->abias = UI(U_BSC, s, 2); t->ascale = UI(U_BSC, s, 3);
        t->cclamp = UI(U_OUT, s, 0); t->aclamp = UI(U_OUT, s, 1);
        t->cout = UI(U_OUT, s, 2) & 3; t->aout = UI(U_OUT, s, 3) & 3;
        t->kcsel = UI(U_KSEL, s, 0); t->kasel = UI(U_KSEL, s, 1);
        t->use_tex = UI(U_USETEX, s, 0) != 0 && s_bound[s] && s_tex[s_bound[s]].used && s_tex[s_bound[s]].mem;
    }
    c->fog_on = UI(U_FOGEN, 0, 0) != 0;
#ifdef XBOX_DBG_NOFOG
    c->fog_on = 0;
#endif
}

/* the program emit_combiners last sent with its constants; with draw_skip
 * the same program and unchanged TEV colours send nothing (NULL: resend) */
static const XRcProg* s_rc_emitted;

static void emit_combiners(const XRcProg* rp, int consts_dirty) {
    int i;
    uint32_t cst[XRC_MAX_STAGES][2], fc[2];
    if (rp == s_rc_emitted && !consts_dirty) return;
    s_rc_emitted = rp;
    for (i = 0; i < rp->nstages; i++) {
        cst[i][0] = pack_const(rp->cref[i][0], rp->cref[i][1]);
        cst[i][1] = pack_const(rp->cref[i][2], rp->cref[i][3]);
    }
    fc[0] = pack_const(rp->fref[0], rp->fref[1]);
    fc[1] = pack_const(rp->fref[2], rp->fref[3]);

    if (!s_rc_valid || !words_eq(&s_rc_cur, rp, sizeof *rp)) {
        put1(NV097_SET_COMBINER_CONTROL,
             (uint32_t)rp->nstages | (1u << 12) | (1u << 16) /* FACTOR0/1 each stage */);
        for (i = 0; i < rp->nstages; i++) {
            put1(NV097_SET_COMBINER_COLOR_ICW + i * 4, rp->cicw[i]);
            put1(NV097_SET_COMBINER_COLOR_OCW + i * 4, rp->cocw[i]);
            put1(NV097_SET_COMBINER_ALPHA_ICW + i * 4, rp->aicw[i]);
            put1(NV097_SET_COMBINER_ALPHA_OCW + i * 4, rp->aocw[i]);
        }
        put1(NV097_SET_COMBINER_SPECULAR_FOG_CW0, rp->cw0);
        put1(NV097_SET_COMBINER_SPECULAR_FOG_CW1, rp->cw1);
        s_rc_cur = *rp;
        s_rc_valid = 1;
        memset(s_rc_consts, 0xA5, sizeof s_rc_consts);
        memset(s_rc_fconsts, 0xA5, sizeof s_rc_fconsts);
    }
    for (i = 0; i < rp->nstages; i++) {
        if (s_rc_consts[i][0] != cst[i][0]) { put1(NV097_SET_COMBINER_FACTOR0 + i * 4, cst[i][0]); s_rc_consts[i][0] = cst[i][0]; }
        if (s_rc_consts[i][1] != cst[i][1]) { put1(NV097_SET_COMBINER_FACTOR1 + i * 4, cst[i][1]); s_rc_consts[i][1] = cst[i][1]; }
    }
    if (s_rc_fconsts[0] != fc[0]) { put1(NV097_SET_SPECULAR_FOG_FACTOR, fc[0]); s_rc_fconsts[0] = fc[0]; }
    if (s_rc_fconsts[1] != fc[1]) { put1(NV097_SET_SPECULAR_FOG_FACTOR + 4, fc[1]); s_rc_fconsts[1] = fc[1]; }
}

static uint32_t s_tex_shadow[4][7];

static uint32_t wrap_mode(int gl) {
    switch (gl) {
        case GL_MIRRORED_REPEAT: return 2;
        case GL_CLAMP_TO_EDGE: return 3;
        default: return 1;
    }
}

static int log2i(int v) { int l = 0; while ((1 << l) < v) l++; return l; }

static void emit_textures(const XTevCfg* c, float scale[3][2]) {
    int s;
    uint32_t prog = 0;
    for (s = 0; s < 4; s++) {
        uint32_t v[7];
        const XTex* t = NULL;
        if (s < c->nstages && c->st[s].use_tex) t = &s_tex[s_bound[s]];
        if (s < 3) { scale[s][0] = 1.0f; scale[s][1] = 1.0f; }
        if (!t) {
            v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 0; v[4] = 0; v[5] = 0; v[6] = 0;
        } else {
            uint32_t filt_min = t->min_f == GL_NEAREST ? 1 : 2, filt_mag = t->mag_f == GL_NEAREST ? 1 : 2;
            v[0] = (uint32_t)t->mem & 0x03FFFFFF;
            v[1] = 1 /* DMA A */ | (1u << 3) /* border from colour: no border texels in the image */ | (2u << 4) /* 2D */ | (t->nvfmt << 8) |
                   (1u << 16) /* 1 mip level */ | ((uint32_t)log2i(t->pw) << 20) | ((uint32_t)log2i(t->ph) << 24);
            v[2] = wrap_mode(t->wrap_s) | (wrap_mode(t->wrap_t) << 8) | (3u << 16);
            v[3] = 0x4003FFC0u;   /* ENABLE | MAX_LOD_CLAMP (the nxdk mesh sample's value) */
            /* swizzled: the pitch isn't used, but PGRAPH checks it. A 4x4
             * AY8 (native_textures) gave 4 and a data error on hardware */
            v[4] = (((uint32_t)t->pw * t->bpp + 63) & ~63u) << 16;
            v[5] = 0x2000u | (filt_min << 16) | (filt_mag << 24);
            v[6] = ((uint32_t)t->pw << 16) | (uint32_t)t->ph;
            scale[s][0] = (float)t->w / (float)t->pw;
            scale[s][1] = (float)t->h / (float)t->ph;
            prog |= 1u << (s * 5);   /* 2D_PROJECTIVE */
        }
        if (!words_eq(v, s_tex_shadow[s], sizeof v)) {
            uint32_t b = (uint32_t)s * 64;
            if (!t) {
                put1(NV097_SET_TEXTURE_CONTROL0 + b, 0);
            } else {
                put1(NV097_SET_TEXTURE_OFFSET + b, v[0]);
                put1(NV097_SET_TEXTURE_FORMAT + b, v[1]);
                put1(NV097_SET_TEXTURE_ADDRESS + b, v[2]);
                put1(NV097_SET_TEXTURE_CONTROL0 + b, v[3]);
                put1(NV097_SET_TEXTURE_CONTROL1 + b, v[4]);
                put1(NV097_SET_TEXTURE_FILTER + b, v[5]);
                put1(NV097_SET_TEXTURE_IMAGE_RECT + b, v[6]);
            }
            memcpy(s_tex_shadow[s], v, sizeof v);
        }
    }
    {
        static uint32_t last_prog = 0xFFFFFFFF;
        if (prog != last_prog) { put1(NV097_SET_SHADER_STAGE_PROGRAM, prog); last_prog = prog; }
    }
}

/* ---- vertex-program constants ---- */
static void mat4_rows_mul(const float* p /*4x4 row-major*/, float out[16]) {
    /* out = Viewport * P, viewport folded so oPos.xyz = (out*eye).xyz / w */
    float sx = G.vw * 0.5f, ox = G.vx + G.vw * 0.5f;
    float sy = -G.vh * 0.5f, oy = (float)SCR_H - G.vy - G.vh * 0.5f;
    float sz = ZMAX * (G.df - G.dn) * 0.5f, oz = ZMAX * ((G.df - G.dn) * 0.5f + G.dn);
    int c;
    for (c = 0; c < 4; c++) {
        out[0 + c] = sx * p[0 + c] + ox * p[12 + c];
        out[4 + c] = sy * p[4 + c] + oy * p[12 + c];
        out[8 + c] = sz * p[8 + c] + oz * p[12 + c];
        out[12 + c] = p[12 + c];
    }
}

/* Builds the rows of the groups in `g` (D_PROJ rows 0-3, D_MV 4-9, D_MAT
 * 10-15, D_LIGHT 16-31, D_TEXGEN 32-40) and leaves the others as they are. */
static void build_vconsts(float vc[41][4], const float scale[3][2], uint32_t g) {
    int i, s;
    if (g & D_PROJ) mat4_rows_mul(&UF(U_PROJ, 0, 0), vc[0]);
    if (g & D_MV) {
        memset(vc[4], 0, sizeof(float) * 6 * 4);
        for (i = 0; i < 3; i++) {
            vc[4 + i][0] = UF(U_MV, 0, i * 4 + 0); vc[4 + i][1] = UF(U_MV, 0, i * 4 + 1);
            vc[4 + i][2] = UF(U_MV, 0, i * 4 + 2); vc[4 + i][3] = UF(U_MV, 0, i * 4 + 3);
            vc[7 + i][0] = UF(U_NRM, 0, i * 3 + 0); vc[7 + i][1] = UF(U_NRM, 0, i * 3 + 1);
            vc[7 + i][2] = UF(U_NRM, 0, i * 3 + 2);
        }
    }
    if (g & D_MAT) {
        memset(vc[10], 0, sizeof(float) * 6 * 4);
        vc[10][0] = 0.0f; vc[10][1] = 1.0f; vc[10][2] = 0.5f;
        for (i = 0; i < 4; i++) { vc[11][i] = UF(U_CHANCOL, 0, i); vc[12][i] = UF(U_CHANCOL, 1, i); }
        vc[13][0] = UI(U_LCFG0, 0, 1) != 0;
        vc[13][1] = UI(U_LCFG0, 0, 2) != 0;
        vc[13][2] = UI(U_LCFG0, 0, 0) != 0;
        vc[13][3] = UI(U_LCFG0, 0, 3) != 0;
        vc[14][0] = UI(U_LCFG1, 0, 1) != 0;
        vc[14][1] = UI(U_LCFG1, 0, 0) != 0;
        vc[14][2] = UI(U_FOGEN, 0, 0) != 0;
        {
            float st = UF(U_FOGP, 0, 1), en = UF(U_FOGP, 0, 2), d = en - st;
            vc[15][0] = st;
            vc[15][1] = 1.0f / (d > 1e-6f ? d : 1e-6f);
        }
    }
    if (g & D_LIGHT) {
        int mask = UI(U_LCFG1, 0, 2);
        memset(vc[16], 0, sizeof(float) * 16 * 4);
        for (i = 0; i < 8; i++) {
            float x = UF(U_LPOS, i, 0), y = UF(U_LPOS, i, 1), z = UF(U_LPOS, i, 2);
            float l = sqrtf(x * x + y * y + z * z);
            if ((mask & (1 << i)) && l > 1e-12f) {
                vc[16 + i][0] = x / l; vc[16 + i][1] = y / l; vc[16 + i][2] = z / l;
                vc[24 + i][0] = UF(U_LCOL, i, 0); vc[24 + i][1] = UF(U_LCOL, i, 1);
                vc[24 + i][2] = UF(U_LCOL, i, 2); vc[24 + i][3] = UF(U_LCOL, i, 3);
            }
        }
    }
    if (g & D_TEXGEN) {
        memset(vc[32], 0, sizeof(float) * 9 * 4);
        for (s = 0; s < 3; s++) {
            int tc = UI(U_TCSRC, s, 0) & 1;
            float r0[4] = {1, 0, 0, 0}, r1[4] = {0, 1, 0, 0};
            if (UI(U_TMEN, tc, 0)) {
                for (i = 0; i < 4; i++) { r0[i] = UF(U_TMR0, tc, i); r1[i] = UF(U_TMR1, tc, i); }
            }
            for (i = 0; i < 4; i++) { vc[32 + s * 3][i] = r0[i] * scale[s][0]; vc[33 + s * 3][i] = r1[i] * scale[s][1]; }
            vc[34 + s * 3][0] = UI(U_TGSRC, tc, 0) == 1 ? 1.0f : 0.0f;
        }
    }
}

/* Only the constant rows that changed are sent (usually just the modelview
 * and normal rows: 24 words instead of 164 per draw). Runs of changed rows
 * closer than 3 apart are merged into one load. Kill switch:
 * -DXBOX_VC_DELTA=0 (always send all 41 rows). */
#ifndef XBOX_VC_DELTA
#define XBOX_VC_DELTA 1
#endif
static void push_vconst_rows(const uint32_t* w, int first, int nrows) {
    int i, words = nrows * 4;
    put1(NV097_SET_TRANSFORM_CONSTANT_LOAD, (uint32_t)(96 + first));
    w += first * 4;
    for (i = 0; i < words; i += 32) {
        int n = words - i < 32 ? words - i : 32, k;
        pb_push(P++, NV097_SET_TRANSFORM_CONSTANT, n);
        for (k = 0; k < n; k++) P[k] = w[i + k];   /* a few rows: a call to memcpy cost more */
        P += n;
    }
}

/* the constants as last built: groups that didn't change keep their rows */
static float s_vc_cur[41][4];

static void emit_vconsts(const float scale[3][2], uint32_t groups) {
    float (*vc)[4] = s_vc_cur;
    const uint32_t* w = (const uint32_t*)vc;
    if (!s_vc_valid) groups = D_ALL;
    else if (!groups) return;
    build_vconsts(vc, scale, groups);
    if (!s_vc_valid || !XBOX_VC_DELTA) {
        push_vconst_rows(w, 0, 41);
        memcpy(s_shadow_vc, s_vc_cur, sizeof s_vc_cur);
    } else {
        int r = 0;
        while (r < 41) {
            int end, gap;
            if (words_eq(&s_shadow_vc[r * 4], vc[r], 16)) { r++; continue; }
            end = r + 1;
            for (gap = 0; end + gap < 41 && gap < 3; ) {
                if (!words_eq(&s_shadow_vc[(end + gap) * 4], vc[end + gap], 16)) { end += gap + 1; gap = 0; }
                else gap++;
            }
            push_vconst_rows(w, r, end - r);
            {   /* only what was sent */
                int k;
                for (k = r * 4; k < end * 4; k++) s_shadow_vc[k] = w[k];
            }
            r = end;
        }
    }
    s_vc_valid = 1;
}

/* The window clip's maximum is inclusive on the NV2A (xemu adds 1 to it as
 * well, Melee-X renderer.md): x + w let one more column and row through
 * than the scissor asked for. Kill switch: -DXBOX_CLIP_INCLUSIVE=0. */
#ifndef XBOX_CLIP_INCLUSIVE
#define XBOX_CLIP_INCLUSIVE 1
#endif
#define CLIP_INCL (XBOX_CLIP_INCLUSIVE ? 1 : 0)

static int s_fixed_last[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

static void emit_fixed(void) {
    int* last = s_fixed_last;
    int v;
    if (last[0] != G.depth_test) put1(NV097_SET_DEPTH_TEST_ENABLE, (uint32_t)(last[0] = G.depth_test));
    if (last[1] != G.depth_func) put1(NV097_SET_DEPTH_FUNC, (uint32_t)(last[1] = G.depth_func));
    if (last[2] != G.depth_mask) put1(NV097_SET_DEPTH_MASK, (uint32_t)(last[2] = G.depth_mask));
    if (last[3] != G.blend) put1(NV097_SET_BLEND_ENABLE, (uint32_t)(last[3] = G.blend));
    if (last[4] != G.sfac) put1(NV097_SET_BLEND_FUNC_SFACTOR, (uint32_t)(last[4] = G.sfac));
    if (last[5] != G.dfac) put1(NV097_SET_BLEND_FUNC_DFACTOR, (uint32_t)(last[5] = G.dfac));
    if (last[6] != G.beq) put1(NV097_SET_BLEND_EQUATION, (uint32_t)(last[6] = G.beq));
#ifdef XBOX_DBG_NOCULL
    G.cull = 0;
#endif
    if (last[7] != G.cull) put1(NV097_SET_CULL_FACE_ENABLE, (uint32_t)(last[7] = G.cull));
    if (last[8] != G.cull_face) put1(NV097_SET_CULL_FACE, (uint32_t)(last[8] = G.cull_face));
    if (last[9] != G.cmask) put1(NV097_SET_COLOR_MASK, (uint32_t)(last[9] = G.cmask));
    {
        int x, y, w, h;
        clear_rect(&x, &y, &w, &h);
        v = (x & 0xFFFF) | (y << 16);   /* w kept whole: 8 bits of it once matched another clip */
        if (last[10] != v || last[11] != ((w & 0xFFFF) | (h << 16))) {
            last[10] = v;
            last[11] = (w & 0xFFFF) | (h << 16);
            if (w <= 0 || h <= 0) { x = y = 0; w = h = 1; }
            put1(NV097_SET_WINDOW_CLIP_HORIZONTAL, (uint32_t)x | ((uint32_t)(x + w - CLIP_INCL) << 16));
            put1(NV097_SET_WINDOW_CLIP_VERTICAL, (uint32_t)y | ((uint32_t)(y + h - CLIP_INCL) << 16));
        }
    }
    /* alpha test from the TEV alpha compare (two refs -> one when possible) */
    {
        int c0 = UI(U_ACTRL, 0, 0), op = UI(U_ACTRL, 0, 1), c1 = UI(U_ACTRL, 0, 2);
        int r0 = UI(U_AREFS, 0, 0), r1 = UI(U_AREFS, 0, 1);
        int en = 1, fn = 7, ref = 0;
        if (c0 == 7 && c1 == 7) en = 0;
        else if (c1 == 7 && op == 0) { fn = c0; ref = r0; }
        else if (c0 == 7 && op == 0) { fn = c1; ref = r1; }
        else if (op == 1 && c1 == 0) { fn = c0; ref = r0; }
        else if (op == 1 && c0 == 0) { fn = c1; ref = r1; }
        else { fn = c0; ref = r0; }
        if (last[12] != en) put1(NV097_SET_ALPHA_TEST_ENABLE, (uint32_t)(last[12] = en));
        if (en) {
            if (last[13] != fn) put1(NV097_SET_ALPHA_FUNC, (uint32_t)0x200 + (uint32_t)(last[13] = fn));
            if (last[14] != ref) put1(NV097_SET_ALPHA_REF, (uint32_t)(last[14] = ref));
        }
    }
}

static void ring_reserve(int n) {
    if (s_ring_pos + (uint32_t)n > s_ring_cap) {
        /* GPU still reads the older part: drain, then restart at 0 */
        wait_idle();
        pb_open();
        s_ring_pos = 0;
    }
}

/* mid-frame restart at the pushbuffer head (see XBOX_PB_GUARD) */
static void pb_budget(void) {
    if (!XBOX_PB_GUARD || pb_used() < (uint32_t)XBOX_PB_GUARD) return;
    pb_note_peak();
    wait_idle();
    pb_reset();
    s_pb_base = pb_begin();
    s_ring_pos = 0;   /* the GPU is idle: the vertex ring is free too */
    if (!s_pb_rewinds++) xbox_logf("[NV2A] pushbuffer nearly full mid-frame: restarting at the head\n");
}

/* Vertex counts the primitive can't use are trimmed rather than sent: the
 * NV2A is less forgiving than a desktop GL driver about partial primitives. */
static int prim_count(GLenum mode, int count) {
    switch (mode) {
        case GL_POINTS: return count;
        case GL_LINES: return count & ~1;
        case GL_LINE_STRIP: return count >= 2 ? count : 0;
        case GL_TRIANGLES: return count - count % 3;
        case 0x0007 /* quads */: return count & ~3;
        default: return count >= 3 ? count : 0;   /* strips, fans */
    }
}

static int s_logged_nes;

/* draw()'s TEV config and program, kept while D_TEV and the texture epoch
 * stand still (draw_skip), and the texture-unit scale that went with them */
static int s_draw_skip = XBOX_DRAW_SKIP;
static XTevCfg s_cfg;
static const XRcProg* s_rp;
static int s_cfg_valid;
static uint32_t s_cfg_epoch;
static float s_scale[3][2];
static uint32_t s_skip_cfg, s_skip_vc;   /* draws that reused the config / sent no constants */
static unsigned long long s_draw_ticks;  /* CPU time in draw(), for the [FRAME] line */

/* The one non-GX program is pc_nes_fixnes.c's: a quad covering the viewport
 * that samples texture unit 0 (uv 0,0 at the top left). Drawn here with the
 * GX vertex program fed identity matrices and a one-stage "output T0"
 * combiner; the GX uniforms are put back afterwards and the fixed-state
 * shadow is dropped, so the next GX draw re-sends everything it needs. */
static const XRcProg k_blit_rc = {
    1,
    { 0x08200000u }, { 0x00000c00u },   /* rgb: T0 x 1 -> R0 */
    { 0x18301010u }, { 0x00000c00u },   /* alpha: T0.a x 1 -> R0 */
    0x00000c00u, 0x00001c80u,           /* final: R0.rgb, R0.a */
    { { 0 } }, { 0 }, 0
};

static void blit_draw(void) {
    static const float quad[6][4] = {   /* x, y, u, v (NDC, y up) */
        { -1, -1, 0, 1 }, { 1, -1, 1, 1 }, { 1, 1, 1, 0 },
        { -1, -1, 0, 1 }, { 1, 1, 1, 0 }, { -1, 1, 0, 0 },
    };
    static const int saved_u[] = { U_PROJ, U_MV, U_NRM, U_LCFG0, U_LCFG1, U_CHANCOL, U_TCSRC, U_TMEN, U_TGSRC, U_FOGEN };
    static UVal save[sizeof saved_u / sizeof saved_u[0]][16 * 4];
    XTevCfg cfg;
    float scale[3][2];
    uint32_t start;
    int i;
    GLuint tex = s_bound[0];

    if (!tex || !s_tex[tex].used || !s_tex[tex].mem) {
        if (!s_logged_nes++) xbox_logf("[NV2A] non-GX draw without a texture skipped (program %u)\n", s_program);
        return;
    }
    frame_open();
    pb_budget();
    pb_open();

    for (i = 0; i < (int)(sizeof saved_u / sizeof saved_u[0]); i++) memcpy(save[i], s_uv[saved_u[i]], sizeof save[i]);
    for (i = 0; i < (int)(sizeof saved_u / sizeof saved_u[0]); i++) memset(s_uv[saved_u[i]], 0, sizeof s_uv[0]);
    for (i = 0; i < 4; i++) { UF(U_PROJ, 0, i * 5) = 1.0f; }
    for (i = 0; i < 3; i++) { UF(U_MV, 0, i * 5) = 1.0f; UF(U_NRM, 0, i * 4) = 1.0f; }
    for (i = 0; i < 4; i++) { UF(U_CHANCOL, 0, i) = 1.0f; UF(U_CHANCOL, 1, i) = 1.0f; }

    memset(&cfg, 0, sizeof cfg);
    cfg.nstages = 1;
    cfg.st[0].use_tex = 1;

    put1(NV097_SET_DEPTH_TEST_ENABLE, 0);
    put1(NV097_SET_DEPTH_MASK, 0);
    put1(NV097_SET_BLEND_ENABLE, 0);
    put1(NV097_SET_CULL_FACE_ENABLE, 0);
    put1(NV097_SET_ALPHA_TEST_ENABLE, 0);
    put1(NV097_SET_COLOR_MASK, 0x01010101);
    {
        int x = G.vx, y = SCR_H - (G.vy + G.vh), w = G.vw, h = G.vh;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > SCR_W) w = SCR_W - x;
        if (y + h > SCR_H) h = SCR_H - y;
        if (w <= 0 || h <= 0) { x = y = 0; w = h = 1; }
        put1(NV097_SET_WINDOW_CLIP_HORIZONTAL, (uint32_t)x | ((uint32_t)(x + w - CLIP_INCL) << 16));
        put1(NV097_SET_WINDOW_CLIP_VERTICAL, (uint32_t)y | ((uint32_t)(y + h - CLIP_INCL) << 16));
    }
    memset(s_fixed_last, 0xFF, sizeof s_fixed_last);   /* -1: resend on the next GX draw */

    s_tex[tex].drawn = s_frame + 1;
    emit_textures(&cfg, scale);
    emit_vconsts(scale, D_ALL);
    emit_combiners(&k_blit_rc, 1);

    ring_reserve(6);
    start = s_ring_pos;
    for (i = 0; i < 6; i++) {
        XVtx* d = &s_ring[start + i];
        d->pos[0] = quad[i][0];
        d->pos[1] = quad[i][1];
        d->pos[2] = 0.0f;
        d->nrm[0] = d->nrm[1] = 0.0f;
        d->nrm[2] = 1.0f;
        d->col[0] = d->col[1] = d->col[2] = d->col[3] = 0xFF;
        d->tc[0] = quad[i][2];
        d->tc[1] = quad[i][3];
    }
    s_ring_pos += 6;
    put1(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
    P[0] = (1u << 18) | NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_DRAW_ARRAYS);
    P[1] = (5u << 24) | start;
    P += 2;
    put1(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

    for (i = 0; i < (int)(sizeof saved_u / sizeof saved_u[0]); i++) memcpy(s_uv[saved_u[i]], save[i], sizeof save[i]);
    /* the GX draw after this rebuilds and resends everything */
    s_udirty = D_ALL;
    s_cfg_valid = 0;
    s_rc_emitted = NULL;
    s_draws++;
}

static void draw(GLenum mode, int count) {
    const uint8_t* src = (const uint8_t*)s_array_data;
    float scale[3][2];
    int i, st;
    uint32_t start;
    uint32_t prim, groups;
    unsigned long long t0, w0;

    if (s_program != s_uber_prog) {
        blit_draw();
        return;
    }
    if (count <= 0 || !src) return;
    t0 = xbox_ticks();
    w0 = s_wait_ticks;
    frame_open();
    if ((uint32_t)count > s_ring_cap) count = (int)s_ring_cap;
    count = prim_count(mode, count);
    if (count <= 0) return;
    pb_budget();

    if (!s_draw_skip) s_udirty = D_ALL;
    groups = s_udirty;
    pb_open();
    emit_fixed();
    if (!s_cfg_valid || (groups & D_TEV) || s_cfg_epoch != s_tex_epoch) {
        build_tev_cfg(&s_cfg);
        s_rp = rc_lookup(&s_cfg);
        s_cfg_valid = 1;
        s_cfg_epoch = s_tex_epoch;
        emit_textures(&s_cfg, scale);
        if (!words_eq(scale, s_scale, sizeof scale)) {
            memcpy(s_scale, scale, sizeof scale);
            groups |= D_TEXGEN;
        }
        groups |= D_TEVK;   /* a new program reads other constants */
    } else {
        s_skip_cfg++;
    }
    if (s_rp->approximated) s_approx_draws++;
    for (st = 0; st < s_cfg.nstages && st < 4; st++)
        if (s_cfg.st[st].use_tex) s_tex[s_bound[st]].drawn = s_frame + 1;
    if (!(groups & (D_PROJ | D_MV | D_MAT | D_LIGHT | D_TEXGEN)) && s_vc_valid) s_skip_vc++;
    emit_vconsts(s_scale, groups);
    emit_combiners(s_rp, (groups & D_TEVK) != 0);
    s_udirty = 0;
    ring_reserve(count);
    start = s_ring_pos;
    for (i = 0; i < count; i++) {
        /* PCGXVertex: pos[3] @0, normal[3] @12, color0 @24, color1 @28, texcoord[8][2] @32 */
        const uint8_t* v = src + (size_t)i * 96;
        XVtx* d = &s_ring[start + i];
        memcpy(d->pos, v, 24);
        memcpy(d->col, v + 24, 4);
        memcpy(d->tc, v + 32, 8);
    }
    s_ring_pos += (uint32_t)count;
#ifdef XBOX_DBG_DRAWLOG
    if (s_frame == XBOX_DBG_DRAWLOG) {
        const XVtx* v0 = &s_ring[start];
        const XTex* t0 = s_cfg.st[0].use_tex ? &s_tex[s_bound[0]] : NULL;
        int k;
        float ez = 0.0f;
        for (k = 0; k < 4; k++) ez += UF(U_MV, 0, 8 + k) * (k < 3 ? v0->pos[k] : 1.0f);
        xbox_logf("[DRAW] %u m%d n%d z%d/%d/%d b%d %d/%d c%d/%d st%d tex%dx%d fog%d proj00 %d p0 %d,%d,%d ez %d col %08x\n",
                  s_draws, (int)mode, count, G.depth_test, G.depth_func & 0xF, G.depth_mask, G.blend, G.sfac, G.dfac,
                  G.cull, G.cull_face & 0xF, s_cfg.nstages, t0 ? t0->w : 0, t0 ? t0->h : 0, s_cfg.fog_on,
                  (int)(UF(U_PROJ, 0, 0) * 1000), (int)v0->pos[0], (int)v0->pos[1], (int)v0->pos[2], (int)ez,
                  *(const uint32_t*)v0->col);
    }
#endif

    prim = (uint32_t)mode + 1;   /* GL_POINTS..GL_TRIANGLE_FAN -> NV097 ops */
    if (mode == 0x0007 /* GL_QUADS, used for the quad EBO path */) prim = NV097_SET_BEGIN_END_OP_QUADS;
    put1(NV097_SET_BEGIN_END, prim);
    {
        int left = count;
        uint32_t first = start;
        while (left > 0) {
            int words = 0, k;
            int batch = left > 256 * 64 ? 256 * 64 : left;
            uint32_t* hdr = P++;
            for (k = 0; k < batch; k += 256) {
                int n = batch - k > 256 ? 256 : batch - k;
                *P++ = ((uint32_t)(n - 1) << 24) | (first + (uint32_t)k);
                words++;
            }
            *hdr = (uint32_t)words << 18 | NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_DRAW_ARRAYS);
            first += (uint32_t)batch;
            left -= batch;
        }
    }
    put1(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    if ((uint32_t)(P - s_pb_mark) >= s_pb_kick) pb_close();
    s_draws++;
    s_draw_ticks += xbox_ticks() - t0 - (s_wait_ticks - w0);   /* waits count as gpu wait */
}

static void gl_draw_arrays(GLenum mode, GLint first, GLsizei count) {
    (void)first;
    s_n_da++;
    if (!s_array_data) s_n_null++;
    draw(mode, count);
}

static void gl_draw_elements(GLenum mode, GLsizei count, GLenum type, const void* idx) {
    (void)mode; (void)type; (void)idx;
    s_n_de++;
    /* pc_gx.c only uses elements for GX_QUADS with its static quad index buffer */
    draw(0x0007, count / 6 * 4);
}

/* ======================================================================
 * Frame + init
 * ====================================================================== */
static void load_vertex_program(void) {
    static const uint32_t prog[] = {
#include "../shaders/gx_vsh.inl"
    };
    int i;
    PB_BEGIN();
    put1(NV097_SET_TRANSFORM_PROGRAM_START, 0);
    put1(NV097_SET_TRANSFORM_EXECUTION_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM |
                                             (NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV << 2));
    put1(NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
    put1(NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    PB_END();
    for (i = 0; i < (int)(sizeof prog / sizeof prog[0]); i += 4) {
        PB_BEGIN();
        pb_push(P++, NV097_SET_TRANSFORM_PROGRAM, 4);
        memcpy(P, &prog[i], 16);
        P += 4;
        PB_END();
    }
}

static void setup_attributes(void) {
    uint32_t base = (uint32_t)s_ring & 0x03FFFFFF;
    int i;
    PB_BEGIN();
    for (i = 0; i < 16; i++) put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + i * 4, 2 /* F, size 0 */);
    /* v0 pos F3, v2 normal F3, v3 diffuse UB_OGL 4, v9 tex0 F2 ; stride 36 */
    put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 0 * 4, 2 | (3 << 4) | (36 << 8));
    put1(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 0 * 4, base + 0);
    put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 2 * 4, 2 | (3 << 4) | (36 << 8));
    put1(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 2 * 4, base + 12);
    put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 3 * 4, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL | (4 << 4) | (36 << 8));
    put1(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 3 * 4, base + 24);
    put1(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2 | (2 << 4) | (36 << 8));
    put1(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 9 * 4, base + 28);
    PB_END();
}

static void setup_state(void) {
    PB_BEGIN();
    put1(NV097_SET_CONTROL0, NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE);
    put1(NV097_SET_LIGHTING_ENABLE, 0);
    /* oSpecular.w carries the fog factor to the final combiner (V1.a). With
     * SPECULAR_ENABLE off the NV2A replaces oD1 with (0,0,0,1), and without
     * ALPHA_FROM_MATERIAL_SPECULAR it forces the alpha to 1: either way the
     * whole scene comes out solid fog colour (xemu pgraph/glsl/vsh.c). */
    put1(NV097_SET_SPECULAR_ENABLE, 1);
    put1(NV097_SET_LIGHT_CONTROL, NV097_SET_LIGHT_CONTROL_V_SEPARATE_SPECULAR |
                                  NV097_SET_LIGHT_CONTROL_V_ALPHA_FROM_MATERIAL_SPECULAR);
    put1(NV097_SET_FOG_ENABLE, 0);
    put1(NV097_SET_SKIN_MODE, NV097_SET_SKIN_MODE_OFF);
    put1(NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    /* GL's default (CCW). The viewport y-flip folded into the projection
     * mirrors screen-space winding, but the NV2A evaluates facing like GL
     * (xemu inverts it again for its GL backend), so CW here culls fronts. */
    put1(NV097_SET_FRONT_FACE, NV097_SET_FRONT_FACE_V_CCW);
    put1(NV097_SET_WINDOW_CLIP_TYPE, 0);
    put1(NV097_SET_ZMIN_MAX_CONTROL, NV097_SET_ZMIN_MAX_CONTROL_CULL_NEAR_FAR | NV097_SET_ZMIN_MAX_CONTROL_ZCLAMP_CULL);
    putf(NV097_SET_CLIP_MIN, 0.0f);
    putf(NV097_SET_CLIP_MAX, ZMAX);
    put1(NV097_SET_SHADER_CLIP_PLANE_MODE, 0);
    PB_END();
}

/* CPU/GPU overlap: xbox_nv2a_present queues the flip without waiting for the
 * GPU, and the next frame's game logic (game_main, before emu64 issues any GL
 * call) runs while the GPU still draws. The first GL call of the next frame
 * lands here: drain the GPU, then free last frame's textures and restart the
 * pushbuffer and vertex ring at their heads, as before. Kill switch:
 * -DXBOX_GPU_OVERLAP=0 (drain in present, as before). */
#ifndef XBOX_GPU_OVERLAP
#define XBOX_GPU_OVERLAP 1
#endif
/* vblank pacing (vbl_pace, below); -DXBOX_VBL_PACE=0 keeps pc_vi.c's timer */
#ifndef XBOX_VBL_PACE
#define XBOX_VBL_PACE 1
#endif
/* also settings.ini [Xbox] gpu_overlap = 0 at runtime (read once at init: the
 * two modes can't be switched between a present and the next frame_open) */
static int s_overlap;
static unsigned long long s_drain_ticks;   /* GPU wait moved here: hitch_log counts it as gpu */
static void frame_open(void) {
    if (s_frame_open) return;
    if (s_overlap) {
        unsigned long long t0 = xbox_ticks();
        wait_idle();
        s_drain_ticks += xbox_ticks() - t0;
        release_deferred();
    }
    pb_reset();
    s_pb_base = pb_begin();
    pb_target_back_buffer();
    s_ring_pos = 0;
    s_frame_open = 1;
}

/* 720p (Options > Video > Output): 1280x720 at 16-bit colour (R5G6B5, the
 * NV2A dithers) with a Z16 depth buffer, so it fits: 3 x 1.8 MB colour +
 * 1.8 MB depth is ~2.5 MB over 640x480x32 + Z24S8, plus 0.6 MB for the
 * bigger XVideo (splash / debug screen) buffer, paid back by a 5 MB texture
 * pool (8 MB at 480): about even. Measured in xemu (2026-09-28): 20.7 MB
 * free after GPU init and 5.6 MB at the title demo, the same as 480. The
 * guard below only catches a console that is already short. Only when the
 * dashboard allows 720p on this AV pack; otherwise the 640x480 mode the
 * splash set stays. */
#ifndef XBOX_720P_MIN_FREE_KB
#define XBOX_720P_MIN_FREE_KB (32 * 1024)
#endif
int g_xbox_video_720p;

/* 720p when asked for, allowed and affordable; 0 leaves the 640x480 mode */
static int video_720p(void) {
    unsigned free_kb;
    /* 720p is drawn through the 16:9 logical screen (pc_gx.c with
     * PC_ENHANCEMENTS); without it the picture would be stretched */
    if (!XBOX_WIDESCREEN || !g_xbox_settings_boot.video_720p) return 0;
    if (!xbox_video_720p_allowed()) {
        xbox_logf("[NV2A] 720p asked for but not allowed (dashboard or AV cable): staying at 480\n");
        return 0;
    }
    free_kb = xbox_mem_free_kb();
    if (free_kb < XBOX_720P_MIN_FREE_KB) {
        xbox_logf("[NV2A] 720p needs %u KB free, have %u KB: staying at 480\n", XBOX_720P_MIN_FREE_KB, free_kb);
        return 0;
    }
    xbox_splash_release();   /* XVideoSetMode frees the splash's buffer */
    if (!XVideoSetMode(1280, 720, 16, REFRESH_DEFAULT)) {
        xbox_logf("[NV2A] XVideoSetMode 1280x720x16 failed: back to 640x480\n");
        xbox_video_set_480();
        return 0;
    }
    pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5, false);
    /* NV2x wants colour and depth of the same width: Z16 with R5G6B5 */
    pb_DepthFmt = NV097_SET_SURFACE_FORMAT_ZETA_Z16;
    s_zmax = 65535.0f;
    s_pool_bytes = XBOX_TEX_POOL_720P_BYTES;
    g_xbox_video_720p = 1;
    return 1;
}

/* The splash set 640x480 in the dashboard's mode, before the settings were
 * read: at 480 with progressive = 0 (settings.ini, or safe video) the mode
 * is set again as 480i (xbox_video_set_480). */
static void video_select(void) {
    if (video_720p()) return;
    if (!g_xbox_settings_boot.progressive && xbox_video_480p_allowed()) {
        xbox_splash_release();   /* XVideoSetMode frees the splash's buffer */
        xbox_video_set_480();
        xbox_logf("[NV2A] 480i (progressive = 0)\n");
    }
}

/* back to the standard mode when 720p can't start (pb_init or the texture
 * pool / vertex ring allocations fail): a console must never be stuck on
 * the "Graphics init failed" screen because of a saved setting */
static void video_standard(void) {
    xbox_video_set_480();
    pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8, false);
    pb_DepthFmt = NV097_SET_SURFACE_FORMAT_ZETA_Z24S8;
    s_zmax = 16777215.0f;
    s_pool_bytes = XBOX_TEX_POOL_BYTES;
    g_xbox_video_720p = 0;
}

int xbox_nv2a_init(void) {
    int err;
    s_overlap = XBOX_GPU_OVERLAP && g_xbox_settings_boot.gpu_overlap;
    /* the backport's switches (settings.ini [Xbox], docs/backport.md): a
     * compile-time 0 wins, otherwise the setting decides */
    s_native_tex = XBOX_NATIVE_TEX && g_xbox_settings_boot.native_tex;
    s_tex_reuse = XBOX_TEX_REUSE && g_xbox_settings_boot.tex_reuse;
    s_draw_skip = XBOX_DRAW_SKIP && g_xbox_settings_boot.draw_skip;
    s_vb_break = XBOX_VB_CACHE_BREAK && g_xbox_settings_boot.vb_cache_break;
    s_strict_idle = XBOX_STRICT_IDLE && g_xbox_settings_boot.strict_gpu_wait;
    s_pb_kick = XBOX_PB_KICK ? (uint32_t)g_xbox_settings_boot.pb_kick_kb * 256 : 0;   /* 0: a kick per draw */
    quant_tables();
    video_select();
    pb_size(1024 * 1024);
    for (;;) {
        err = pb_init();
        if (err) {
            xbox_logf("[NV2A] pb_init failed: %d\n", err);
            if (!g_xbox_video_720p) return 0;
            video_standard();
            continue;
        }
        pool_init();
        s_ring = (XVtx*)MmAllocateContiguousMemoryEx(XBOX_VTX_RING_BYTES, 0, MAXRAM, 0,
                                                     PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (s_pool && s_ring) break;
        xbox_logf("[NV2A] contiguous alloc failed (pool=%p ring=%p)\n", s_pool, s_ring);
        if (!g_xbox_video_720p) return 0;
        if (s_pool) MmFreeContiguousMemory(s_pool);
        if (s_ring) MmFreeContiguousMemory(s_ring);
        free(s_blocks);
        s_pool = NULL;
        s_ring = NULL;
        s_blocks = NULL;
        pb_kill();
        video_standard();
    }
    if (g_xbox_video_720p == 0 && g_xbox_settings_boot.video_720p)
        xbox_logf("[NV2A] running at 480\n");
    pb_show_front_screen();
    s_fbw = (int)pb_back_buffer_width();
    s_fbh = (int)pb_back_buffer_height();
    s_fb_bpp = g_xbox_video_720p ? 16 : 32;
    if (g_xbox_video_720p) {
        /* R5G6B5 wants dithering, or skies and fog band */
        uint32_t* p = pb_begin();
        p = pb_push1(p, NV097_SET_DITHER_ENABLE, 1);
        pb_end(p);
    }
    s_ring_cap = XBOX_VTX_RING_BYTES / sizeof(XVtx);
    frame_open();
    load_vertex_program();
    setup_attributes();
    setup_state();
    xbox_logf("[NV2A] up: %dx%d %d-bit, tex pool %u KB, vertex ring %u verts, gpu overlap %d, vblank pacing %d "
              "(replaces the VI timer at max_fps 60)\n",
              s_fbw, s_fbh, s_fb_bpp, s_pool_bytes / 1024, s_ring_cap, s_overlap, XBOX_VBL_PACE);
    xbox_logf("[NV2A] native textures %d, texture reuse %d, draw skip %d, vertex cache break %d, strict gpu wait %d, "
              "kick %u KB, clip inclusive %d\n",
              s_native_tex, s_tex_reuse, s_draw_skip, s_vb_break, s_strict_idle, s_pb_kick / 256, XBOX_CLIP_INCLUSIVE);
    return 1;
}

/* Hitch log: any frame slower than XBOX_HITCH_MS, or presented with fewer
 * than 3 draws (a candidate black/stale frame), is reported with what the
 * frame spent its time on. -DXBOX_HITCH_MS=0 turns it off. */
#ifndef XBOX_HITCH_MS
#define XBOX_HITCH_MS 40
#endif
/* perf.log: one line a minute on the HDD (hardware has no serial port):
 * average fps, average cpu ms, worst frame, frames > 17.5 ms (a missed
 * vblank), > 33 ms and > 100 ms.
 * Times arrive in 0.1 ms units. -DXBOX_PERF_LOG=0 disables. */
#ifndef XBOX_PERF_LOG
#define XBOX_PERF_LOG 1
#endif
/* perf.log extras: the minute's [HITCH] lines of 100 ms and over, [PACE] and
 * [NES] lines are kept here and written under that minute's perf line, so
 * perf.log holds the whole session (last.log only has the last 4 KB) and
 * the disk is touched once a minute, not at the hitch. */
static char s_perf_extra[2048];
static unsigned s_perf_extra_len, s_perf_extra_drop;
static void perf_note(const char* line) {   /* one line, no line ending */
    size_t n = strlen(line);
    if (!XBOX_PERF_LOG) return;
    if (s_perf_extra_len + n + 2 > sizeof s_perf_extra) {
        s_perf_extra_drop++;
        return;
    }
    memcpy(s_perf_extra + s_perf_extra_len, line, n);
    memcpy(s_perf_extra + s_perf_extra_len + n, "\r\n", 2);
    s_perf_extra_len += (unsigned)n + 2;
}

/* Pace log: under vblank pacing, a frame over 17.5 ms missed its vblank (the
 * previous picture shows twice); 17-33 ms frames are under the hitch
 * threshold, and a run of them is what reads as choppy. One "[PACE]" line
 * per 5 s window in which frames missed at least XBOX_PACE_MISSES times a
 * second on average, so last.log holds a choppy stretch without the log
 * itself rewriting last.log every 3 s. -DXBOX_PACE_MISSES=0 turns it off. */
#ifndef XBOX_PACE_MISSES
#define XBOX_PACE_MISSES 6
#endif
static int s_vbl_on;   /* vbl_pace ran this frame */
static void pace_account(unsigned t10, unsigned cpu10, unsigned draws, unsigned tex_n) {
    static unsigned n, missed, sum, cpu_sum, draw_sum, tex_sum;
    if (!XBOX_PACE_MISSES) return;
    if (!s_vbl_on) {
        n = missed = sum = cpu_sum = draw_sum = tex_sum = 0;
        return;
    }
    n++;
    sum += t10;
    cpu_sum += cpu10;
    draw_sum += draws;
    tex_sum += tex_n;
    if (t10 > 175) missed++;
    if (sum < 50000) return;   /* 5 s */
    if (missed >= 5 * XBOX_PACE_MISSES) {
        char line[160];
        snprintf(line, sizeof line,
                 "  [PACE] frame %u: %u of %u frames missed a vblank in 5 s | avg %u ms, cpu %u ms | draws %u, tex %u",
                 s_frame, missed, n, sum / n / 10, cpu_sum / n / 10, draw_sum / n, tex_sum);
        xbox_logf("%s\n", line + 2);
        perf_note(line);
    }
    n = missed = sum = cpu_sum = draw_sum = tex_sum = 0;
}

/* NES play: famicom.cpp is built with pc_fixnes_frame renamed to this
 * (xbox/CMakeLists.txt), so the emulator's CPU time per NES frame is
 * measured apart from the upload and draw. One [NES] line per 300 frames
 * (5 s) in perf.log, and in the log too when the average is over 14 ms
 * (then fixNES itself is what chops). */
/* -DXBOX_NES_SHOT=N (test builds): the framebuffer of the Nth NES frame
 * of each game goes to UDATA nes_shot.raw (16-byte header "OCXS", width,
 * height, bpp; then the rows), for what the console really shows; the
 * serial dump only exists in xemu. tools/xbox/raw_to_png.py converts it. */
#ifndef XBOX_NES_SHOT
#define XBOX_NES_SHOT 0
#endif
static int s_nes_shot;
unsigned short* pc_fixnes_frame(void);
unsigned short* xbox_nes_frame(void) {
    static unsigned n, last_frame, game_frames;
    static unsigned long long sum, worst;
    unsigned long long t0 = xbox_ticks(), d;
    unsigned short* fb = pc_fixnes_frame();
    d = xbox_ticks() - t0;
    if (s_frame - last_frame > 2) {   /* a new game */
        n = 0;
        game_frames = 0;
        sum = worst = 0;
    }
    last_frame = s_frame;
    if (XBOX_NES_SHOT && ++game_frames == XBOX_NES_SHOT) s_nes_shot = 1;   /* nes_shot.raw at this present */
    sum += d;
    if (d > worst) worst = d;
    if (++n == 300) {
        unsigned long long f = xbox_ticks_per_sec() / 10000;   /* 0.1 ms */
        unsigned avg = (unsigned)(sum / n / f), top = (unsigned)(worst / f);
        char line[128];
        snprintf(line, sizeof line, "  [NES] frame %u: emulator %u.%u ms avg, worst %u.%u ms per NES frame", s_frame,
                 avg / 10, avg % 10, top / 10, top % 10);
        if (avg > 140) xbox_logf("%s\n", line + 2);   /* only when it is the problem */
        perf_note(line);
        n = 0;
        sum = worst = 0;
    }
    return fb;
}

void xbox_flush_file(HANDLE h);
static HANDLE s_perf_h = INVALID_HANDLE_VALUE;
static int perf_open(void) {
    if (s_perf_h == INVALID_HANDLE_VALUE)
        s_perf_h = CreateFileA(XBOX_UDATA_DIR "perf.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    return s_perf_h != INVALID_HANDLE_VALUE;
}
/* write the noted lines: every 15 s if there are any (a quit or freeze
 * loses at most that much), and ahead of each minute's summary line */
static void perf_flush_notes(void) {
    DWORD w;
    if (!s_perf_extra_len && !s_perf_extra_drop) return;
    if (perf_open()) {
        if (s_perf_extra_len) WriteFile(s_perf_h, s_perf_extra, s_perf_extra_len, &w, NULL);
        if (s_perf_extra_drop) {
            char line[64];
            int len = snprintf(line, sizeof line, "  (%u more lines not kept)\r\n", s_perf_extra_drop);
            WriteFile(s_perf_h, line, (DWORD)len, &w, NULL);
        }
        xbox_flush_file(s_perf_h);
    }
    s_perf_extra_len = s_perf_extra_drop = 0;
}

static void perf_account(unsigned t10, unsigned cpu10) {
    static unsigned n, worst, over17, over33, over100;
    static unsigned long long sum, cpu_sum;
    static unsigned minute;
    HANDLE h;
    if (!XBOX_PERF_LOG) return;
    n++;
    sum += t10;
    cpu_sum += cpu10;
    if (t10 > worst) worst = t10;
    if (t10 > 175) over17++;
    if (t10 > 330) over33++;
    if (t10 > 1000) over100++;
    if (sum % 150000 < t10) perf_flush_notes();   /* every 15 s, if any */
    if (sum < 600000) return;   /* 60 s */
    minute++;
    perf_flush_notes();   /* the minute's notes go above its summary */
    h = perf_open() ? s_perf_h : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        char line[300];
        DWORD w;
        int len = snprintf(line, sizeof line,
                           "min %u (frame %u): %u.%u fps avg, cpu %u.%u ms avg, worst %u ms, >17ms %u, >33ms %u, >100ms %u | "
                           "pb peak %u KB, tex %u KB, free %u KB, gpu faults %u | since boot: uploads 1/2/4 B "
                           "%u/%u/%u, %u KB saved, %u rewritten in place\r\n",
                           minute, s_frame, (unsigned)(n * 100000ull / sum) / 10, (unsigned)(n * 100000ull / sum) % 10,
                           (unsigned)(cpu_sum / n) / 10, (unsigned)(cpu_sum / n) % 10, worst / 10, over17, over33, over100,
                           s_pb_peak / 1024, s_pool_used / 1024, xbox_mem_free_kb(), (unsigned)s_gf_count,
                           s_tex_by_bpp[1], s_tex_by_bpp[2], s_tex_by_bpp[4], (unsigned)(s_tex_saved_bytes / 1024), s_tex_reused_total);
        s_pb_peak = 0;
        WriteFile(h, line, (DWORD)len, &w, NULL);
        xbox_flush_file(h);
        xbox_logf("[PERF] %s", line);
    }
    n = worst = over17 = over33 = over100 = 0;
    sum = cpu_sum = 0;
}

/* [FRAME]: every 5 s, where the frames went, in ms per frame: the shim's
 * own draw work (GL calls to pushbuffer), texture uploads, every wait for
 * the GPU, the pacing waits after the flip, file reads (all threads), and
 * the rest (game logic, emu64, pc_gx). Plus how often draw_skip reused the
 * TEV config or sent no vertex constants. Kill switch: -DXBOX_FRAME_LOG=0. */
#ifndef XBOX_FRAME_LOG
#define XBOX_FRAME_LOG 1
#endif
static unsigned long long s_pace_ticks;   /* this frame's flip + scanout + vblank waits */
static void frame5_account(unsigned long long frame) {
    static unsigned n, draws, skip_cfg, skip_vc;
    static unsigned long long sum, shim, tex, gpu, pace, file;
    unsigned long long f = xbox_ticks_per_sec() / 10000;   /* 0.1 ms */
    if (!XBOX_FRAME_LOG) return;
    n++;
    sum += frame;
    shim += s_draw_ticks;
    tex += g_xfs.tex_ticks;
    gpu += s_wait_ticks;
    pace += s_pace_ticks;
    file += g_xfs.fread_ticks;
    draws += s_draws;
    skip_cfg += s_skip_cfg;
    skip_vc += s_skip_vc;
    if (sum >= 5 * xbox_ticks_per_sec()) {
        unsigned fps10 = (unsigned)(n * 10ull * xbox_ticks_per_sec() / sum);
        unsigned t = (unsigned)(sum / n / f), sh = (unsigned)(shim / n / f), tx = (unsigned)(tex / n / f);
        unsigned gw = (unsigned)(gpu / n / f), pc = (unsigned)(pace / n / f), fl = (unsigned)(file / n / f);
        unsigned busy = sh + tx + gw + pc, rest = t > busy ? t - busy : 0;
        xbox_logf_quiet("[FRAME] %u.%u fps, ms/frame: total %u.%u | game+emu64 %u.%u, shim %u.%u, tex %u.%u, gpu wait %u.%u, "
                  "pace %u.%u (file %u.%u) | draws %u, cfg reused %u%%, no constants %u%% | tex in place %u\n",
                  fps10 / 10, fps10 % 10, t / 10, t % 10, rest / 10, rest % 10, sh / 10, sh % 10, tx / 10, tx % 10,
                  gw / 10, gw % 10, pc / 10, pc % 10, fl / 10, fl % 10, draws / n,
                  draws ? (unsigned)(skip_cfg * 100ull / draws) : 0, draws ? (unsigned)(skip_vc * 100ull / draws) : 0,
                  s_tex_reused);
        n = draws = skip_cfg = skip_vc = 0;
        sum = shim = tex = gpu = pace = file = 0;
        s_tex_reused = 0;
    }
}

static void hitch_log(unsigned long long t_enter, unsigned long long t_done) {
    static unsigned long long t_last;
    unsigned long long f = xbox_ticks_per_sec() / 1000;
    /* with the overlap, the wait for last frame's GPU work happened inside
     * this frame (frame_open): count it as gpu, not cpu */
    unsigned long long cpu_ticks = t_enter - t_last > s_drain_ticks ? t_enter - t_last - s_drain_ticks : 0;
    s_drain_ticks = 0;
    if (XBOX_HITCH_MS && t_last) {
        /* a fade or load draws nothing for many frames: report the first
         * frame of such a run and its length, not every frame of it */
        static unsigned s_sparse_run;
        unsigned total = (unsigned)((t_done - t_last) / f), cpu = (unsigned)(cpu_ticks / f);
        int sparse = s_draws < 3;
        if (!sparse && s_sparse_run > 1) xbox_logf("[HITCH] %u frames with < 3 draws, up to frame %u\n", s_sparse_run, s_frame - 1);
        s_sparse_run = sparse ? s_sparse_run + 1 : 0;
        if (s_sparse_run && s_sparse_run % 600 == 0) xbox_logf("[HITCH] %u frames with < 3 draws so far\n", s_sparse_run);
        if (total >= XBOX_HITCH_MS || s_sparse_run == 1) {
            char line[200];
            snprintf(line, sizeof line,
                     "  [HITCH] frame %u: %u ms (cpu %u, gpu+flip %u) draws %u clears %u | tex %u / %u ms | "
                     "fread %u / %u KB / %u ms",
                     s_frame, total, cpu, total - cpu, s_draws, s_n_clr_frame, g_xfs.tex_n,
                     (unsigned)(g_xfs.tex_ticks / f), g_xfs.fread_n, (unsigned)(g_xfs.fread_bytes / 1024),
                     (unsigned)(g_xfs.fread_ticks / f));
            xbox_logf("%s\n", line + 2);
            if (total >= 100) perf_note(line);
        }
    }
    if (t_last) {
        unsigned t10 = (unsigned)((t_done - t_last) * 10 / f), cpu10 = (unsigned)(cpu_ticks * 10 / f);
        perf_account(t10, cpu10);
        pace_account(t10, cpu10, s_draws, g_xfs.tex_n);
        frame5_account(t_done - t_last);
    }
    t_last = t_done;
    memset(&g_xfs, 0, sizeof g_xfs);
    s_n_clr_frame = 0;
    s_draw_ticks = s_wait_ticks = s_pace_ticks = 0;
    s_skip_cfg = s_skip_vc = 0;
}

/* one line of renderer state for last.log / hang.log / crash.log */
int xbox_nv2a_state(char* buf, int cap) {
    return snprintf(buf, (size_t)cap,
                    "[STATE] frame %u draws %u | pb peak %u KB rewinds %u | tex pool %u KB (peak %u) recover %u "
                    "fail %u | gpu faults %u last kind %u %08x %08x %08x%s\n",
                    s_frame, s_draws, s_pb_peak / 1024, s_pb_rewinds, s_pool_used / 1024, s_pool_peak / 1024,
                    s_tex_recover, s_tex_fail, (unsigned)s_gf_count, (unsigned)s_gf_last[0], (unsigned)s_gf_last[1],
                    (unsigned)s_gf_last[2], (unsigned)s_gf_last[3], ocx_pb_irq_off ? " IRQ-MASKED" : "");
}

/* Vblank pacing. pc_vi.c's limiter paced each frame 16.667 ms after the end
 * of the previous one: a late frame's overrun was never made up, and its
 * 60.00 Hz beat against the 59.94 Hz vblank. With frame times near the budget
 * that tipped whole stretches into repeated pictures, differently from boot
 * to boot (the title demo, 1 boot in 3), and its last 2 ms were a busy spin
 * the audio producer couldn't use. Here every frame is due one vblank after
 * the previous one: an early frame sleeps until its vblank, a late one lets
 * the next start at once (the triple buffer absorbs it), and one more than
 * XBOX_VBL_SLACK vblanks behind (a load) resyncs rather than racing to catch
 * up. The wait is in 2 ms slices (the vblank event is pulsed: a vblank
 * between reading the counter and waiting would otherwise cost a frame).
 * When it applies, and what pc_vi.c's timer sees otherwise, is
 * xbox_vi_pace_policy (xbox_settings.c); with the GPU interrupt masked there
 * are no vblank events and the timer takes over. Kill switch:
 * -DXBOX_VBL_PACE=0 (the timer, as before). */
#define XBOX_VBL_SLACK 2
DWORD ocx_pb_wait_for_vbl_timeout(LONGLONG timeout_100ns);   /* patch_pbkit.py */

static void vbl_pace(void) {
    static DWORD s_due;
    int guard = 20;   /* 40 ms: never hang on a vblank that doesn't come */
    DWORD now;
    if (!xbox_vi_pace_policy(XBOX_VBL_PACE && !ocx_pb_irq_off)) {
        s_vbl_on = 0;
        return;
    }
    now = pb_get_vbl_counter();
    if (!s_vbl_on) {
        s_due = now;
        s_vbl_on = 1;
    }
    s_due++;
    if ((int)(now - s_due) > XBOX_VBL_SLACK) s_due = now;
    while (guard-- && (int)(pb_get_vbl_counter() - s_due) < 0) ocx_pb_wait_for_vbl_timeout(20000);
}

/* On-screen frame rate (settings.ini fps_counter, Options > Video), from
 * Melee-X: frames presented over the last half second, drawn by the GPU
 * after the frame as colour fills of the lit runs of each font row. Yellow
 * 5x7 digits on a black box inside the TV-safe area, 2x (3x at 720p).
 * Applies live; screenshots show it. */
/* a colour fill of the rect, pushed at P (an open block) */
static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    put1(NV097_SET_CLEAR_RECT_HORIZONTAL, ((uint32_t)(x + w - 1) << 16) | (uint32_t)x);
    put1(NV097_SET_CLEAR_RECT_VERTICAL, ((uint32_t)(y + h - 1) << 16) | (uint32_t)y);
    put1(NV097_SET_COLOR_CLEAR_VALUE, color);
    put1(NV097_CLEAR_SURFACE, 0xF0);   /* colour only */
}

static void fps_overlay(void) {
    static const uint8_t font[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },   { 14, 17, 1, 2, 4, 8, 31 },
        { 31, 2, 4, 2, 1, 17, 14 },     { 2, 6, 10, 18, 31, 2, 2 },  { 31, 16, 30, 1, 1, 17, 14 },
        { 6, 8, 16, 30, 17, 17, 14 },   { 31, 1, 2, 4, 8, 8, 8 },    { 14, 17, 17, 14, 17, 17, 14 },
        { 14, 17, 17, 15, 1, 2, 12 },
    };
    static uint32_t s_val, s_frames;
    static unsigned long long s_t0;
    unsigned long long now = xbox_ticks(), hz = xbox_ticks_per_sec();
    uint32_t v, digits[3], nd = 0, d, cy;
    int z = SCR_H >= 720 ? 3 : 2, x0 = SCR_W / 16, y0 = SCR_H / 16;
    s_frames++;
    if (!s_t0 || now - s_t0 > 2 * hz) {
        s_t0 = now;
        s_frames = 0;
    } else if (now - s_t0 >= hz / 2) {
        s_val = (uint32_t)((s_frames * hz + (now - s_t0) / 2) / (now - s_t0));
        s_t0 = now;
        s_frames = 0;
    }
    v = s_val > 999 ? 999 : s_val;
    do {
        digits[nd++] = v % 10;
        v /= 10;
    } while (v && nd < 3);
    /* one pushbuffer block of clear-rect fills (pb_fill would be a block and
     * a kick per run); colours in the surface's format: black, yellow */
    pb_close();
    PB_BEGIN();
    fill_rect(x0, y0, (int)nd * 6 * z + 2 * z, 9 * z, 0);
    for (d = 0; d < nd; d++)
        for (cy = 0; cy < 7; cy++) {
            uint32_t bits = font[digits[nd - 1 - d]][cy], cx = 0;
            while (cx < 5) {   /* runs of lit cells */
                uint32_t run = 0;
                while (cx + run < 5 && (bits >> (4 - (cx + run)) & 1)) run++;
                if (run)
                    fill_rect(x0 + z + (int)(d * 6 + cx) * z, y0 + z + (int)cy * z, (int)run * z, z,
                              s_fb_bpp == 16 ? 0xFFE0u : 0xFFFFFF00u);
                cx += run ? run : 1;
            }
        }
    PB_END();
}

static void shot_file(void) {
    HANDLE h;
    DWORD wr;
    uint32_t hdr[4] = { 0x5358434Fu /* "OCXS" */, (uint32_t)SCR_W, (uint32_t)SCR_H, (uint32_t)s_fb_bpp };
    const uint8_t* fb = (const uint8_t*)pb_back_buffer();
    uint32_t pitch = pb_back_buffer_pitch(), row = (uint32_t)SCR_W * (uint32_t)s_fb_bpp / 8;
    int y;
    h = CreateFileA(XBOX_UDATA_DIR "nes_shot.raw", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, hdr, sizeof hdr, &wr, NULL);
    for (y = 0; y < SCR_H; y++) WriteFile(h, fb + (size_t)y * pitch, row, &wr, NULL);
    CloseHandle(h);
    xbox_logf("[NV2A] nes_shot.raw written (%dx%d, %d-bit), frame %u\n", SCR_W, SCR_H, s_fb_bpp, s_frame);
}

void xbox_nv2a_present(void) {
    unsigned long long t_enter = xbox_ticks(), t_pace;
    int dump;
    frame_open();
    pb_note_peak();
    if (g_xbox_settings.fps_counter) fps_overlay();
    s_frame++;
    dump = (g_xbox_fbdump_every > 0 && (s_frame % (uint32_t)g_xbox_fbdump_every) == 0) || g_xbox_fbdump_once;
    g_xbox_fbdump_once = 0;
    if (s_overlap && !dump) pb_close();   /* kick; frame_open drains */
    else wait_idle();
    gpu_fault_log(s_frame - 1);
    if (s_nes_shot) {
        s_nes_shot = 0;
        wait_idle();
        shot_file();
    }
    if (dump) {
        xbox_logf("[NV2A] frame %u draws=%u approx=%u rc=%d pool=%uKB peak=%uKB\n", s_frame, s_draws,
                  s_approx_draws, s_rc_count, s_pool_used / 1024, s_pool_peak / 1024);
        xbox_logf("[NV2A] calls: drawarrays=%u drawelems=%u bufdata=%u clear=%u nulldata=%u prog=%u\n",
                  s_n_da, s_n_de, s_n_bd, s_n_clr, s_n_null, s_program);
        {
            extern int pc_emu64_frame_cmds, pc_emu64_frame_tri_cmds, pc_emu64_frame_vtx_cmds, pc_emu64_frame_dl_cmds;
            extern void** game_class_p;   /* GAME*: [0] graph, [1] exec, [3] next_game_init */
            xbox_logf("[NV2A] emu64 this frame: cmds=%d tri=%d vtx=%d dl=%d | game exec=%p next=%p disp_off=%d\n",
                      pc_emu64_frame_cmds, pc_emu64_frame_tri_cmds, pc_emu64_frame_vtx_cmds, pc_emu64_frame_dl_cmds,
                      game_class_p ? game_class_p[1] : NULL, game_class_p ? game_class_p[3] : NULL,
                      game_class_p ? ((unsigned char*)game_class_p)[0x9E] : -1);
        }
        xbox_mem_log("frame");
        { extern void xbox_aram_log(void); xbox_aram_log(); }
        { extern int pc_audio_get_buffer_fill(void); xbox_logf("[AUDIO] fill=%d\n", pc_audio_get_buffer_fill()); }
        xbox_fbdump(pb_back_buffer(), SCR_W, SCR_H, s_fb_bpp, (int)pb_back_buffer_pitch());
    }
    t_pace = xbox_ticks();
    while (pb_finished()) {}
    /* pbkit triple-buffers but only refuses a flip once its ready table is
     * full: with two flips queued, the next back buffer IS the one being
     * scanned out, and the next frame's clear shows as a black band rolling
     * up the screen (real hardware only; xemu doesn't model scanout). Wait
     * for vblank until the CRTC has moved off the buffer we're about to draw.
     * Kill switch: -DXBOX_NO_SCANOUT_WAIT. */
#ifndef XBOX_NO_SCANOUT_WAIT
    {
        int guard = 4;
        while (guard-- && (PCRTC_START_REG & 0x03FFFFFF) == ((uint32_t)pb_back_buffer() & 0x03FFFFFF))
            pb_wait_for_vbl();
    }
#endif
    vbl_pace();
    s_pace_ticks = xbox_ticks() - t_pace;
    hitch_log(t_enter, xbox_ticks());
    s_frame_open = 0;
    s_draws = 0;
    s_approx_draws = 0;
    if (!s_overlap) {
        release_deferred();
        frame_open();
    }
}

/* ======================================================================
 * glad loader
 * ====================================================================== */
typedef struct { const char* name; GLADapiproc fn; } Entry;
#define E(n, f) { n, (GLADapiproc)(f) }
static const Entry k_entries[] = {
    E("glGetUniformLocation", gl_get_uniform_location),
    E("glUniform1i", u1i), E("glUniform2i", u2i), E("glUniform3i", u3i), E("glUniform4i", u4i),
    E("glUniform1f", u1f), E("glUniform2f", u2f), E("glUniform3f", u3f), E("glUniform4f", u4f),
    E("glUniform1iv", u1iv), E("glUniform2iv", u2iv), E("glUniform3iv", u3iv), E("glUniform4iv", u4iv),
    E("glUniform1fv", u1fv), E("glUniform2fv", u2fv), E("glUniform3fv", u3fv), E("glUniform4fv", u4fv),
    E("glUniformMatrix3fv", um3), E("glUniformMatrix4fv", um4),
    E("glGenBuffers", gl_gen_objs), E("glGenVertexArrays", gl_gen_objs), E("glGenFramebuffers", gl_gen_objs),
    E("glGenTextures", gl_gen_textures), E("glDeleteTextures", gl_delete_textures),
    E("glCreateShader", gl_create), E("glCreateProgram", gl_create),
    E("glGetShaderiv", gl_getiv_status), E("glGetProgramiv", gl_getiv_status),
    E("glGetIntegerv", gl_get_integerv), E("glGetString", gl_get_string),
    E("glCheckFramebufferStatus", gl_fb_status),
    E("glUseProgram", gl_use_program),
    E("glActiveTexture", gl_active_texture), E("glBindTexture", gl_bind_texture),
    E("glTexParameteri", gl_tex_parameteri), E("glTexImage2D", gl_tex_image_2d),
    E("glBufferData", gl_buffer_data),
    E("glEnable", gl_enable), E("glDisable", gl_disable),
    E("glDepthFunc", gl_depth_func), E("glDepthMask", gl_depth_mask),
    E("glBlendFunc", gl_blend_func), E("glBlendEquation", gl_blend_equation),
    E("glCullFace", gl_cull_face), E("glColorMask", gl_color_mask),
    E("glScissor", gl_scissor), E("glViewport", gl_viewport), E("glDepthRange", gl_depth_range),
    E("glClearColor", gl_clear_color), E("glClearDepth", gl_clear_depth), E("glClear", gl_clear),
    E("glReadPixels", gl_read_pixels),
    E("glDrawArrays", gl_draw_arrays), E("glDrawElements", gl_draw_elements),
};

static GLADapiproc xbox_gl_getproc(const char* name) {
    size_t i;
    for (i = 0; i < sizeof k_entries / sizeof k_entries[0]; i++)
        if (strcmp(name, k_entries[i].name) == 0) return k_entries[i].fn;
    return (GLADapiproc)gl_noop;
}

int xbox_gl_nv2a_load(void) {
    int v = gladLoadGL(xbox_gl_getproc);
    xbox_logf("[NV2A] GL shim loaded (glad=%d)\n", v);
    return v;
}
