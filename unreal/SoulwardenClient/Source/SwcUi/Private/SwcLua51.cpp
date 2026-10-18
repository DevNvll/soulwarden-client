// The core and the standard libraries of the vendored Lua 5.1.5 (Source/ThirdParty/Lua51/lua-5.1.5, unchanged), in one
// translation unit in the order of etc/all.c of the release, without the stand-alone interpreter (lua.c), without
// loslib.c (SwcLua51Os.cpp: luaconf.h has a block for that file only), and without the define luaall_c of all.c (it
// gives const data with no initializer, which C++ refuses). Compiled as C++: a Lua error is a C++ exception
// (luaconf.h), so it unwinds the C++ frames of a native function. No engine header: the macros of the engine (check,
// verify) would change the Lua sources.

// The C headers first: ldebug.h defines a macro getline, and the C library of C++ declares a function getline.
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma GCC visibility push(default)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#include "lapi.c"
#include "lcode.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmem.c"
#include "lobject.c"
#include "lopcodes.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "ltable.c"
#include "ltm.c"
#include "lundump.c"
#include "lvm.c"
#include "lzio.c"
#include "lauxlib.c"
#include "lbaselib.c"
#include "ldblib.c"
#include "liolib.c"
#include "linit.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "lstrlib.c"
#include "ltablib.c"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#pragma GCC visibility pop
