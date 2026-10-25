#!/usr/bin/env bash
# The test of cooked_wp_map (tools/lib/cooked.sh): the cooked NewWorld map of the staged build for a run from a worktree. No engine,
# a temporary repository with a worktree. Usage: tests/cook/test_cooked_map.sh
set -uo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
checks=0; failures=0
check() {
    checks=$((checks + 1))
    if [[ "$2" == "$3" ]]; then echo "  ok  $1"; else echo "FAIL  $1: '$2', not '$3'"; failures=$((failures + 1)); fi
}
relative="unreal/SoulwardenClient/Saved/Cooked/Linux/SoulwardenClient/Plugins/SwcGameContent/Content/World/NewWorld.umap"
git init -q "${tmp}/main" && git -C "${tmp}/main" -c user.name=t -c user.email=t@t -c commit.gpgsign=false commit -q --allow-empty -m init
git -C "${tmp}/main" worktree add -q "${tmp}/tree" -b tree
main_map="$(cd "${tmp}/main" && pwd -P)/${relative}"
mkdir -p "$(dirname "${main_map}")" && touch "${main_map}"
echo '{"commit": "x", "time": 0, "map": "default"}' > "${tmp}/old.json"
export SWC_WORK="${tmp}/work" SWC_COOKED="${tmp}/cooked"
source "${here}/tools/lib/cooked.sh"
SWC_REPO="${tmp}/tree"
check "a stamp with no wpMap, a run from a worktree: the map of the main checkout" "$(cooked_wp_map "${tmp}/old.json")" "${main_map}"
SWC_REPO="${tmp}/main"
check "a stamp with no wpMap, a run from the main checkout: its own map" "$(cooked_wp_map "${tmp}/old.json")" "${tmp}/main/${relative}"
echo '{"commit": "x", "time": 0, "map": "default", "wpMap": "/cook/NewWorld.umap"}' > "${tmp}/new.json"
SWC_REPO="${tmp}/tree"
check "a stamp with wpMap: that map" "$(cooked_wp_map "${tmp}/new.json")" "/cook/NewWorld.umap"
mkdir -p "${tmp}/tree/$(dirname "${relative}")" && touch "${tmp}/tree/${relative}"
check "a worktree with its own cooked map: its own map" "$(cooked_wp_map "${tmp}/old.json")" "${tmp}/tree/${relative}"
echo "${checks} checks, ${failures} failed"
[[ ${failures} -eq 0 ]]
