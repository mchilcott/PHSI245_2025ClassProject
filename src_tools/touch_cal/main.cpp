// Touch button calibration tool.
//
// Reads the raw touch-key (capacitance) value of each button pad on PA0..PA5
// and reports it on the OLED and over USB serial, so that sensible values for
// TOUCH_THRESHOLD / TOUCH_DEBOUNCE in HAL.h can be chosen.
//
// A touched pad has more capacitance, charges more slowly, and so reads LOWER.
//
// USB serial output is one CSV line per update: UP,LEFT,DOWN,RIGHT,B,A
// Send 'r' to reset the min/max columns shown on the screen.

#include <Arduino.h>
#include <Arduboy2.h>
#include <CH32X035_USBSerial.h>
#include <HAL.h>

using namespace wch::usbcdc;

extern "C" uint8_t CDC_ready(void);

#define NUM_BUTTONS     6
#define NUM_AVERAGES    16      // readings averaged per reported value
#define REPORT_PERIOD   100     // ms between reports
#define USB_TIMEOUT     20      // ms to wait for the PC to take a byte

// In ADC channel order, channel n is pin PAn
const char *const button_names[NUM_BUTTONS] = {"UP", "LEFT", "DOWN", "RIGHT", "B", "A"};

Arduboy2 arduboy;

uint16_t value[NUM_BUTTONS];
uint16_t value_min[NUM_BUTTONS];
uint16_t value_max[NUM_BUTTONS];

bool usb_was_connected = false;

void resetMinMax()
{
    for (int i = 0; i < NUM_BUTTONS; ++i)
    {
        value_min[i] = 0xFFFF;
        value_max[i] = 0;
    }
}

void readButtons()
{
    for (int i = 0; i < NUM_BUTTONS; ++i)
    {
        uint32_t sum = 0;
        for (int n = 0; n < NUM_AVERAGES; ++n)
            sum += Touch_Key_Adc(i);
        value[i] = sum / NUM_AVERAGES;

        if (value[i] < value_min[i]) value_min[i] = value[i];
        if (value[i] > value_max[i]) value_max[i] = value[i];
    }
}

// The library's write() blocks forever if nothing on the PC is reading the
// port, so give up on the rest of the line if the PC stops taking data.
void usbPrint(const char *s)
{
    for (; *s; ++s)
    {
        uint32_t start = millis();
        while (!CDC_ready())
            if (millis() - start > USB_TIMEOUT) return;
        USBSerial.write((uint8_t)*s);
    }
}

void reportUSB()
{
    char line[64];

    while (USBSerial.available())
        if (USBSerial.read() == 'r') resetMinMax();

    // Only talk when a terminal has the port open
    if (!USBSerial.dtr())
    {
        usb_was_connected = false;
        return;
    }
    if (!usb_was_connected)
    {
        usb_was_connected = true;
        usbPrint("UP,LEFT,DOWN,RIGHT,B,A\r\n");
    }
    snprintf(line, sizeof(line), "%u,%u,%u,%u,%u,%u\r\n",
             value[0], value[1], value[2], value[3], value[4], value[5]);
    usbPrint(line);
}

void reportScreen()
{
    char line[24];

    arduboy.clear();
    arduboy.setCursor(0, 0);
    arduboy.print("BTN    now  min  max");
    for (int i = 0; i < NUM_BUTTONS; ++i)
    {
        snprintf(line, sizeof(line), "%-5s %4u %4u %4u",
                 button_names[i], value[i], value_min[i], value_max[i]);
        arduboy.setCursor(0, 8 * (i + 1));
        arduboy.print(line);
    }
    snprintf(line, sizeof(line), "thr %4u  hyst %4u", TOUCH_THRESHOLD, TOUCH_DEBOUNCE);
    arduboy.setCursor(0, 56);
    arduboy.print(line);
    arduboy.display();
}

void setup()
{
    // boot() rather than begin(): begin() checks for held buttons at start up,
    // which is no good before the thresholds are right.
    arduboy.boot();
    resetMinMax();
    USBSerial.begin(115200);
}

void loop()
{
    static uint32_t last_report = 0;

    if (millis() - last_report < REPORT_PERIOD) return;
    last_report = millis();

    readButtons();
    reportScreen();
    reportUSB();
}
