---
name: embedded-peripheral-clock-debug
description: Debug embedded peripherals (I2C/SPI/UART) that silently fail due to missing GPIO port clock enables — symptoms, diagnostic technique, and the standard fix pattern
source: auto-skill
extracted_at: '2026-07-16T20:30:00.000Z'
---

# Debugging "Display/Device Not Working" on Embedded Peripherals

When an I2C/SPI/UART peripheral driver configures its pins and initializes the
peripheral correctly, but the attached device (OLED display, sensor, SD card) shows
no sign of life, the root cause is often a **missing GPIO port clock enable**.

## The symptom: works on one revision, not another

The most confusing variant: the same code works on hardware revision A but not
revision B. Both boards have the device connected to the SAME pins, and the
peripheral init code is identical. Why the difference?

**Answer**: on revision A, some OTHER init path (SPI setup, ADC config, timer
init) happens to enable the GPIO port clock before the peripheral init runs. On
revision B, that other init path is guarded by a build flag or runs in a
different order, so the clock is still off when the peripheral tries to configure
its pins.

### Real example: I2C OLED on CH32X035

An I2C driver configures PA10 (SCL) and PA11 (SDA) for an SSD1306 OLED:

```cpp
void i2c_init(void) {
    // ❌ Missing: RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  // wrong port!
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;   // PA10
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);       // writes to GPIOA — but clock is off!
```

In this codebase, `spi_init()` (called during SD card mount) enables
`RCC_APB2Periph_GPIOA`, so when `i2c_init()` runs later, the clock is already on.
Display works. If `spi_init()` is ever reordered or compiled out, `i2c_init()`
would write to GPIOA's CFGLR/CFGHR registers while the clock is off — the
hardware ignores writes to an unclocked peripheral, PA10/PA11 stay in reset
state (floating input), and the display stays dark.

## Why the peripheral appears to work

The I2C/SPI/UART peripheral itself is clocked and functional. Its status registers
report success. The `while(!I2C_CheckEvent(...))` loops don't hang — the peripheral
thinks it's communicating. The problem is purely electrical: the signals are routed
to the default pins (e.g. PB6/PB7 for I2C1 on CH32X035) instead of the configured
pins (PA10/PA11), because the pin configuration didn't take effect.

## Debugging technique: compare init orders

When a peripheral works on one build configuration but not another:

1. **List every init call** that runs before the peripheral init, for both
   configurations
2. **For each, check which GPIO clocks it enables** — use `grep` on the source
   for `RCC_APB2PeriphClockCmd` and `RCC_APB1PeriphClockCmd`
3. **If the peripheral uses a port whose clock is never enabled before `GPIO_Init`**,
   that's the bug

Quick check: `grep` for the peripheral's port across all init code:

```bash
grep -r "RCC.*GPIOA" lib/ src/
```

If the peripheral's `GPIO_Init( port, ...)` appears in a file that doesn't also
enable `RCC_..._GPIOx` for that port, add the clock enable.

## The fix pattern

Add the missing clock enable immediately before the first `GPIO_Init` call that
targets that port:

```cpp
void i2c_init(void) {
    // ✅ Enable the port clock BEFORE touching its registers
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    // ... GPIO_Init(GPIOA, ...)
}
```

Rule: **every `GPIO_Init(GPIOx, ...)` call must have a corresponding
`RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOx, ENABLE)` that runs before it.**

The clock can be enabled in any init function that runs before the GPIO_Init call
— it doesn't have to be in the same function. But putting it in the same function
makes the dependency explicit and prevents future init-order regressions.

## Why this trap exists

On MCUs like STM32, CH32, and GD32, the SPL `GPIO_Init()` function writes to
port configuration registers (CFGLR, CFGHR, etc.) without checking the clock
status. If the clock is off, the write is silently dropped by the hardware bus
matrix. There's no error return, no fault, no warning — the pins simply don't
change.

This is different from the peripheral itself: if you try to use `I2C_Init()`
without `RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE)`, the I2C peripheral
won't respond and `while` loops may hang. But GPIO configuration silently fails,
making it much harder to spot.

## Diagnostic technique: audio beep before the suspect init

When the display doesn't work AND the MCU might be hanging inside the display init
function (rather than completing init and running the main loop), use a buzzer/beep
to bisect the setup sequence:

1. **Place a beep BEFORE the suspect init call.** If you hear this beep, the MCU
   reached that point — the init hasn't hung yet.
2. **Place a second beep AFTER the suspect init call.** If you hear BOTH beeps, the
   init completed and the problem is elsewhere (bus wiring, device power, data
   format). If you only hear the first beep, the init hung.

```cpp
void setup() {
    beep::init();
    beep::beep_startup();          // ← beep 1: "I reached this point"

    gfx::init();                    // ← suspect: I2C init inside

    beep::beep_startup();          // ← beep 2: "display init completed"
    // ... rest of setup
}
```

| What you hear | Diagnosis |
|---|---|
| No beep | MCU not reaching setup (power, clock, bootloader issue) |
| One beep, then silence | `gfx::init()` hung — likely I2C waiting for a slave |
| Two beeps | Display init completed; problem is elsewhere |

This technique is especially valuable when the failing peripheral's init uses polling
loops (`while(!flag)`) that hang forever if the bus/slave isn't responding. Without
the beep, you can't distinguish "MCU crashed" from "MCU is spinning in an I2C wait
loop."

## Pitfall: AFIO remap that moves pins AWAY from the device

When GPIO port clock and pin configuration both look correct but the peripheral still
doesn't work, the instinct is to add an AFIO/GPIO remap. **Check which pins are the
default mapping before adding a remap.** On many MCUs (CH32X035, STM32F1), the default
I2C1/SPI1/USART1 pins are on certain ports, and a remap moves them to DIFFERENT pins.
If you add a remap thinking "this remap enables the peripheral on these pins," you
may actually be moving the peripheral AWAY from the pins where the device is connected.

### Real example: I2C OLED on CH32X035

The CH32X035C8T6 (48-pin package) defaults I2C1 to PA10 (SCL) / PA11 (SDA). Adding
`GPIO_FullRemap_I2C1` moves I2C1 to different pins — so the I2C peripheral starts
sending clock and data to the WRONG pins. The init code doesn't hang (the peripheral
itself is happy), but the OLED receives nothing because it's still connected to
PA10/PA11 which are now in GPIO mode.

**Rule**: before adding a peripheral remap, verify:
1. What are the DEFAULT pins for this peripheral on this package?
2. Does the remap move the peripheral TO or AWAY from the pins you're using?

The SPL's `GPIO_PinRemapConfig()` function silently changes the pin routing with no
way to read back the current mapping. If you guess wrong, the symptom is identical to
a missing GPIO clock — peripheral works, device unresponsive.

## Anti-patterns that mask the bug

- **Enabling the clock for a different reason**: `spi_init()` happens to enable
  GPIOA because it needs PA5/PA7. If SPI is later removed or conditionally
  compiled out, the display breaks. This is a hidden dependency.
- **Calling `GPIO_Init` before any port clock is enabled**: on some MCUs this
  works if the clock was left on by the bootloader or reset state, creating a
  false sense of correctness that breaks on a different board revision or after
  a deep-sleep cycle.
- **Enabling ALL GPIO clocks in main()**: works but wastes power and masks
  the real dependency. Each init function should be self-contained.
