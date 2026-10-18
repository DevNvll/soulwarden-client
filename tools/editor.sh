#!/usr/bin/env bash
# Opens the Unreal editor on the project (a window on the desktop of the user).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
"${SWC_REPO}/tools/content-link.sh"
exec "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor" "${SWC_UPROJECT}" "$@"
