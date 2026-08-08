# AGENTS.md

## Hardware

The project targets the **v2** board revision only (buttons A/B on PB0/PB1, SD
card on SPI1 with PA9–PA12). Pinout is defined in `lib/PHSI245_HAL/HAL.h`. There
is no v1 board support — code should not use `HW_VERSION` guards.

## Build & Verify

- `make build` builds the bootloader (`src/`) for v2. `make upload` builds and
  flashes.
- The bootloader check (`check_boot.py`) + copy to `build/bootloader.bin`, and
  the game wrap (`mkapp.py`) into `build/games/<Name>.bin`, also run
  automatically from plain `pio run` via the
  `post:prog_tools/pio_post_build.py` extra script (`platformio.ini`), so the
  Makefile is optional. The Makefile calls them explicitly too; both paths are
  idempotent.
- **VS Code-only workflow (no Make):** use the generated tasks in
  `.vscode/tasks.json` (regenerate with `python3 prog_tools/gen_tasks.py`, or
  run the **Refresh Games List** task when a game is added/removed):
  - **Upload Bootloader** — `pio run -e v2 -t upload` from `src_os`; writes
    `build/bootloader.bin` and programs it.
  - **Build Game: `<Name>`** — `pio run -e v2` with `PLATFORMIO_SRC_DIR` and
    `PLATFORMIO_BUILD_FLAGS` set (mirrors `make build-game`); wraps the image
    into `build/games/<Name>.bin` for SD-card use.
  - **Upload Game: `<Name>`** — same but with `-t upload`; the `upload_command`
    runs `prog_tools/pio_upload.py`, which programs the whole bootloader + game
    + descriptor pair in one pass (`chip_tool flash-pair`). Upload the
    bootloader once before any game upload.
- `make build-game GAME=<Name>` builds a game from `src_games/<Name>/`. The
  `GAME` var is required; the target overrides `src_dir` via the
  `PLATFORMIO_SRC_DIR` env var, so `platformio.ini` is never modified. The
  game is linked for the app region via `PLATFORMIO_BUILD_FLAGS="-Wl,-T,
  linker/app_flash.ld"` (the `-Wl,-T` flag suppresses the framework's
  `Link.ld`), then wrapped with a 20-byte `PH25` header + CRC-32 by
  `prog_tools/mkapp.py` into `build/games/<GAME>.bin`. Copy that file to the
  SD card and select it with **A** in the bootloader browser.
- `pio run -e v2 -t upload` from `src_os` flashes the bootloader (via
  `prog_tools/pio_upload.py` → `chip_tool flash`); with `PLATFORMIO_SRC_DIR=src_games/<Name>`
  it flashes the bootloader + game pair instead.
- The app region is bounded by `APP_BASE`/`APP_MAX_SIZE`, defined in
  `src_os/flash_load.h`; they must match `linker/app_flash.ld` and
  `prog_tools/mkapp.py` (which rejects builds that exceed the region).
  `APP_BASE` is deliberately **above** the end of the bootloader image; the
  `build` target refuses a bootloader image that reaches `APP_BASE`, because
  loading an app over the bootloader's `.data` tail is what bricks the device
  (dead on next power-up, no LED).
- A game's directory must contain `<Name>.ino` — the file name must match the
  directory name (PlatformIO convention).
- There is no test suite or linter; a clean build is the verification.
- **Privilege mode (critical):** `cores/arduino/ch32/startup_ch32yyxx.S` pulls
  in the machine startup via `#include "startup_ch32x035.S"`. The repo
  **overrides** that file: `framework_overrides/startup_ch32x035.S` is a
  vendored copy patched to `csrs mstatus, 0x1888` instead of stock `0x88`
  (MPP=11 machine mode). Stock `0x88` has MPP=00, so after `handle_reset`/
  `mret` everything (bootloader runtime and any app it jumps to) runs in
  **USER mode**, where M-only CSR writes (`mtvec`, `mstatus`, `mepc`, `mret`)
  and `0xbc0` trap as illegal instructions and hang silently — only user
  custom CSRs (`0x804`, `0x800`) work. The override is wired via
  `extra_scripts = pre:framework_overrides/override_startup.py` in
  `platformio.ini`, which PREPENDS `framework_overrides/` to the include path
  so the quoted include resolves there before the framework's own
  `system/CH32X035/SRC/Startup/` dir. This applies to BOTH `make build` and
  `make build-game` (games override `build_flags` but not `extra_scripts`).
  The framework file on disk is stock `0x88` — do not patch it; keep the
  override vendored file in sync with the framework version instead. After
  updating/adding the override, clear the cached framework objects
  (`rm -rf .pio/build/v2/FrameworkArduino .pio/build/v2/FrameworkArduinoVariant`).
- `linker/app_flash.ld` uses `KEEP(*(.vector))`: without it, `--gc-sections`
  drops the app's whole vector table (the image shows 0x3C zero bytes of ALIGN
  padding at `0x8004`), so IRQ dispatch jumps through zeros. Do not remove the
  `KEEP`.
- Flashing requires `pip install chprog` — install it into the PlatformIO
  terminal (PIO's bundled Python), or a system Python will be used via the
  fallback in `prog_tools/pio_upload.py`. The `upload_command` in
  `platformio.ini` runs the wrapper with `$PYTHONEXE` (PIO's Python) and
  re-execs under `python3`/`python`/`py` if that interpreter lacks
  `pyusb`/`chprog`. The device must be in BOOT mode (hold the BOOT pin/button).
  Linux needs udev rules; lab machines choke on UNC paths — see `README.md`.

## Bootloader (`src/main.cpp`)

State machine: auto-boot check (jump to the flashed game unless **B** is held
or no valid app is installed) → blink test → loading screen (SD mount happens
at 30–60% via `storage::sdAvailable()`) → file browser or "No SD Card" info
screen. Selecting **A** on a `.bin` file verifies + flashes it into the app
region (at `APP_BASE`, see `src_os/flash_load.h`) and jumps to it (real
load-and-run, `src/flash_load.cpp`).
A main menu is reachable with:
- **Play Pong** — uses the shared `phsi245_pong` library
- **Calibrate Touch** — recalibrates thresholds; calibration also runs at
  boot in `os_libs.cpp` keyed to a build hash
- **Format SD Card** (only shown when a card is present) — calls `fat::format()`
- **USB Mass Storage** — exposes the SD card to a host over USB
- **Restart** — `NVIC_SystemReset()`

`usb_msd::update()` must be called every `loop()`; it exposes the SD card to a
host over USB. Full detail: `os/BOOTLOADER.md`.

## Games (`src_games/`)

- Each game is self-contained; `os/APPLICATIONS.md` is the game-dev guide and
  `src_games/OSDemo/OSDemo.ino` is the reference example. `src_games/README.md`
  describes the ~38 ported games.
- Games include `"os_libs.h"` (from `src/`) to pull in all OS libraries.
- Use `storage::sdAvailable()` (not `fat::mount` directly) to detect the SD
  card.
- The FAT library is minimal: 8.3 filenames only, single 512-byte sector
  buffer, one open file at a time, no delete/rename.

## OS Libraries (`lib/`)

The OS stack is the `phsi245_*` libraries (HAL, gfx, input, led, timer, beep,
random, storage, pong). `lib/Arduboy2*` are the legacy framework used only by
older games.

**When adding a new OS library**, add it to BOTH `src/os_libs.h` and
`src/os_libs.cpp` (`os_libs_init()`); the init calls there are what stop the
linker from stripping the library out of the bootloader.

## Documentation

`os/doc/html/` is generated Doxygen, committed to the repo. Regenerate after
changing any header/source in the seven lib dirs listed in `os/Doxyfile`
(PHSI245_HAL, beep, gfx, input, led, storage, timer):

```bash
doxygen os/Doxyfile
```

Other sources: `os/BOOTLOADER.md` (bootloader state machine),
`os/APPLICATIONS.md` (game API reference), `NEW_VERSION.md` (v2 migration).
