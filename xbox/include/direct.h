/* MSVCRT _mkdir for nxdk. Implementation: xbox/src/xbox_posix.c. */
#ifndef XBOX_DIRECT_H
#define XBOX_DIRECT_H
#ifdef __cplusplus
extern "C" {
#endif
int _mkdir(const char* path);
#ifdef __cplusplus
}
#endif
#endif
