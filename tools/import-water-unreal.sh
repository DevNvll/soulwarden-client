#!/usr/bin/env bash
# Makes the material instances of the Unreal water (docs/WATER.md "The Unreal water"; Scripts/water_unreal_materials.py), with no
# window. Default: /SwcGameContent/Water/Unreal (the shared plugin: only with the word of the orchestrator). --check: the check
# plugin SwcWaterCheck (SWC_WORK/unreal/SwcWaterCheck, linked into this checkout only; git ignores the link) with the folder
# /SwcWaterCheck/Water/Unreal; a run uses it with -SwcWaterMode=Unreal -SwcWaterMaterials=/SwcWaterCheck/Water/Unreal.
# --near: the near color term of the ocean (Scripts/water_unreal_near.py; docs/WATER.md "Stage 2c"): a copy of the plugin parent and of
# the ocean instance chain in the check plugin only (/SwcWaterCheck/Water/Near; a run uses it with -SwcWaterMaterials=/SwcWaterCheck/Water/Near).
# --under: the underwater view of the source path (Scripts/water_underwater.py; docs/WATER.md "The underwater view"): the post-process
# material M_SwcUnderwater and the underside material M_SwcWaterUnder in /SwcWaterCheck/Water/Under (a run uses them with
# -SwcUnderwater=/SwcWaterCheck/Water/Under); --under --shared: /SwcGameContent/Water/Under (only with the word of the orchestrator).
# Usage: tools/import-water-unreal.sh [--check] [--near] [--under [--shared]] [--force]
# The editor commandlet uses at most SWC_CPU_QUOTA (default 400%: 4 cores) and SWC_MEMORY_MAX (default 12G).
# One import at a time: the import waits for SWC_WORK/unreal/import.lock (the lock of tools/import.sh; 30 min at most).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
dest="/SwcGameContent/Water/Unreal"
script="water_unreal_materials.py"
for arg in "$@"; do
    case "${arg}" in
        --check) dest="/SwcWaterCheck/Water/Unreal" ;;
        --near) dest="/SwcWaterCheck/Water/Near"; script="water_unreal_near.py" ;;
        --under) dest="/SwcWaterCheck/Water/Under"; script="water_underwater.py" ;;
        --shared) shared=1 ;;
        --force) export SWC_IMPORT_FORCE=1 ;;
        *) echo "usage: tools/import-water-unreal.sh [--check] [--near] [--under [--shared]] [--force]" >&2; exit 2 ;;
    esac
done
if [[ "${shared:-0}" == 1 ]]; then
    if [[ "${script}" != "water_underwater.py" ]]; then
        echo "import-water-unreal: --shared goes only with --under" >&2
        exit 2
    fi
    dest="/SwcGameContent/Water/Under"
fi
"${SWC_REPO}/tools/content-link.sh"
if [[ "${dest}" == /SwcWaterCheck/* ]]; then
    check="${SWC_WORK}/unreal/SwcWaterCheck"
    if [[ ! -f "${check}/SwcWaterCheck.uplugin" ]]; then
        mkdir -p "${check}/Content"
        cat > "${check}/SwcWaterCheck.uplugin" <<'JSON'
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "Soulwarden water check",
	"Description": "The check copy of the Unreal water assets (docs/WATER.md). Not in git.",
	"Category": "Soulwarden",
	"EnabledByDefault": true,
	"CanContainContent": true,
	"Installed": false
}
JSON
    fi
    link="${SWC_REPO}/unreal/SoulwardenClient/Plugins/SwcWaterCheck"
    if [[ "$(readlink "${link}" 2>/dev/null)" != "${check}" ]]; then
        if [[ -e "${link}" && ! -L "${link}" ]]; then
            echo "import-water-unreal: ${link} exists and is not a link; nothing changed" >&2
            exit 1
        fi
        ln -sfn "${check}" "${link}"
    fi
fi
mkdir -p "${SWC_WORK}/logs" "${SWC_WORK}/unreal"
export SWC_WATER_UNREAL_DEST="${dest}"
export SWC_WATER_UNREAL_REPORT="${SWC_WORK}/logs/import-water-unreal-report.json"
rm -f "${SWC_WATER_UNREAL_REPORT}"
status=0
# flock -o: the editor does not get the descriptor of the lock (its trace and cache servers live after it).
flock -w 1800 -o -E 75 "${SWC_WORK}/unreal/import.lock" \
    systemd-run --user --scope -q -p CPUQuota="${SWC_CPU_QUOTA:-400%}" -p MemoryMax="${SWC_MEMORY_MAX:-12G}" \
    nice -n 10 "${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${SWC_UPROJECT}" \
    -run=pythonscript -script="${SWC_REPO}/unreal/SoulwardenClient/Scripts/${script}" "${SWC_EDITOR_DDC}" \
    -unattended -nullrhi -nosplash -nosound -nopause -stdout > "${SWC_WORK}/logs/import-water-unreal.log" 2>&1 || status=$?
if [[ ${status} -eq 75 ]]; then
    echo "import-water-unreal: another import holds ${SWC_WORK}/unreal/import.lock" >&2
    exit 1
fi
if [[ -f "${SWC_WATER_UNREAL_REPORT}" ]]; then
    cat "${SWC_WATER_UNREAL_REPORT}"
    echo
    python3 -B -c "import json,sys; sys.exit(1 if json.load(open('${SWC_WATER_UNREAL_REPORT}'))['failed'] else 0)"
else
    echo "no report: the commandlet stopped with status ${status}; see ${SWC_WORK}/logs/import-water-unreal.log" >&2
    exit 1
fi
