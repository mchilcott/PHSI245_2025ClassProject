# OS Bootloader

The bootloader (`src/main.cpp`) is the main firmware for the PHSI245 Game Boy.
It provides a loading screen, SD card file browser, and a main menu with a
pong game, touch calibration, USB mass storage, and restart capability.

## Screens and State Machine

```
                    ┌───────────────┐
                    │  Auto-boot?   │  App installed + B not held
                    │  flash CRC ok │──► (jump to app)
                    └──────┬────────┘
                           │ no app / B held
                     ┌─────▼─────┐
                     │ Blink Test│  3 LED flashes
                     │ + Beep    │  1 startup beep
                     └─────┬─────┘
                           │
                     ┌─────▼──────┐
                     │  Loading   │  Progress bar animation
                     │            │  SD card mount
                     └─┬──────┬───┘
                       │      │
             SD OK ┌───┘      └───┐ No SD
                   │              │
          ┌────────▼────────┐ ┌───▼──────────┐
          │    Browse       │ │  No SD Card  │  Error beep (3x)
          │  File list      │ │  Info screen │
          │  A:Open B:Menu  │ │  A:Menu      │
          └──┬───────┬──────┘ └───┬──────────┘
             │       │            │
   A:Open ┌──┘       └── B:Menu ──┤
          │                        │
   ┌──────▼────────┐               │
   │   File Info   │               │
   │   A:Exec      │               │
   │   B:Back      │               │
   └──────┬────────┘               │
          │                        │
   ┌──────▼────────┐  fail ────────┼────► Load Failed (B → Browse)
   │  Executing    │               │
   │  Verify+Flash │  success      │
   │  (jump to app)│──────────────►│
   └───────────────┘               │
                    ┌──────────────▼──────────────┐
                    │         Main Menu           │
                    │  1. Play Pong               │
                    │  2. Calibrate Touch         │
                    │  3. Format SD Card          │
                    │  4. USB Mass Storage        │
                    │  5. Restart                 │
                    └──┬──────────┬──────────┬────┘
                       │          │          │
             ┌────────▼──┐  ┌────▼──────┐  ┌▼─────────┐
             │   Pong    │  │ Calibrate │  │ ...      │
             │  B:Quit   │  │  B:Back   │  │          │
             └───────────┘  └───────────┘  └──────────┘
```

### Blink Test

On startup, the LED (PB12) blinks 3 times and the buzzer beeps once. This
confirms the MCU is alive before display initialization begins.

### Loading Screen

An animated progress bar fills from 0–100%. During the 30–60% phase, the
bootloader attempts to mount an SD card:

- **With SD card**: proceeds to the file browser
- **Without SD card**: shows the "No SD Card" info screen

### No SD Card Info

Shows a message explaining that no card is present. Press **A** to open the
main menu.

### File Browser

Lists files and directories from the current directory on the SD card (the
header shows the current path; browsing starts at the root `/`):

| Button | Action |
|--------|--------|
| UP / DOWN | Navigate file list |
| A | Open file (shows file info) or enter directory |
| B | Go up one level (root → main menu) |

Only files with a `.bin` extension are executable.

Entering the browser from the main menu (**B**) always returns to the root `/`
and re-reads the directory listing from the card, so files added/removed
during a USB-MSD session or a format are picked up.

### File Info

Shows the selected file's name, type, and size:

| Button | Action |
|--------|--------|
| A | Execute (only for `.bin` files; otherwise A is ignored) |
| B | Back to file browser |

### Main Menu

The main menu (reached with **B** from the browser root, or **A** from the No
SD screen) offers:

| Option | Description |
|--------|-------------|
| **Play Pong** | Single-player pong — UP/DOWN moves paddle, score counter, B quits |
| **Calibrate Touch** | Recalibrates the touch-button thresholds |
| **Format SD Card** | Re-formats the card with a fresh FAT32 filesystem (only when a card is present) |
| **USB Mass Storage** | Exposes the SD card to a host over USB (only when a card is present) |
| **Restart** | Full MCU reset via `NVIC_SystemReset()` |

### Hardware Reset

The board includes an external reset button on **PA21 (NRST, pin 7)**.
Momentarily pulling NRST to GND triggers a full hardware reset that works
regardless of firmware state. No firmware configuration is needed — the
CH32X035 defaults PA21 to active-low reset input.

The "Restart" menu option provides a software-only reset.

## Building

```bash
# v2 hardware
make build
make upload
```

The `HW_VERSION` build flag is gone — the firmware targets the v2 board only.
See `lib/PHSI245_HAL/HAL.h` for the pin mapping details.

## OS Libraries Used

The bootloader uses the full OS library stack:

| Library | Namespace | Purpose |
|---------|-----------|---------|
| `phsi245_gfx` | `gfx` | 128×64 OLED display: pixels, shapes, sprites, text |
| `phsi245_input` | `input` | Button polling with edge detection |
| `phsi245_led` | `led` | LED on/off/toggle control |
| `phsi245_timer` | `ostime` | Millisecond tick counter and delay |
| `phsi245_beep` | `beep` | Buzzer audio feedback |
| `phsi245_storage` | `fat`, `sd` | FAT filesystem and SD card |
| `PHSI245_HAL` | *(global)* | Hardware abstraction: GPIO, I2C, SPI, touch keys |

## Audio Feedback

| Pattern | Meaning |
|---------|---------|
| 1 beep before display init | MCU alive, starting display init |
| 1 beep after display init | Display init completed successfully |
| 3 rapid beeps | SD card error (no card, mount failure) |

No beep at all means the MCU isn't running (power or clock issue). One beep
followed by silence means the display init is hanging.

## Program Loading

### Overview

The bootloader's file browser lets the user select a `.bin` game image from
the SD card and run it. Selecting **A** on a `.bin` file's File Info screen
verifies the image, flashes it into the app region of flash, and jumps into
it — the selected game actually boots.

After a game has been loaded, the device **auto-boots it at power-up**: the
bootloader validates the flashed image and jumps to it without showing the
menu. Hold **B** at reset to skip auto-boot and enter the bootloader UI.

### Flash Layout

| Region | Start | Size | Contents |
|--------|-------|------|----------|
| Bootloader | `0x00000000` | 32 KB | This bootloader (must stay `< 0x8000`) |
| App (code) | `0x00008000` | 30464 B | Linked game image (`APP_BASE` + `APP_MAX_SIZE`) |
| Descriptor page | `0x0000F700` | 256 B | Magic + loadaddr + size + CRC32 of flashed app |
| — | `0x0000F800` | — | End of 62 KB flash (no flash above `0xF7FF`) |

The CPU executes and reads flash through the alias at `0x00000000`, but the
flash controller only accepts **physical** addresses at `0x08000000` — the
alias cannot be erased or programmed. `flashProgramPage()` therefore adds
`FLASH_REGION_BASE` (`0x08000000`) to every address passed to
`FLASH_BufLoad` / `FLASH_ErasePage_Fast` / `FLASH_ProgramPage_Fast`.

The app image is linked for `0x00008000` (see `linker/app_flash.ld`) so its
reset vector lands at the app base; its startup (`_start` → `handle_reset`) is
fully self-contained — it re-initializes SP, `.data`, `.bss`, sets `mtvec` to
its own vector table, runs `SystemInit`, and calls `main`.

### SD Image Format

`make build-game GAME=<Name>` emits a 20-byte header + code image to
`build/games/<Name>.bin` (via `prog_tools/mkapp.py`):

```
Offset  Size  Field
------  ----  -----
0x00    4     Magic      "PH25"
0x04    2     Version    = 1
0x06    2     Flags      = 0
0x08    4     Load addr  = 0x00008000 (APP_BASE)
0x0C    4     Code size  (≤ 30464)
0x10    4     CRC-32     (zlib, over code)
0x14    N     Raw code
```

Games larger than 30464 bytes are rejected at build time; the bootloader also
rejects images that fail the header/CRC checks.

### Load & Run Flow

```
Browse ──A──► File Info ──A──► Executing ──success──► (jump to app)
  ▲              │                  │
  │              │                  │
  └─────B────────┘        fail ─────┴──► Load Failed (B → browse)
```

1. **Verify (pass 1)** — the image is streamed from the SD card and its
   CRC-32 is checked against the header **before any flash is erased**. Any
   failure shows the "Load Failed" screen with a specific reason and leaves
   flash untouched:
   - `Missing PH25 header.` — not a PHSI245 image (e.g. a raw firmware dump)
   - `Bad image version.` / `Bad load address.` / `Image too large.`
   - `File too small.` / `Can't open file.` — SD/FAT read problems
   - `SD read error.` — the card stopped responding mid-stream
   - `Image failed CRC check.` — file corrupted or wrong content
2. **Program (pass 2)** — with interrupts disabled, each 256-byte page is
   read from the SD card, loaded into the flash buffer
   (`FLASH_BufLoad`), erased, and programmed (`FLASH_ErasePage_Fast` /
   `FLASH_ProgramPage_Fast`), then read back and compared.
3. **Descriptor** — on success the 256-byte descriptor page at `0x0000F700`
   is written (magic `PH25`, load addr, code size, CRC-32). It is only written
   after a fully verified write, so a partial flash never auto-boots.
4. **Jump** — `appJump()` disables interrupts and jumps to `0x00008000`,
   which is the app's `_start` (`j handle_reset`).

### Boot-Time Validation

On reset (before the menu), if **B is not held** the bootloader reads the
descriptor page and, if the magic/size are sane, recomputes the CRC-32 over
the flashed app region and jumps to it when it matches. If the check fails
(no app installed, or a partial/corrupt flash), normal boot continues.

Implementation lives in `src/flash_load.{h,cpp}` (`crc32`, header parse,
`appVerifyFile`, `appFlashFromFile`, `appValidInstalled`, `appJump`). The
CH32X035 flash controller programs whole 256-byte pages from an internal
buffer (fast program mode).
