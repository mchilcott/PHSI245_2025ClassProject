#!/usr/bin/env python3
"""Generate .vscode/tasks.json for the PlatformIO/VS Code workflow.

Scans src_games/ for game directories (any dir containing a .ino sketch) and
writes .vscode/tasks.json with:

  * "Refresh Games List"       -> python3 prog_tools/gen_tasks.py (this script,
                                   re-runs after new games are added)
  * "Upload Bootloader"        -> pio run -e v2 -t upload  (src_dir = src_os)
  * "Build Game: <Name>"       -> pio run -e v2            (src_dir override)
  * "Upload Game: <Name>"      -> pio run -e v2 -t upload  (src_dir override)

The game tasks set PLATFORMIO_SRC_DIR and PLATFORMIO_BUILD_FLAGS (the app
linker), mirroring `make build-game GAME=<Name>`. PlatformIO's src_dir is a
global option that can only be overridden per-run via the PLATFORMIO_SRC_DIR
env var, which is why the tasks (not plain per-game [env:...] sections) set it.

A game build's "Build" task also wraps the image into build/games/<Name>.bin
(see prog_tools/pio_post_build.py) for SD-card use; "Upload Game" programs the
full bootloader + game pair. Run the "Upload Bootloader" task once before any
"Upload Game" task.

Usage: python3 prog_tools/gen_tasks.py  (run from the repo root)
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "src_games")
OUT = os.path.join(ROOT, ".vscode", "tasks.json")

APP_LINKER = "-Wl,-T,${workspaceFolder}/linker/app_flash.ld"


def _game_names():
    names = []
    for entry in sorted(os.listdir(GAME_DIR)):
        full = os.path.join(GAME_DIR, entry)
        if not os.path.isdir(full):
            continue
        if any(fn.endswith(".ino") for fn in os.listdir(full)):
            names.append(entry)
    return names


def _task(label, args, env=None, command="pio", matcher=("$platformio",)):
    task = {
        "label": label,
        "type": "process",
        "command": command,
        "args": args,
        "options": {"cwd": "${workspaceFolder}"},
        "problemMatcher": list(matcher),
        "group": "build",
    }
    if env:
        task["options"]["env"] = env
    return task


def main():
    names = _game_names()
    tasks = [
        _task("Refresh Games List", ["prog_tools/gen_tasks.py"],
              command="python3", matcher=()),
        _task("Upload Bootloader", ["run", "-e", "v2", "-t", "upload"]),
    ]
    for name in names:
        env = {
            "PLATFORMIO_SRC_DIR": "${workspaceFolder}/src_games/" + name,
            "PLATFORMIO_BUILD_FLAGS": APP_LINKER,
        }
        tasks.append(_task(f"Build Game: {name}", ["run", "-e", "v2"], env))
        tasks.append(_task(
            f"Upload Game: {name}", ["run", "-e", "v2", "-t", "upload"], env))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump({"version": "2.0.0", "tasks": tasks}, f, indent=2)
        f.write("\n")

    print(f"Wrote {OUT}: {len(names)} games -> {len(tasks)} tasks")
    for name in names:
        print(f"  - {name}")


if __name__ == "__main__":
    main()
