#!/usr/bin/env bash
# Builds the C++ module of the client. Usage: tools/build.sh [editor|game] [Development|DebugGame|Shipping]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
target="SoulwardenClientEditor"
[[ "${1:-editor}" == "game" ]] && target="SoulwardenClient"
configuration="${2:-Development}"
exec nice -n 10 "${UE_ROOT}/Engine/Build/BatchFiles/Linux/Build.sh" "${target}" Linux "${configuration}" \
    -Project="${SWC_UPROJECT}" -WaitMutex -MaxParallelActions="${SWC_BUILD_JOBS:-4}"
