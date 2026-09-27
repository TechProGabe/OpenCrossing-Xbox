/* xbox_gl_stub.c — headless GL for bring-up (M2).
 *
 * pc_gx*.c still talk to OpenGL through glad's function pointers. There is no
 * GL on nxdk, so gladLoadGL() is fed this loader: every entry point becomes a
 * cdecl no-op (xbox/include/glad/gl.h forces APIENTRY to cdecl, so one
 * variadic-safe stub fits any signature — the caller cleans the stack), except
 * the handful whose outputs pc_gx reads back. Replaced by the NV2A backend. */
#include <string.h>
#include <glad/gl.h>
#include "xbox_io.h"

static GLuint s_next_id = 1;

static int stub_noop(void) { return 0; }

static void stub_gen(GLsizei n, GLuint* ids) {
    GLsizei i;
    for (i = 0; i < n; i++) ids[i] = s_next_id++;
}

static GLuint stub_create(void) { return s_next_id++; }

/* glGetShaderiv / glGetProgramiv: report success for COMPILE/LINK_STATUS,
 * zero-length logs for INFO_LOG_LENGTH. */
static void stub_getiv(GLuint obj, GLenum pname, GLint* p) {
    (void)obj;
    if (p) *p = (pname == GL_COMPILE_STATUS || pname == GL_LINK_STATUS) ? 1 : 0;
}

static void stub_get_integerv(GLenum pname, GLint* p) {
    (void)pname;
    if (p) *p = 0;
}

static const GLubyte* stub_get_string(GLenum name) {
    (void)name;
    /* glad parses "%d.%d" out of GL_VERSION to decide which pointers to load */
    return (const GLubyte*)(name == GL_VERSION ? "3.3 OpenCrossing-Xbox headless" : "OpenCrossing-Xbox");
}

static GLint stub_uniform_location(GLuint prog, const GLchar* name) {
    (void)prog; (void)name;
    return 0;
}

static GLenum stub_fb_status(GLenum target) {
    (void)target;
    return GL_FRAMEBUFFER_COMPLETE;
}

static GLADapiproc xbox_gl_getproc(const char* name) {
    if (!strncmp(name, "glGen", 5)) return (GLADapiproc)stub_gen;
    if (!strcmp(name, "glCreateShader") || !strcmp(name, "glCreateProgram")) return (GLADapiproc)stub_create;
    if (!strcmp(name, "glGetShaderiv") || !strcmp(name, "glGetProgramiv")) return (GLADapiproc)stub_getiv;
    if (!strcmp(name, "glGetIntegerv")) return (GLADapiproc)stub_get_integerv;
    if (!strcmp(name, "glGetString")) return (GLADapiproc)stub_get_string;
    if (!strcmp(name, "glGetUniformLocation")) return (GLADapiproc)stub_uniform_location;
    if (!strcmp(name, "glCheckFramebufferStatus")) return (GLADapiproc)stub_fb_status;
    return (GLADapiproc)stub_noop;
}

int xbox_gl_stub_load(void) {
    int v = gladLoadGL(xbox_gl_getproc);
    xbox_logf("[XBOX] headless GL stub loaded (glad=%d)\n", v);
    return v;
}
