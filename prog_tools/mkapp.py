#!/usr/bin/env python3
"""Wrap a game firmware.bin with the PHSI245 app header.

Reads the raw linker output of a game build (linked at APP_BASE) and emits a
20-byte header + code image for the bootloader's "Load & Run" path:

    [0..3]   magic     "PH25"
    [4..5]   version   1
    [6..7]   flags     0
    [8..11]  loadaddr  APP_BASE (0x00008000)
    [12..15] codesize  len(code)
    [16..19] crc32     zlib CRC-32 of code (matches on-device table CRC-32)
    [20..]   code

Usage: mkapp.py <firmware.bin> <out.bin>
"""

import struct
import sys
import zlib

APP_BASE = 0x00008000
APP_MAX_SIZE = 0x7700  # 30464 B — mirrors APP_MAX_SIZE in src/flash_load.h
APP_MAGIC = b"PH25"
APP_VERSION = 1
HEADER_SIZE = 20


def main():
    if len(sys.argv) != 3:
        print("Usage: mkapp.py <firmware.bin> <out.bin>")
        return 2

    src, dst = sys.argv[1], sys.argv[2]

    with open(src, "rb") as f:
        code = f.read()

    if len(code) > APP_MAX_SIZE:
        print(
            f"ERROR: game image is {len(code)} bytes, over the {APP_MAX_SIZE} "
            f"byte app-region limit ({APP_BASE:#x}..{APP_BASE + APP_MAX_SIZE:#x})."
        )
        return 1

    crc = zlib.crc32(code) & 0xFFFFFFFF
    header = (
        APP_MAGIC
        + struct.pack("<HHI", APP_VERSION, 0, APP_BASE)
        + struct.pack("<II", len(code), crc)
    )
    assert len(header) == HEADER_SIZE

    with open(dst, "wb") as f:
        f.write(header)
        f.write(code)

    print(f"Wrote {dst}: {len(code)} bytes code + {HEADER_SIZE}-byte header, "
          f"crc32={crc:08x}, loadaddr={APP_BASE:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
