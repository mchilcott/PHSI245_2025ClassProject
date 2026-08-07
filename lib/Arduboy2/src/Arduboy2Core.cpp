/**
 * @file Arduboy2Core.cpp
 * \brief
 * The Arduboy2Core class for Arduboy hardware initilization and control.
 */

#include "Arduboy2Core.h"
#include <HAL.h>

//========================================
//========== class Arduboy2Core ==========
//========================================

// Commands sent to the OLED display to initialize it
const PROGMEM uint8_t Arduboy2Core::lcdBootProgram[] = {
  // boot defaults are commented out but left here in case they
  // might prove useful for reference
  //
  // Further reading: https://www.adafruit.com/datasheets/SSD1306.pdf
  //
  // Display Off
  0xAE,

  // Set Display Clock Divisor v = 0xF0
  // default is 0x80
  0xD5, 0xF0,

  // Set Multiplex Ratio v = 0x3F
  0xA8, 0x3F,

  // Set Display Offset v = 0
  0xD3, 0x00,

  // Set Start Line (0)
  0x40,

  // Charge Pump Setting v = enable (0x14)
  // default is disabled
  0x8D, 0x14,

  // Set Segment Re-map (A0) | (b0001)
  // default is (b0000)
  0xA1,

  // Set COM Output Scan Direction
  0xC8,

  // Set COM Pins v
  0xDA, 0x12,

  // Set Contrast v = 0xCF
  0x81, 0xCF,

  // Set Precharge = 0xF1
  0xD9, 0xF1,

  // Set VCom Detect
  0xDB, 0x40,

  // Entire Display ON
  0xA4,

  // Set normal/inverse display
  0xA6,

  // Display On
  0xAF,

  // set display mode = horizontal addressing mode (0x00)
  0x20, 0x00,

  // set col address range
  // 0x21, 0x00, COLUMN_ADDRESS_END,

  // set page address range
  // 0x22, 0x00, PAGE_ADDRESS_END
};

void Arduboy2Core::boot()
{
  bootPins();
  bootSPI();
  bootOLED();
  bootPowerSaving();
  EEPROM.begin();

  // Load the calibrated touch thresholds stored by the bootloader (if any)
  // so button reads are reliable from the very first poll. Silent: no prompt
  // is shown, so this is safe to run for every app at startup.
  touchCalibrate();
}

// Pins are set to the proper modes and levels for the specific hardware.
// This routine must be modified if any pins are moved to a different port
void Arduboy2Core::bootPins()
{
  // Set up the GPIO pins

    // Configure the touch button pins
    initTouchButtons();

    // Configure the LED pins
    pinMode(PIN_LED_0, OUTPUT);
    pinMode(PIN_LED_1, OUTPUT);

    // Configure the Speaker pin
    digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
    pinMode(PIN_BEEP, OUTPUT);
    
}

void Arduboy2Core::bootOLED()
{
  // A little patch to let the OLED have time for the power supply to get going.
  delay(50);
  // run our customized boot-up command sequence against the
  // OLED to initialize it properly for Arduboy
  i2c_start();
  i2c_send_byte(0x00);
  for (uint8_t i = 0; i < sizeof(lcdBootProgram); i++) 
    i2c_send_byte(pgm_read_byte(lcdBootProgram + i));
  i2c_stop();
}

// Initialize the SPI interface for the display
void Arduboy2Core::bootSPI()
{
  i2c_init();
}

// Write to the SPI bus (MOSI pin)
void Arduboy2Core::SPItransfer(uint8_t data)
{
  // A crappy shim, in case someone is using it to write to the display
  i2c_start();
  i2c_send_byte(0x40);
  i2c_send_byte(data);
  i2c_stop();
}

// Write to and read from the SPI bus (out to MOSI pin, in from MISO pin)
uint8_t Arduboy2Core::SPItransferAndRead(uint8_t data)
{
  SPItransfer(data);
  return 0;
}

void Arduboy2Core::safeMode()
{
  if (buttonsState() == UP_BUTTON)
  {
    digitalWriteRGB(RED_LED, RGB_ON);

#ifndef ARDUBOY_CORE // for Arduboy core timer 0 should remain enabled
    // prevent the bootloader magic number from being overwritten by timer 0
    // when a timer variable overlaps the magic number location
    
    //power_timer0_disable();
#endif

    while (true) { }
  }
}


/* Power Management */

void Arduboy2Core::idle()
{
  // SMCR = _BV(SE); // select idle mode and enable sleeping
  // sleep_cpu();
  // SMCR = 0; // disable sleeping
}

void Arduboy2Core::bootPowerSaving()
{
  // disable Two Wire Interface (I2C) and the ADC
  // All other bits will be written with 0 so will be enabled
  // PRR0 = _BV(PRTWI) | _BV(PRADC);
  // disable USART1
  // PRR1 |= _BV(PRUSART1);
}

// Shut down the display
void Arduboy2Core::displayOff()
{

    //I2C shim
    i2c_start();
    i2c_send_byte(0x00);
    i2c_send_byte(0xAE); // display off
    i2c_send_byte(0x8D); // charge pump:
    i2c_send_byte(0x10); //   disable
    i2c_stop();

  delayShort(250);

}

// Restart the display after a displayOff()
void Arduboy2Core::displayOn()
{
  bootOLED();
}


/* Drawing */

void Arduboy2Core::paint8Pixels(uint8_t pixels)
{
  i2c_start();
  i2c_send_byte(0x40);
  i2c_send_byte(pixels);
  i2c_stop();
}

void Arduboy2Core::paintScreen(const uint8_t *image)
{
  // I2C
  for (int i=0; i<(WIDTH*HEIGHT/(16*8));) {
    // send a bunch of data in one transmission
    i2c_start();
    i2c_send_byte(0x40);
    
    for(uint8_t x=0; x < 16; x++,i++)
    {
      i2c_send_byte(pgm_read_byte(image + i));
    }
    i2c_stop();
  }
}

// paint from a memory buffer, this should be FAST as it's likely what
// will be used by any buffer based subclass
//
// The following assembly code runs "open loop". It relies on instruction
// execution times to allow time for each byte of data to be clocked out.
// It is specifically tuned for a 16MHz CPU clock and SPI clocking at 8MHz.
// For reference, this is the "closed loop" C++ version of paintScreen()
// used prior to the above version.
void Arduboy2Core::paintScreen(uint8_t image[], bool clear)
{
  for (int i=0; i<(WIDTH*HEIGHT/(8));) {
    // send a bunch of data in one transmission
    i2c_start();
    i2c_send_byte(0x40);
    
    for(uint8_t x=0; x < 16; x++,i++)
    {
      i2c_send_byte(pgm_read_byte(image + i));
    }
    i2c_stop();
  }

  if (clear){
    for (uint8_t i=0; i<(WIDTH*HEIGHT/(16*8));) image[i] = 0;
  }
}

void Arduboy2Core::blank()
{
  i2c_start();
  i2c_send_byte(0x40);
  for (int i = 0; i < (HEIGHT*WIDTH)/8; i++)
    i2c_send_byte(0x00);
  i2c_stop();
}

void Arduboy2Core::sendLCDCommand(uint8_t command)
{
  i2c_start();
  i2c_send_byte(0x00);
  i2c_send_byte(command);
  i2c_stop();
}

// invert the display or set to normal
// when inverted, a pixel set to 0 will be on
void Arduboy2Core::invert(bool inverse)
{
  sendLCDCommand(inverse ? OLED_PIXELS_INVERTED : OLED_PIXELS_NORMAL);
}

// turn all display pixels on, ignoring buffer contents
// or set to normal buffer display
void Arduboy2Core::allPixelsOn(bool on)
{
  sendLCDCommand(on ? OLED_ALL_PIXELS_ON : OLED_PIXELS_FROM_RAM);
}

// flip the display vertically or set to normal
void Arduboy2Core::flipVertical(bool flipped)
{
  sendLCDCommand(flipped ? OLED_VERTICAL_FLIPPED : OLED_VERTICAL_NORMAL);
}

// flip the display horizontally or set to normal
void Arduboy2Core::flipHorizontal(bool flipped)
{
  sendLCDCommand(flipped ? OLED_HORIZ_FLIPPED : OLED_HORIZ_NORMAL);
}

/* RGB LED */

void Arduboy2Core::setRGBled(uint8_t red, uint8_t green, uint8_t blue)
{
  // only two LEDs on DevKit, without setup PWM
  digitalWrite(PIN_LED_0, red ? 1 : 0 );
  digitalWrite(PIN_LED_1, green ? 1 : 0 );
  (void)blue;  // parameter unused

}

void Arduboy2Core::setRGBled(uint8_t color, uint8_t val)
{
  if (color == RED_LED)
  {
    digitalWrite(PIN_LED_0, val ? 1 : 0 );
  }
  else if (color == GREEN_LED)
  {
    digitalWrite(PIN_LED_1, val ? 1 : 0 );
  }
}

void Arduboy2Core::freeRGBled()
{
// clear the COM bits to return the pins to normal I/O mode
}

void Arduboy2Core::digitalWriteRGB(uint8_t red, uint8_t green, uint8_t blue)
{
  digitalWrite(PIN_LED_0, red);
  digitalWrite(PIN_LED_1, green);
  (void)blue;  // parameter unused
}

void Arduboy2Core::digitalWriteRGB(uint8_t color, uint8_t val)
{
  if (color == RED_LED)
  {
    digitalWrite(PIN_LED_0, val);
  }
  else if (color == GREEN_LED)
  {
    digitalWrite(PIN_LED_1, val);
  }
}

/* Buttons */

uint8_t Arduboy2Core::buttonsState()
{
  uint8_t buttons (0);
  if (IsTouched(PIN_BTN_UP)) buttons |= UP_BUTTON;
  if (IsTouched(PIN_BTN_LEFT)) buttons |= LEFT_BUTTON;
  if (IsTouched(PIN_BTN_DOWN)) buttons |= DOWN_BUTTON;
  if (IsTouched(PIN_BTN_RIGHT)) buttons |= RIGHT_BUTTON;
  if (IsTouched(PIN_BTN_A)) buttons |= A_BUTTON;
  if (IsTouched(PIN_BTN_B)) buttons |= B_BUTTON;
  return buttons;
}

unsigned long Arduboy2Core::generateRandomSeed()
{
  unsigned long seed;

  seed = Touch_Key_Adc(0) + micros();
  return seed;
}

// delay in ms with 16 bit duration
void Arduboy2Core::delayShort(uint16_t ms)
{
  delay((unsigned long) ms);
}

void Arduboy2Core::exitToBootloader()
{
}
