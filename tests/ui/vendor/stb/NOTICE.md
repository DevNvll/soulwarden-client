# stb_truetype.h (vendored, tests only)

An unchanged copy of `stb_truetype.h` v1.26 of https://github.com/nothings/stb, commit
`6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760` (2024-07-15), SHA-256
`ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`. License: MIT or public domain (the end of the
file). Author: Sean Barrett.

Only the offline UI tests use it (`tools/test-ui.sh`): the font metrics of the converted font files (TrueType and CFF),
so that the text layouts of the tests have real widths. The client does not use it (the client measures with Slate).

Do not edit the file. To update, copy it again from a commit and record the commit and the checksum.
