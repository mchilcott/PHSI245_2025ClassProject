#!/usr/bin/env python3
"""Fail the build if the bootloader image would overlap the app region.

The bootloader image occupies flash starting at 0x00000000; loading an app
writes from APP_BASE upward. If the bootloader image reaches APP_BASE, an app
load overwrites the bootloader's .data initializers (the tail of its flash
image), and the device comes up dead on the next power-up. This check refuses
such an image at build time so the bug can't silently return.

Usage: check_boot.py <firmware.bin>
"""

import sys

from mkapp import APP_BASE


def main():
    if len(sys.argv) != 2:
        print("Usage: check_boot.py <firmware.bin>")
        return 2

    with open(sys.argv[1], "rb") as f:
        size = len(f.read())

    end = 0x00000000 + size
    if end > APP_BASE:
        print(
            f"ERROR: bootloader image is {size} bytes, ending at 0x{end:04x} "
            f"which is past APP_BASE 0x{APP_BASE:04x} (={APP_BASE} bytes). "
            f"Loading an app would corrupt the bootloader's flash tail and "
            f"brick the device (dead on next power-up, no LED). "
            f"Trim the bootloader or move APP_BASE up; see src_os/flash_load.h."
        )
        return 1

    print(
        f"bootloader image OK: {size} bytes, ends at 0x{end:04x} "
        f"(headroom to APP_BASE 0x{APP_BASE:04x}: {APP_BASE - end} bytes)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
