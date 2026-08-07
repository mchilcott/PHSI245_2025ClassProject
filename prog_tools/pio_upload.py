#!/usr/bin/env python3
"""PlatformIO upload wrapper (driven by platformio.ini upload_command).

Invoked as

    "$PYTHONEXE" prog_tools/pio_upload.py $SOURCE $PROJECT_SRC_DIR $PROJECT_DIR

and dispatches on the src_dir the build used (same value the post-build hook
sees, so the two can't disagree):

  * src_os            -> chip_tool flash <SOURCE>             (bootloader+verify)
  * src_games/<Name>  -> chip_tool flash-pair <boot> <SOURCE>

so a game "Upload" programs the full bootloader + game + descriptor pair in one
pass. The bootloader image must already exist at build/bootloader.bin, which the
pio_post_build.py hook writes whenever the bootloader env is built; run the
"Upload Bootloader" VS Code task (or `pio run -e v2 -t upload`) first.

If the interpreter PlatformIO picked ($PYTHONEXE, its bundled Python) can't
import chip_tool — it needs pyusb + chprog (see AGENTS.md) — the script
re-execs itself under python3/python/py so a system Python that does have the
deps is used.
"""

import os
import shutil
import subprocess
import sys

# chprog.py needs system DLLs; program.py used the same workaround.
if os.name == "nt":
    os.environ["PATH"] = os.environ.get("WINDIR", r"C:\Windows") \
        + r"\System32;" + os.environ["PATH"]

USAGE = "usage: pio_upload.py <firmware.bin> <src_dir> <project_dir>"


def _run_chip_tool(argv):
    """Run chip_tool.main, re-execing under another interpreter if needed."""
    try:
        import chip_tool
    except Exception:
        return _reexec()
    return chip_tool.main(argv)


def _reexec():
    if os.environ.get("PIO_UPLOAD_REEXEC"):
        sys.stderr.write(
            "ERROR: no Python interpreter with prog_tools deps (pyusb/chprog) "
            "was found.\nInstall chprog first: `pip install chprog` "
            "(see AGENTS.md).\n")
        return 1
    env = dict(os.environ)
    env["PIO_UPLOAD_REEXEC"] = "1"
    here = os.path.abspath(__file__)
    for name in ("python3", "python", "py"):
        path = shutil.which(name)
        if not path or os.path.realpath(path) == os.path.realpath(sys.executable):
            continue
        try:
            return subprocess.call([path, here] + sys.argv[1:], env=env)
        except OSError:
            continue
    sys.stderr.write(
        "ERROR: could not re-exec under python3/python/py with chprog.\n")
    return 1


def main(argv):
    if len(argv) != 3:
        print(USAGE)
        return 2
    source, src_dir, project_dir = argv

    if not os.path.exists(source):
        sys.stderr.write(f"ERROR: firmware image not found: {source}\n")
        return 1

    norm = os.path.normpath(src_dir)
    is_game = os.path.basename(os.path.dirname(norm)) == "src_games"

    if not is_game:
        print(f"pio_upload: flashing bootloader image "
              f"{os.path.basename(source)} (verify included)")
        return _run_chip_tool(["flash", source])

    boot = os.path.join(project_dir, "build", "bootloader.bin")
    if not os.path.exists(boot):
        sys.stderr.write(
            "ERROR: build/bootloader.bin not found. Build + upload the "
            "bootloader first\n"
            "(VS Code task 'Upload Bootloader', or `pio run -e v2 -t upload` "
            "from src_os),\nthen retry this game upload.\n")
        return 1

    print(f"pio_upload: flashing bootloader + game pair "
          f"({os.path.basename(boot)} + {os.path.basename(source)})")
    return _run_chip_tool(["flash-pair", boot, source])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
