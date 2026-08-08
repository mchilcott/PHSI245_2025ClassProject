// USBHID — composite USB HID app for the PHSI245 v2 board.
//
// Three modes:
//   Gamepad       — standard HID gamepad (6 buttons via D-pad + A/B)
//   Mouse Jiggler — auto-jiggle with manual D-pad control; manual input
//                   disables auto-jiggle, A clicks, short B right-clicks,
//                   hold B exits
//   Script        — browse the SD card and run a text HID script (#KEY, text
//                   typing, #MOVE/#CLICK/#SCROLL, #DELAY/#BEEP/#LED/#MSG/...)
//
// Exit modes with B; B held at power-up (reset) returns to the bootloader.

#include <Arduino.h>
#include <HAL.h>
#include <string.h>
#include "gfx.h"
#include "input.h"
#include "led.h"
#include "beep.h"
#include "usb_hid.h"
#include "script.h"
#include "storage.h"

// --- Mode ---

enum Mode {
    MODE_MENU,
    MODE_GAMEPAD,
    MODE_JIGGLE,
    MODE_SCRIPT_BROWSE,
    MODE_SCRIPT_RUN,
    MODE_SCRIPT_DONE,
};
static Mode mode = MODE_MENU;

// --- Main menu ---

static const char *menuItems[] = { "Gamepad", "Mouse Jiggler", "Run Script", "Restart" };
static const uint8_t menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
static uint8_t menuCursor = 0;

// --- Jiggler ---

static const uint32_t jiggleIntervals[] = { 1000, 5000, 30000, 60000, 120000 };
static uint8_t        jiggleIdx   = 1;
static uint32_t       lastJiggle  = 0;
static uint8_t        jigglePhase = 0;
static bool           jiggleEnabled = true;  // auto-jiggle on; manual D-pad disables
static uint32_t       jiggleBHold   = 0;     // B press time (0 = not held); short=right click, hold=exit

// --- Gamepad ---

static uint32_t gamepadExitAt = 0;   // B held this long exits to the menu

// --- Script browser ---

#define MAX_FILES   64
#define MAX_PATH    64
#define LIST_Y      12
#define MAX_VISIBLE 5

struct FileEntry {
    char name[13];
    bool isDir;
    uint32_t size;
};

static FileEntry files[MAX_FILES];
static uint8_t  fileCount  = 0;
static uint8_t  cursor     = 0;
static uint8_t  scrollOff  = 0;
static char     currentPath[MAX_PATH] = "/";

static char     browseErr[20];
static uint32_t browseErrUntil = 0;
static bool     sdUnresponsive = false;   // last listDir found the card unresponsive
static char     runName[13];

static uint32_t   doneUntil = 0;
static const char *doneMsg  = "";

// --- Small helpers ---

static void sendMove(int8_t dx, int8_t dy)
{
    uint8_t rep[4] = { 0, (uint8_t)dx, (uint8_t)dy, 0 };
    usb_hid::sendReport(usb_hid::RID_MOUSE, rep, 4);
}

// Send a click: press, hold briefly so the host sees it, release. `right`
// selects the right button (0x02); otherwise left (0x01).
static void sendClick(bool right)
{
    uint8_t rep[4] = { right ? 0x02 : 0x01, 0, 0, 0 };
    usb_hid::sendReport(usb_hid::RID_MOUSE, rep, 4);
    delay(50);
    rep[0] = 0;
    usb_hid::sendReport(usb_hid::RID_MOUSE, rep, 4);
}

// A full gfx::display() pushes the 1 KB framebuffer over I2C (~20-30 ms). If
// every loop iteration rendered, script timing (#SPEED per-char delay, #DELAY
// granularity) would be stretched to multiples of that period. HID/script/input
// logic runs every loop; only the OLED refresh is throttled to ~20 fps.
static uint32_t renderDue = 0;
static bool renderGate()
{
    uint32_t now = millis();
    if ((int32_t)(now - renderDue) < 0) return false;
    renderDue = now + 50;
    return true;
}

// --- Browser helpers (mirror src_os/main.cpp) ---

static uint8_t pathLen()
{
    uint8_t n = 0;
    while (currentPath[n]) n++;
    return n;
}

static bool isAtRoot()
{
    return currentPath[0] == '/' && currentPath[1] == '\0';
}

static void pathPop()
{
    uint8_t len = pathLen();
    while (len > 1) {
        len--;
        if (currentPath[len] == '/') {
            currentPath[len] = '\0';
            return;
        }
    }
    currentPath[0] = '/';
    currentPath[1] = '\0';
}

static bool pathPush(const FileEntry *e)
{
    uint8_t base = pathLen();
    if (base > 0 && currentPath[base - 1] != '/') {
        if (base + 1 >= MAX_PATH) return false;
        currentPath[base++] = '/';
    }
    uint8_t len = 0;
    while (e->name[len]) len++;
    if (base + len >= MAX_PATH) return false;
    for (uint8_t i = 0; i < len; i++)
        currentPath[base + i] = e->name[i];
    currentPath[base + len] = '\0';
    return true;
}

static void buildFilePath(char *out, uint8_t outSize, const FileEntry *e)
{
    uint8_t base = pathLen();
    if (base >= outSize) base = outSize - 1;
    for (uint8_t i = 0; i < base; i++)
        out[i] = currentPath[i];
    uint8_t pos = base;
    if (pos > 0 && out[pos - 1] != '/') {
        if (pos + 1 >= outSize) { out[pos] = '\0'; return; }
        out[pos++] = '/';
    }
    uint8_t len = 0;
    while (e->name[len]) len++;
    if (pos + len >= outSize) len = outSize - pos - 1;
    for (uint8_t i = 0; i < len; i++)
        out[pos + i] = e->name[i];
    out[pos + len] = '\0';
}

// The script browser only opens .TXT files (directories stay navigable).
static bool isTxtFile(const char *name)
{
    uint8_t len = 0;
    while (name[len]) len++;
    if (len < 5) return false;              // "X.TXT" is 5 chars
    const char *ext = name + len - 4;       // ".TXT"
    return ext[0] == '.' &&
           (ext[1] == 'T' || ext[1] == 't') &&
           (ext[2] == 'X' || ext[2] == 'x') &&
           (ext[3] == 'T' || ext[3] == 't');
}

static uint8_t listDir(const char *path)
{
    uint8_t count = 0;
    char name[13];
    bool isDir;
    uint32_t size;

    if (fat::openDir(path) != fat::OK)
        return 0;

    while (fat::readDir(name, &isDir, &size) && count < MAX_FILES) {
        if (name[0] == '.' && name[1] == '\0') continue;
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') continue;
        if (!isDir && !isTxtFile(name)) continue;   // only .TXT files open
        files[count].isDir = isDir;
        files[count].size  = size;
        strncpy(files[count].name, name, 12);
        files[count].name[12] = '\0';
        count++;
    }
    return count;
}

// --- Setup ---

void setup()
{
    gfx::init();
    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);
    input::init();
    led::init();
    beep::init();
    touchCalibrate();
    usb_hid::init();
    script::init();
    storage::sdAvailable();   // warm the SD mount check
}

// --- Mode: menu ---

static void modeMenu()
{
    if (input::justPressed(PIN_BTN_UP)) {
        if (menuCursor > 0) menuCursor--;
    }
    if (input::justPressed(PIN_BTN_DOWN)) {
        if (menuCursor < menuCount - 1) menuCursor++;
    }
    if (input::justPressed(PIN_BTN_A)) {
        delay(150);
        switch (menuCursor) {
        case 0: {
            uint8_t zero = 0;
            usb_hid::sendReport(usb_hid::RID_GAMEPAD, &zero, 1);
            mode = MODE_GAMEPAD; gamepadExitAt = 0; break;
        }
        case 1: mode = MODE_JIGGLE; lastJiggle = millis(); jigglePhase = 0;
                jiggleEnabled = true; jiggleBHold = 0; break;
        case 2: mode = MODE_SCRIPT_BROWSE;
                currentPath[0] = '/'; currentPath[1] = '\0';
                browseReload(); break;
        case 3: NVIC_SystemReset(); break;
        }
    }
    if (input::justPressed(PIN_BTN_B)) {
        delay(150);
        NVIC_SystemReset();
    }

    gfx::clear();
    gfx::setCursor(2, 1);
    gfx::print("USB HID");
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
    for (uint8_t i = 0; i < menuCount; i++) {
        gfx::setCursor(2, 12 + i * 10);
        if (i == menuCursor) gfx::print("> ");
        else                 gfx::print("  ");
        gfx::print(menuItems[i]);
    }
    gfx::setCursor(2, 57);
    gfx::print(usb_hid::isConnected() ? "USB connected" : "USB not connected");
    if (renderGate()) gfx::display();
}

// --- Mode: gamepad ---

static void modeGamepad()
{
    uint8_t rep = 0;
    if (input::pressed(PIN_BTN_A))     rep |= 0x01;  // button 1
    if (input::pressed(PIN_BTN_B))     rep |= 0x02;  // button 2
    if (input::pressed(PIN_BTN_UP))    rep |= 0x04;  // button 3
    if (input::pressed(PIN_BTN_DOWN))  rep |= 0x08;  // button 4
    if (input::pressed(PIN_BTN_LEFT))  rep |= 0x10;  // button 5
    if (input::pressed(PIN_BTN_RIGHT)) rep |= 0x20;  // button 6
    usb_hid::sendReport(usb_hid::RID_GAMEPAD, &rep, 1);

    if (input::pressed(PIN_BTN_B)) {
        if (gamepadExitAt == 0) gamepadExitAt = millis() + 800;
        if ((int32_t)(millis() - gamepadExitAt) >= 0) {
            // Release button 2: the host only learns about a release through a
            // report, so send an all-zero gamepad report before the mode stops
            // emitting them.
            uint8_t zero = 0;
            usb_hid::sendReport(usb_hid::RID_GAMEPAD, &zero, 1);
            delay(150);
            mode = MODE_MENU;
            return;
        }
    } else {
        gamepadExitAt = 0;
    }

    gfx::clear();
    gfx::setCursor(2, 1);
    gfx::print("Gamepad");
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, 12);
    gfx::print("A,B: buttons 1,2\n");
    gfx::print("D-pad: buttons 3-6\n");
    gfx::setCursor(2, 48);
    gfx::print("Hold B: exit");
    if (gamepadExitAt) {
        gfx::setCursor(2, 57);
        gfx::print("B releasing...");
    }
    if (renderGate()) gfx::display();
}

// --- Mode: jiggler ---

static void modeJiggle()
{
    uint32_t now = millis();

    // Auto-jiggle: small diamond pattern on the configured interval. Manual
    // D-pad input below disables it for the rest of this session.
    if (jiggleEnabled && (now - lastJiggle) >= jiggleIntervals[jiggleIdx]) {
        lastJiggle = now;
        static const int8_t pat[4][2] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };
        sendMove(pat[jigglePhase][0], pat[jigglePhase][1]);
        jigglePhase = (uint8_t)((jigglePhase + 1) & 3);
    }

    // Manual control via D-pad: moving the mouse manually disables auto-jiggle
    // so it stops fighting the user's input.
    if (input::pressed(PIN_BTN_UP))    { jiggleEnabled = false; sendMove(0, -3); }
    if (input::pressed(PIN_BTN_DOWN))  { jiggleEnabled = false; sendMove(0, 3); }
    if (input::pressed(PIN_BTN_LEFT))  { jiggleEnabled = false; sendMove(-3, 0); }
    if (input::pressed(PIN_BTN_RIGHT)) { jiggleEnabled = false; sendMove(3, 0); }

    // A: left click.
    if (input::justPressed(PIN_BTN_A)) sendClick(false);

    // B: short press = right click (fired on release), hold >= 800 ms = exit.
    if (input::justPressed(PIN_BTN_B)) jiggleBHold = now;
    if (input::justReleased(PIN_BTN_B) && jiggleBHold) {
        if ((int32_t)(now - jiggleBHold) < 800) sendClick(true);
        jiggleBHold = 0;
    }
    if (jiggleBHold && (int32_t)(now - jiggleBHold) >= 800) {
        delay(150);
        mode = MODE_MENU;
        return;
    }

    gfx::clear();
    gfx::setCursor(2, 1);
    gfx::print("Mouse Jiggler");
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, 12);
    if (jiggleEnabled) {
        gfx::print("Auto:");
        gfx::print((int16_t)(jiggleIntervals[jiggleIdx] / 1000));
        gfx::print("s\n");
    } else {
        gfx::print("Jiggler disabled\n");
    }
    gfx::print("D-pad: manual move\n");
    gfx::print("A: click  B: right\n");
    gfx::setCursor(2, 57);
    gfx::print("Hold B: exit");
    if (renderGate()) gfx::display();
}

// --- Mode: script browser ---

// Reload the current directory. Returns true if the listing is usable; false
// only if the card stays unresponsive even after recovery.
//
// A sustained script run can leave the card ignoring even CMD0. Re-init once
// (mirroring src_os/main.cpp collectFiles()), then try an extended clock-burst
// recovery (CS high + a long burst of clocks) before giving up.
static bool browseReload()
{
    cursor = 0;
    scrollOff = 0;
    fileCount = listDir(currentPath);
    if (fileCount > 0) {
        sdUnresponsive = false;
        return true;
    }
    sdUnresponsive = true;

    // Normal recovery: full card re-init (CMD0/ACMD41 at ~187kHz) + re-list.
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        if (!sd::init()) continue;
        fileCount = listDir(currentPath);
        if (fileCount > 0) {
            sdUnresponsive = false;
            return true;
        }
    }

    // Extended clock-burst recovery: hold CS high and clock the bus hard for
    // tens of ms (ChaN recommends 74+ clocks), then re-init. Some latched
    // cards need far more clocks than a single init's 80.
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        spi_cs_high();
        for (int i = 0; i < 8192; i++) sd::spiRecv();
        if (!sd::init()) continue;
        fileCount = listDir(currentPath);
        if (fileCount > 0) {
            sdUnresponsive = false;
            return true;
        }
    }
    return false;
}

static void modeScriptBrowse()
{
    if (input::justPressed(PIN_BTN_UP) && cursor > 0) cursor--;
    if (input::justPressed(PIN_BTN_DOWN) && cursor < fileCount - 1) cursor++;

    if (input::justPressed(PIN_BTN_A)) {
        if (fileCount > 0) {
            const FileEntry *e = &files[cursor];
            if (e->isDir) {
                if (pathPush(e)) browseReload();
            } else {
                char path[MAX_PATH];
                buildFilePath(path, sizeof path, e);
                if (script::start(path)) {
                    strncpy(runName, e->name, 12);
                    runName[12] = '\0';
                    mode = MODE_SCRIPT_RUN;
                } else {
                    strncpy(browseErr, "Can't open file", sizeof browseErr - 1);
                    browseErr[sizeof browseErr - 1] = '\0';
                    browseErrUntil = millis() + 1500;
                }
            }
        }
        delay(150);
    }

    if (input::justPressed(PIN_BTN_B)) {
        delay(150);
        if (isAtRoot()) {
            mode = MODE_MENU;
        } else {
            pathPop();
            browseReload();
        }
    }

    gfx::clear();
    // Header: current path (truncated)
    gfx::setCursor(1, 1);
    uint8_t pl = pathLen();
    if (pl <= 20) gfx::print(currentPath);
    else {
        char buf[22];
        uint8_t keep = 18;
        for (uint8_t i = 0; i < keep; i++) buf[i] = currentPath[i];
        buf[keep] = '.'; buf[keep + 1] = '.'; buf[keep + 2] = '\0';
        gfx::print(buf);
    }
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);

    if (fileCount == 0) {
        gfx::setCursor(2, 20);
        gfx::print(sdUnresponsive ? "No SD response" : "(empty)");
        gfx::setCursor(2, 48);
        gfx::print("B: back");
    } else {
        const char *items[MAX_VISIBLE];
        uint8_t n = fileCount;
        if (n > MAX_VISIBLE) n = MAX_VISIBLE;
        uint8_t sc = scrollOff;
        uint8_t visible = gfx::clampScroll(cursor, fileCount, MAX_VISIBLE);
        for (uint8_t i = 0; i < n; i++) items[i] = files[visible + i].name;
        gfx::drawScrollMenu(2, LIST_Y, items, n, cursor - visible, MAX_VISIBLE, &sc);
        scrollOff = visible;
        gfx::setCursor(2, 57);
        gfx::print("A: open   B: back");
    }

    if ((int32_t)(millis() - browseErrUntil) < 0 && browseErr[0]) {
        gfx::fillRect(0, 28, GFX_WIDTH, 10, GFX_BLACK);
        gfx::setCursor(2, 29);
        gfx::print(browseErr);
    }
    if (renderGate()) gfx::display();
}

// --- Mode: script run ---

static void modeScriptRun()
{
    if (script::isRunning()) {
        script::tick();
    }

    if (input::justPressed(PIN_BTN_B)) {
        delay(150);
        script::abort();
        browseReload();
        mode = MODE_SCRIPT_BROWSE;
        return;
    }

    if (!script::isRunning()) {
        doneMsg = "Script done";
        doneUntil = millis() + 1500;
        mode = MODE_SCRIPT_DONE;
        return;
    }

    gfx::clear();
    // Persistent #MSG text sits at the top and stays until the next script
    // action that uses the screen replaces it (or the script ends).
    if (script::message()[0]) {
        gfx::setCursor(2, 2);
        gfx::print(script::message());
    }
    // Application activity/status lives at the bottom of the display so it
    // never displaces the script message.
    gfx::drawFastHLine(0, 38, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, 41);
    gfx::print("Running: ");
    gfx::print(runName);
    gfx::setCursor(2, 50);
    if (script::isPaused())
        gfx::print("Paused - A: resume");
    else
        gfx::print("B: abort");
    if (renderGate()) gfx::display();
}

// --- Mode: script done ---

static void modeScriptDone()
{
    if (input::justPressed(PIN_BTN_A) || input::justPressed(PIN_BTN_B)) {
        delay(150);
        browseReload();
        mode = MODE_SCRIPT_BROWSE;
        return;
    }
    if ((int32_t)(millis() - doneUntil) >= 0) {
        browseReload();
        mode = MODE_SCRIPT_BROWSE;
        return;
    }

    gfx::clear();
    gfx::setCursor(1, 1);
    gfx::print("Script");
    gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, 24);
    gfx::print(doneMsg);
    gfx::setCursor(2, 57);
    gfx::print("A/B: continue");
    if (renderGate()) gfx::display();
}

// --- Loop ---

void loop()
{
    input::update();

    switch (mode) {
    case MODE_MENU:            modeMenu(); break;
    case MODE_GAMEPAD:         modeGamepad(); break;
    case MODE_JIGGLE:          modeJiggle(); break;
    case MODE_SCRIPT_BROWSE:   modeScriptBrowse(); break;
    case MODE_SCRIPT_RUN:      modeScriptRun(); break;
    case MODE_SCRIPT_DONE:     modeScriptDone(); break;
    }
}
