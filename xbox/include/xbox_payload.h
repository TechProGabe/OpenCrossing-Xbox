/* xbox_payload.h — data appended to default.xbe (xbox/src/xbox_payload.c,
 * tools/xbox/xbe_payload.py). */
#ifndef XBOX_PAYLOAD_H
#define XBOX_PAYLOAD_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the entry `tag` (4 chars): its file offset and size; 0 if absent */
int xbox_payload_find(const char* tag, unsigned* off, unsigned* size);
/* a handle of your own on default.xbe, and a read from it */
HANDLE xbox_payload_open(void);
int xbox_payload_read(HANDLE h, unsigned off, void* dst, unsigned n);
#ifdef __cplusplus
}
#endif
#endif
