#include "HAL.h"
#include <Arduino.h>
#include <ch32x035.h>

uint16_t TouchState (0);

// Per-channel calibrated thresholds (loaded from EEPROM)
static uint16_t touchThreshold[TOUCH_MAX_CHANNELS];
static uint16_t touchDebounce[TOUCH_MAX_CHANNELS];
static bool     touchCalibrated = false;

// Map pin name to ADC touch-key channel (TK0-TK9).
// Arduino pin numbers are 0xC0+ (PIN_A0..); the ADC channel is the low index.
uint8_t pin_to_touch_adc(uint8_t pin)
{
    if (pin >= PA0 && pin <= PA7) return pin - PA0; // PA0-PA7 -> ADC channels 0-7
    if (pin == PB0) return 8;
    if (pin == PB1) return 9;
    return 0;
}

void initTouchButtons()
{
    // Set defaults for all channels
    for (uint8_t ch = 0; ch < TOUCH_MAX_CHANNELS; ch++) {
        touchThreshold[ch] = TOUCH_THRESHOLD_DEFAULT;
        touchDebounce[ch]  = TOUCH_DEBOUNCE_DEFAULT;
    }
    touchCalibrated = false;

    GPIO_InitTypeDef GPIO_InitStructure={0};
	ADC_InitTypeDef ADC_InitStructure={0};
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE );
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );

    // GPIOA pins: UP, LEFT, DOWN, RIGHT (PA0-PA3)
    const unsigned char pa_pins[4] = {PIN_BTN_UP, PIN_BTN_LEFT, PIN_BTN_DOWN, PIN_BTN_RIGHT};
    const uint8_t pa_count = 4;
	for (uint8_t i = 0; i < pa_count; ++i)
	{
		uint8_t ch = pin_to_touch_adc(pa_pins[i]);
		GPIO_InitStructure.GPIO_Pin = (1 << ch); // PA pins map 1:1 to ADC channels
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}

    // GPIOB pins: A, B (PB0-PB1, ADC channels 8-9)
    const uint16_t pb_pins[2] = {GPIO_Pin_0, GPIO_Pin_1};
    for (int i = 0; i < 2; ++ i)
    {
        GPIO_InitStructure.GPIO_Pin = pb_pins[i];
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
    }

    ADC_CLKConfig(ADC1, ADC_CLK_Div6);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
	ADC_Cmd(ADC1, ENABLE);
	TKey1->CTLR1 |= (1<<24);     // Enable TouchKey

}

// TODO: Add some hysteresis for debouncing.

uint16_t Touch_Key_Adc(uint8_t ch)
{
  ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_11Cycles );
  TKey1->IDATAR1 =0x80;  //Charging Time
  TKey1->RDATAR =0x8;   //Discharging Time
  while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC ));
  return (uint16_t) TKey1->RDATAR;
}

int IsTouched(unsigned long int channel)
{
    uint8_t adc_ch = pin_to_touch_adc(channel);
    uint16_t val = Touch_Key_Adc(adc_ch);
    uint16_t thresh = touchThreshold[adc_ch];
    uint16_t deb    = touchDebounce[adc_ch];

    if ( !(TouchState & (1 << adc_ch)) && val < thresh )
        {
            TouchState |= (1 << adc_ch);
            return 1;
        }
    if ( (TouchState & (1 << adc_ch)) && val > (thresh + deb) )
        {
            TouchState &= ~(1 << adc_ch);
            return 0;
        }
    return (TouchState & (1 << adc_ch));
}

#include <EEPROM.h>
#include "gfx.h"

// Calibration stored at EEPROM offsets 0–11 (12 bytes for 6 channels).
// Offset 12 stores a build hash to detect firmware reflash.
#define CALIB_EEPROM_OFFSET 0
#define CALIB_HASH_OFFSET   12

// Stable marker written with calibration data; any non-zero value means a
// calibration exists. Bump when the calibration FORMAT changes so old stored
// data (e.g. 0xE00-clamped thresholds that latch buttons on v2) is ignored.
#define CALIB_FW_HASH       0x5B

// Packed calibration: 8-bit values (threshold/16, debounce/16) per channel.
struct CalibData {
    uint8_t threshold[6];
    uint8_t debounce[6];
};

// Map logical channels (0-5) to physical ADC channels + button names
static uint8_t calibChannelMap[6];
static const char *calibNames[] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B"};

static EEPROMClass calibEEPROM;

// Internal: run the interactive calibration, store results
static void doInteractiveCalibration(uint8_t buildHash)
{
    CalibData calib;

    // --- Interactive calibration ---
    gfx::clear();
    gfx::setCursor(4, 4);
    gfx::print("Touch Calibration");
    gfx::drawFastHLine(0, 12, GFX_WIDTH, GFX_WHITE);
    gfx::display();

    // Wait for user to release all buttons (they just pressed A/B to enter)
    uint32_t startWait = millis();
    bool anyTouched = true;
    while (anyTouched && (millis() - startWait) < 3000) {
        anyTouched = false;
        for (uint8_t i = 0; i < 6; i++) {
            if (Touch_Key_Adc(calibChannelMap[i]) < TOUCH_THRESHOLD_DEFAULT)
                anyTouched = true;
        }
        gfx::setCursor(8, 18);
        gfx::print("Release all buttons");
        gfx::display();
        delay(50);
    }
    if (anyTouched) {
        // Timed out — user didn't release, abort calibration
        gfx::clear();
        gfx::setCursor(8, 22);
        gfx::print("Calibration");
        gfx::setCursor(8, 34);
        gfx::print("cancelled.");
        gfx::display();
        delay(1200);
        return;
    }
    delay(500);

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t ch = calibChannelMap[i];

        gfx::clear();
        gfx::setCursor(4, 4);
        gfx::print("Touch Calibration");
        gfx::drawFastHLine(0, 12, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(8, 18);
        gfx::print("Press and hold: ");
        gfx::setCursor(8, 30);
        gfx::setTextSize(2);
        gfx::print(calibNames[i]);
        gfx::setTextSize(1);
        gfx::setCursor(8, 50);
        gfx::print("Then release...");
        gfx::display();

        uint16_t minVal = 0xFFFF;
        bool wasTouched = false;
        while (true) {
            uint16_t val = Touch_Key_Adc(ch);
            if (val < TOUCH_THRESHOLD_DEFAULT) {
                wasTouched = true;
                if (val < minVal) minVal = val;
            } else if (wasTouched) {
                break;
            }

            // Show live ADC values on the right
            gfx::setTextBgColor(GFX_BLACK);
            gfx::setCursor(80, 18);
            gfx::print("ADC:");
            gfx::setCursor(80, 28);
            gfx::print((int16_t)val);
            gfx::setCursor(80, 40);
            gfx::print("min:");
            gfx::setCursor(80, 50);
            gfx::print((int16_t)minVal);
            gfx::setTextBgColor(GFX_TRANSPARENT);
            gfx::display();

            delay(30);
        }

        uint16_t thresh = minVal * 2;
        if (thresh < 0x300) thresh = 0x300;
        if (thresh > 0xC00) thresh = 0xC00;

        touchThreshold[ch] = thresh;
        touchDebounce[ch]  = thresh / 8;

        calib.threshold[i] = (uint8_t)(thresh >> 4);
        calib.debounce[i]  = (uint8_t)(touchDebounce[ch] >> 4);

        gfx::clear();
        gfx::setCursor(4, 4);
        gfx::print("Touch Calibration");
        gfx::drawFastHLine(0, 12, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(8, 18);
        gfx::print("Press and hold: ");
        gfx::setCursor(8, 30);
        gfx::setTextSize(2);
        gfx::print(calibNames[i]);
        gfx::setTextSize(1);
        gfx::setCursor(8, 50);
        gfx::print("OK!");
        gfx::display();
        delay(300);
    }

    // Save to internal flash
    for (uint8_t i = 0; i < 6; i++) {
        calibEEPROM.write(CALIB_EEPROM_OFFSET + i,     calib.threshold[i]);
        calibEEPROM.write(CALIB_EEPROM_OFFSET + 6 + i, calib.debounce[i]);
    }
    calibEEPROM.write(CALIB_HASH_OFFSET, buildHash);
    calibEEPROM.commit();

    gfx::clear();
    gfx::setCursor(8, 22);
    gfx::print("Calibration done!");
    gfx::display();
    delay(800);

    touchCalibrated = true;
}

void touchCalibrate()
{
    calibChannelMap[0] = pin_to_touch_adc(PIN_BTN_UP);
    calibChannelMap[1] = pin_to_touch_adc(PIN_BTN_DOWN);
    calibChannelMap[2] = pin_to_touch_adc(PIN_BTN_LEFT);
    calibChannelMap[3] = pin_to_touch_adc(PIN_BTN_RIGHT);
    calibChannelMap[4] = pin_to_touch_adc(PIN_BTN_A);
    calibChannelMap[5] = pin_to_touch_adc(PIN_BTN_B);

    calibEEPROM.begin();

    // Load stored calibration only if it matches the current firmware's
    // calibration format (stable CALIB_FW_HASH). This ignores data written by
    // older builds (e.g. the __DATE__/__TIME__-keyed or 0xE00-clamped values)
    // that would leave buttons latched on this board.
    if (calibEEPROM.read(CALIB_HASH_OFFSET) == CALIB_FW_HASH) {
        for (uint8_t i = 0; i < 6; i++) {
            uint8_t ch = calibChannelMap[i];
            touchThreshold[ch] = ((uint16_t)calibEEPROM.read(CALIB_EEPROM_OFFSET + i)) << 4;
            touchDebounce[ch]  = ((uint16_t)calibEEPROM.read(CALIB_EEPROM_OFFSET + 6 + i)) << 4;
        }
        touchCalibrated = true;
        return;
    }
}

void touchCalibrate(uint8_t buildHash)
{
    calibChannelMap[0] = pin_to_touch_adc(PIN_BTN_UP);
    calibChannelMap[1] = pin_to_touch_adc(PIN_BTN_DOWN);
    calibChannelMap[2] = pin_to_touch_adc(PIN_BTN_LEFT);
    calibChannelMap[3] = pin_to_touch_adc(PIN_BTN_RIGHT);
    calibChannelMap[4] = pin_to_touch_adc(PIN_BTN_A);
    calibChannelMap[5] = pin_to_touch_adc(PIN_BTN_B);

    calibEEPROM.begin();

    // Check if already calibrated with this firmware build
    if (calibEEPROM.read(CALIB_HASH_OFFSET) == buildHash) {
        for (uint8_t i = 0; i < 6; i++) {
            uint8_t ch = calibChannelMap[i];
            touchThreshold[ch] = ((uint16_t)calibEEPROM.read(CALIB_EEPROM_OFFSET + i)) << 4;
            touchDebounce[ch]  = ((uint16_t)calibEEPROM.read(CALIB_EEPROM_OFFSET + 6 + i)) << 4;
        }
        touchCalibrated = true;

        // Brief visual confirmation so user knows something happened
        gfx::clear();
        gfx::setCursor(8, 22);
        gfx::print("Already calibrated.");
        gfx::display();
        delay(800);
        return;
    }

    doInteractiveCalibration(buildHash);
}

void touchRecalibrate(uint8_t buildHash)
{
    calibChannelMap[0] = pin_to_touch_adc(PIN_BTN_UP);
    calibChannelMap[1] = pin_to_touch_adc(PIN_BTN_DOWN);
    calibChannelMap[2] = pin_to_touch_adc(PIN_BTN_LEFT);
    calibChannelMap[3] = pin_to_touch_adc(PIN_BTN_RIGHT);
    calibChannelMap[4] = pin_to_touch_adc(PIN_BTN_A);
    calibChannelMap[5] = pin_to_touch_adc(PIN_BTN_B);

    calibEEPROM.begin();
    doInteractiveCalibration(buildHash);
}

void touchCalibrateNow()
{
    calibChannelMap[0] = pin_to_touch_adc(PIN_BTN_UP);
    calibChannelMap[1] = pin_to_touch_adc(PIN_BTN_DOWN);
    calibChannelMap[2] = pin_to_touch_adc(PIN_BTN_LEFT);
    calibChannelMap[3] = pin_to_touch_adc(PIN_BTN_RIGHT);
    calibChannelMap[4] = pin_to_touch_adc(PIN_BTN_A);
    calibChannelMap[5] = pin_to_touch_adc(PIN_BTN_B);

    calibEEPROM.begin();
    doInteractiveCalibration(CALIB_FW_HASH);
}

bool touchIsCalibrated()
{
    return touchCalibrated;
}
