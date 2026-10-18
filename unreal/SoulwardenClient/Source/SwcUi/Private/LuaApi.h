#pragma once

// The C API of the vendored Lua 5.1.5 (Source/ThirdParty/Lua51). SwcLua51.cpp compiles it as C++: no extern "C".
// The functions have the default visibility (the engine hides the functions of a module by default).
#pragma GCC visibility push(default)
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#pragma GCC visibility pop
