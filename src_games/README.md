# A set of Arduboy applications for the class project.

Some of these have worked without modification, but many of these have subtle
differences from their original versions.

## Building Games

Games are built from the project root using `make`:

```bash
# Build a specific game (uses src_games/<name>/ as src_dir)
make build-game GAME=OSDemo
make build-game GAME=HelloNOISE
```

The `build-game` target overrides `src_dir` via the `PLATFORMIO_SRC_DIR`
environment variable, so `platformio.ini` is never modified. The game is
linked for the app region (`0x6F00`+, max 34816 bytes) and wrapped with a
20-byte `PH25` header + CRC-32 (`prog_tools/mkapp.py`) into
`build/games/<GAME>.bin`, so a later bootloader build (which overwrites
`.pio/build/v2/firmware.bin`) can't clobber it. Copy that file to the SD card
and run it with **A** in the bootloader browser.

The default `make build` target builds the bootloader (`src/`).

You can also manually edit `platformio.ini` and set `src_dir`:

```ini
[platformio]
src_dir = src_games/HelloNOISE
```

or set the same variable on the command line:

```bash
PLATFORMIO_SRC_DIR=src_games/HelloNOISE pio run -e v2
```

Remove or comment out the `src_dir` line to go back to using the default
`src/` directory for the bootloader.

## OS Libraries

The project provides a set of OS libraries under `lib/` that games can use
as an alternative to the legacy Arduboy2 framework:

| Library | Header | Purpose |
|---------|--------|---------|
| `phsi245_gfx` | `gfx.h` | 128×64 OLED: pixels, shapes, sprites, text |
| `phsi245_input` | `input.h` | Button polling with edge detection |
| `phsi245_led` | `led.h` | LED on/off/toggle control |
| `phsi245_timer` | `ostime.h` | Millisecond timer and delay |
| `phsi245_beep` | `beep.h` | Buzzer audio feedback |
| `phsi245_storage` | `storage.h` | FAT filesystem and SD card (v2 only) |
| `PHSI245_HAL` | `HAL.h` | Hardware abstraction: GPIO, I2C, SPI, touch keys |

See `lib/` and `NEW_VERSION.md` for API details. The reference example is
`src_games/OSDemo/OSDemo.ino`, which demonstrates graphics, SD card mounting,
and file I/O using only the OS libraries (no Arduboy2 dependency).

## A list of games we have

### 3in1 Series
    A set of games by Daniel C, which fit 3 small games in one firmware image
#### 3in1A
    - Tiny Invaders
    - Tiny Pinball
    - PAC-MAN
#### 3in1B
    - Tiny Bomber
    - Tiny Arkanoid (Block Breaker)
    - Tiny Gilbert
#### 3in1C
    - Tiny Bert (Q*bert)
    - Tiny Bike (Bike Racing)
    - Tiny Trick (Hockey)
#### 3in1D 
    - Tiny DDug
    - Tiny Plaque
    - Tiny Tris (Tetris)
#### 3in1E
    - Tiny Missile
    - Tiny Morpion (O and Xs)
    - Tiny Pipe
### ArduboyLife
    Conway's game of life. (More a mathematical oddity than a game to play)
### ArduLO
    Light's Out style game: You want to extinguish all the lights, but turning off/on one light also inverts its neighbours.
### ArduMetronome
    A metronome
### CastleBoy
### Catacombs of the Damned
    A DOOM-like 3D shooter
### CircuitDude
    Place components on the footprints to complete a level, but the components you place block your way.
### FlappyBall
    Time your flaps to fit through the course
### HelloNOISE
    A noise synthesiser.
### HP35boy
    A simulator for an HP35 calculator, which uses reverse polish notation.
### minesweeper
    A simple minesweeper game
### MysticBaloon
    Deploy baloons while jumping to float around this platform game.
### Picovaders
    Another space invaders clone
### Shadowrunner
    Jump and slide.
### Snake
    Classic game of feeding the snake and not eating your own tail (or the walls). This is actually running an emulator for a hand-held sega console.
### Tiny-1010
    Fill a 10x10 grid in a similar way to tetris.
### USBHID
    Composite USB HID device (mouse + keyboard + gamepad over one cable):
    a HID gamepad mode, a mouse-jiggler mode with manual D-pad control, and a
    script runner that reads a text file from the SD card and types/keys it on
    the host. See `USBHID/HID_SCRIPT.md` for the script format.
### Virus LQP-79 (VLQP_AB)
    2D shooter
### Obono Games
    None of these have working high-score systems, sorry. Sound plays on the
    single speaker (PA15) via TIM3.

    - Ardubullets: Space Shooter
    - chiemagari
    - chribocchi: Juggle
    - evasion: Run away while not running into something
    - gosencho
    - hollow: Don't get crushed
    - hopper: Hop up in 3D without falling
    - knightmove
    - pi24k: Investigate the first 24k digits of pi with sound and light 
    - quarto: Play the quarto board game
    - reversi: Play the reversi board game
    - rysk
    - samegame
    - stairsweep: Tetris with a twist



