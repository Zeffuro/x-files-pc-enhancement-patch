"""Generate small synthetic parser seeds without redistributing game assets."""

import struct
import sys
from pathlib import Path


def words(*values):
    return struct.pack(">" + "I" * len(values), *values)


def atom(kind, body):
    return words(len(body) + 8) + kind.encode("ascii") + body


def movie():
    description = bytearray(78)
    for offset, value in ((7, 1), (25, 80), (27, 60), (75, 24)):
        description[offset] = value
    table = atom("stsd", words(0, 2) + atom("jpeg", description) + atom("cvid", description))
    for name, values in (
        ("stco", (0, 2, 8, 16)),
        ("stsc", (0, 2, 1, 2, 1, 2, 2, 2)),
        ("stsz", (0, 4, 4)),
        ("stts", (0, 2, 2, 10, 2, 20)),
        ("stss", (0, 2, 1, 3)),
    ):
        table += atom(name, words(*values))
    media = atom("mdhd", words(0, 0, 0, 100, 60))
    media += atom("hdlr", words(0, 0) + b"vide")
    media += atom("minf", atom("stbl", table))
    track = atom("tkhd", words(15, 0, 0, 1, 0, 65))
    track += atom("edts", atom("elst", words(0, 2, 5, 0xFFFFFFFF, 65536, 60, 0, 65536)))
    track += atom("mdia", media)
    return atom("mdat", bytes.fromhex("ffd80102ffd803040000000400000004")) + atom(
        "moov", atom("mvhd", words(0, 0, 0, 100, 65)) + atom("trak", track)
    )


def picture():
    def shorts(*values):
        return struct.pack(">" + "H" * len(values), *values)

    rect = shorts(10, 20, 12, 22)
    header = shorts(0) + rect + shorts(0x11, 0x02FF, 0x0C00) + bytes(24)
    bitmap = words(255) + shorts(0x8008) + rect + shorts(0, 4) + bytes(12)
    bitmap += shorts(16, 32, 4, 8) + bytes(12) + rect + rect + shorts(0)
    bitmap += bytes((9, 7, 255, 255, 255, 0, 0, 255, 0, 0))
    bitmap += bytes((9, 7, 255, 255, 0, 255, 0, 255, 255, 255))
    return header + shorts(0x009A) + bitmap + shorts(0x00FF)


def save():
    data = bytearray(512)

    def put(offset, value, width=4):
        data[offset:offset + width] = value.to_bytes(width, "big")

    for offset, value in ((0, 5), (8, 24), (12, 4), (16, 4), (20, 0x501), (26, 1), (43, 1)):
        put(offset, value)
    put(30, 4, 2)
    put(40, 3, 2)
    put(42, 1, 1)
    for i, cls in enumerate((0x46, 0x50, 0x56)):
        entry, node, record = 82 + i * 43, 256 + i * 32, 384 + i * 32
        for offset, value in ((entry, 1), (entry + 4, cls), (entry + 8, 1),
                              (entry + 24, node), (entry + 28, cls), (node + 2, 1),
                              (node + 8, record), (node + 12, i + 1), (record + 2, 1)):
            put(offset, value)
        put(node, 0x80, 1)
        put(node + 6, 1, 2)
    return data


if __name__ == "__main__":
    root = Path(sys.argv[1])
    for name, factory in (("movie", movie), ("picture", picture), ("save", save)):
        directory = root / name
        directory.mkdir(parents=True, exist_ok=True)
        data = factory()
        (directory / "valid").write_bytes(data)
        (directory / "truncated").write_bytes(data[:len(data) // 2])
