#pragma once

#include <stdint.h>

#define PIN_BTN_UP PA0
#define PIN_BTN_LEFT PA1
#define PIN_BTN_DOWN PA2
#define PIN_BTN_RIGHT PA3
#define PIN_BTN_A PA5
#define PIN_BTN_B PA4

#define PIN_LED_0 PB12
#define PIN_LED_1 PB13

#define PIN_I2C_SDA PA11
#define PIN_I2C_SCL PA10

#define PIN_BEEP PA9
#define PIN_BEEP_OFF HIGH // THe speaker is not drawing current when the pin is high.

#define IO_D0 PA15
#define IO_D1 PA16
#define IO_D2 PA17
#define IO_D3 PA18

// Button Management
    // Tune Sensitivity
    #define TOUCH_THRESHOLD 0x0D00
    #define TOUCH_DEBOUNCE  0x0100

    // Detect if a certain pin is currently touched
    extern int IsTouched(unsigned long int channel);
    // Start up button adc
    extern void initTouchButtons();

    // Useful for random numbers
    uint16_t Touch_Key_Adc(uint8_t ch);

/// I2C connection to display
    void i2c_init(void); 
    void i2c_start(void);
    void i2c_send_byte(unsigned char data);
    void i2c_stop(void);

#include "extras.h"
