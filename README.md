# Soulwarden Client

A personal, local project: an open world game client in Unreal Engine 5.8 for the Soulwarden backend, with the
content of the game copy of the user converted on this computer, the Steam account of the user, and the same
characters as the retail game. The Soulwarden project (`~/Developer/azoth`) keeps the retail
client and the backend; both projects run in parallel. The look can be different for now; the feel of the
gameplay and the UI must match the retail client.

Read [AGENTS.md](AGENTS.md) first (the rules and the workflows). The plan is in [docs/ROADMAP.md](docs/ROADMAP.md),
the design in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Game data and legal

This repository has only source code, scripts, tests, and documents. It has no game data: no texture, mesh, map,
sound, or script of the game. The converter reads your own game copy on your computer, and the converted and
imported content stays outside the repository. You need your own copy of New World (Steam app 1063730, client build
1.400.6031.6004151). This project is not affiliated with or endorsed by Amazon Games. New World is a trademark of its
owner. The backend is the Soulwarden project (github.com/DevNvll/soulwarden).

## Needs

- The game copy in `~/Games/soulwarden/game` (read only).
- Unreal Engine 5.8 in `~/Applications/UnrealEngine-5.8.0` (its Oodle library also decodes the paks).
- Python 3 with numpy and Pillow (numba for the previews).
- A Soulwarden release in `~/Games/soulwarden/releases` (`tools/backend-update.sh` copies its backend and game data).

## Quick start

```sh
tools/convert.sh proofs     # convert the four proofs of the research into ~/Games/soulwarden-client/content
tools/build.sh editor       # build the C++ module
tools/import.sh             # import the converted content into /Game/Imported (no window)
tools/editor.sh             # open the editor
```

## Status

Milestone 1 (the open world, the connection, the character, the creatures, the first HUD) is done. The first fight
loop (P2), parts of the combat families (P3), and the world fidelity work (the terrain, the materials, the light,
the water) are in progress. The order and the status are in docs/ROADMAP.md and docs/PARITY.md.
