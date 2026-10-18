#!/usr/bin/env bash
# Builds and runs the offline tests of the UI runtime (tests/ui) with the system C++ compiler: the core rules, then the
# original HUD scripts on the converted content (SWC_CONTENT/ui) in the embedded Lua 5.1.5 with no engine. The build
# output and the reports (the stub log, the error log, the missing textures, the draw list) go to SWC_WORK/build/ui-tests.
# Then the fixtures of the original scripts (tests/ui/fixtures/*.lua, each in a runtime of its own).
# Usage: tools/test-ui.sh [--preview] [TEST ARGUMENTS]   (--preview: the pictures of the draw lists, uipreview.py;
#        --fixture NAME runs only that fixture, --no-hud skips the HUD run)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SwcUi"
lua="${SWC_REPO}/unreal/SoulwardenClient/Source/ThirdParty/Lua51/lua-5.1.5/src"
out="${SWC_WORK}/build/ui-tests"
preview=0
if [[ "${1:-}" == "--preview" ]]; then
    preview=1
    shift
fi
mkdir -p "${out}" "${out}/reports"
# Lua as C++ (the same files as the module), compiled one time.
for part in SwcLua51 SwcLua51Os; do
    if [[ ! -f "${out}/${part}.o" || "${module}/Private/${part}.cpp" -nt "${out}/${part}.o" ]]; then
        nice -n 10 "${compiler}" -std=c++20 -O2 -g -fexceptions -I"${lua}" -c "${module}/Private/${part}.cpp" -o "${out}/${part}.o"
    fi
done
nice -n 10 "${compiler}" -std=c++20 -O1 -g -fexceptions -Wall -Wextra -Wno-unused-parameter \
    -I"${module}/Public" -I"${module}/Private" -I"${lua}" \
    "${SWC_REPO}/tests/ui/ui_tests.cpp" "${module}/Private/Json.cpp" "${module}/Private/Model.cpp" \
    "${module}/Private/Draw.cpp" "${module}/Private/LuaHost.cpp" "${module}/Private/LuaSource.cpp" "${out}/SwcLua51.o" "${out}/SwcLua51Os.o" -o "${out}/swc-ui-tests"
status=0
rm -rf "${out}/reports/fixtures"
"${out}/swc-ui-tests" "${SWC_CONTENT}/ui" "${SWC_REPO}/unreal/SoulwardenClient/Lua" "${out}/reports" \
    --draw "${out}/reports/draw.json" --fixtures "${SWC_REPO}/tests/ui/fixtures" "$@" || status=$?
if [[ "${preview}" == 1 ]]; then
    (cd "${SWC_REPO}/converter" && python3 -B -m swc_convert.uipreview "${out}/reports/draw.json" "${out}/reports/hud.png")
    # The pictures of the fixtures (picture(NAME) in a fixture: reports/fixtures/FIXTURE/draw/NAME.json).
    for draw in "${out}"/reports/fixtures/*/draw/*.json; do
        [[ -f "${draw}" ]] || continue
        (cd "${SWC_REPO}/converter" && python3 -B -m swc_convert.uipreview "${draw}" "${draw%.json}.png")
    done
fi
exit "${status}"
