#include <Arduino.h>
#include <HAL.h>
#include "gfx.h"
#include "storage.h"

// State machine for demo
enum State {
    STATE_INIT,
    STATE_CARD_CHECK,
    STATE_MENU,
    STATE_FILE_LIST,
    STATE_FILE_READ,
};

static State state = STATE_INIT;
static bool sdReady = false;

void setup()
{
    gfx::init();
    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);

    // Touch buttons need explicit init in an app (the bootloader does this at
    // boot, but an app binary has its own zeroed threshold arrays). initTouch
    // configures the ADC/TKey hardware + defaults; touchCalibrate() loads the
    // calibration the bootloader stored in EEPROM when it matches this build.
    initTouchButtons();
    touchCalibrate();
}

void drawTitle(const char *title)
{
    gfx::clear();
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, 1);
    gfx::print(title);
    gfx::setCursor(0, 12);
}

void loop()
{
    switch (state) {
    case STATE_INIT:
        drawTitle("OS Demo v0.1");
        gfx::print("Graphics: OK\n");
        gfx::print("Initializing SD...");
        gfx::display();
        state = STATE_CARD_CHECK;
        break;

    case STATE_CARD_CHECK: {
        fat::Result r = fat::mount(&sd::DEVICE);
        if (r == fat::OK) {
            sdReady = true;
            gfx::print(" OK!\n");
            gfx::print("Blocks: ");
            gfx::print((uint16_t)sd::blockCount());
            gfx::display();
            delay(1000);
            state = STATE_MENU;
        } else {
            sdReady = false;
            gfx::print(" FAIL\n");
            gfx::print("Err: ");
            gfx::print((int16_t)r);
            gfx::print("\n\nNo SD card found.\nDemo continues.");
            gfx::display();
            delay(3000);
            state = STATE_MENU;
        }
        break;
    }

    case STATE_MENU:
        drawTitle("Main Menu");
        gfx::print("A: List Files\n");
        gfx::print("B: Read TEST.TXT\n");
        gfx::print("");
        gfx::print("Hold any key...");
        gfx::display();

        // Simple menu: check touch buttons
        if (IsTouched(PIN_BTN_A)) {
            state = STATE_FILE_LIST;
            delay(200);
        }
        if (IsTouched(PIN_BTN_B)) {
            if (sdReady) {
                state = STATE_FILE_READ;
            }
            delay(200);
        }
        break;

    case STATE_FILE_LIST:
        if (!sdReady) {
            drawTitle("File List");
            gfx::print("No SD card!");
            gfx::display();
            delay(2000);
            state = STATE_MENU;
            break;
        }

        {
            drawTitle("Files on SD:");
            fat::openDir("/");
            char name[13];
            bool isDir;
            uint32_t size;
            int count = 0;
            int16_t y = 12;

            while (fat::readDir(name, &isDir, &size) && count < 6) {
                gfx::setCursor(2, y);
                if (isDir) gfx::print("[");
                gfx::print(name);
                if (isDir) gfx::print("]");
                y += 8;
                count++;
            }
            if (count == 0) {
                gfx::print("(empty)");
            }
            gfx::display();
            delay(3000);
            state = STATE_MENU;
        }
        break;

    case STATE_FILE_READ:
        if (!sdReady) {
            state = STATE_MENU;
            break;
        }

        drawTitle("Reading TEST.TXT");
        {
            fat::Result r = fat::open("TEST.TXT");
            if (r == fat::OK) {
                char buf[21];
                uint16_t br;
                int16_t y = 12;
                int lines = 0;

                while (lines < 6) {
                    r = fat::read(buf, 20, &br);
                    if (r != fat::OK || br == 0) break;
                    buf[br] = '\0';

                    // Print, replacing newlines with display wrapping
                    for (int i = 0; buf[i] && y < GFX_HEIGHT - 8; i++) {
                        if (buf[i] == '\n') {
                            y += 8;
                            gfx::setCursor(2, y);
                        } else {
                            gfx::setCursor(2 + (i % 20) * 6, y);
                            char tmp[2] = {buf[i], 0};
                            gfx::print(tmp);
                        }
                    }
                    lines++;
                }
                fat::close();
            } else {
                gfx::print("No TEST.TXT\nfound.");
            }
            gfx::display();
            delay(3000);
            state = STATE_MENU;
        }
        break;
    }
}
