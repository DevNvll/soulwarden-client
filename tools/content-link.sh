#!/usr/bin/env bash
# Makes the shared place of the imported Unreal content and links it into this checkout.
# The imported assets are made from the game copy: they cannot be in git, and one copy serves the root checkout and
# each worktree. The place is a content plugin outside the repository (SWC_GAME_CONTENT, mount point
# /SwcGameContent/); this checkout gets the link unreal/SoulwardenClient/Plugins/SwcGameContent (git ignores it).
# The tools that start Unreal call this script; it does nothing when the link is in place.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
descriptor="${SWC_GAME_CONTENT}/SwcGameContent.uplugin"
if [[ ! -f "${descriptor}" ]]; then
    mkdir -p "${SWC_GAME_CONTENT}/Content"
    cat > "${descriptor}" <<'JSON'
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "Soulwarden game content",
	"Description": "The Unreal assets made from the game copy of the user (converted and imported on this computer). Not in git.",
	"Category": "Soulwarden",
	"EnabledByDefault": true,
	"CanContainContent": true,
	"Installed": false
}
JSON
fi
link="${SWC_REPO}/unreal/SoulwardenClient/Plugins/SwcGameContent"
if [[ "$(readlink "${link}" 2>/dev/null)" != "${SWC_GAME_CONTENT}" ]]; then
    mkdir -p "$(dirname "${link}")"
    if [[ -e "${link}" && ! -L "${link}" ]]; then
        echo "content-link: ${link} exists and is not a link; nothing changed" >&2
        exit 1
    fi
    ln -sfn "${SWC_GAME_CONTENT}" "${link}"
fi
