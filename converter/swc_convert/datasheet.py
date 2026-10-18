"""Data tables of the game (.datasheet): a reader for the rows of one table.

The layout is the layout that the item catalog tool of the Soulwarden repository reads
(tools/gamedata/item_catalog.py, class Sheet; little-endian, format version 0x12):

  0x08 nameOffset:u32   0x10 typeOffset:u32   0x38 textBlock:u32 (relative to 0x3C)
  0x44 columns:u32 rows:u32
  0x5C columns * {crc:u32 nameOffset:u32 kind:u32}, then rows * columns * {text:u32 value:u32}
  A cell of the kind 1 is a text (offset in the text block), 2 is a float, 3 is a flag.
"""
from __future__ import annotations

import struct

from . import source

TABLES_FOLDER = "sharedassets/springboardentitites/datatables/"
HEADER_SIZE = 0x5C
CELL_TEXT, CELL_NUMBER = 1, 2


class Sheet:
    def __init__(self, data: bytes) -> None:
        if len(data) < HEADER_SIZE:
            raise ValueError("the data table is too short")
        self._data = data
        self._texts = struct.unpack_from("<I", data, 0x38)[0] + 0x3C
        self.name = self._text(struct.unpack_from("<I", data, 0x08)[0])
        self.type = self._text(struct.unpack_from("<I", data, 0x10)[0])
        column_count, self.row_count = struct.unpack_from("<II", data, 0x44)
        self.columns: list[str] = []
        self._kinds: list[int] = []
        offset = HEADER_SIZE
        for _ in range(column_count):
            _, name_offset, kind = struct.unpack_from("<III", data, offset)
            self.columns.append(self._text(name_offset))
            self._kinds.append(kind)
            offset += 12
        self._cells = offset

    def _text(self, offset: int) -> str:
        start = self._texts + offset
        return self._data[start:self._data.index(b"\0", start)].decode("utf-8", "replace")

    def cell(self, row: int, column: int):
        offset = self._cells + (row * len(self.columns) + column) * 8
        text, raw = struct.unpack_from("<II", self._data, offset)
        kind = self._kinds[column]
        if kind == CELL_TEXT:
            return self._text(text)
        if kind == CELL_NUMBER:
            return struct.unpack_from("<f", self._data, offset + 4)[0]
        return bool(raw)

    def rows(self):
        """@return Each row as a dict of column name to value."""
        for row in range(self.row_count):
            yield {name: self.cell(row, column) for column, name in enumerate(self.columns)}


def load(file_name: str) -> Sheet:
    """@return The table of one file of the data table folder (for example javelindata_itemappearancedefinitions.datasheet)."""
    return Sheet(source.read(TABLES_FOLDER + file_name))
