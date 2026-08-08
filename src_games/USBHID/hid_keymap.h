// ASCII -> HID keyboard usage translation table and named-key lookup for
// the USBHID script language's #KEY command.
//
// Each ASCII entry is {modifierMask, usageCode}. The keyboard report is
// bytes: [0]=modifiers, [1]=reserved, [2..7]=key codes.

#pragma once

#include <stdint.h>
#include <string.h>

namespace hidkey {

// Modifier bits (report byte 0)
enum {
    MOD_LCTRL  = 0x01,
    MOD_LSHIFT = 0x02,
    MOD_LALT   = 0x04,
    MOD_LGUI   = 0x08,
    MOD_RCTRL  = 0x10,
    MOD_RSHIFT = 0x20,
    MOD_RALT   = 0x40,
    MOD_RGUI   = 0x80,
};

struct AsciiKey {
    uint8_t mod;
    uint8_t usage;
};

// Index 0..127, direct lookup by ASCII code (US keyboard layout).
// HID keyboard usage codes: A-Z=0x04-0x1D, digits=0x1E-0x27,
// return=0x28, esc=0x29, backspace=0x2A, tab=0x2B, space=0x2C,
// minus/equal/bracket/backslash/grave/semicolon/apostrophe/
// comma/period/slash=0x2D-0x38.
static const AsciiKey asciiTable[128] = {
    // 0x00-0x1F control block
    {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, // 00-07
    {0, 0x2A}, {0, 0x2B}, {0, 0x28}, {0, 0x00}, {0, 0x00}, {0, 0x28}, {0, 0x00}, {0, 0x00}, // 08-0F  BS,TAB,CR
    {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, // 10-17
    {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, {0, 0x00}, // 18-1F
    // 0x20 space, then printable
    {0, 0x2C},                // ' '
    {MOD_LSHIFT, 0x1E},       // '!'  shift+1
    {MOD_LSHIFT, 0x34},       // '"'  shift+'
    {MOD_LSHIFT, 0x20},       // '#'  shift+3
    {MOD_LSHIFT, 0x21},       // '$'  shift+4
    {MOD_LSHIFT, 0x22},       // '%'  shift+5
    {MOD_LSHIFT, 0x24},       // '&'  shift+7
    {MOD_LSHIFT, 0x34},       // '\'' shift+'
    {MOD_LSHIFT, 0x26},       // '('  shift+9
    {MOD_LSHIFT, 0x27},       // ')'  shift+0
    {MOD_LSHIFT, 0x25},       // '*'  shift+8
    {MOD_LSHIFT, 0x2E},       // '+'  shift+=
    {0, 0x36},                // ','
    {0, 0x2D},                // '-'
    {0, 0x37},                // '.'
    {0, 0x38},                // '/'
    {0, 0x27},                // '0'
    {0, 0x1E},                // '1'
    {0, 0x1F},                // '2'
    {0, 0x20},                // '3'
    {0, 0x21},                // '4'
    {0, 0x22},                // '5'
    {0, 0x23},                // '6'
    {0, 0x24},                // '7'
    {0, 0x25},                // '8'
    {0, 0x26},                // '9'
    {MOD_LSHIFT, 0x33},       // ':'  shift+;
    {0, 0x33},                // ';'
    {MOD_LSHIFT, 0x36},       // '<'  shift+,
    {0, 0x2E},                // '='
    {MOD_LSHIFT, 0x37},       // '>'  shift+.
    {MOD_LSHIFT, 0x38},       // '?'  shift+/
    {MOD_LSHIFT, 0x1F},       // '@'  shift+2
    {MOD_LSHIFT, 0x04}, {MOD_LSHIFT, 0x05}, {MOD_LSHIFT, 0x06}, {MOD_LSHIFT, 0x07}, // A-D
    {MOD_LSHIFT, 0x08}, {MOD_LSHIFT, 0x09}, {MOD_LSHIFT, 0x0A}, {MOD_LSHIFT, 0x0B}, // E-H
    {MOD_LSHIFT, 0x0C}, {MOD_LSHIFT, 0x0D}, {MOD_LSHIFT, 0x0E}, {MOD_LSHIFT, 0x0F}, // I-L
    {MOD_LSHIFT, 0x10}, {MOD_LSHIFT, 0x11}, {MOD_LSHIFT, 0x12}, {MOD_LSHIFT, 0x13}, // M-P
    {MOD_LSHIFT, 0x14}, {MOD_LSHIFT, 0x15}, {MOD_LSHIFT, 0x16}, {MOD_LSHIFT, 0x17}, // Q-T
    {MOD_LSHIFT, 0x18}, {MOD_LSHIFT, 0x19}, {MOD_LSHIFT, 0x1A}, {MOD_LSHIFT, 0x1B}, // U-X
    {MOD_LSHIFT, 0x1C}, {MOD_LSHIFT, 0x1D}, // Y-Z
    {0, 0x2F},                // '['
    {0, 0x31},                // '\'
    {0, 0x30},                // ']'
    {MOD_LSHIFT, 0x23},       // '^'  shift+6
    {MOD_LSHIFT, 0x2D},       // '_'  shift+-
    {0, 0x35},                // '`'
    {0, 0x04}, {0, 0x05}, {0, 0x06}, {0, 0x07}, // a-d
    {0, 0x08}, {0, 0x09}, {0, 0x0A}, {0, 0x0B}, // e-h
    {0, 0x0C}, {0, 0x0D}, {0, 0x0E}, {0, 0x0F}, // i-l
    {0, 0x10}, {0, 0x11}, {0, 0x12}, {0, 0x13}, // m-p
    {0, 0x14}, {0, 0x15}, {0, 0x16}, {0, 0x17}, // q-t
    {0, 0x18}, {0, 0x19}, {0, 0x1A}, {0, 0x1B}, // u-x
    {0, 0x1C}, {0, 0x1D}, // y-z
    {MOD_LSHIFT, 0x2F},       // '{'  shift+[
    {MOD_LSHIFT, 0x31},       // '|'  shift+backslash
    {MOD_LSHIFT, 0x30},       // '}'  shift+]
    {MOD_LSHIFT, 0x35},       // '~'  shift+`
};

// Named keys for #KEY: name, modifierMask, usageCode. Order matters: longer
// names first so "LEFT CTRL" matches before "LEFT".
struct NamedKey {
    const char *name;
    uint8_t     mod;
    uint8_t     usage;
};

static const NamedKey namedKeys[] = {
    {"ESC",       0, 0x29},
    {"ENTER",     0, 0x28},
    {"RETURN",    0, 0x28},
    {"TAB",       0, 0x2B},
    {"BACKSPACE", 0, 0x2A},
    {"DELETE",    0, 0x4C},
    {"INSERT",    0, 0x49},
    {"HOME",      0, 0x4A},
    {"END",       0, 0x4D},
    {"PAGEUP",    0, 0x4B},
    {"PAGEDOWN",  0, 0x4E},
    {"UP",        0, 0x52},
    {"DOWN",      0, 0x51},
    {"LEFT",      0, 0x50},
    {"RIGHT",     0, 0x4F},
    {"SPACE",     0, 0x2C},
    {"F1", 0, 0x3A}, {"F2", 0, 0x3B}, {"F3", 0, 0x3C}, {"F4", 0, 0x3D},
    {"F5", 0, 0x3E}, {"F6", 0, 0x3F}, {"F7", 0, 0x40}, {"F8", 0, 0x41},
    {"F9", 0, 0x42}, {"F10", 0, 0x43}, {"F11", 0, 0x44}, {"F12", 0, 0x45},
    {"CTRL",    MOD_LCTRL,  0},
    {"CONTROL", MOD_LCTRL,  0},
    {"SHIFT",   MOD_LSHIFT, 0},
    {"ALT",     MOD_LALT,   0},
    {"GUI",     MOD_LGUI,   0},
    {"WIN",     MOD_LGUI,   0},
};

static const uint8_t namedKeyCount = sizeof(namedKeys) / sizeof(NamedKey);

// Case-insensitive lookup. Returns true and fills mod/usage on success.
static bool lookupName(const char *name, uint8_t &mod, uint8_t &usage)
{
    for (uint8_t i = 0; i < namedKeyCount; i++) {
        const char *a = namedKeys[i].name;
        const char *b = name;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == 0 && *b == 0) {
            mod   = namedKeys[i].mod;
            usage = namedKeys[i].usage;
            return true;
        }
    }
    return false;
}

// Translate a single printable char. Returns false for chars with no key.
static bool lookupAscii(char c, uint8_t &mod, uint8_t &usage)
{
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) return false;
    mod   = asciiTable[idx].mod;
    usage = asciiTable[idx].usage;
    return usage != 0;
}

} // namespace hidkey
