#!/usr/bin/env bash
# The tests of the menu document reader of the client (stage F1 of docs/FRONTEND.md, Menu/SwcMenuDocument.cpp) with the documents
# of the backend writer (state/creation_data.cpp of the Soulwarden backend, read only). No engine. Usage: tools/test-menu.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
compiler="${CXX:-clang++}"
backend="${SOULWARDEN_REPO}/apps/backend"
module="${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient"
out="${SWC_WORK}/build/menu-tests"
mkdir -p "${out}"
nice -n 10 "${compiler}" -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
    -I"${module}" -I"${backend}/src" \
    "${SWC_REPO}/tests/net/menu_tests.cpp" "${module}/Menu/SwcMenuDocument.cpp" \
    "${backend}/src/state/creation_data.cpp" "${backend}/src/core/encoding/base64.cpp" \
    "${backend}/src/middleware/compression/zlib.cpp" \
    -lz -o "${out}/swc-menu-tests"
exec "${out}/swc-menu-tests"
