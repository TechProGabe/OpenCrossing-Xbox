/* ja_calc.c includes ".../msl/..." but the tree has ".../Msl/...". macOS
 * ignores case, Linux (CI) does not. include_next skips xbox/include, so on
 * macOS this can't find itself again. */
#include_next "PowerPC_EABI_Support/Msl/MSL_C/PPC_EABI/cmath_gcn.h"
