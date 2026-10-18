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
: "${SWC_BACKEND_DIR:=${SWC_WORK}/backend/current}"              # a copy of a Soulwarden release (tools/backend-update.sh)
: "${SWC_BACKEND_EXE:=${SWC_BACKEND_DIR}/soulwarden-backend}"
: "${SWC_GAMEDATA_DIR:=${SWC_BACKEND_DIR}/gamedata}"               # the game data of the same release
# Player runs (tools/play.sh, started by the user) use the data folder of the user: the same characters as the
# retail game, with the run.lock of the Soulwarden launcher. Development runs of agents never use it.
: "${SWC_PLAYER_DATA:=${XDG_DATA_HOME:-${HOME}/.local/share}/soulwarden}"
: "${SWC_DEV_DATA:=${SWC_WORK}/dev-data}"                          # development runs: own database, a seed character
: "${SOULWARDEN_RELEASES:=${HOME}/Games/soulwarden/releases}"     # the releases of the Soulwarden project (read only)
: "${SWC_STEAM_DIR:=${HOME}/.local/share/Steam}"                  # only config/loginusers.vdf is read (tools/play.sh)
: "${SWC_HTTP_PORT:=32200}"                                        # the HTTP listener of the backend, in the run namespace
: "${SWC_CLIENT_EXE:=${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor}"       # the client window (game mode)
: "${SWC_CLIENT_CMD:=${UE_ROOT}/Engine/Binaries/Linux/UnrealEditor-Cmd}"   # the client with no window (-nullrhi)
SWC_UPROJECT="${SWC_REPO}/unreal/SoulwardenClient/SoulwardenClient.uproject"
export SWC_REPO SWC_WORK SWC_CONTENT SOULWARDEN_REPO SOULWARDEN_GAME_DIR UE_ROOT SOULWARDEN_OODLE_LIB \
    SWC_BACKEND_DIR SWC_BACKEND_EXE SWC_GAMEDATA_DIR SWC_PLAYER_DATA SWC_DEV_DATA SOULWARDEN_RELEASES SWC_STEAM_DIR \
    SWC_HTTP_PORT SWC_CLIENT_EXE SWC_CLIENT_CMD SWC_UPROJECT
export PYTHONDONTWRITEBYTECODE=1
