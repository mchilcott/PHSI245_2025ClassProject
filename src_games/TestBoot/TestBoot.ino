#include <Arduino.h>
#include <HAL.h>
#include "gfx.h"
#include <stdlib.h>

// ============================================================================
// TestBoot v4.6 — skip the csrw 0xbc0 write.
//
// Ground truth so far:
//   v4.2/4.3/4.4 (our app_start) and v4.5 (PURE STOCK startup) all hang in
//   the app region with NO visible activity. The bootloader's own startup runs
//   the identical `csrw 0xbc0,0x1f` fine at power-on, so the write itself is
//   the suspect: on WCH cores 0xBC0 is the undocumented instruction-prediction
//   CSR, and the app re-writing it AFTER the bootloader has been running with
//   prediction active appears to hang the core.
//
// This build DOES NOT write 0xbc0 at all (the bootloader left it at 0x1F).
// It keeps csrw 0x804 + csrs mstatus (with post-write probes) so we can see
// whether the rest of the block is OK.
//
// LED0 markers:
//   L1  LED1 self-test (2 blinks on LED1)
//   A1  fast 1-blink -> csrw 0x804 returned OK
//   A2  fast 2-blinks-> csrs mstatus returned OK
//   R3  CSR block (sans 0xbc0) fully returned
//   T   LED0: 9 long blinks looped = trap caught by app handler
//   R1  entered (mtvec->trapCrash installed)
//   R2  .data copy + .bss clear OK
//   [   CSR 0x800 readout, twice   ]
//   R4  real mtvec + atexit OK
//   R5  __libc_init_array (static ctors) OK
//   R6  SystemInit OK
//   R7  main() entered, pre_init/systick_init OK, setup() reached
//   R8  pinMode OK
//   R9  Stage A raw LED-off OK
//   R10 SysTick CNT moving (fast-fail 10-blink = frozen)
//   R11 delay(50) returned
//   R12 gfx::init OK (then "TEST APP RUNS")
//
// If the app STILL hangs before A1, the problem is not 0xbc0 (csrw 0x804
// hangs too). If it reaches R12, 0xbc0 was the culprit and the fix is to
// skip that write in app startup.
// ============================================================================

#define GPIOB_CFGHR    (*(volatile uint32_t *)0x40010C04)
#define GPIOB_BSHR     (*(volatile uint32_t *)0x40010C10)  // set pin bits
#define GPIOB_BCR      (*(volatile uint32_t *)0x40010C14)  // clear pin bits
#define RCC_APB2PCENR  (*(volatile uint32_t *)0x40021018)
#define SYSTICK_BASE   0xE000F000

static void led0On(void)  { GPIOB_BSHR = (1u << 12); }
static void led0Off(void) { GPIOB_BCR  = (1u << 12); }
static void led1On(void)  { GPIOB_BSHR = (1u << 13); }
static void led1Off(void) { GPIOB_BCR  = (1u << 13); }

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

static void marker1(uint8_t n)
{
    busyWait(2500000);                        // lead gap
    for (uint8_t i = 0; i < n; i++) {
        led1On();  busyWait(200000);
        led1Off(); busyWait(200000);
    }
    busyWait(4000000);                        // trail gap
}

static void probeFast(uint8_t n)
{
    busyWait(300000);                         // short lead gap
    for (uint8_t i = 0; i < n; i++) {
        led0On();  busyWait(80000);
        led0Off(); busyWait(80000);
    }
    busyWait(500000);                         // short trail gap
}

static void blinkByte0(uint8_t b)
{
    busyWait(2500000);                        // lead gap
    for (int bit = 7; bit >= 0; bit--) {
        led0On();  busyWait(200000);
        led0Off();
        if (b & (1u << bit)) busyWait(1200000);
        else                busyWait(300000);
    }
    busyWait(4000000);                        // trail gap
}

static void gpioInitRaw(void)
{
    RCC_APB2PCENR |= 0x8u;                    // GPIOB clock on
    GPIOB_CFGHR = (GPIOB_CFGHR & ~((0xFu << 16) | (0xFu << 20)))
                | (0x3u << 16) | (0x3u << 20); // PB12/PB13 output PP 50MHz
}

extern "C" void trapCrash(void) __attribute__((used, noinline, aligned(4)));
extern "C" void trapCrash(void)
{
    for (;;) {
        for (uint8_t i = 0; i < 9; i++) {
            led0On();  busyWait(300000);
            led0Off(); busyWait(300000);
        }
        busyWait(5000000);                     // long pause between rounds
    }
}

extern "C" void handle_reset(void);
extern "C" __attribute__((used, noinline)) void app_start(void);

extern "C" void handle_reset(void) __attribute__((naked));
extern "C" void handle_reset(void)
{
    __asm__ volatile(
        ".option push\n"
        ".option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        "la sp, _eusrstack\n"
        "j app_start"
    );
}

extern "C" {
    void SystemInit(void);
    extern void __libc_init_array(void);
    extern void __libc_fini_array(void);
    extern char _data_lma[], _data_vma[], _edata[];
    extern char _sbss[], _ebss[];
    extern char _start[];
}

extern "C" __attribute__((used, noinline)) void app_start(void)
{
    gpioInitRaw();
    marker1(2);  // L1: LED1 self-test

    // CSR block, NO csrw 0xbc0 (bootloader already set it). Post-write probes.
    {
        uint32_t t = 0x3;
        __asm__ volatile("csrw 0x804, %0" :: "r"(t));
    }
    probeFast(1);                      // A1
    {
        uint32_t t = 0x88;
        __asm__ volatile("csrs mstatus, %0" :: "r"(t));
    }
    probeFast(2);                      // A2
    marker(3);   // R3: CSR block (sans 0xbc0) returned

    // Trap protection for the rest of startup.
    {
        uint32_t hv = (uint32_t)trapCrash;
        __asm__ volatile("csrw mtvec, %0" :: "r"(hv));
    }
    marker(1);   // R1

    {
        uint32_t *src = (uint32_t *)_data_lma;
        uint32_t *dst = (uint32_t *)_data_vma;
        while (dst < (uint32_t *)_edata) *dst++ = *src++;
    }
    {
        uint32_t *p = (uint32_t *)_sbss;
        while (p < (uint32_t *)_ebss) *p++ = 0;
    }
    marker(2);   // R2: data + bss done

    {
        uint32_t gate;
        __asm__ volatile("csrr %0, 0x800" : "=r"(gate));
        blinkByte0((uint8_t)gate);
        blinkByte0((uint8_t)gate);
    }

    {
        uint32_t vbase = ((uint32_t)_start + 4u) | 3u;
        __asm__ volatile("csrw mtvec, %0" :: "r"(vbase));
        atexit(__libc_fini_array);
    }
    marker(4);   // R4: mtvec + atexit done

    __libc_init_array();
    marker(5);   // R5: static ctors done

    SystemInit();
    marker(6);   // R6: SystemInit done

    __asm__ volatile("la t0, main\ncsrw mepc, t0\nmret");
    for (;;) { }   // fallback if mret ever returns
}

void setup()
{
    marker(7);   // R7: main entered, pre_init (systick_init) done

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
