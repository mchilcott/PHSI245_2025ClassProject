#pragma once

#include <stdint.h>

#define PIN_BTN_UP PA0
#define PIN_BTN_LEFT PA1
#define PIN_BTN_DOWN PA2
#define PIN_BTN_RIGHT PA3

#define PIN_BTN_A PB0
#define PIN_BTN_B PB1

#define PIN_LED_0 PB12
#define PIN_LED_1 PB13

#define PIN_BEEP PA15
#define PIN_BEEP_OFF HIGH // The speaker is not drawing current when the pin is high.

#define IO_D0 PA4
#define IO_D1 PA16
#define IO_D2 PA17
#define IO_D3 PA18

// SD card interface — SPI1 PartialRemap2
#define SD_SPI_PORT     SPI1
#define SD_CS_PIN       PA12
#define SD_SCK_PIN      PA11
#define SD_MISO_PIN     PA9
#define SD_MOSI_PIN     PA10

// Button Management
    // Default touch thresholds (used until calibration runs)
    // Chosen so the release point (thresh + deb = 0x0E00) sits below the
    // untouched baseline (~0x0F00) but above the touched range (<=0x0B00):
    //  thresh 0x0E20 + deb 0x01A0 -> release 0x0FC0 latched buttons.
    #define TOUCH_THRESHOLD_DEFAULT 0x0C00
    #define TOUCH_DEBOUNCE_DEFAULT  0x0200
    #define TOUCH_MAX_CHANNELS 10

    // Detect if a certain pin is currently touched
    extern int IsTouched(unsigned long int channel);
    // Start up button adc
    extern void initTouchButtons();

    // Run touch calibration (measures baseline, stores in EEPROM).
    // Call once on first boot — subsequent calls are no-ops if already calibrated.
    extern void touchCalibrate();
    // Returns true if calibration has been run.
    extern bool touchIsCalibrated();

    // Run calibration unconditionally (ignores stored state), saves to flash.
    // `buildHash` is a one-byte identifier that changes on firmware reflash
    // (e.g. XOR of __DATE__ __TIME__ from a recompiled source file).
    extern void touchCalibrate(uint8_t buildHash);

    // Force recalibration regardless of stored state.
    extern void touchRecalibrate(uint8_t buildHash);

    // Run interactive calibration immediately (menu option), stores with the
    // stable CALIB_FW_HASH marker so later boots load it silently.
    extern void touchCalibrateNow();

    // Useful for random numbers
    uint16_t Touch_Key_Adc(uint8_t ch);

    // Map pin to ADC touch-key channel
    uint8_t pin_to_touch_adc(uint8_t pin);

/// I2C connection to display
    void i2c_init(void);
    void i2c_start(void);
    void i2c_send_byte(unsigned char data);
    void i2c_stop(void);

/// SPI connection (for SD card)
    void spi_init(void);
    void spi_set_prescaler(uint16_t prescaler);  // raise clock after SD init
    uint8_t spi_transfer(uint8_t data);
    void spi_cs_low(void);
    void spi_cs_high(void);

#include "extras.h"