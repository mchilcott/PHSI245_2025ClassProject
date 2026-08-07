#include <Arduino.h>
#include <HAL.h>
#include "gfx.h"
#include "input.h"
#include "ostime.h"
#include "beep.h"
#include "random.h"
#include "pong.h"

void setup()
{
    gfx::init();
    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);
    input::init();
    ostime::init();
    beep::init();
    rng::init();
    pong::init();
    ostime::setFrameRate(40);
}

void loop()
{
    if (!ostime::nextFrame()) return;
    input::update();

    pong::update();
    pong::draw();
    gfx::display();

    if (!pong::isPlaying() && input::justPressed(PIN_BTN_B)) {
        NVIC_SystemReset();
    }
}
