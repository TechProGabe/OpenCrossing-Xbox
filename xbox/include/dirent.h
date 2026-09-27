/* Minimal POSIX dirent for nxdk (pc_disc.c, pc_card.c, famicom.cpp), on
 * FindFirstFileA. Implementation: xbox/src/xbox_posix.c. */
#ifndef XBOX_DIRENT_H
#define XBOX_DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
struct dirent { char d_name[260]; };
typedef struct XboxDIR DIR;
DIR* opendir(const char* path);
struct dirent* readdir(DIR* d);
int closedir(DIR* d);
#ifdef __cplusplus
}
#endif
#endif
