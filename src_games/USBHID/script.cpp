// Minimal text-script interpreter for the USBHID app (see script.h and
// HID_SCRIPT.md). Reads an SD text file line by line and drives the HID
// device plus local hardware (beep, LEDs, display).

#include "script.h"
#include "usb_hid.h"
#include "hid_keymap.h"
#include "storage.h"
#include <HAL.h>
#include <Arduino.h>
#include <string.h>
#include "input.h"
#include "led.h"
#include "beep.h"

namespace script {

// --- Constants ---

#define LINE_MAX     80
#define CHUNK_MAX    64
#define CLICK_STEPS  4

// Mouse button bits (report byte 0)
#define MBTN_LEFT    0x01
#define MBTN_RIGHT   0x02
#define MBTN_MIDDLE  0x04

// --- State ---

enum State {
    ST_IDLE,
    ST_READING,       // accumulating a line from the file
    ST_CMD,           // parsing/dispatching a #command
    ST_TYPING,        // press the current character
    ST_TYPING_WAIT,   // holding the current character
    ST_WAIT,          // #DELAY or #MSG timeout
    ST_WAIT_BUTTON,   // #PAUSE — waiting for A
    ST_KEY,           // #KEY held for 30 ms
    ST_CLICK,         // #CLICK button sequence
};

static State state = ST_IDLE;

// Line / chunk buffers
static char    line[LINE_MAX];
static uint8_t lineLen;
static uint8_t typePos;
static char    chunk[CHUNK_MAX];
static uint8_t chunkLen;
static uint8_t chunkPos;
static bool    fileEof;

// Timing / typing
static uint8_t  speedMs;    // per-character delay (#SPEED)
static uint32_t holdEnd;    // millis() deadline for the current wait/hold

// On-screen message (#MSG). Cleared on script end; replaced by the next #MSG.
static char dispMsg[LINE_MAX];

// Keyboard state
static uint8_t kbMod;
static uint8_t kbKeys[6];
static uint8_t kbKeyCount;

// Mouse state
static uint8_t mouseBtn;    // buttons currently down (sticky + transient)
static int8_t  mouseDx, mouseDy, mouseWheel;

// #CLICK sequence
struct ClickStep { uint8_t action; uint16_t afterMs; };  // 0 = press, 1 = release
static ClickStep clickSteps[CLICK_STEPS];
static uint8_t   clickPos;
static uint8_t   clickCount;
static uint8_t   clickBtn;

// Current #KEY report
static uint8_t keyMod;
static uint8_t keyUsage;

// --- Small helpers ---

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static bool caseEq(const char *a, const char *b)
{
    while (*a && *b) {
        if (up(*a) != up(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

// Parse a leading unsigned integer. Returns true and fills *out if digits found.
static bool parseUint(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    bool any = false;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
        any = true;
    }
    if (any) *out = v;
    return any;
}

// Parse a leading signed integer. Returns true and fills *out if digits found.
static bool parseInt(const char *s, int32_t *out)
{
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    uint32_t v;
    if (!parseUint(s, &v)) return false;
    int32_t sv = (v > 0x7FFFFFFF) ? 0x7FFFFFFF : (int32_t)v;
    *out = neg ? -sv : sv;
    return true;
}

// Map a button name to a mouse button bit. Defaults to left.
static uint8_t parseBtn(const char *s)
{
    if (caseEq(s, "RIGHT") || caseEq(s, "R")) return MBTN_RIGHT;
    if (caseEq(s, "MIDDLE") || caseEq(s, "MID") || caseEq(s, "M")) return MBTN_MIDDLE;
    return MBTN_LEFT;
}

// --- HID report senders ---

static void kbSend()
{
    uint8_t rep[8] = { kbMod, 0, kbKeys[0], kbKeys[1], kbKeys[2],
                       kbKeys[3], kbKeys[4], kbKeys[5] };
    usb_hid::sendReport(usb_hid::RID_KEYBOARD, rep, 8);
}

static void kbPush(uint8_t usage)
{
    if (kbKeyCount < 6) kbKeys[kbKeyCount++] = usage;
    kbSend();
}

static void kbReleaseAll()
{
    kbMod = 0;
    kbKeyCount = 0;
    memset(kbKeys, 0, sizeof kbKeys);
    kbSend();
}

static void mouseSend()
{
    uint8_t rep[4] = { mouseBtn, (uint8_t)mouseDx, (uint8_t)mouseDy, (uint8_t)mouseWheel };
    usb_hid::sendReport(usb_hid::RID_MOUSE, rep, 4);
    mouseDx = 0;
    mouseDy = 0;
    mouseWheel = 0;
}

// --- File reading ---

static bool refillChunk()
{
    if (fileEof) return false;
    uint16_t br = 0;
    if (fat::read(chunk, CHUNK_MAX, &br) != fat::OK) {
        fileEof = true;
        return false;
    }
    chunkLen = (uint8_t)br;
    chunkPos = 0;
    if (chunkLen == 0) {
        fileEof = true;
        return false;
    }
    return true;
}

// Start reading the next line from the file.
static void beginReading()
{
    state   = ST_READING;
    lineLen = 0;
    typePos = 0;
}

// A complete line is in `line`. Classify it and move on.
static void finishLine()
{
    line[lineLen] = '\0';

    if (lineLen == 0) {                       // blank line
        beginReading();
        return;
    }
    if (line[0] == '#' && line[1] == '#') {   // comment
        beginReading();
        return;
    }
    if (line[0] == '#') {                     // command
        state = ST_CMD;
        return;
    }
    typePos = 0;                              // literal text
    state   = ST_TYPING;
}

// Stop the script, releasing every HID key/button.
static void endScript()
{
    kbReleaseAll();
    mouseBtn = 0;
    mouseSend();
    dispMsg[0] = '\0';
    fat::close();
    state = ST_IDLE;
}

// --- Typing machinery ---

// Resolve the next logical unit from the text line into a keypress.
// Handles the escapes \n, \t, \\, \#. Returns false when the line is consumed.
static bool resolveTypedUnit(uint8_t &mod, uint8_t &usage)
{
    if (typePos >= lineLen) return false;
    char c = line[typePos++];
    if (c == '\\' && typePos < lineLen) {
        char e = line[typePos++];
        switch (e) {
        case 'n': mod = 0; usage = 0x28; return true;             // Enter
        case 't': mod = 0; usage = 0x2B; return true;             // Tab
        case '\\': return hidkey::lookupAscii('\\', mod, usage);
        case '#':  return hidkey::lookupAscii('#', mod, usage);
        default:   // unknown escape: backslash is literal, e is typed next
            typePos--;
            return hidkey::lookupAscii('\\', mod, usage);
        }
    }
    return hidkey::lookupAscii(c, mod, usage);
}

// --- #KEY parsing ("CTRL+C", "ALT+TAB", "F5") ---

static void keyCommand(const char *arg)
{
    uint8_t mods = 0;
    uint8_t usage = 0;
    const char *p = arg;

    while (*p) {
        char tok[24];
        uint8_t ti = 0;
        while (*p && *p != '+' && ti < 23) tok[ti++] = *p++;
        tok[ti] = '\0';

        uint8_t m = 0, u = 0;
        if (ti == 1 && ((tok[0] >= 'a' && tok[0] <= 'z') ||
                        (tok[0] >= 'A' && tok[0] <= 'Z'))) {
            // Single letter: use the lowercase key (no shift), so CTRL+C is
            // control+c rather than control+shift+c.
            char lc = (tok[0] >= 'A' && tok[0] <= 'Z') ? (char)(tok[0] + 32) : tok[0];
            if (!hidkey::lookupAscii(lc, m, u)) { beginReading(); return; }
            mods |= m;
            usage = u;
        } else if (hidkey::lookupName(tok, m, u)) {
            if (u == 0) mods |= m;       // pure modifier (CTRL/SHIFT/ALT/GUI)
            else        usage = u;       // named key (F5, TAB, ...)
        } else if (ti == 1 && hidkey::lookupAscii(tok[0], m, u)) {
            mods |= m;
            usage = u;
        } else {
            beginReading();              // unknown token — ignore the line
            return;
        }
        if (*p == '+') p++;
    }

    if (usage == 0) { beginReading(); return; }   // modifiers alone do nothing

    keyMod   = mods;
    keyUsage = usage;
    kbMod    = mods;
    kbKeyCount = 0;
    memset(kbKeys, 0, sizeof kbKeys);
    kbPush(keyUsage);
    holdEnd = millis() + 30;
    state   = ST_KEY;
}

// --- #CLICK setup ---

static void clickCommand(const char *arg)
{
    clickPos   = 0;
    clickCount = 0;
    clickBtn   = parseBtn(arg);

    bool dbl = false;
    if (caseEq(arg, "DOUBLE") || caseEq(arg, "D")) dbl = true;

    if (dbl) {
        clickSteps[0] = {0, 15};   // press
        clickSteps[1] = {1, 15};   // release
        clickSteps[2] = {0, 40};   // gap, then press again
        clickSteps[3] = {1, 15};   // release
        clickCount = 4;
    } else {
        clickSteps[0] = {0, 15};   // press
        clickSteps[1] = {1, 15};   // release
        clickCount = 2;
    }

    holdEnd = millis() + clickSteps[0].afterMs;
    state   = ST_CLICK;
}

// --- #PRESS / #RELEASE ---

static void buttonCommand(const char *arg, bool pressed)
{
    uint8_t b = parseBtn(arg);
    if (pressed) mouseBtn |= b;
    else         mouseBtn &= (uint8_t)~b;
    mouseSend();
    beginReading();
}

// --- Command dispatch ---

static void dispatchCommand(const char *name, const char *arg)
{
    uint32_t v;

    if (caseEq(name, "DELAY")) {
        if (parseUint(arg, &v)) {
            holdEnd = millis() + v;
            state   = ST_WAIT;
        } else beginReading();
    } else if (caseEq(name, "BEEP")) {
        uint16_t ms = 100;
        if (parseUint(arg, &v)) ms = (uint16_t)v;
        // The SD card wedges (needs USB unplug) when the speaker toggles at
        // 2 kHz (beep_ms) while the card is mounted — beep1/beep2/beep5/beep100
        // stress all reproduced it. Low-frequency tones are safe: 200/100/50 Hz
        // (>=5 ms between toggle edges) survive the stress indefinitely, so
        // beep as a 200 Hz tone. The 10 ms settle delay matches the tested
        // gap and lets the bus go quiet before the next SD read.
        beep::tone(200, ms);
        delay(10);
        beginReading();
    } else if (caseEq(name, "LED")) {
        if (parseUint(arg, &v)) {
            led::off(PIN_LED_0);
            led::off(PIN_LED_1);
            if (v == 1 || v == 3) led::on(PIN_LED_0);
            if (v == 2 || v == 3) led::on(PIN_LED_1);
        }
        beginReading();
    } else if (caseEq(name, "MSG")) {
        strncpy(dispMsg, arg, LINE_MAX - 1);
        dispMsg[LINE_MAX - 1] = '\0';
        // Persist until the next script action that uses the screen replaces
        // it (a later #MSG) or the script ends. Never times out.
        beginReading();
    } else if (caseEq(name, "PAUSE")) {
        // The app shows the "Paused - A: resume" hint in its status area; the
        // on-screen message (if any) stays untouched.
        state = ST_WAIT_BUTTON;
    } else if (caseEq(name, "SPEED")) {
        if (parseUint(arg, &v) && v <= 255) speedMs = (uint8_t)v;
        beginReading();
    } else if (caseEq(name, "STOP")) {
        endScript();
    } else if (caseEq(name, "KEY")) {
        keyCommand(arg);
    } else if (caseEq(name, "MOVE")) {
        int32_t x = 0, y = 0;
        const char *p = arg;
        if (parseInt(p, &x)) {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            if (!parseInt(p, &y)) y = 0;
        }
        mouseDx = (int8_t)x;
        mouseDy = (int8_t)y;
        mouseSend();
        beginReading();
    } else if (caseEq(name, "CLICK")) {
        clickCommand(arg);
    } else if (caseEq(name, "PRESS")) {
        buttonCommand(arg, true);
    } else if (caseEq(name, "RELEASE")) {
        buttonCommand(arg, false);
    } else if (caseEq(name, "SCROLL")) {
        int32_t s = 0;
        if (parseInt(arg, &s)) {
            mouseWheel = (int8_t)s;
            mouseSend();
        }
        beginReading();
    } else {
        beginReading();   // unknown command — ignored
    }
}

// --- Public API ---

void init()
{
    state      = ST_IDLE;
    lineLen    = 0;
    typePos    = 0;
    chunkLen   = 0;
    chunkPos   = 0;
    fileEof    = false;
    speedMs    = 20;
    kbMod      = 0;
    kbKeyCount = 0;
    mouseBtn   = 0;
    mouseDx = mouseDy = mouseWheel = 0;
    clickPos = clickCount = 0;
    dispMsg[0] = '\0';
    memset(kbKeys, 0, sizeof kbKeys);
}

bool start(const char *path)
{
    if (fat::open(path) != fat::OK) return false;
    init();
    state = ST_READING;
    return true;
}

void abort()
{
    endScript();
}

bool isRunning() { return state != ST_IDLE; }
bool isPaused()  { return state == ST_WAIT_BUTTON; }
const char *message() { return dispMsg; }

void tick()
{
    switch (state) {

    case ST_IDLE:
        break;

    case ST_READING: {
        if (chunkPos >= chunkLen && !refillChunk()) {
            // End of file: wrap up any partial line, then stop.
            if (lineLen > 0) {
                finishLine();
                if (state == ST_READING) endScript();   // empty/comment at EOF
            } else {
                endScript();
            }
            break;
        }
        uint8_t c = chunk[chunkPos++];
        if (c == '\n' || c == '\r') {
            finishLine();
            break;
        }
        if (lineLen < LINE_MAX - 1) line[lineLen++] = (char)c;
        break;
    }

    case ST_CMD: {
        // line[0] == '#', command starts at line[1]
        const char *p = line + 1;
        char name[16];
        uint8_t ni = 0;
        while (*p && *p != ' ' && ni < 15) name[ni++] = up(*p++);
        name[ni] = '\0';
        while (*p == ' ') p++;
        dispatchCommand(name, p);
        break;
    }

    case ST_TYPING: {
        uint8_t mod, usage;
        if (resolveTypedUnit(mod, usage)) {
            kbMod = mod;
            kbKeyCount = 0;
            memset(kbKeys, 0, sizeof kbKeys);
            kbPush(usage);
            holdEnd = millis() + ((speedMs > 0) ? speedMs : 8);
            state   = ST_TYPING_WAIT;
        } else {
            beginReading();
        }
        break;
    }

    case ST_TYPING_WAIT:
        if ((int32_t)(millis() - holdEnd) >= 0) {
            kbReleaseAll();
            if (typePos >= lineLen) beginReading();
            else                    state = ST_TYPING;
        }
        break;

    case ST_WAIT:   // #DELAY only — never touches the on-screen message
        if ((int32_t)(millis() - holdEnd) >= 0) {
            beginReading();
        }
        break;

    case ST_WAIT_BUTTON:
        if (input::justPressed(PIN_BTN_A)) {
            beginReading();
        }
        break;

    case ST_KEY:
        if ((int32_t)(millis() - holdEnd) >= 0) {
            kbReleaseAll();
            beginReading();
        }
        break;

    case ST_CLICK:
        if ((int32_t)(millis() - holdEnd) >= 0) {
            if (clickSteps[clickPos].action == 0) mouseBtn |= clickBtn;
            else                                   mouseBtn &= (uint8_t)~clickBtn;
            mouseSend();
            clickPos++;
            if (clickPos >= clickCount) {
                beginReading();
            } else {
                holdEnd = millis() + clickSteps[clickPos].afterMs;
            }
        }
        break;
    }
}

} // namespace script
