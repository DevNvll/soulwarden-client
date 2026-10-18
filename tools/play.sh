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
# Usage: tools/play.sh [--check] [-- CLIENT ARGUMENTS]
#   --check   checks everything and starts nothing. Status 0: a start can begin now.
# SWC_STEAM_ID=<17 digits> (with SWC_PERSONA_NAME) selects a different Steam id; SWC_STEAM_ID=local selects the
# fallback identity of the Soulwarden hook.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
unset SOULWARDEN_DEV_RUN SWC_DEV_RUN

check_only=0
if [[ "${1:-}" == "--check" ]]; then
    check_only=1
    shift
fi
[[ "${1:-}" == "--" ]] && shift

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

# 3. The client: the engine and the module of the project.
module="${SWC_REPO}/unreal/SoulwardenClient/Binaries/Linux/libUnrealEditor-SoulwardenClient.so"
if [[ -x "${SWC_CLIENT_EXE}" && -f "${module}" ]]; then
    report ok "client ${SWC_CLIENT_EXE##*/} with the module of the project"
else
    report FAIL "no client: ${SWC_CLIENT_EXE} or ${module} is missing (run tools/build.sh editor)"
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

logs="${SWC_WORK}/logs/play"
mkdir -p "${logs}"
stamp="$(date +%Y%m%d-%H%M%S)"
if [[ -n "${steam_id}" ]]; then
    export SWC_STEAM_ID="${steam_id}" SWC_PERSONA_NAME="${persona_name:-Soulwarden}"
fi
export SWC_RUN=player
export SWC_SESSION_DATA="${SWC_PLAYER_DATA}" SWC_SESSION_LOCK="${SWC_PLAYER_DATA}/run.lock" SWC_SESSION_SEED=""
exec "${SWC_REPO}/tools/lib/session.sh" -- \
    "${SWC_CLIENT_EXE}" "${SWC_UPROJECT}" -game -SwcConnect -abslog="${logs}/client-${stamp}.log" "$@"
