# Soulwarden wire codec and state decoders (vendored)

Unchanged copies of these files of the Soulwarden repository (`~/Developer/azoth`, commit e0a3d97, the backend of the
release `2026-10-17-e0a3d97`), MIT License (file `LICENSE`, Copyright (c) 2026 DevNvll):

| File here | File of the Soulwarden repository |
| --- | --- |
| `src/server/hub/wire.h`, `wire.cpp` | `apps/backend/src/server/hub/` (the primitive encodings: numbers, size codes, texts, frames) |
| `tests/support/state_decoder.h` | `apps/backend/tests/support/` (the reader of a state bundle: each state type that the backend sends) |
| `tests/support/item_decoder.h`, `paperdoll_decoder.h`, `progress_decoder.h` | `apps/backend/tests/support/` (the readers of the item, paperdoll, and progress values) |

The backend tests use these readers to prove that a record of the backend has no unknown byte. The client uses them
to read each fragment of a record to its end: a fragment has no length, thus the client must know each state type
that the backend sends.

Do not edit the files here. To update, copy the files again from the Soulwarden repository, and write the new commit
in this file. Update them when the Soulwarden orchestrator reports a change of the wire bytes (the golden file of
case T2) or a new state type.
