"""Validate supplemental IDA names and match bounded original-image evidence."""

import json
from pathlib import Path
import re


CATALOG = Path(__file__).resolve().parents[1] / "ida/symbols.json"
LIMIT = 0x100000000
EA_LIMIT = 0x10000000000000000
MAX_EXTENT = 0x100000
NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_@]*\Z")
SOURCE = re.compile(r"https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/blob/[0-9a-f]{40}/[^\s?#]+\Z")


def _integer(value, minimum=0, maximum=LIMIT - 1):
    return type(value) is int and minimum <= value <= maximum


def _keys(value, required, optional=()):
    return isinstance(value, dict) and set(required) <= set(value) <= set(required) | set(optional)


def _hex(value):
    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9a-f]{2})+", value):
        raise ValueError("Invalid fingerprint hex bytes")
    return bytes.fromhex(value)


def _fingerprint(symbol):
    fingerprint = symbol["fingerprint"]
    if fingerprint is None:
        if symbol["kind"] != "data" or not symbol["zero_fill"]:
            raise ValueError("Missing fingerprint requires zero-fill data")
        return
    if symbol["zero_fill"] or not _keys(fingerprint, ("bytes", "mask", "pointers")):
        raise ValueError("Invalid fingerprint fields")
    original, mask = _hex(fingerprint["bytes"]), _hex(fingerprint["mask"])
    if (not 4 <= len(original) <= min(32, symbol["size"]) or len(original) != len(mask)
            or any(value not in (0, 255) for value in mask)):
        raise ValueError("Invalid fingerprint length or mask")
    pointers = fingerprint["pointers"]
    if not isinstance(pointers, list):
        raise ValueError("Invalid fingerprint pointers")
    occupied = set()
    for pointer in pointers:
        if (not _keys(pointer, ("offset", "rva")) or not _integer(pointer["offset"], 0, len(original) - 4)
                or not _integer(pointer["rva"], 1)):
            raise ValueError("Invalid fingerprint pointer")
        offsets = set(range(pointer["offset"], pointer["offset"] + 4))
        if occupied & offsets or any(mask[offset] for offset in offsets):
            raise ValueError("Fingerprint pointers must have separate masked slots")
        occupied.update(offsets)
    if mask.count(255) + len(occupied) < 4:
        raise ValueError("Fingerprint needs at least four checked bytes")


def _shape(symbol):
    if "shape" not in symbol:
        return
    shape = symbol["shape"]
    if (symbol["kind"] != "data" or not _keys(shape, ("rows", "columns", "stride", "storage"))
            or not _integer(shape["rows"], 1) or not _integer(shape["columns"], 1)
            or not _integer(shape["stride"], 1) or shape["storage"] not in ("u32", "cstring")):
        raise ValueError("Invalid data shape")
    if shape["storage"] == "u32":
        if (shape["stride"] < shape["columns"] * 4
                or (shape["rows"] - 1) * shape["stride"] + shape["columns"] * 4 != symbol["size"]):
            raise ValueError("Data shape exceeds the verified extent")
    elif shape["rows"] != 1 or shape["columns"] != 1:
        raise ValueError("Invalid string shape")


def validate(catalog, profiles):
    """Reject malformed names and evidence before an importer can use them."""
    if not _keys(catalog, ("schema", "sources", "builds")) or type(catalog["schema"]) is not int or catalog["schema"] != 1:
        raise ValueError("Invalid supplemental symbol schema")
    if not isinstance(profiles, dict) or not isinstance(profiles.get("builds"), list):
        raise ValueError("Invalid native profiles")
    known = {}
    for profile in profiles["builds"]:
        if (not isinstance(profile, dict) or not isinstance(profile.get("id"), str)
                or not isinstance(profile.get("sha256"), str) or profile["id"] in known):
            raise ValueError("Invalid native profile identity")
        known[profile["id"]] = profile["sha256"]
    sources = catalog["sources"]
    if (not isinstance(sources, dict) or not sources
            or any(not isinstance(key, str) or NAME.fullmatch(key) is None
                   or not isinstance(url, str) or SOURCE.fullmatch(url) is None for key, url in sources.items())):
        raise ValueError("Invalid pinned symbol sources")
    builds = catalog["builds"]
    if not isinstance(builds, dict) or not builds:
        raise ValueError("Invalid supplemental builds")
    for build_id, build in builds.items():
        if build_id not in known:
            raise ValueError("Unknown supplemental build")
        if (not _keys(build, ("sha256", "symbols")) or build["sha256"] != known[build_id]
                or not isinstance(build["sha256"], str) or re.fullmatch(r"[0-9a-f]{64}", build["sha256"]) is None):
            raise ValueError("Supplemental build SHA-256 differs from its native profile")
        if not isinstance(build["symbols"], list):
            raise ValueError("Invalid supplemental symbol rows")
        names = set()
        for symbol in build["symbols"]:
            if not _keys(symbol, ("name", "source_name", "source", "kind", "rva", "size", "fingerprint", "zero_fill"), ("shape",)):
                raise ValueError("Invalid supplemental symbol fields")
            if (not isinstance(symbol["name"], str) or NAME.fullmatch(symbol["name"]) is None
                    or not symbol["name"].startswith("XFiles_") or symbol["name"] in names
                    or not isinstance(symbol["source_name"], str) or NAME.fullmatch(symbol["source_name"]) is None):
                raise ValueError("Invalid or duplicate supplemental name")
            names.add(symbol["name"])
            if not isinstance(symbol["source"], str) or symbol["source"] not in sources:
                raise ValueError("Unknown supplemental symbol source")
            if symbol["kind"] not in ("function", "data") or type(symbol["zero_fill"]) is not bool:
                raise ValueError("Invalid supplemental symbol kind")
            if (not _integer(symbol["rva"], 1) or not _integer(symbol["size"], 1, MAX_EXTENT)
                    or symbol["rva"] + symbol["size"] > LIMIT):
                raise ValueError("Invalid supplemental RVA or size")
            _fingerprint(symbol)
            _shape(symbol)


def load(profiles):
    """Load the shipped catalog and bind it to the native profile identities."""
    try:
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("Cannot read supplemental symbol catalog") from error
    validate(catalog, profiles)
    return catalog


def rows(catalog, build_id):
    return catalog["builds"][build_id]["symbols"]


def matches(symbol, base, read_bytes):
    """Check the prefix and rebased pointers. The caller checks mapping and code."""
    try:
        if (not _integer(base, 0, EA_LIMIT - 1) or not _integer(symbol["rva"], 1)
                or not _integer(symbol["size"], 1, MAX_EXTENT)
                or base + symbol["rva"] + symbol["size"] > EA_LIMIT):
            return False
        _fingerprint(symbol)
        fingerprint = symbol["fingerprint"]
        if fingerprint is None:
            return symbol["kind"] == "data" and symbol["zero_fill"] is True
        original, mask = bytes.fromhex(fingerprint["bytes"]), bytes.fromhex(fingerprint["mask"])
        actual = read_bytes(base + symbol["rva"], len(original))
        if not isinstance(actual, (bytes, bytearray)) or len(actual) != len(original) or len(mask) != len(original):
            return False
        if any((value ^ expected) & enabled for value, expected, enabled in zip(actual, original, mask)):
            return False
        for pointer in fingerprint["pointers"]:
            offset, target = pointer["offset"], base + pointer["rva"]
            if target >= LIMIT or int.from_bytes(actual[offset:offset + 4], "little") != target:
                return False
        return True
    except Exception:
        return False
