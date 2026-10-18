# Soulwarden Client

A personal, local project: a game client in Unreal Engine 5.8 for the Soulwarden backend, with the content of the
game copy of the user converted on this computer. The Soulwarden project (`~/Developer/azoth`) keeps the retail
client and the backend; both projects run in parallel. The look can be different for now; the feel of the
gameplay and the UI must match the retail client.

Read [AGENTS.md](AGENTS.md) first (the rules and the workflows). The plan is in [docs/ROADMAP.md](docs/ROADMAP.md),
the design in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Needs

- The game copy in `~/Games/soulwarden/game` (read only).
- Unreal Engine 5.8 in `~/Applications/UnrealEngine-5.8.0` (its Oodle library also decodes the paks).
- Python 3 with numpy and Pillow (numba for the previews).
- The Soulwarden backend, built in `~/Developer/azoth/apps/backend/dist`.

## Quick start

```sh
tools/convert.sh proofs     # convert the four proofs of the research into ~/Games/soulwarden-client/content
tools/build.sh editor       # build the C++ module
tools/import.sh             # import the converted content into /Game/Imported (no window)
tools/editor.sh             # open the editor
```

## Status

Stage P0 (foundation) is done. Next: milestone 1, "walk in the start area" (docs/ROADMAP.md).
