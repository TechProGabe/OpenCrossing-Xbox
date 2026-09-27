/* xbox_io.h — path resolution + COM1 logging (xbox/src/xbox_io.c). */
#ifndef XBOX_IO_H
#define XBOX_IO_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The XBE's own folder. nxdk mounts it as D: for HDD launches and it is the
 * DVD for disc launches (verified in xemu 2026-09-27: D:\default.xbe opens). */
#define XBOX_DISC_DIR  "D:\\"
/* Saves + settings. Always on the HDD, so a burned-DVD boot can still save. */
#define XBOX_UDATA_ROOT "E:\\UDATA\\4f430001"
#define XBOX_UDATA_DIR  XBOX_UDATA_ROOT "\\"

#ifndef XBOX_LOG_DEFAULT
#define XBOX_LOG_DEFAULT 1
#endif
extern int g_xbox_log;

enum { XBOX_PATH_READ, XBOX_PATH_WRITE, XBOX_PATH_DISC };
const char* xbox_resolve(const char* in, int mode, char* out, size_t cap);

void xbox_log_write(const char* s, size_t n);
int  xbox_logf(const char* fmt, ...);
int  xbox_vlogf(const char* fmt, va_list ap);

FILE* xbox_fopen(const char* path, const char* mode);
int   xbox_remove(const char* path);
int   xbox_rename(const char* from, const char* to);
int   xbox_printf(const char* fmt, ...);
int   xbox_vprintf(const char* fmt, va_list ap);
int   xbox_fprintf(FILE* f, const char* fmt, ...);
int   xbox_vfprintf(FILE* f, const char* fmt, va_list ap);
int   xbox_puts(const char* s);

#ifdef __cplusplus
}
#endif
#endif
