#!/usr/bin/env python3
"""chip_tool - USB programming/diagnostic tool for the PHSI245 (CH32X035).

A friendlier front-end over chprog.py with read-only diagnostics. The device
must be in BOOT mode (hold the programming pin / use the physical BOOT button).

Subcommands:
  detect                    Connect and print chip info (no writes, no reboot).
  wipe                      Erase the ENTIRE code flash (factory reset).
  flash <bin>               Erase + program a bootloader image, then verify it.
  verify <bin>              Read-only check that the bootloader region [0, APP_BASE)
                            matches <bin> (padded with 0xFF). On a mismatch,
                            reports the first differing 8-byte window.
  flash-pair <boot> <game>  Program a complete bootloader + game into flash in
                            one pass: bootloader at 0x00000000, game code at
                            APP_BASE, and the app descriptor page at 0xF700, so
                            the device boots straight into the game. The game
                            may be a wrapped build/games/*.bin (PH25 header) or
                            a raw image linked at APP_BASE. After programming,
                            the device reboots; release the BOOT pin (or
                            power-cycle) to run the new bootloader+game.

Why verify exists: the bootloader is flashed at 0x00000000 and a game load
writes from APP_BASE upward. If the bootloader image ever reaches APP_BASE,
loading a game overwrites the bootloader's .data tail and the device comes up
dead on the next power-up. `verify <bin>` proves the bootloader region is
intact without erasing or writing anything. APP_BASE and friends must match
src/flash_load.h / linker/app_flash.ld / mkapp.py (imported here).
"""

import argparse
import re
import struct
import sys
import zlib

import chprog
from mkapp import APP_BASE, APP_MAX_SIZE, APP_MAGIC, APP_VERSION

# Full 62 KB of the CH32X035 (must equal isp.code_flash_size). The app
# descriptor page sits at the top; see src/flash_load.h.
FLASH_SIZE = 0xF800
DESC_ADDR = 0xF700

# PH25 wrapper header (see prog_tools/mkapp.py)
APP_HEADER_FMT = "<IHHIII"  # magic, version, flags, loadAddr, codeSize, crc


def connect():
    isp = chprog.Programmer()
    isp.detect()
    return isp


def cmd_detect(isp, args):
    uid = " ".join("%02x" % b for b in isp.chipuid)
    print(f"Chip:          {isp.chipname}")
    print(f"Bootloader:    v{isp.bootloader}")
    print(f"Code flash:    {isp.code_flash_size} bytes")
    print(f"Data flash:    {isp.data_flash_size} bytes")
    print(f"UID:           {uid}")
    return 0, None


def cmd_wipe(isp, args):
    print(f"Erasing {isp.code_flash_size} bytes of code flash ...")
    isp.erase(isp.code_flash_size)
    return 0, None


def cmd_flash(isp, args):
    with open(args.bin, "rb") as f:
        data = f.read()
    if len(data) > isp.code_flash_size:
        print(f"ERROR: image is {len(data)} bytes, larger than code flash.")
        return 1, None
    print(f"Erasing {len(data)} bytes and programming {args.bin} ...")
    isp.flash(data)
    return _verify_and_report(isp, _padded(data),
                              f"bootloader region [0x00000000, 0x{APP_BASE:04x})",
                              args.bin)


def _padded(data):
    """Bootloader image padded with 0xFF up to APP_BASE."""
    if len(data) > APP_BASE:
        print(f"ERROR: image is {len(data)} bytes, past APP_BASE 0x{APP_BASE:04x} "
              f"({APP_BASE} bytes). Rebuild with make build; check_boot.py should "
              f"have refused this.")
        sys.exit(1)
    return data + b"\xff" * (APP_BASE - len(data))


def parse_game(path):
    """Return (code, codeSize, crc) from a wrapped .bin or a raw game image.

    A wrapped image starts with the 20-byte PH25 header (build/games/*.bin);
    anything else is treated as raw code already linked at APP_BASE.
    """
    with open(path, "rb") as f:
        data = f.read()

    if data[:4] == APP_MAGIC:
        if len(data) < 20:
            raise Exception(f"{path}: PH25 header but file is only {len(data)} bytes")
        magic, version, flags, load, size, crc = struct.unpack(APP_HEADER_FMT, data[:20])
        if version != APP_VERSION:
            raise Exception(f"{path}: bad header version {version}, expected {APP_VERSION}")
        if load != APP_BASE:
            raise Exception(f"{path}: load addr 0x{load:04x}, expected APP_BASE 0x{APP_BASE:04x}")
        if size == 0 or size > APP_MAX_SIZE:
            raise Exception(f"{path}: code size {size} outside (0, {APP_MAX_SIZE}]")
        code = data[20:20 + size]
        if len(code) != size:
            raise Exception(f"{path}: header says {size} bytes but the file has "
                            f"only {len(data) - 20} after the header")
        calc = zlib.crc32(code) & 0xFFFFFFFF
        if calc != crc:
            raise Exception(f"{path}: CRC mismatch (header {crc:08x}, "
                            f"computed {calc:08x})")
        return code, size, crc

    if len(data) == 0 or len(data) > APP_MAX_SIZE:
        raise Exception(f"{path}: raw image size {len(data)} outside (0, {APP_MAX_SIZE}]")
    return data, len(data), zlib.crc32(data) & 0xFFFFFFFF


def build_image(boot, code, size, crc):
    """Assemble the full 62 KB image: bootloader + game code + descriptor page.

    Layout mirrors the bootloader's own load path (src/flash_load.cpp): game
    code at APP_BASE and a 256-byte descriptor page at 0xF700 holding
    magic / APP_BASE / codeSize / crc as little-endian u32s, rest 0xFF.
    """
    if len(boot) > APP_BASE:
        raise Exception(f"bootloader image is {len(boot)} bytes, past APP_BASE "
                        f"0x{APP_BASE:04x} ({APP_BASE} bytes)")
    if APP_BASE + size > DESC_ADDR:
        raise Exception(f"game code ({size} bytes) would overlap the descriptor "
                        f"page at 0x{DESC_ADDR:04x}")

    img = bytearray(b"\xff") * FLASH_SIZE
    img[0:len(boot)] = boot
    img[APP_BASE:APP_BASE + size] = code
    img[DESC_ADDR:DESC_ADDR + 4] = b"PH25"
    img[DESC_ADDR + 4:DESC_ADDR + 8] = struct.pack("<I", APP_BASE)
    img[DESC_ADDR + 8:DESC_ADDR + 12] = struct.pack("<I", size)
    img[DESC_ADDR + 12:DESC_ADDR + 16] = struct.pack("<I", crc)
    return img


def cmd_flash_pair(isp, args):
    if isp.code_flash_size < FLASH_SIZE:
        print(f"ERROR: this {isp.chipname} has {isp.code_flash_size} bytes of "
              f"code flash, but the PHSI245 layout needs {FLASH_SIZE}.")
        return 1, None
    with open(args.boot, "rb") as f:
        boot = f.read()
    code, size, crc = parse_game(args.game)
    img = build_image(boot, code, size, crc)

    print(f"Image: bootloader {len(boot)} B, game code {size} B (crc {crc:08x}), "
          f"descriptor at 0x{DESC_ADDR:04x}.")
    print(f"Erasing {len(img)} bytes of code flash ...")
    isp.erase(len(img))
    print("Programming ...")
    isp.writeflash(bytes(img), chprog.CH_CMD_CODE_WRITE, offset=0)
    return _verify_and_report(isp, bytes(img), "full flash image [0x00000000, "
                              f"0x{FLASH_SIZE:04x})", args.boot)


def _verify_region(isp, data):
    """Compare flash [0, len(data)) against data (must be a multiple of 8 bytes).
    Returns (exit_code, first_mismatch_offset_or_None)."""
    try:
        isp.writeflash(data, chprog.CH_CMD_CODE_VERIFY, offset=0)
        return 0, None
    except Exception as ex:
        m = re.search(r"0x([0-9a-f]+)", str(ex))
        off = int(m.group(1), 16) if m else 0
        return 1, _drill(isp, data, off)


def _drill(isp, data, off):
    """Narrow a failing 56-byte chunk down to the first mismatching 8-byte
    window. Returns the window's offset, or `off` if nothing re-verifies."""
    for i in range(0, 56, 8):
        window = data[off + i:off + i + 8]
        if len(window) < 8:
            window += b"\xff" * (8 - len(window))
        try:
            isp.writeflash(window, chprog.CH_CMD_CODE_VERIFY, offset=off + i)
        except Exception:
            return off + i
    return off


def _verify_and_report(isp, data, region_desc, src_name):
    """Verify, then print a PASS/FAIL summary and return (rc, first_diff)."""
    rc, first_diff = _verify_region(isp, data)
    if rc == 0:
        print(f"VERIFY OK: {region_desc} matches {src_name}.")
    else:
        sys.stderr.write(
            f"VERIFY FAILED: first difference at offset 0x{first_diff:04x} "
            f"({region_desc} differs from {src_name}).\n")
    return rc, first_diff


def cmd_verify(isp, args):
    with open(args.bin, "rb") as f:
        data = f.read()
    return _verify_and_report(isp, _padded(data),
                              f"bootloader region [0x00000000, 0x{APP_BASE:04x})",
                              args.bin)


def _finish(isp, args):
    try:
        isp.exit()
    except Exception:
        pass


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="PHSI245 (CH32X035) USB programming/diagnostic tool")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("detect", help="print chip info")
    sub.add_parser("wipe", help="erase entire code flash")
    p_flash = sub.add_parser("flash", help="erase + program + verify a bootloader image")
    p_flash.add_argument("bin", help="firmware.bin")
    p_verify = sub.add_parser("verify", help="read-only compare of the bootloader region")
    p_verify.add_argument("bin", help="firmware.bin")
    p_pair = sub.add_parser("flash-pair", help="program a bootloader + game pair")
    p_pair.add_argument("boot", help="bootloader image (raw firmware.bin)")
    p_pair.add_argument("game", help="game image (wrapped .bin or raw)")

    args = parser.parse_args(argv)

    try:
        isp = connect()
        print(f"Found {isp.chipname} with bootloader v{isp.bootloader}.")
    except Exception as ex:
        sys.stderr.write(f"ERROR: {ex}!\n")
        return 1

    handlers = {
        "detect": cmd_detect,
        "wipe": cmd_wipe,
        "flash": cmd_flash,
        "verify": cmd_verify,
        "flash-pair": cmd_flash_pair,
    }
    rc = 0
    try:
        rc, _ = handlers[args.command](isp, args)
    except Exception as ex:
        sys.stderr.write(f"ERROR: {ex}!\n")
        rc = 1

    if args.command != "detect":
        _finish(isp, args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
