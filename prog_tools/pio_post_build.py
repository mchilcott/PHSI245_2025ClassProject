"""PlatformIO post-build hook: bootloader check + game wrapping.

Registered as `post:prog_tools/pio_post_build.py` in extra_scripts (see
platformio.ini) for every env. A post: script runs when the build is
*configured*, before SCons actually builds anything, so this script only
attaches an `env.AddPostAction` to the firmware.bin node — the work runs after
the image exists. It gives the PlatformIO/VS Code workflow the same guards the
Makefile enforces:

  * bootloader build (src_dir == src_os)            -> check_boot.py (the image
    must stay below APP_BASE, or a game load would brick the device) and copy
    the image to build/bootloader.bin, the stable path the upload tooling uses.
  * game build (src_dir == src_games/<Name>)        -> mkapp.py wraps the image
    into build/games/<Name>.bin (PH25 header + CRC-32) for SD-card use.

The build type is read from $PROJECT_SRC_DIR, which is set once at startup
from the src_dir option (honouring the PLATFORMIO_SRC_DIR env var), so it is
consistent between this hook and the upload wrapper.
"""

import os
import shutil
import sys

from SCons.Errors import UserError
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

project_dir = env.subst("$PROJECT_DIR")
src_dir = env.subst("$PROJECT_SRC_DIR")
bin_path = os.path.join(env.subst("$BUILD_DIR"), env.subst("$PROGNAME") + ".bin")

# prog_tools/ holds check_boot.py / mkapp.py; make sure they import. Note the
# imports happen here (load time), not inside the post-action: SCons resets
# sys.path when it runs actions, so an import there would fail.
prog_tools = os.path.join(project_dir, "prog_tools")
if prog_tools not in sys.path:
    sys.path.insert(0, prog_tools)

from check_boot import check_image
from mkapp import wrap_image

GAME_PARENT = os.path.join(project_dir, "src_games")


def _game_name():
    """Return the game name when this build is a src_games/<Name> build."""
    norm = os.path.normpath(src_dir)
    if norm and os.path.basename(os.path.dirname(norm)) == "src_games" \
            and norm != GAME_PARENT:
        return os.path.basename(norm)
    return None


def _after_build(target, source, env):
    # The post-action's target is the firmware.bin node; its source is the ELF.
    img = str(target[0]) if target else bin_path
    game = _game_name()
    if game:
        out_dir = os.path.join(project_dir, "build", "games")
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, game + ".bin")
        if wrap_image(img, out):
            raise UserError(f"pio_post_build: mkapp.py refused {img}")
        print(f"pio_post_build: wrapped game image -> {out}")
    else:
        if check_image(img):
            raise UserError(
                "pio_post_build: bootloader image reaches APP_BASE; refusing "
                "build (see prog_tools/check_boot.py)")
        boot_dir = os.path.join(project_dir, "build")
        os.makedirs(boot_dir, exist_ok=True)
        shutil.copy2(img, os.path.join(boot_dir, "bootloader.bin"))
        print(f"pio_post_build: copied bootloader image -> "
              f"{os.path.join(boot_dir, 'bootloader.bin')}")


env.AddPostAction(bin_path, env.VerboseAction(_after_build, "PHSI245 post-build"))
