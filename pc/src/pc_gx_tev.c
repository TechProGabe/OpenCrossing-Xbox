#include "pc_gx_internal.h"
#include "pc_shader_seed.h"

int g_pc_uber_shader_only = 0; /* --uber-shader: disable specialization */

/* --- file I/O --- */

static char* load_text_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (read != (size_t)len) {
        fprintf(stderr, "WARNING: Partial read of %s (got %zu of %ld bytes)\n", path, read, len);
        free(buf);
        return NULL;
    }
    buf[read] = '\0';
    return buf;
}

static char* load_shader(const char* filename) {
    char path[512];
    snprintf(path, sizeof(path), "shaders/%s", filename);
    char* src = load_text_file(path);
    if (src) {
        printf("[PC/TEV] Loaded shader: %s\n", path);
    } else {
        fprintf(stderr, "FATAL: Could not load shader: %s\n", path);
    }
    return src;
}

/* --- compile / link --- */

static GLuint default_program = 0;
static GLuint s_vertex_shader = 0;   /* shared by all programs */
static char*  s_frag_base = NULL;    /* kept for variant generation */

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "WARNING: Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Links vs+fs. Deletes fs; vs is shared and kept alive. */
static GLuint link_program(GLuint vert, GLuint frag) {
    if (!vert || !frag) {
        if (frag) glDeleteShader(frag);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);

    glBindAttribLocation(prog, 0, "a_position");
    glBindAttribLocation(prog, 1, "a_normal");
    glBindAttribLocation(prog, 2, "a_color0");
    glBindAttribLocation(prog, 3, "a_texcoord0");

    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "WARNING: Program link error: %s\n", log);
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(frag);
    return prog;
}

/* Sampler units never change: set once per program. Caller restores the
 * previously bound program. */
static void set_sampler_uniforms(GLuint prog, const PCGXUloc* u) {
    glUseProgram(prog);
    if (u->texture0 >= 0) glUniform1i(u->texture0, 0);
    if (u->texture1 >= 0) glUniform1i(u->texture1, 1);
    if (u->texture2 >= 0) glUniform1i(u->texture2, 2);
    for (int i = 0; i < 4; i++) {
        if (u->ind_tex[i] >= 0) glUniform1i(u->ind_tex[i], 3 + i);
    }
}

/* --- variant cache --- */

#define PC_GX_SHADER_CACHE_SIZE 128

/* Dirty groups whose values feed the shader key */
#define PC_GX_SHADER_CONFIG_DIRTY \
    (PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_SWAP_TABLES | PC_GX_DIRTY_ALPHA_CMP | \
     PC_GX_DIRTY_LIGHTING | PC_GX_DIRTY_TEXTURES | PC_GX_DIRTY_INDIRECT | \
     PC_GX_DIRTY_FOG)

static PCGXShaderVariant s_uber;
static PCGXShaderVariant s_variants[PC_GX_SHADER_CACHE_SIZE];
static PCGXShaderVariant* s_current = NULL;
static int s_variant_count = 0;
static int s_cache_full_warned = 0;
static int s_precompiling = 0;

/* Keys seen in earlier runs are precompiled at boot so first encounters
 * don't hitch mid-game (driver GLSL compiles can take tens of ms) */
#define PC_GX_KEY_CACHE_FILE  "shader_cache.bin"
#define PC_GX_KEY_CACHE_MAGIC 0x41435356u /* ACSV */

static void key_cache_append(const PCGXShaderKey* k) {
    FILE* f = fopen(PC_GX_KEY_CACHE_FILE, "r+b");
    if (!f) {
        f = fopen(PC_GX_KEY_CACHE_FILE, "wb");
        if (!f) return;
        u32 hdr[2] = { PC_GX_KEY_CACHE_MAGIC, (u32)sizeof(PCGXShaderKey) };
        fwrite(hdr, sizeof(hdr), 1, f);
    }
    fseek(f, 0, SEEK_END);
    fwrite(k, sizeof(*k), 1, f);
    fclose(f);
}

static void variant_init(PCGXShaderVariant* v, GLuint prog) {
    v->used = 1;
    v->prog = prog;
    pc_gx_cache_uniform_locations(prog, &v->uloc);
    memset(v->uploaded_seq, 0xFF, sizeof(v->uploaded_seq));
    set_sampler_uniforms(prog, &v->uloc);
}

static void pc_gx_build_shader_key(PCGXShaderKey* k) {
    memset(k, 0, sizeof(*k));

    int ns = g_gx.num_tev_stages;
    if (ns < 0) ns = 0;
    if (ns > PC_GX_MAX_TEV_STAGES) ns = PC_GX_MAX_TEV_STAGES;
    k->num_stages = (u8)ns;

    int ni = g_gx.num_ind_stages;
    if (ni < 0) ni = 0;
    if (ni > 4) ni = 4;
    k->num_ind = (u8)ni;

    k->fog_enable = g_gx.fog_type != 0;

    k->alpha[0] = (u8)g_gx.alpha_comp0;
    k->alpha[1] = (u8)g_gx.alpha_ref0;
    k->alpha[2] = (u8)g_gx.alpha_op;
    k->alpha[3] = (u8)g_gx.alpha_comp1;
    k->alpha[4] = (u8)g_gx.alpha_ref1;

    k->light[0] = (u8)(g_gx.chan_ctrl_enable[0] != 0);
    k->light[1] = (u8)g_gx.chan_ctrl_mat_src[0];
    k->light[2] = (u8)g_gx.chan_ctrl_amb_src[0];
    k->light[3] = (u8)g_gx.num_chans;
    k->light[4] = (u8)(g_gx.chan_ctrl_enable[1] != 0);
    k->light[5] = (u8)g_gx.chan_ctrl_mat_src[1];
    k->light[6] = (u8)g_gx.chan_ctrl_light_mask[0];

    for (int t = 0; t < 4; t++) {
        k->swap_tbl[t * 4 + 0] = (u8)g_gx.tev_swap_table[t].r;
        k->swap_tbl[t * 4 + 1] = (u8)g_gx.tev_swap_table[t].g;
        k->swap_tbl[t * 4 + 2] = (u8)g_gx.tev_swap_table[t].b;
        k->swap_tbl[t * 4 + 3] = (u8)g_gx.tev_swap_table[t].a;
    }

    for (int s = 0; s < ns; s++) {
        const PCGXTevStage* ts = &g_gx.tev_stages[s];
        k->st[s].cin[0] = (u8)ts->color_a;
        k->st[s].cin[1] = (u8)ts->color_b;
        k->st[s].cin[2] = (u8)ts->color_c;
        k->st[s].cin[3] = (u8)ts->color_d;
        k->st[s].ain[0] = (u8)ts->alpha_a;
        k->st[s].ain[1] = (u8)ts->alpha_b;
        k->st[s].ain[2] = (u8)ts->alpha_c;
        k->st[s].ain[3] = (u8)ts->alpha_d;
        k->st[s].cop = (u8)ts->color_op;
        k->st[s].aop = (u8)ts->alpha_op;
        k->st[s].bsc[0] = (u8)ts->color_bias;
        k->st[s].bsc[1] = (u8)ts->color_scale;
        k->st[s].bsc[2] = (u8)ts->alpha_bias;
        k->st[s].bsc[3] = (u8)ts->alpha_scale;
        k->st[s].outc[0] = (u8)(ts->color_clamp != 0);
        k->st[s].outc[1] = (u8)(ts->alpha_clamp != 0);
        k->st[s].outc[2] = (u8)ts->color_out;
        k->st[s].outc[3] = (u8)ts->alpha_out;
        k->st[s].swap[0] = (u8)ts->ras_swap;
        k->st[s].swap[1] = (u8)ts->tex_swap;
        k->st[s].ksel[0] = (u8)ts->k_color_sel;
        k->st[s].ksel[1] = (u8)ts->k_alpha_sel;
        k->st[s].tc_src = (u8)pc_gx_tc_src_normalize(ts->tex_coord, s);
        k->st[s].use_tex = (u8)(ts->tex_map >= 0 && ts->tex_map < 8 &&
                                g_gx.gl_textures[ts->tex_map] != 0);
        if (ni > 0) {
            k->st[s].ind[0] = (u8)ts->ind_stage;
            k->st[s].ind[1] = (u8)ts->ind_mtx;
            k->st[s].ind[2] = (u8)ts->ind_bias;
            k->st[s].ind[3] = (u8)ts->ind_alpha;
            k->st[s].ind[4] = (u8)ts->ind_wrap_s;
            k->st[s].ind[5] = (u8)ts->ind_wrap_t;
            k->st[s].ind[6] = (u8)ts->ind_add_prev;
        }
    }
}

/* --- specialized source generation --- */

/* Uniforms replaced by generated const declarations */
static const char* const s_folded_uniforms[] = {
    "u_num_tev_stages", "u_tev_color_in", "u_tev_alpha_in", "u_tev_color_op",
    "u_tev_alpha_op", "u_tev_tc_src", "u_tev_bsc", "u_tev_out", "u_tev_swap",
    "u_tev_ksel", "u_tev_ind_cfg", "u_tev_ind_wrap", "u_use_texture",
    "u_lighting_cfg0", "u_lighting_cfg1", "u_swap_table", "u_alpha_cmp",
    "u_alpha_ref1", "u_num_ind_stages", "u_fog_enable", NULL
};

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* True if this source line declares one of the folded uniforms */
static int line_declares_folded_uniform(const char* line, const char* line_end) {
    const char* p = line;
    while (p < line_end && (*p == ' ' || *p == '\t')) p++;
    if (line_end - p < 8 || strncmp(p, "uniform", 7) != 0) return 0;

    for (int i = 0; s_folded_uniforms[i]; i++) {
        const char* name = s_folded_uniforms[i];
        size_t nlen = strlen(name);
        for (const char* q = p; q + nlen <= line_end; q++) {
            if (memcmp(q, name, nlen) == 0 &&
                !is_ident_char(q[-1]) &&
                (q + nlen == line_end || !is_ident_char(q[nlen]))) {
                return 1;
            }
        }
    }
    return 0;
}

#define EMIT(...) do { \
    n = snprintf(buf + pos, cap - pos, __VA_ARGS__); \
    if (n < 0 || (size_t)n >= cap - pos) return -1; \
    pos += (size_t)n; \
} while (0)

static int emit_const_block(char* buf, size_t cap, const PCGXShaderKey* k) {
    size_t pos = 0;
    int n;

    EMIT("const int u_num_tev_stages = %d;\n", k->num_stages);

    EMIT("const ivec4 u_tev_color_in[3] = ivec4[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->st[s].cin[0], k->st[s].cin[1],
             k->st[s].cin[2], k->st[s].cin[3], s < 2 ? "," : ");\n");

    EMIT("const ivec4 u_tev_alpha_in[3] = ivec4[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->st[s].ain[0], k->st[s].ain[1],
             k->st[s].ain[2], k->st[s].ain[3], s < 2 ? "," : ");\n");

    EMIT("const int u_tev_color_op[3] = int[3](%d,%d,%d);\n",
         k->st[0].cop, k->st[1].cop, k->st[2].cop);
    EMIT("const int u_tev_alpha_op[3] = int[3](%d,%d,%d);\n",
         k->st[0].aop, k->st[1].aop, k->st[2].aop);
    EMIT("const int u_tev_tc_src[3] = int[3](%d,%d,%d);\n",
         k->st[0].tc_src, k->st[1].tc_src, k->st[2].tc_src);

    EMIT("const ivec4 u_tev_bsc[3] = ivec4[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->st[s].bsc[0], k->st[s].bsc[1],
             k->st[s].bsc[2], k->st[s].bsc[3], s < 2 ? "," : ");\n");

    EMIT("const ivec4 u_tev_out[3] = ivec4[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->st[s].outc[0], k->st[s].outc[1],
             k->st[s].outc[2], k->st[s].outc[3], s < 2 ? "," : ");\n");

    EMIT("const ivec2 u_tev_swap[3] = ivec2[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec2(%d,%d)%s", k->st[s].swap[0], k->st[s].swap[1],
             s < 2 ? "," : ");\n");

    EMIT("const ivec3 u_tev_ksel[3] = ivec3[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec3(%d,%d,%d)%s", k->st[s].ksel[0], k->st[s].ksel[1], s,
             s < 2 ? "," : ");\n");

    EMIT("const ivec4 u_tev_ind_cfg[3] = ivec4[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->st[s].ind[0], k->st[s].ind[1],
             k->st[s].ind[2], k->st[s].ind[3], s < 2 ? "," : ");\n");

    EMIT("const ivec3 u_tev_ind_wrap[3] = ivec3[3](");
    for (int s = 0; s < 3; s++)
        EMIT("ivec3(%d,%d,%d)%s", k->st[s].ind[4], k->st[s].ind[5],
             k->st[s].ind[6], s < 2 ? "," : ");\n");

    EMIT("const int u_use_texture[3] = int[3](%d,%d,%d);\n",
         k->st[0].use_tex, k->st[1].use_tex, k->st[2].use_tex);

    EMIT("const ivec4 u_lighting_cfg0 = ivec4(%d,%d,%d,%d);\n",
         k->light[0], k->light[1], k->light[2], k->light[3]);
    EMIT("const ivec4 u_lighting_cfg1 = ivec4(%d,%d,%d,0);\n",
         k->light[4], k->light[5], k->light[6]);

    EMIT("const ivec4 u_swap_table[4] = ivec4[4](");
    for (int t = 0; t < 4; t++)
        EMIT("ivec4(%d,%d,%d,%d)%s", k->swap_tbl[t*4+0], k->swap_tbl[t*4+1],
             k->swap_tbl[t*4+2], k->swap_tbl[t*4+3], t < 3 ? "," : ");\n");

    EMIT("const ivec4 u_alpha_cmp = ivec4(%d,%d,%d,%d);\n",
         k->alpha[0], k->alpha[1], k->alpha[2], k->alpha[3]);
    EMIT("const int u_alpha_ref1 = %d;\n", k->alpha[4]);
    EMIT("const int u_num_ind_stages = %d;\n", k->num_ind);
    EMIT("const int u_fog_enable = %d;\n", k->fog_enable);

    return (int)pos;
}

#undef EMIT

/* Builds the specialized fragment source: #version line + const block + the
 * base source with the #version line and folded uniform decls stripped. */
static char* build_specialized_source(const PCGXShaderKey* k) {
    char consts[4096];
    int clen = emit_const_block(consts, sizeof(consts), k);
    if (clen < 0) return NULL;

    size_t base_len = strlen(s_frag_base);
    char* out = (char*)malloc(base_len + (size_t)clen + 64);
    if (!out) return NULL;

    size_t pos = 0;
    memcpy(out + pos, "#version 330 core\n", 18); pos += 18;
    memcpy(out + pos, consts, (size_t)clen); pos += (size_t)clen;

    const char* p = s_frag_base;
    while (*p) {
        const char* nl = strchr(p, '\n');
        const char* line_end = nl ? nl : p + strlen(p);
        int skip = (strncmp(p, "#version", 8) == 0) ||
                   line_declares_folded_uniform(p, line_end);
        if (!skip) {
            size_t len = (size_t)(line_end - p);
            memcpy(out + pos, p, len);
            pos += len;
            out[pos++] = '\n';
        }
        p = nl ? nl + 1 : line_end;
    }
    out[pos] = '\0';
    return out;
}

static PCGXShaderVariant* compile_variant(const PCGXShaderKey* k) {
    PCGXShaderVariant* v = &s_variants[s_variant_count++];
    memset(v, 0, sizeof(*v));

    GLuint prog = 0;
    char* src = build_specialized_source(k);
    if (src) {
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, src);
        prog = link_program(s_vertex_shader, fs);
        free(src);
    }

    if (!prog) {
        /* Fall back to the uber program for this config; entry stays cached
         * so the failing compile isn't retried every flush */
        fprintf(stderr, "WARNING: shader variant compile failed, using uber shader\n");
        prog = default_program;
    } else if (!s_precompiling) {
        key_cache_append(k);
    }
    if (prog != default_program && g_pc_verbose) {
        printf("[PC/TEV] shader variant %d compiled (stages=%d tex=%d%d%d light=%d ind=%d fog=%d)\n",
               s_variant_count, k->num_stages,
               k->st[0].use_tex, k->st[1].use_tex, k->st[2].use_tex,
               k->light[0], k->num_ind, k->fog_enable);
    }

    variant_init(v, prog);
    v->key = *k;
    /* variant_init bound the new program for sampler setup; restore so a
     * still-deferred draw isn't affected */
    glUseProgram(g_gx.current_shader);
    return v;
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    if (g_pc_uber_shader_only || !s_frag_base)
        return &s_uber;

    /* Key can only change when a config group was dirtied */
    if (s_current && !(g_gx.dirty & PC_GX_SHADER_CONFIG_DIRTY))
        return s_current;

    PCGXShaderKey key;
    pc_gx_build_shader_key(&key);

    if (s_current && memcmp(&key, &s_current->key, sizeof(key)) == 0)
        return s_current;

    for (int i = 0; i < s_variant_count; i++) {
        if (memcmp(&key, &s_variants[i].key, sizeof(key)) == 0) {
            s_current = &s_variants[i];
            return s_current;
        }
    }

    if (s_variant_count >= PC_GX_SHADER_CACHE_SIZE) {
        if (!s_cache_full_warned) {
            fprintf(stderr, "WARNING: shader variant cache full (%d), new configs use uber shader\n",
                    PC_GX_SHADER_CACHE_SIZE);
            s_cache_full_warned = 1;
        }
        s_current = &s_uber;
        return s_current;
    }

    s_current = compile_variant(&key);
    return s_current;
}

/* Compiles the key unless already cached. Returns 1 if compiled. */
static int precompile_key(const PCGXShaderKey* key) {
    if (s_variant_count >= PC_GX_SHADER_CACHE_SIZE) return 0;
    for (int i = 0; i < s_variant_count; i++) {
        if (memcmp(key, &s_variants[i].key, sizeof(*key)) == 0) return 0;
    }
    compile_variant(key);
    return 1;
}

static void pc_gx_tev_precompile_cached(void) {
    if (g_pc_uber_shader_only) return;

    int loaded = 0;
    Uint32 t0 = SDL_GetTicks();
    s_precompiling = 1;

    /* Built-in seed: configs seen during development, so a fresh install
     * gets no first-encounter compile hitches either */
    if (PC_SHADER_SEED_KEY_SIZE == (int)sizeof(PCGXShaderKey)) {
        for (int i = 0; i < PC_SHADER_SEED_COUNT; i++) {
            PCGXShaderKey key;
            memcpy(&key, pc_shader_seed_keys[i], sizeof(key));
            loaded += precompile_key(&key);
        }
    } else {
        fprintf(stderr, "WARNING: pc_shader_seed.h key size mismatch, regenerate with gen_shader_seed.py\n");
    }

    /* Local cache: configs this machine discovered beyond the seed */
    FILE* f = fopen(PC_GX_KEY_CACHE_FILE, "rb");
    if (f) {
        u32 hdr[2] = { 0, 0 };
        if (fread(hdr, sizeof(hdr), 1, f) != 1 ||
            hdr[0] != PC_GX_KEY_CACHE_MAGIC || hdr[1] != (u32)sizeof(PCGXShaderKey)) {
            /* Stale format (key struct changed): drop the file */
            fclose(f);
            f = NULL;
            remove(PC_GX_KEY_CACHE_FILE);
            printf("[PC/TEV] shader key cache format changed, discarded\n");
        } else {
            PCGXShaderKey key;
            while (fread(&key, sizeof(key), 1, f) == 1) {
                loaded += precompile_key(&key);
            }
            fclose(f);
        }
    }

    s_precompiling = 0;
    if (loaded) printf("[PC/TEV] precompiled %d shader variants in %lums\n",
                       loaded, (unsigned long)(SDL_GetTicks() - t0));
}

/* --- init / shutdown --- */

void pc_gx_tev_init(void) {
    char* vs_src = load_shader("default.vert");
    char* fs_src = load_shader("default.frag");

    if (!vs_src || !fs_src) {
        fprintf(stderr, "FATAL: Shader files missing from shaders/ directory.\n"
                        "Expected: shaders/default.vert and shaders/default.frag\n"
                        "Make sure shader files are next to the executable.\n");
        free(vs_src);
        free(fs_src);
        exit(1);
    }

    s_vertex_shader = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    default_program = link_program(s_vertex_shader, fs);
    free(vs_src);

    if (!default_program) {
        fprintf(stderr, "FATAL: uber shader failed to compile/link.\n");
        free(fs_src);
        exit(1);
    }

    s_frag_base = fs_src;

    memset(&s_uber, 0, sizeof(s_uber));
    variant_init(&s_uber, default_program);
    glUseProgram(0);

    pc_gx_tev_precompile_cached();
}

void pc_gx_tev_shutdown(void) {
    for (int i = 0; i < s_variant_count; i++) {
        if (s_variants[i].prog && s_variants[i].prog != default_program)
            glDeleteProgram(s_variants[i].prog);
    }
    s_variant_count = 0;
    s_current = NULL;

    if (default_program) {
        glDeleteProgram(default_program);
        default_program = 0;
    }
    if (s_vertex_shader) {
        glDeleteShader(s_vertex_shader);
        s_vertex_shader = 0;
    }
    free(s_frag_base);
    s_frag_base = NULL;
}
