// The source name of loaded game functions. It uses the internal headers of the vendored Lua 5.1.5 (the Proto of a Lua
// function); the Lua files stay unchanged. In its own file: the internal headers of Lua have short macros (G, gt).
#include "LuaSource.h"

#include <string.h>

#include "LuaApi.h"

#pragma GCC visibility push(default)
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#pragma GCC visibility pop

namespace swc::ui {
namespace {

void set_proto_source(lua_State* L, Proto* p, TString* source) {
    p->source = source;
    luaC_objbarrier(L, p, source);
    for (int i = 0; i < p->sizep; ++i) {
        set_proto_source(L, p->p[i], source);
    }
}

} // namespace

void set_chunk_source(lua_State* L, int index, const char* source) {
    if (!lua_isfunction(L, index) || lua_iscfunction(L, index)) {
        return;
    }
    auto* closure = static_cast<Closure*>(const_cast<void*>(lua_topointer(L, index)));
    TString* name = luaS_new(L, source);
    set_proto_source(L, closure->l.p, name);
}

} // namespace swc::ui
