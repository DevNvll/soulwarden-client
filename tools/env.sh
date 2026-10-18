#!/usr/bin/env bash
# Paths of the Soulwarden client project. Source this file; each value can be set before.
# Nothing here is a private value of the user.
: "${SWC_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
: "${SWC_WORK:=${HOME}/Games/soulwarden-client}"              # work folder, outside the repository
: "${SWC_CONTENT:=${SWC_WORK}/content}"                          # converted content (made from the game copy)
: "${SOULWARDEN_REPO:=${HOME}/Developer/azoth}"                  # the Soulwarden repository (the backend)
: "${SOULWARDEN_GAME_DIR:=${HOME}/Games/soulwarden/game}"        # the game copy of the user (read only)
: "${UE_ROOT:=${HOME}/Applications/UnrealEngine-5.8.0}"
: "${SOULWARDEN_OODLE_LIB:=${UE_ROOT}/Engine/Binaries/DotNET/AutomationTool/AutomationScripts/BuildGraph/net10.0/runtimes/linux-x64/native/liboo2core.so}"
: "${SWC_BACKEND_EXE:=${SOULWARDEN_REPO}/apps/backend/dist/soulwarden-backend}"
: "${SWC_GAMEDATA_DIR:=${XDG_DATA_HOME:-${HOME}/.local/share}/soulwarden/gamedata}"   # read only for the backend
: "${SWC_BACKEND_DATA:=${SWC_WORK}/backend-data}"                # own database and logs, never the data of the user
SWC_UPROJECT="${SWC_REPO}/unreal/SoulwardenClient/SoulwardenClient.uproject"
export SWC_REPO SWC_WORK SWC_CONTENT SOULWARDEN_REPO SOULWARDEN_GAME_DIR UE_ROOT SOULWARDEN_OODLE_LIB \
    SWC_BACKEND_EXE SWC_GAMEDATA_DIR SWC_BACKEND_DATA SWC_UPROJECT
export PYTHONDONTWRITEBYTECODE=1
