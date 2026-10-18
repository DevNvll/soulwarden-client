# Soulwarden readers (vendored)

Unchanged copies of `tools/gamedata/nav_assets.py`, `nav_terrain.py`, `nav_slices.py`, `nav_shapes.py`, and
`navcore.cpp` of the Soulwarden repository (`~/Developer/azoth`, commit e0a3d97), MIT License (file `LICENSE`,
Copyright (c) 2026 DevNvll), and of its `vendor/open-simplex-noise` (public domain, its own `LICENSE`). Do not edit
them here. To update, copy the files again from the Soulwarden repository.

`navcore.cpp` gives the terrain cookies of the capitals (`navcore noise`, `navcore cookies`): the converter compiles it
with `-ffp-contract=off` (converter/swc_convert/terrain.py).
