# The own agent desktop of a window run (AGENTS.md, the run rules). Source it after tools/env.sh.
# The trap (2026-10-19): the desktop soulwarden-client-dev became stale (its headless output showed a normal workspace of the
# user, not __agent:NAME), and omarchy-agent-desktop refused the launch ("the headless output has unexpected windows").

# desktop_ready LABEL ID: makes the desktop ID ready for a launch. `omarchy-agent-desktop status --json ID` (read only):
# no desktop: create it; "ready" or "entered": use it; "stale" or "offline" with no window: `cleanup ID`, then create it
# again; "stale" or "offline" with windows: change nothing, and the status is 3 (no run).
desktop_ready() {
    local label="$1" id="$2" state status windows
    if ! state="$(omarchy-agent-desktop status --json "${id}" 2>/dev/null)"; then
        omarchy-agent-desktop create "Soulwarden client" --id "${id}" --mode "${SWC_DESKTOP_MODE:-1280x720@60}" >/dev/null
        return
    fi
    read -r status windows < <(printf '%s' "${state}" | python3 -B -c \
        'import json, sys; d = json.load(sys.stdin)["desktop"]; print(d.get("status", ""), d.get("windowCount", 0))' 2>/dev/null)
    case "${status}" in
        ready | entered)
            return 0 ;;
        stale | offline)
            if (( windows > 0 )); then
                echo "${label}: the agent desktop ${id} is ${status} and has ${windows} window(s): no run, nothing changed" \
                    "(look at it with: omarchy-agent-desktop windows ${id})." >&2
                return 3
            fi
            echo "${label}: the agent desktop ${id} is ${status} with no window: it is made again." >&2
            omarchy-agent-desktop cleanup "${id}" >/dev/null
            omarchy-agent-desktop create "Soulwarden client" --id "${id}" --mode "${SWC_DESKTOP_MODE:-1280x720@60}" >/dev/null ;;
        *)
            echo "${label}: the agent desktop ${id} has the unknown status '${status}': no run." >&2
            return 3 ;;
    esac
}
