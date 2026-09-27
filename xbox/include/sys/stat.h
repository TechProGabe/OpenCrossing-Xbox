/* Minimal stat/mkdir for nxdk. Implementation: xbox/src/xbox_posix.c. */
#ifndef XBOX_SYS_STAT_H
#define XBOX_SYS_STAT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef XBOXRT_STAT
typedef uint32_t mode_t;
#endif
struct stat { uint32_t st_mode; uint32_t st_size; };
#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
int stat(const char* path, struct stat* st);
int mkdir(const char* path, mode_t mode);
#ifdef __cplusplus
}
#endif
#endif
