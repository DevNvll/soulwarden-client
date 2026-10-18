"""The game copy as a source: pak entries, the asset catalog, and object streams.

The pak reader is the reader of the Soulwarden repository (vendor/soulwarden, unchanged copy).
"""
from __future__ import annotations

import pickle
import uuid

from . import env  # noqa: F401  (sets the paths of the vendor readers)
from nav_assets import (Catalog, Element, Failure, Paks, asset_id, field_id, number, parse,  # noqa: F401
                        real, text, walk)

CATALOG_ENDINGS = (".mtl", ".cgf", ".skin", ".chr", ".cdf", ".dds", ".caf", ".dynamicslice", ".rnr", ".adb",
                   ".chrparams", ".tif", ".png", ".bspace", ".comb", ".animevents", ".cloth", ".xml", ".aliasasset",
                   ".skintonedb", ".hairstyledb", ".ent", ".timeline", ".actionlist")

_paks: Paks | None = None
_catalog: dict[bytes, list[tuple[int, str]]] | None = None


def paks() -> Paks:
    global _paks
    if _paks is None:
        _paks = Paks(env.cache_dir() / "pak-index.pickle")
    return _paks


def read(path: str) -> bytes:
    """@return The plain content of one pak entry (lower-case path)."""
    return paks().read(path.lower().replace("\\", "/"))


def has(path: str) -> bool:
    return paks().has(path.lower().replace("\\", "/"))


def catalog() -> dict[bytes, list[tuple[int, str]]]:
    """@return The asset catalog: 16-byte GUID -> [(sub id, product path)]."""
    global _catalog
    if _catalog is None:
        cache = env.cache_dir() / "catalog.pickle"
        if cache.is_file():
            _catalog = pickle.loads(cache.read_bytes())
        else:
            table = Catalog(read("assetcatalog.catalog"), CATALOG_ENDINGS)
            _catalog = dict(table.by_guid)
            cache.write_bytes(pickle.dumps(_catalog))
    return _catalog


def guid_bytes(text_id: str) -> bytes:
    return uuid.UUID(text_id.strip("{}").split("}")[0]).bytes


def product(guid: bytes, sub: int | None = None, ending: str = "") -> str | None:
    """@return The product path of an asset id (exact sub id first), with a wanted ending."""
    products = catalog().get(guid, [])
    if sub is not None:
        for s, p in products:
            if s == sub and p.endswith(ending):
                return p
    for _s, p in products:
        if p.endswith(ending):
            return p
    return None


def product_of_text(asset_text: str, ending: str = "") -> str | None:
    """@return The product of an asset id text such as "{GUID}:100000" or "{GUID}"."""
    if not asset_text or not asset_text.startswith("{"):
        return None
    guid = asset_text.split("}")[0] + "}"
    sub = None
    if "}:" in asset_text:
        try:
            sub = int(asset_text.split("}:")[1], 16)
        except ValueError:
            sub = None
    return product(guid_bytes(guid), sub, ending)
