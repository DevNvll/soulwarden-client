# Soulwarden wire codec and state decoders (vendored)

Unchanged copies of these files of the Soulwarden repository (`~/Developer/azoth`, commit 340fbc7, the backend of the
release `2026-10-30-340fbc7`; before: 5b79d75 of `2026-10-30-5b79d75`, 1bd3e43 of `2026-10-29-1bd3e43`, 7f31a6e of `2026-10-29-7f31a6e`, a5ceec7 of `2026-10-29-a5ceec7`, 87865ea of `2026-10-29-87865ea`, ef7daf6 of `2026-10-22-ef7daf6`), MIT License (file `LICENSE`, Copyright (c) 2026 DevNvll):

| File here | File of the Soulwarden repository |
| --- | --- |
| `src/server/hub/wire.h`, `wire.cpp` | `apps/backend/src/server/hub/` (the primitive encodings: numbers, size codes, texts, frames) |
| `tests/support/state_decoder.h` | `apps/backend/tests/support/` (the reader of a state bundle: each state type that the backend sends) |
| `tests/support/item_decoder.h`, `paperdoll_decoder.h`, `progress_decoder.h` | `apps/backend/tests/support/` (the readers of the item, paperdoll, and progress values) |

The backend tests use these readers to prove that a record of the backend has no unknown byte. The client uses them
to read each fragment of a record to its end: a fragment has no length, thus the client must know each state type
that the backend sends.

Do not edit the files here. To update, copy the files again from the Soulwarden repository, and write the new commit
in this file. The copy of each new backend release is a commit of its own (through the orchestrator).

The check (2026-10-29): `tools/play.sh --check` and `tools/backend-update.sh` compare these files with the files of the
commit of the backend copy (release.json "commit"; `tools/lib/vendor_check.py`, `git show` in the read-only Soulwarden
repository). A difference fails the check ("the vendored decoder is older than the backend release: copy it"), and
`tools/backend-update.sh` ends with status 4 after its copy. Update them when the Soulwarden orchestrator reports a change of the wire bytes (the golden file of
case T2) or a new state type.
