#!/usr/bin/env bash
# Runs one Python script of unreal/SoulwardenClient/Scripts in the editor commandlet, with no window.
# Usage: tools/editor-script.sh SCRIPT.py [--render]
#   --render  starts the renderer with no window (-AllowCommandletRendering -RenderOffscreen) for scene captures;
#             else the commandlet has no renderer (-nullrhi).
# The commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
# The log goes to SWC_WORK/logs/SCRIPT.log.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
script="${1:?usage: tools/editor-script.sh SCRIPT.py [--render]}"
[[ "${script}" == */* ]] || script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/${script}"
rhi=(-nullrhi)
[[ "${2:-}" == "--render" ]] && rhi=(-AllowCommandletRendering -RenderOffscreen -NoTextureStreaming)
mkdir -p "${SWC_WORK}/logs"
log="${SWC_WORK}/logs/$(basename "${script}" .py).log"
status=0
systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${script}" "${rhi[@]}" \
    -unattended -nosplash -nosound -nopause -stdout > "${log}" 2>&1 || status=$?
# Python log lines (Log verbosity) are only in the full log of the project.
grep -h "SWC " "${SWC_REPO}/unreal/SoulwardenClient/Saved/Logs/SoulwardenClient.log" | cut -c30-400 || true
echo "status ${status}; log ${log}"
