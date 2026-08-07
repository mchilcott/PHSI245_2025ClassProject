# Coding and Compiling Applications

This guide covers writing and building games or applications that use the
PHSI245 OS libraries. See `src_games/OSDemo/OSDemo.ino` for a complete
working example.

## Quick Start

```bash
mkdir -p src_games/MyGame
touch src_games/MyGame/MyGame.ino
make build-game GAME=MyGame
```

## Project Structure

Each game lives in its own directory under `src_games/`. The file name
must match the directory name (PlatformIO `.ino` convention):

```
src_games/
├── MyGame/
│   └── MyGame.ino        ← entry point (setup + loop)
├── OSDemo/
│   └── OSDemo.ino        ← reference game using all OS libraries
└── HelloNOISE/
    └── HelloNOISE.ino    ← legacy Arduboy2-based game
```

## Minimal Game Skeleton

```cpp
#include <Arduino.h>
#include "os_libs.h"

void setup()
{
    gfx::init();
    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);
    input::init();
    led::init();
    ostime::init();
}

void loop()
{
    input::update();

    gfx::clear();
    gfx::setCursor(0, 0);
    gfx::print("Hello v2!");

    if (input::justPressed(PIN_BTN_A)) {
        led::on(PIN_LED_0);
    }
    if (input::justPressed(PIN_BTN_B)) {
        led::off(PIN_LED_0);
    }

    gfx::display();
    ostime::delay_ms(16);  // ~60 fps
}
```

The `os_libs.h` header pulls in all OS libraries: `gfx.h`, `input.h`,
`led.h`, `ostime.h`, `beep.h`, `storage.h`, and `HAL.h`.

## OS Library API Reference

### Graphics (`gfx.h`)

| Function | Description |
|----------|-------------|
| `gfx::init()` | Initialize SSD1306 OLED (128×64) over I2C |
| `gfx::clear(color)` | Fill frame buffer (default `GFX_BLACK`) |
| `gfx::display()` | Flush frame buffer to display |
| `gfx::display(clear)` | Flush and optionally clear buffer |
| `gfx::drawPixel(x, y, color)` | Set a single pixel |
| `gfx::drawLine(x0, y0, x1, y1, color)` | Bresenham line |
| `gfx::drawFastVLine(x, y, h, color)` | Vertical line |
| `gfx::drawFastHLine(x, y, w, color)` | Horizontal line |
| `gfx::drawRect(x, y, w, h, color)` | Rectangle outline |
| `gfx::fillRect(x, y, w, h, color)` | Filled rectangle |
| `gfx::drawCircle(x0, y0, r, color)` | Circle outline |
| `gfx::fillCircle(x0, y0, r, color)` | Filled circle |
| `gfx::drawBitmap(x, y, data, w, h, color)` | PROGMEM bitmap |
| `gfx::drawSprite(x, y, bitmap, w, h, mode)` | Sprite with draw mode |
| `gfx::drawSpriteSheet(x, y, sheet, frame, mode)` | Data-first sprite sheet |
| `gfx::setCursor(x, y)` | Text cursor position |
| `gfx::setTextSize(s)` | Text scale factor (1 = 6×8 px per char) |
| `gfx::setTextColor(c)` | Text color (`GFX_BLACK` / `GFX_WHITE`) |
| `gfx::print(str)` | Print C string with 5×7 font |
| `gfx::print(n)` | Print signed/unsigned 16-bit integer |

Sprite draw modes: `GFX_SPRITE_UNMASKED`, `GFX_SPRITE_MASKED`,
`GFX_SPRITE_SELF_MASKED`, `GFX_SPRITE_ERASE`, `GFX_SPRITE_PLUS_MASK`.

The frame buffer is 1024 bytes (`gfx::buffer`) in RAM. Call `gfx::display()`
to push it to the OLED.

**Score & Menu Helpers**:

```cpp
// Right-aligned 5-digit score at x=120 (right edge)
gfx::drawNumber(120, 0, score, 5);

// Simple text menu with 3 items, line spacing 14px
const char *items[] = {"Play", "Options", "Quit"};
gfx::drawMenu(10, 16, items, 3, cursor, 14);
```

### Input (`input.h`)

| Function | Description |
|----------|-------------|
| `input::init()` | Initialize GPIO pins for touch buttons |
| `input::update()` | Poll all buttons (call once per frame) |
| `input::pressed(pin)` | True while button is held |
| `input::justPressed(pin)` | True on the frame the button is first pressed |
| `input::justReleased(pin)` | True on the frame the button is released |
| `input::heldFor(pin, delay, repeat)` | True after `delay` ms, then every `repeat` ms |
| `input::allPressed(n, pins[])` | True if all N given pins are pressed |
| `input::anyPressed(n, pins[])` | True if any of N given pins are pressed |

Button pins: `PIN_BTN_UP`, `PIN_BTN_DOWN`, `PIN_BTN_LEFT`, `PIN_BTN_RIGHT`,
`PIN_BTN_A`, `PIN_BTN_B`.

### LED (`led.h`)

| Function | Description |
|----------|-------------|
| `led::init()` | Initialize LED GPIOs |
| `led::on(pin)` | Turn LED on (active low) |
| `led::off(pin)` | Turn LED off |
| `led::toggle(pin)` | Toggle LED state |

LED pins: `PIN_LED_0` (PB12), `PIN_LED_1` (PB13).

### Timer (`ostime.h`)

| Function | Description |
|----------|-------------|
| `ostime::init()` | Initialize SysTick timer |
| `ostime::ticks()` | Millisecond counter (32-bit, wraps every ~49 days) |
| `ostime::delay_ms(ms)` | Blocking delay in milliseconds |
| `ostime::setFrameRate(fps)` | Set target frame rate (e.g. 30) |
| `ostime::nextFrame()` | Block until next frame boundary, returns true |
| `ostime::frameCount()` | Monotonic frame counter since boot |

Typical game loop using frame-rate control:

```cpp
void setup() {
    ostime::setFrameRate(30);
}
void loop() {
    if (!ostime::nextFrame()) return;

    input::update();
    gfx::clear();
    // ... draw ...
    gfx::display();
}
```

### Random (`random.h`)

| Function | Description |
|----------|-------------|
| `rng::init()` | Seed from touch-key ADC noise |
| `rng::next(max)` | Random integer in [0, max-1] |
| `rng::next(min, max)` | Random integer in [min, max] inclusive |

Uses xorshift32 — fast, no division, good distribution.

```cpp
rng::init();
uint8_t x = rng::next(10);       // 0..9
uint8_t y = rng::next(5, 15);    // 5..15
```

### Audio (`beep.h`)

| Function | Description |
|----------|-------------|
| `beep::init()` | Initialize speaker GPIO (PA15) |
| `beep::beep_ms(ms)` | Square-wave tone at ~2 kHz for `ms` milliseconds |
| `beep::tone(freq, ms)` | Play frequency in Hz for N ms (blocks) |
| `beep::beep_startup()` | Single 50 ms beep |
| `beep::beep_error()` | Three 100 ms beeps with 50 ms gaps |
| `beep::playMelody(notes)` | Play PROGMEM note sequence (non-blocking, call each frame) |

Melody format: 4 bytes per note — freqHi, freqLo, durHi, durLo (16-bit Hz, 16-bit ms).
Freq=0 is a rest. Terminated by `{0,0,0,0}`.

```cpp
// C-E-G arpeggio, 200ms each
static const PROGMEM uint8_t arp[] = {
    0x01, 0x04, 0x00, 0xC8,  // 260 Hz, 200ms (C4)
    0x01, 0x4E, 0x00, 0xC8,  // 334 Hz, 200ms (E4)
    0x01, 0x90, 0x00, 0xC8,  // 400 Hz, 200ms (G4)
    0x00, 0x00, 0x00, 0x00,  // end
};
void loop() {
    beep::playMelody(arp);  // call each frame
}
```

### Storage (`storage.h`)

**SD Card** (namespace `sd`):

| Function | Description |
|----------|-------------|
| `sd::DEVICE` | Block device singleton for FAT mounting |
| `sd::init()` | Initialize SD card in SPI mode |
| `sd::readBlock(block, buf)` | Read 512-byte sector |
| `sd::writeBlock(block, buf)` | Write 512-byte sector |
| `sd::blockCount()` | Total number of 512-byte blocks |

**FAT Filesystem** (namespace `fat`):

| Function | Description |
|----------|-------------|
| `fat::mount(&dev)` | Mount a block device. Returns `fat::OK` on success. |
| `fat::open("path")` | Open a file for reading. Supports 8.3 filenames. |
| `fat::read(buf, len, &bytesRead)` | Read bytes from open file (streaming). |
| `fat::close()` | Close the current file. |
| `fat::openDir("path")` | Open a directory for listing. |
| `fat::readDir(name, &isDir, &size)` | Read next directory entry. Returns false when done. |
| `fat::create("path")` | Create a new file or truncate existing. |
| `fat::write(buf, len, &bytesWritten)` | Append data to an open file. |

FAT result codes: `OK`, `DISK_ERR`, `NOT_READY`, `NO_FILE`, `NOT_OPENED`,
`NOT_ENABLED`, `NO_FILESYSTEM`.

**EEPROM Save/Load** (namespace `storage`):

| Function | Description |
|----------|-------------|
| `storage::initSave()` | Initialize EEPROM save area (26 bytes available) |
| `storage::saveGame(tag, data, len)` | Save blob under 4-char tag. Returns bool. |
| `storage::loadGame(tag, buf, maxLen)` | Load blob, returns bytes read (0 if none) |
| `storage::eraseGame(tag)` | Delete saved data for a tag |
| `storage::highScoreLoad()` | Load high score (uses "HISC" tag) |
| `storage::highScoreSave(score)` | Save high score if higher |

```cpp
storage::initSave();

// Save/load game state
uint8_t level = 3;
storage::saveGame("LVL", &level, sizeof(level));
uint8_t loaded;
storage::loadGame("LVL", &loaded, sizeof(loaded));

// High score
if (score > storage::highScoreLoad())
    storage::highScoreSave(score);
```

### Hardware Abstraction (`HAL.h`)

| Function | Description |
|----------|-------------|
| `IsTouched(pin)` | True if a touch button is currently touched |
| `initTouchButtons()` | Initialize ADC for capacitive touch sensing |
| `Touch_Key_Adc(ch)` | Read raw ADC value for a touch channel |

Pin defines: `PIN_BTN_UP` (PA0), `PIN_BTN_LEFT` (PA1), `PIN_BTN_DOWN` (PA2),
`PIN_BTN_RIGHT` (PA3), `PIN_BTN_A` (PB0), `PIN_BTN_B` (PB1),
`PIN_LED_0` (PB12), `PIN_LED_1` (PB13), `PIN_BEEP` (PA15).

## Building

```bash
# Build a specific game
make build-game GAME=MyGame
```

The `build-game` target overrides `src_dir` via the `PLATFORMIO_SRC_DIR`
environment variable, so `platformio.ini` is never modified. The OS libraries
in `lib/` are automatically found by PlatformIO's dependency finder.

The build links the game for the app region (`linker/app_flash.ld`, flash
`0x8000`–`0xF6FF`, 30464 bytes max) and wraps the result with a 20-byte
`PH25` header + CRC-32 (`prog_tools/mkapp.py`), producing
`build/games/<GAME>.bin`. Copy that file to the SD card and select it with
**A** in the bootloader browser to flash and boot it. Games larger than 30464
bytes are rejected at build time.

A game is fully self-contained at runtime: the WCH startup (`_start` →
`handle_reset`) re-initializes SP, `.data`, `.bss`, sets `mtvec` to the game's
own vector table, runs `SystemInit`, and calls `main` — the bootloader just
jumps to the app base after flashing. The device auto-boots the last-loaded
game at power-up; hold **B** at reset to reach the bootloader menu.

To add a new game, just create the directory and `.ino` file — no Makefile
or build configuration changes are needed.

The firmware targets the v2 board revision only; SD code needs no version
guards. `PIN_BTN_A` and `PIN_BTN_B` map to PB0/PB1, and the touch button API
(`IsTouched(PIN_BTN_A)`) works directly on them.

## Frame Loop Pattern

A typical game loop runs at a fixed frame rate:

```cpp
void loop()
{
    input::update();          // poll buttons once per frame

    // --- game logic ---

    gfx::clear();             // clear frame buffer
    gfx::setCursor(0, 0);
    gfx::print("Score: 42");

    // --- drawing ---

    gfx::display();           // push to OLED
    ostime::delay_ms(33);     // ~30 fps
}
```

## Tips

- Use `input::justPressed()` for single-fire actions (menu select, jump)
  and `input::pressed()` for continuous actions (movement, scrolling).
- Call `gfx::display()` once per frame at the end of `loop()`.
- The FAT library uses a single 512-byte sector buffer. Only one file
  can be open at a time.
- 8.3 filenames only (no long filename support). Paths use `/` separator.
- Font is 5×7 pixels at `setTextSize(1)`. Each character is 6×8 px
  including spacing.
- `gfx::print(n)` handles both signed and unsigned 16-bit integers.
