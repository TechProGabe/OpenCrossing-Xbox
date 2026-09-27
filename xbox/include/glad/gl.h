/* Xbox shim for pc/lib/glad: there is no OpenGL on nxdk. APIENTRY is forced to
 * cdecl so one generic no-op can stand in for any GL entry point (caller cleans
 * the stack) during headless bring-up. Replaced by the NV2A backend at M3. */
#ifndef XBOX_GLAD_SHIM_H
#define XBOX_GLAD_SHIM_H
#undef APIENTRY
#define APIENTRY
#include "../../../pc/lib/glad/include/glad/gl.h"
#endif
