/* xbox_decomp_prelude.h — force-included into decomp TUs only (src/).
 *
 * pdclib defines errno as a macro; padmgr.c uses `errno` as a struct field
 * (PADStatus.errno). Pull errno.h in first so its guard is set, then drop the
 * macro. Decomp code never reads the C errno. */
#ifndef XBOX_DECOMP_PRELUDE_H
#define XBOX_DECOMP_PRELUDE_H
#include <errno.h>
#undef errno
#endif
