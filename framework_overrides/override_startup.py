# PlatformIO extra_scripts (pre) hook.
#
# Makes `cores/arduino/ch32/startup_ch32yyxx.S` pick up the repo-local
# startup_ch32x035.S (which runs in machine mode, csrs mstatus 0x1888)
# instead of the framework's stock copy.
#
# startup_ch32yyxx.S does `#include "startup_ch32x035.S"`; the file is not in
# the including directory, so resolution walks the -I path. Prepend our dir to
# CPPPATH so it is searched before the framework's system/.../SRC/Startup dir.
# Prepend wins regardless of whether this script runs before or after the
# framework builder (which only Appends).
import os

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

override_dir = os.path.join(env.subst("$PROJECT_DIR"), "framework_overrides")
override_file = os.path.join(override_dir, "startup_ch32x035.S")

with open(override_file) as f:
    text = f.read()

if "0x1888" not in text or "csrs mstatus" not in text:
    raise SystemExit(
        "framework_overrides/startup_ch32x035.S is missing the machine-mode "
        "patch (li t0, 0x1888 / csrs mstatus, t0). Refusing to build."

    )

env.Prepend(CPPPATH=[override_dir])
print("override_startup.py: using repo startup_ch32x035.S (machine mode)")
