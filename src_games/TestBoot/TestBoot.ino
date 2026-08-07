#include <Arduino.h>
#include <HAL.h>
#include "gfx.h"
#include <stdlib.h>

// ============================================================================
// TestBoot v4.10 — full framework startup, now running in MACHINE mode.
//
// ROOT CAUSE (found with v4.9): the framework startup_ch32x035.S does
// `csrs mstatus, 0x88` then `mret`. 0x88 has MPP = 00 = USER mode, so after
// handle_reset the bootloader runtime AND anything it jumps to (the app) run
// in USER mode. In U-mode, M-only CSR writes (mstatus, mtvec, mepc, mret) and
// the WCH 0xbc0 register trap as ILLEGAL instructions (mcause=2) -> dispatch
// through the bootloader's vector table -> silent hang. Only USER-accessible
// custom CSRs (0x804, 0x800) worked, which is why every experiment failed at
// the first M-only write and `csrw 0x804,0x3` alone always passed.
//
// FIX (applied in the framework startup package):
//   `csrs mstatus, 0x88`  ->  `csrs mstatus, 0x1888`   (MPP=11 = MACHINE)
// The bootloader and every game now run in M-mode, so the app's own
// handle_reset CSR writes are legal. This mirrors WCH's official
// `startup_ch32x035_highcode_machine.S`. The bootloader must be reflashed
// (chprog) since it is the component that hands M-mode to the app.
//
// SECOND BUG (fixed in linker/app_flash.ld): the app's `.vector` section was
// not KEEP'd, so --gc-sections dropped the whole vector table (firmware.bin
// showed 0x3C zero bytes of ALIGN padding at 0x8004). It is now KEEP'd so IRQ
// dispatch through the app's mtvec gets real handler addresses.
//
// This build uses the stock framework startup (no custom handle_reset). If it
// reaches R7, the ENTIRE M-mode startup path (data/bss copy, 0xbc0/0x804/
// mstatus/mtvec writes, ctors, SystemInit, mret into main) worked.
//
// LED0 markers (LED1 unused in this build):
//   R7  main() entered, pre_init/systick_init OK, setup() reached
//   R8  pinMode OK
//   R9  Stage A raw LED-off OK
//   R10 SysTick CNT moving (fast-fail 10-blink = frozen)
//   R11 delay(50) returned
//   R12 gfx::init OK (then "TEST APP RUNS")
//   fast 10-blink loop at R10 = SysTick counter frozen
// ============================================================================

#define GPIOB_CFGHR    (*(volatile uint32_t *)0x40010C04)
#define GPIOB_BSHR     (*(volatile uint32_t *)0x40010C10)  // set pin bits
#define GPIOB_BCR      (*(volatile uint32_t *)0x40010C14)  // clear pin bits
#define RCC_APB2PCENR  (*(volatile uint32_t *)0x40021018)
#define SYSTICK_BASE   0xE000F000

static void led0On(void)  { GPIOB_BSHR = (1u << 12); }
static void led0Off(void) { GPIOB_BCR  = (1u << 12); }

static void busyWait(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++) { }
}

static void marker(uint8_t n)
{
    busyWait(2500000);                        // lead gap
    for (uint8_t i = 0; i < n; i++) {
        led0On();  busyWait(200000);
        led0Off(); busyWait(200000);
    }
    busyWait(4000000);                        // trail gap
}

static void gpioInitRaw(void)
{
    RCC_APB2PCENR |= 0x8u;                    // GPIOB clock on
    GPIOB_CFGHR = (GPIOB_CFGHR & ~((0xFu << 16) | (0xFu << 20)))
                | (0x3u << 16) | (0x3u << 20); // PB12/PB13 output PP 50MHz
}

void setup()
{
    gpioInitRaw();
    led0Off();
    marker(7);   // R7: main entered, pre_init/systick_init OK, setup() reached

    pinMode(PIN_LED_0, OUTPUT);
    marker(8);   // R8: pinMode OK

    led0Off();
    marker(9);   // R9: Stage A OK

    volatile uint32_t *syst = (volatile uint32_t *)SYSTICK_BASE;
    uint32_t t0 = syst[2];
    uint32_t moved = 0;
    for (volatile uint32_t i = 0; i < 3000000; i++) {
        if (syst[2] != t0) { moved = 1; break; }
    }
    if (!moved) {
        for (uint8_t i = 0; i < 10; i++) {
            led0On();  busyWait(80000);
            led0Off(); busyWait(80000);
        }
        led0On();
        for (;;) { }
    }
    marker(10);  // R10: SysTick CNT moving

    delay(50);
    marker(11);  // R11: delay() returned

    gfx::init();
    marker(12);  // R12: gfx init OK

    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);
    gfx::clear();
    gfx::setCursor(0, 0);
    gfx::print("TEST APP RUNS");
    gfx::display();

    led0Off();
}

void loop()
{
    led0On();  busyWait(2000000);
    led0Off(); busyWait(2000000);
}
