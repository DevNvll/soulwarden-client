# Lua 5.1.5 (vendored)

An unchanged copy of the Lua 5.1.5 release of PUC-Rio (`lua-5.1.5.tar.gz` of https://www.lua.org/ftp/, released
2012-02-13, SHA-256 `2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333`), MIT License (file
`lua-5.1.5/COPYRIGHT`, Copyright (C) 1994-2012 Lua.org, PUC-Rio). The copy has the folders `src` and `etc` and the
top-level files of the release; the folders `doc` and `test` are not here.

Why this version: the scripts of the game are Lua 5.1 bytecode (2 bytes, then the stock 5.1 header with int 4,
size_t 8, instruction 4, number 8). The stock 5.1.5 loads all 1,394 scripts (research
`~/Games/soulwarden-client/research/ui.md`, section 3.1). LuaJIT and Lua 5.2 or later cannot load this bytecode.

Do not edit the files here. To update, copy the files again from a release and check the checksum.

The module `SwcUi` (`Source/SwcUi/Private/SwcLua51.cpp`) compiles the core and the libraries of this copy as C++
(Lua errors are C++ exceptions, so a Lua error unwinds the C++ frames of a native function). It does not compile
`lua.c` and `luac.c` (the stand-alone programs). The offline tests (`tools/test-ui.sh`) compile the same file.
