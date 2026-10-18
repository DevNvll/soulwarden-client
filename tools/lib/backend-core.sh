# The core library of the backend for the parity tests of the client (tools/test-net.sh, tools/test-ranged.sh): the root CMake file
# of the Soulwarden repository (read only) with no game hook, built outside it in SWC_WORK/build/backend-core (4 jobs, a memory
# limit). Ninja builds again only the files that changed: the library always has the backend sources of the tests (the headers of
# its fixtures). Source it after tools/env.sh.
backend_core() {
    local core="${SWC_WORK}/build/backend-core"
    if [[ ! -f "${core}/build.ninja" ]]; then
        mkdir -p "${core}"
        nice -n 10 cmake -S "${SOULWARDEN_REPO}" -B "${core}" -G Ninja -DSOULWARDEN_BUILD_GAME_HOOK=OFF -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang > "${core}/configure.log"
    fi
    systemd-run --user --scope -q -p MemoryMax=8G nice -n 10 ninja -j "${SWC_BUILD_JOBS:-4}" -C "${core}" soulwarden_backend_core > "${core}/build.log"
    BACKEND_CORE="${core}/apps/backend"
}
