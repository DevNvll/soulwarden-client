// The library os of the vendored Lua 5.1.5 (loslib.c), compiled as C++ in its own translation unit: luaconf.h has a
// block for this file only (loslib_c). LUA_USE_MKSTEMP: os.tmpname with mkstemp, not with the tmpnam of the C library
// (the client removes os.tmpname; the runtime removes it too).
#define LUA_USE_MKSTEMP

#pragma GCC visibility push(default)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#include "loslib.c"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#pragma GCC visibility pop
