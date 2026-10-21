# The cooked build of tools/cook.sh (docs/WORLD.md "The cooked build"). Source it after tools/env.sh.
# SWC_COOKED (default SWC_WORK/cooked): the staged build; its stamp Linux/swc-cook.json has the commit, the time, and the map.

cooked_dir="${SWC_COOKED:-${SWC_WORK}/cooked}/Linux"
cooked_binary="${cooked_dir}/SoulwardenClient/Binaries/Linux/SoulwardenClient"

# cooked_code_hash: a hash of the files of the client code in the checkout (Source, Lua, Config; also changes that are not
# committed: a cook takes the files as they are).
cooked_code_hash() {
    (cd "${SWC_REPO}/unreal/SoulwardenClient" && find Source Lua Config -type f -not -name "*.pyc" -print0 | sort -z | xargs -0 sha1sum) \
        | sha1sum | cut -c1-16
}

# cooked_state: prints one line and returns 0 when the cooked build is current: it exists, its map is the default (the whole
# world), the content has no import after the cook (the state files of the plugin and the manifest of the converters), no
# converter data that the client reads at run time is newer than the cook (tools/lib/loose-data.txt, stage MVP-K), and the client
# code (Source, Lua, Config) is the same as at the cook. Else 1 (the line says why).
cooked_state() {
    local stamp="${cooked_dir}/swc-cook.json"
    if [[ ! -x "${cooked_binary}" || ! -f "${stamp}" ]]; then
        echo "no cooked build in ${cooked_dir} (tools/cook.sh)"
        return 1
    fi
    local commit code time map
    read -r commit code time map < <(python3 -B -c "import json, sys; s = json.load(open(sys.argv[1])); print(s['commit'], s.get('code', '-'), int(s['time']), s['map'])" "${stamp}")
    if [[ "${map}" != "default" ]]; then
        echo "the cooked build is of the test map ${map}"
        return 1
    fi
    local newest
    newest=$(stat -c %Y "${SWC_GAME_CONTENT}"/*.json "${SWC_CONTENT}/manifest.json" 2>/dev/null | sort -n | tail -1)
    if [[ -n "${newest}" && "${newest}" -gt "${time}" ]]; then
        echo "the content changed after the cook of $(date -d "@${time}" '+%F %T') (run tools/cook.sh)"
        return 1
    fi
    # The converter data that the client reads at run time (tools/lib/loose-data.txt; the cooked build does not contain it).
    local loose loose_file
    read -r loose loose_file < <(python3 -B "${SWC_REPO}/tools/lib/cook_audit.py" --newest)
    if [[ -n "${loose}" && "${loose}" -gt "${time}" ]]; then
        echo "the converter data ${loose_file} changed after the cook of $(date -d "@${time}" '+%F %T') (run tools/cook.sh)"
        return 1
    fi
    if [[ "${code}" != "$(cooked_code_hash)" ]]; then
        echo "the client code (Source, Lua, Config) changed after the cook of commit ${commit:0:7} (run tools/cook.sh)"
        return 1
    fi
    echo "the cooked build of commit ${commit:0:7} ($(date -d "@${time}" '+%F %T')) is current"
    return 0
}
