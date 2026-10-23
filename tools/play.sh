#!/usr/bin/env bash
# The player run: the client with the Steam account of the user and the same characters as the retail game.
# Only the user starts it (an agent runs only --check). It uses:
#   - the Steam id and the persona name of the most recent Steam login (config/loginusers.vdf, as the Soulwarden
#     launcher reads them): only these two values, into the environment of the client, never into a file;
#   - the data folder of the user (SWC_PLAYER_DATA) with the run.lock of the Soulwarden launcher: the retail game and
#     a player run do not run at the same time on one database, and a Soulwarden release waits while one of them runs;
#   - the release backend copy (SWC_BACKEND_DIR), which must be the current release of the user (shared database
#     schema: a newer backend migrates the database, and then the retail game refuses it);
#   - one network namespace with loopback only for the backend and the client (tools/lib/session.sh).
# Usage: tools/play.sh [--check] [--cooked | --uncooked] [-- CLIENT ARGUMENTS]
#   --check     checks everything and starts nothing. Status 0: a start can begin now.
#   (default)   the cooked build of tools/cook.sh when it is current (no import and no client code change after the cook;
#               tools/lib/cooked.sh), else the editor binary with the project; a line says which one and why.
#   --cooked    the cooked build also when it is older than the content or the code; --uncooked: the editor binary.
# SWC_STEAM_ID=<17 digits> (with SWC_PERSONA_NAME) selects a different Steam id; SWC_STEAM_ID=local selects the
# fallback identity of the Soulwarden hook.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN SWC_DEV_RUN

check_only=0
build=auto
while [[ $# -gt 0 ]]; do
    case "$1" in
        --check) check_only=1 ;;
        --cooked) build=cooked ;;
        --uncooked) build=uncooked ;;
        --) shift; break ;;
        *) break ;;
    esac
    shift
done

failures=0
report() { # report ok|FAIL TEXT
    printf '%-4s %s\n' "$1" "$2" >&2
    [[ "$1" == "ok" ]] || failures=$((failures + 1))
}

# Prints "Steam id<TAB>persona name" of the account that logged in to Steam last on this computer: the block of
# config/loginusers.vdf with MostRecent = 1, else the block with the largest Timestamp. The same rule as the
# Soulwarden launcher (linux-compat/run-soulwarden.sh). Only this file is read, and only these two values leave it.
steam_account() {
    local login_file="${SWC_STEAM_DIR}/config/loginusers.vdf"
    [[ -r "${login_file}" ]] || return 0
    awk '
        { text = text $0 "\n" }
        END {
            depth = 0; pending = 0; total = length(text)
            for (i = 1; i <= total; ++i) {
                c = substr(text, i, 1)
                if (c == "{") { ++depth; pending = 0; continue }
                if (c == "}") { --depth; pending = 0; continue }
                if (c != "\"") continue
                value = ""
                for (++i; i <= total; ++i) {
                    c = substr(text, i, 1)
                    if (c == "\\") { c = substr(text, ++i, 1); if (c == "n" || c == "t") c = " " }
                    else if (c == "\"") break
                    value = value c
                }
                if (depth == 1) { id = value; order[++count] = id; continue }
                if (depth != 2) continue
                if (!pending) { key = tolower(value); pending = 1; continue }
                field[id, key] = value; pending = 0
            }
            best = ""
            for (n = 1; n <= count; ++n) {
                id = order[n]
                if (id !~ /^[0-9]+$/ || length(id) != 17) continue
                if (field[id, "mostrecent"] == "1") { best = id; break }
                if (best == "" || field[id, "timestamp"] + 0 > field[best, "timestamp"] + 0) best = id
            }
            if (best != "") printf "%s\t%s\n", best, field[best, "personaname"]
        }
    ' "${login_file}" 2>/dev/null || true
}

# A Steam id of an individual account in the public universe: 17 digits, and the high 32 bits are 0x01100001.
is_steam_id() {
    [[ "$1" =~ ^[0-9]{17}$ ]] && (( (10#$1 >> 32) == 0x01100001 ))
}

# Prints the state of a lock file from /proc/locks, with no lock taken: free, held, or absent.
lock_state() {
    local file="$1"
    [[ -e "${file}" ]] || { echo absent; return; }
    local key
    key="$(printf '%02x:%02x:%s' "$(stat -c %Hd "${file}")" "$(stat -c %Ld "${file}")" "$(stat -c %i "${file}")")"
    if grep -q " ${key} " /proc/locks 2>/dev/null; then echo held; else echo free; fi
}

json_version() {
    python3 -B -I -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$1" 2>/dev/null || true
}

# 1. Tools.
for tool in unshare ip flock curl awk python3; do
    if command -v "${tool}" >/dev/null 2>&1; then report ok "tool ${tool}"; else report FAIL "tool ${tool} was not found"; fi
done

# 2. The backend copy and its version: the version of the current release of the user (shared database schema).
backend_version="$(json_version "${SWC_BACKEND_DIR}/release.json")"
user_version="$(cat "${SOULWARDEN_RELEASES}/current/VERSION" 2>/dev/null || true)"
if [[ -x "${SWC_BACKEND_EXE}" && -d "${SWC_GAMEDATA_DIR}" && -n "${backend_version}" ]]; then
    report ok "backend copy ${backend_version} (${SWC_BACKEND_DIR})"
else
    report FAIL "no backend copy at ${SWC_BACKEND_DIR} (run tools/backend-update.sh)"
fi
if [[ -z "${user_version}" ]]; then
    report FAIL "no current Soulwarden release (${SOULWARDEN_RELEASES}/current/VERSION)"
elif [[ "${backend_version}" != "${user_version}" ]]; then
    report FAIL "the backend copy is ${backend_version:-missing}, the current release of the user is ${user_version}: run tools/backend-update.sh (the database schema is shared)"
else
    report ok "the backend copy is the current release of the user"
fi
# The game data of the copy against its release record (tools/lib/backend_data.py: each file and folder of release.json with
# its size; a new file of a release, for example quest-data.json and poi-data.json of schema 15 and 16, must be there).
data_line="$(python3 -B -I "${SWC_REPO}/tools/lib/backend_data.py" "${SWC_BACKEND_DIR}")"
if [[ "${data_line}" == ok* ]]; then
    report ok "the game data of the backend copy: ${data_line#ok }"
else
    report FAIL "${data_line#FAIL }"
fi

# 3. The client: the cooked build when it is current (or --cooked), else the engine and the module of the project.
source "${SWC_REPO}/tools/lib/cooked.sh"
cooked_line="$(cooked_state)" && cooked_current=1 || cooked_current=0
client=("${SWC_CLIENT_EXE}" "${SWC_UPROJECT}")
if [[ "${build}" == "cooked" || ( "${build}" == "auto" && ${cooked_current} -eq 1 ) ]]; then
    if [[ -x "${cooked_binary}" ]]; then
        # -NoVerifyGC: a Development game build verifies the assumptions of the garbage collection at each collection (only the
        # editor and a Shipping or Test build skip it; AST-273 P2).
        client=("${cooked_binary}" SoulwardenClient -NoVerifyGC)
        report ok "client: the cooked build (${cooked_line})"
    else
        report FAIL "no cooked build in ${cooked_dir} (run tools/cook.sh)"
    fi
else
    # Each module of the .uproject must be built and newer than the code (Source): else the engine shows its dialog "Missing
    # SoulwardenClient Modules" (a build of an older commit, 2026-10-22). A start builds the modules first (a check reports it).
    modules_state() {
        # Each module against the code of its own Source folder (a build leaves a module with no change as it is).
        local project_dir="${SWC_REPO}/unreal/SoulwardenClient" name file built newest_source
        for name in $(python3 -B -c "import json, sys; print(' '.join(m['Name'] for m in json.load(open(sys.argv[1]))['Modules']))" "${SWC_UPROJECT}"); do
            file="${project_dir}/Binaries/Linux/libUnrealEditor-${name}.so"
            [[ -f "${file}" ]] || { echo "missing ${name}"; return 1; }
            built=$(stat -c %Y "${file}")
            [[ -d "${project_dir}/Source/${name}" ]] || continue
            newest_source=$(find "${project_dir}/Source/${name}" -type f -printf '%T@\n' | sort -n | tail -1 | cut -d. -f1)
            [[ -z "${newest_source}" || "${newest_source}" -le "${built}" ]] || { echo "older than the code (${name})"; return 1; }
        done
        echo "current"
    }
    modules_line="$(modules_state)" && modules_current=1 || modules_current=0
    if [[ ${modules_current} -eq 0 && ${check_only} -eq 0 && -x "${SWC_CLIENT_EXE}" ]]; then
        echo "play: the editor modules of the project are ${modules_line}: tools/build.sh editor (some minutes)" >&2
        if "${SWC_REPO}/tools/build.sh" editor >&2; then
            modules_line="$(modules_state)" && modules_current=1 || modules_current=0
        fi
    fi
    if [[ -x "${SWC_CLIENT_EXE}" && ${modules_current} -eq 1 ]]; then
        report ok "client ${SWC_CLIENT_EXE##*/} with the modules of the project (cooked build: ${cooked_line})"
    elif [[ ${check_only} -eq 1 && -x "${SWC_CLIENT_EXE}" ]]; then
        report ok "client ${SWC_CLIENT_EXE##*/}: the editor modules are ${modules_line}; a start builds them first (cooked build: ${cooked_line})"
    else
        report FAIL "no client: ${SWC_CLIENT_EXE} or the editor modules (${modules_line}): run tools/build.sh editor"
    fi
fi
# The imported content and the world level (the default map): the shared content plugin (docs/WORLD.md).
if "${SWC_REPO}/tools/content-link.sh" && [[ -f "${SWC_GAME_CONTENT}/Content/World/NewWorld.umap" ]]; then
    report ok "the shared content plugin with the world level"
else
    report FAIL "no world level in ${SWC_GAME_CONTENT}: run tools/build-world.sh (docs/WORLD.md)"
fi

# 4. The Steam account (only the last 4 digits of the Steam id in a message, never the persona name).
steam_id="${SWC_STEAM_ID:-}"
persona_name="${SWC_PERSONA_NAME:-}"
unset SWC_STEAM_ID SWC_PERSONA_NAME
if [[ "${steam_id}" == "local" ]]; then
    steam_id=""
    report ok "Steam account: the fallback identity (SWC_STEAM_ID=local)"
elif [[ -n "${steam_id}" ]]; then
    if is_steam_id "${steam_id}"; then
        report ok "Steam account ...${steam_id: -4} (SWC_STEAM_ID)"
    else
        report FAIL "SWC_STEAM_ID is not the Steam id of an individual account (17 digits) or \"local\""
    fi
else
    account="$(steam_account)"
    steam_id="${account%%$'\t'*}"
    if [[ -z "${persona_name}" && "${account}" == *$'\t'* ]]; then
        persona_name="${account#*$'\t'}"
    fi
    if is_steam_id "${steam_id}"; then
        report ok "Steam account ...${steam_id: -4} (the most recent login of ${SWC_STEAM_DIR})"
    else
        steam_id=""
        report ok "Steam account: none in ${SWC_STEAM_DIR}, the fallback identity"
    fi
fi

# 5. The data folder of the user and its lock (the lock of the Soulwarden launcher). The check takes no lock.
if [[ "$(realpath -m -- "${SWC_PLAYER_DATA}")" == "$(realpath -m -- "${SWC_DEV_DATA}")" ]]; then
    report FAIL "SWC_PLAYER_DATA is the data folder of the development runs"
elif [[ -d "${SWC_PLAYER_DATA}" ]]; then
    report ok "data folder of the user ${SWC_PLAYER_DATA}"
else
    report FAIL "no data folder of the user at ${SWC_PLAYER_DATA}"
fi
case "$(lock_state "${SWC_PLAYER_DATA}/run.lock")" in
    held) report FAIL "run.lock is held: the retail game or a player run uses the data folder now" ;;
    *) report ok "run.lock is free" ;;
esac

if [[ "${failures}" -gt 0 ]]; then
    echo "play: ${failures} check(s) failed. Nothing was started." >&2
    exit 1
fi
if [[ "${check_only}" == "1" ]]; then
    echo "play: each check passed. Nothing was started (--check)." >&2
    exit 0
fi

# The client loads the imported content from the shared content plugin (the link of this checkout).
"${SWC_REPO}/tools/content-link.sh"
logs="${SWC_WORK}/logs/play"
mkdir -p "${logs}"
stamp="$(date +%Y%m%d-%H%M%S)"
if [[ -n "${steam_id}" ]]; then
    export SWC_STEAM_ID="${steam_id}" SWC_PERSONA_NAME="${persona_name:-Soulwarden}"
fi
export SWC_RUN=player
export SWC_SESSION_DATA="${SWC_PLAYER_DATA}" SWC_SESSION_LOCK="${SWC_PLAYER_DATA}/run.lock" SWC_SESSION_SEED=""
exec "${SWC_REPO}/tools/lib/session.sh" -- \
    "${client[@]}" -game -SwcConnect -abslog="${logs}/client-${stamp}.log" "${SWC_CLIENT_ARGS[@]}" "$@"
