#pragma once

struct lua_State;

namespace swc::ui {

/** Sets the source name of a Lua function (at a stack index) and of each function inside it. The game bytecode has no
 *  debug data (its source is "=?"); with the script path the stack traces and debug.getinfo name the script. */
void set_chunk_source(lua_State* L, int index, const char* source);

} // namespace swc::ui
