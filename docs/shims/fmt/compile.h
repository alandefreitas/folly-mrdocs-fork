// Minimal {fmt} compile.h stand-in for MrDocs; see core.h.
#ifndef FMT_SHIM_COMPILE_H_
#define FMT_SHIM_COMPILE_H_
#include <fmt/core.h>
#ifndef FMT_COMPILE
#define FMT_COMPILE(s) (s)
#endif
#endif
