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

#define SCR_W 640
#define SCR_H 480
#define ZMAX  16777215.0f

#define SETF(x) (*(const uint32_t*)&(x))

#ifndef XBOX_FBDUMP_EVERY
#define XBOX_FBDUMP_EVERY 0
#endif
int g_xbox_fbdump_every = XBOX_FBDUMP_EVERY;
static uint32_t s_n_da, s_n_de, s_n_bd, s_n_clr, s_n_null;

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

static void set_f(GLint loc, int comps, int count, const GLfloat* v) {
    int room, n, i;
    UVal* d = uslot(loc, &room);
    if (!d) return;
    n = comps * count;
    if (n > room) n = room;
    for (i = 0; i < n; i++) d[i].f = v[i];
}

static void set_i(GLint loc, int comps, int count, const GLint* v) {
    int room, n, i;
    UVal* d = uslot(loc, &room);
    if (!d) return;
    n = comps * count;
    if (n > room) n = room;
    for (i = 0; i < n; i++) d[i].i = v[i];
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
#define POOL_ALIGN 128

typedef struct Blk { uint32_t off, size; int free; struct Blk* next; } Blk;
static uint8_t* s_pool;
static Blk* s_blocks;
static uint32_t s_pool_used, s_pool_peak;

static void pool_init(void) {
    s_pool = (uint8_t*)MmAllocateContiguousMemoryEx(XBOX_TEX_POOL_BYTES, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    s_blocks = (Blk*)calloc(1, sizeof(Blk));
    s_blocks->size = XBOX_TEX_POOL_BYTES;
    s_blocks->free = 1;
}

static void* pool_alloc(uint32_t size) {
    Blk* b;
    size = (size + POOL_ALIGN - 1) & ~(uint32_t)(POOL_ALIGN - 1);
    for (b = s_blocks; b; b = b->next) {
        if (!b->free || b->size < size) continue;
        if (b->size > size) {
            Blk* n = (Blk*)calloc(1, sizeof(Blk));
            n->off = b->off + size;
            n->size = b->size - size;
            n->free = 1;
            n->next = b->next;
            b->next = n;
            b->size = size;
        }
        b->free = 0;
        s_pool_used += size;
        if (s_pool_used > s_pool_peak) s_pool_peak = s_pool_used;
        return s_pool + b->off;
    }
    return NULL;
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
    0, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0.0f, 1.0f, {0, 0, 0, 0}, 1.0f
};

/* ======================================================================
 * Push helpers
 * ====================================================================== */
static uint32_t* P;
#define PB_BEGIN() (P = pb_begin())
#define PB_END() pb_end(P)
static inline void put1(uint32_t m, uint32_t v) { P = pb_push1(P, m, v); }
static inline void putf(uint32_t m, float v) { P = pb_push1(P, m, SETF(v)); }

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
/* The GPU may still read a texture this frame: free after the flip. If the
 * list is full, drain the GPU and free everything now rather than leak. */
static void defer_free(void* p) {
    if (s_ndeferred >= (int)(sizeof s_deferred_free / sizeof s_deferred_free[0])) {
        int i;
        wait_idle();
        for (i = 0; i < s_ndeferred; i++) pool_free(s_deferred_free[i]);
        s_ndeferred = 0;
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
    }
}

static void gl_active_texture(GLenum t) { s_active_unit = (int)(t - GL_TEXTURE0) & 7; }
static void gl_bind_texture(GLenum target, GLuint id) { (void)target; s_bound[s_active_unit] = id < MAX_TEX ? id : 0; }

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
}

static uint32_t s_tex_fail;

static void gl_tex_image_2d(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                            GLenum fmt, GLenum type, const void* data) {
    GLuint id = s_bound[s_active_unit];
    XTex* t;
    int pw, ph, x, y;
    uint32_t* dst;
    const uint8_t* src = (const uint8_t*)data;
    (void)target; (void)ifmt; (void)border; (void)fmt; (void)type;
    if (level != 0 || !id || !s_tex[id].used || w <= 0 || h <= 0 || w > 1024 || h > 1024) return;
    t = &s_tex[id];
    if (t->mem) { defer_free(t->mem); t->mem = NULL; }
    pw = pot(w);
    ph = pot(h);
    dst = (uint32_t*)pool_alloc((uint32_t)(pw * ph * 4));
    if (!dst) {
        if ((s_tex_fail++ & 255) == 0) xbox_logf("[NV2A] texture pool full (%u used), %dx%d dropped\n", s_pool_used, w, h);
        return;
    }
    t->w = w; t->h = h; t->pw = pw; t->ph = ph; t->mem = dst;
    swz_tables(pw, ph);
    for (y = 0; y < ph; y++) {
        int sy = y < h ? y : h - 1;   /* pad by edge replication */
        uint32_t yo = swz_y[y];
        const uint8_t* row = src ? src + (size_t)sy * (size_t)w * 4 : NULL;
        for (x = 0; x < pw; x++) {
            int sx = x < w ? x : w - 1;
            uint32_t argb = 0xFFFFFFFFu;
            if (row) {
                const uint8_t* p = row + sx * 4;
                argb = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            }
            dst[yo | swz_x[x]] = argb;
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
static void gl_scissor(GLint x, GLint y, GLsizei w, GLsizei h) { G.sx = x; G.sy = y; G.sw = w; G.sh = h; }
static void gl_viewport(GLint x, GLint y, GLsizei w, GLsizei h) { G.vx = x; G.vy = y; G.vw = w; G.vh = h; }
static void gl_depth_range(GLdouble n, GLdouble f) { G.dn = (float)n; G.df = (float)f; }
static void gl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    G.clear_c[0] = r; G.clear_c[1] = g; G.clear_c[2] = b; G.clear_c[3] = a;
}
static void gl_clear_depth(GLdouble d) { G.clear_d = (float)d; }

static int s_frame_open;
static void frame_open(void);

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
    s_n_clr++;
    clear_rect(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t c = ((uint32_t)f2b(G.clear_c[3]) << 24) | ((uint32_t)f2b(G.clear_c[0]) << 16) |
                     ((uint32_t)f2b(G.clear_c[1]) << 8) | f2b(G.clear_c[2]);
        pb_fill(x, y, w, h, c);
    }
    if (mask & GL_DEPTH_BUFFER_BIT) pb_erase_depth_stencil_buffer(x, y, w, h);
}

static void wait_idle(void) {
    while (pb_busy()) {}
}

static void gl_read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* out) {
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
        int sy = SCR_H - 1 - (y + r);
        for (c = 0; c < w; c++) {
            int sx = x + c;
            uint8_t* d = o + ((size_t)r * w + c) * 4;
            if (sx < 0 || sx >= SCR_W || sy < 0 || sy >= SCR_H) { d[0] = d[1] = d[2] = 0; d[3] = 255; continue; }
            {
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
static uint32_t s_draws, s_approx_draws, s_frame;

typedef struct { XTevCfg cfg; XRcProg prog; } RcEntry;
#define RC_CACHE 256
static RcEntry s_rc_cache[RC_CACHE];
static int s_rc_count;

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

static const XRcProg* rc_lookup(const XTevCfg* cfg) {
    int k;
    for (k = 0; k < s_rc_count; k++)
        if (memcmp(&s_rc_cache[k].cfg, cfg, sizeof *cfg) == 0) return &s_rc_cache[k].prog;
    k = s_rc_count < RC_CACHE ? s_rc_count++ : (int)(s_draws % RC_CACHE);
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

static void emit_combiners(const XRcProg* rp) {
    int i;
    uint32_t cst[XRC_MAX_STAGES][2], fc[2];
    for (i = 0; i < rp->nstages; i++) {
        cst[i][0] = pack_const(rp->cref[i][0], rp->cref[i][1]);
        cst[i][1] = pack_const(rp->cref[i][2], rp->cref[i][3]);
    }
    fc[0] = pack_const(rp->fref[0], rp->fref[1]);
    fc[1] = pack_const(rp->fref[2], rp->fref[3]);

    if (!s_rc_valid || memcmp(&s_rc_cur, rp, sizeof *rp) != 0) {
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
            v[1] = 1 /* DMA A */ | (1u << 3) /* border from colour: no border texels in the image */ | (2u << 4) /* 2D */ | (NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8 << 8) |
                   (1u << 16) /* 1 mip level */ | ((uint32_t)log2i(t->pw) << 20) | ((uint32_t)log2i(t->ph) << 24);
            v[2] = wrap_mode(t->wrap_s) | (wrap_mode(t->wrap_t) << 8) | (3u << 16);
            v[3] = 0x4003FFC0u;   /* ENABLE | MAX_LOD_CLAMP (the nxdk mesh sample's value) */
            v[4] = (uint32_t)(t->pw * 4) << 16;
            v[5] = 0x2000u | (filt_min << 16) | (filt_mag << 24);
            v[6] = ((uint32_t)t->pw << 16) | (uint32_t)t->ph;
            scale[s][0] = (float)t->w / (float)t->pw;
            scale[s][1] = (float)t->h / (float)t->ph;
            prog |= 1u << (s * 5);   /* 2D_PROJECTIVE */
        }
        if (memcmp(v, s_tex_shadow[s], sizeof v) != 0) {
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

static void build_vconsts(float vc[41][4], const float scale[3][2]) {
    float proj[16];
    int i, s;
    memset(vc, 0, sizeof(float) * 41 * 4);
    mat4_rows_mul(&UF(U_PROJ, 0, 0), proj);
    memcpy(vc[0], proj, sizeof proj);
    for (i = 0; i < 3; i++) {
        vc[4 + i][0] = UF(U_MV, 0, i * 4 + 0); vc[4 + i][1] = UF(U_MV, 0, i * 4 + 1);
        vc[4 + i][2] = UF(U_MV, 0, i * 4 + 2); vc[4 + i][3] = UF(U_MV, 0, i * 4 + 3);
        vc[7 + i][0] = UF(U_NRM, 0, i * 3 + 0); vc[7 + i][1] = UF(U_NRM, 0, i * 3 + 1);
        vc[7 + i][2] = UF(U_NRM, 0, i * 3 + 2);
    }
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
    {
        int mask = UI(U_LCFG1, 0, 2);
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

static void emit_vconsts(const XTevCfg* c, const float scale[3][2]) {
    float vc[41][4];
    (void)c;
    build_vconsts(vc, scale);
    if (s_vc_valid && memcmp(vc, s_shadow_vc, sizeof vc) == 0) return;
    put1(NV097_SET_TRANSFORM_CONSTANT_LOAD, 96);
    {
        int i;
        const uint32_t* w = (const uint32_t*)vc;
        for (i = 0; i < 41 * 4; i += 32) {
            int n = 41 * 4 - i < 32 ? 41 * 4 - i : 32;
            pb_push(P++, NV097_SET_TRANSFORM_CONSTANT, n);
            memcpy(P, w + i, (size_t)n * 4);
            P += n;
        }
    }
    memcpy(s_shadow_vc, vc, sizeof vc);
    s_vc_valid = 1;
}

static void emit_fixed(void) {
    static int last[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
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
        v = (x & 0xFFF) | ((y & 0xFFF) << 12) | (((w > 0 ? w : 0) & 0x7FF) << 24);
        if (last[10] != v || last[11] != h) {
            last[10] = v;
            last[11] = h;
            if (w <= 0 || h <= 0) { x = y = 0; w = h = 1; }
            put1(NV097_SET_WINDOW_CLIP_HORIZONTAL, (uint32_t)x | ((uint32_t)(x + w) << 16));
            put1(NV097_SET_WINDOW_CLIP_VERTICAL, (uint32_t)y | ((uint32_t)(y + h) << 16));
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
        PB_END();
        wait_idle();
        PB_BEGIN();
        s_ring_pos = 0;
    }
}

static int s_logged_nes;

static void draw(GLenum mode, int count) {
    const uint8_t* src = (const uint8_t*)s_array_data;
    XTevCfg cfg;
    const XRcProg* rp;
    float scale[3][2];
    int i;
    uint32_t start;
    uint32_t prim;

    if (count <= 0 || !src) return;
    if (s_program != s_uber_prog) {
        if (!s_logged_nes++) xbox_logf("[NV2A] draw with non-GX program %u skipped (NES path not ported)\n", s_program);
        return;
    }
    frame_open();
    if ((uint32_t)count > s_ring_cap) count = (int)s_ring_cap;

    build_tev_cfg(&cfg);
    rp = rc_lookup(&cfg);
    if (rp->approximated) s_approx_draws++;

    PB_BEGIN();
    emit_fixed();
    emit_textures(&cfg, scale);
    emit_vconsts(&cfg, scale);
    emit_combiners(rp);
    PB_END();

    PB_BEGIN();
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
        const XTex* t0 = cfg.st[0].use_tex ? &s_tex[s_bound[0]] : NULL;
        int k;
        float ez = 0.0f;
        for (k = 0; k < 4; k++) ez += UF(U_MV, 0, 8 + k) * (k < 3 ? v0->pos[k] : 1.0f);
        xbox_logf("[DRAW] %u m%d n%d z%d/%d/%d b%d %d/%d c%d/%d st%d tex%dx%d fog%d proj00 %d p0 %d,%d,%d ez %d col %08x\n",
                  s_draws, (int)mode, count, G.depth_test, G.depth_func & 0xF, G.depth_mask, G.blend, G.sfac, G.dfac,
                  G.cull, G.cull_face & 0xF, cfg.nstages, t0 ? t0->w : 0, t0 ? t0->h : 0, cfg.fog_on,
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
    PB_END();
    s_draws++;
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

static void frame_open(void) {
    if (s_frame_open) return;
    pb_reset();
    pb_target_back_buffer();
    s_ring_pos = 0;
    s_frame_open = 1;
}

int xbox_nv2a_init(void) {
    int err;
    pb_size(1024 * 1024);
    err = pb_init();
    if (err) {
        xbox_logf("[NV2A] pb_init failed: %d\n", err);
        return 0;
    }
    pb_show_front_screen();
    pool_init();
    s_ring_cap = XBOX_VTX_RING_BYTES / sizeof(XVtx);
    s_ring = (XVtx*)MmAllocateContiguousMemoryEx(XBOX_VTX_RING_BYTES, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!s_pool || !s_ring) {
        xbox_logf("[NV2A] contiguous alloc failed (pool=%p ring=%p)\n", s_pool, s_ring);
        return 0;
    }
    frame_open();
    load_vertex_program();
    setup_attributes();
    setup_state();
    xbox_logf("[NV2A] up: tex pool %u KB, vertex ring %u verts\n", XBOX_TEX_POOL_BYTES / 1024, s_ring_cap);
    return 1;
}

void xbox_nv2a_present(void) {
    int i;
    frame_open();
    wait_idle();
    s_frame++;
    if (g_xbox_fbdump_every > 0 && (s_frame % (uint32_t)g_xbox_fbdump_every) == 0) {
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
        xbox_fbdump(pb_back_buffer(), SCR_W, SCR_H, 32, (int)pb_back_buffer_pitch());
    }
    while (pb_finished()) {}
    for (i = 0; i < s_ndeferred; i++) pool_free(s_deferred_free[i]);
    s_ndeferred = 0;
    s_frame_open = 0;
    s_draws = 0;
    s_approx_draws = 0;
    frame_open();
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
