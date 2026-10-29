#!/usr/bin/env bash
# Copies the backend and the game data of a Soulwarden release into the work folder of the client.
# The client runs only this copy: a backend stage of the Soulwarden project rebuilds apps/backend/dist before its
# live check, so the working copy can have a backend that is not tested. Update the copy on purpose.
# Usage: tools/backend-update.sh [RELEASE]      (default: the current release of the user; read only)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
releases="${HOME}/Games/soulwarden/releases"
release_dir="$(readlink -f "${releases}/${1:-current}")"
version="$(cat "${release_dir}/VERSION")"
target="${SWC_WORK}/backend/${version}"
if [[ ! -d "${target}" ]]; then
    mkdir -p "${target}.part"
    install -m 755 "${release_dir}/apps/backend/dist/soulwarden-backend" "${target}.part/soulwarden-backend"
    # Hard links: the files of a release do not change after the release; a later prune of the release keeps them.
    cp -al "${release_dir}/gamedata" "${target}.part/gamedata" 2>/dev/null || cp -a "${release_dir}/gamedata" "${target}.part/gamedata"
    cp "${release_dir}/release.json" "${target}.part/release.json"
    mv "${target}.part" "${target}"
fi
ln -sfn "${version}" "${SWC_WORK}/backend/current"
echo "client backend: ${version} (${target})"
user_version="$(cat "${releases}/current/VERSION")"
if [[ "${version}" != "${user_version}" ]]; then
    echo "note: the current release of the user is ${user_version}; a player run needs that version (shared database schema)" >&2
fi
# The vendored state decoders of the client against this release (tools/lib/vendor_check.py): a new state type of the backend needs a
# new copy (a commit of its own; Net/Vendor/soulwarden/NOTICE.md). Status 4: the copy is done, the decoders are older.
vendor_line="$(python3 -B -I "${SWC_REPO}/tools/lib/vendor_check.py" "${target}" \
    "${SWC_REPO}/unreal/SoulwardenClient/Source/SoulwardenClient/Net/Vendor/soulwarden" "${SOULWARDEN_REPO:-${HOME}/Developer/azoth}")"
echo "${vendor_line}" >&2
[[ "${vendor_line}" == ok* ]] || exit 4
