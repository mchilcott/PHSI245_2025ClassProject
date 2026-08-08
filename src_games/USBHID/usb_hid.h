#pragma once

#include <stdint.h>
#include <stdbool.h>

// Register-level USB HID device stack for the CH32X035 USBFS peripheral.
// Modeled on lib/phsi245_storage/usb_msd.cpp (the bootloader's proven USB
// device implementation). Presents a single composite HID interface with
// three report IDs: mouse, keyboard and gamepad.

namespace usb_hid {

// HID report IDs (first byte of every report)
enum {
    RID_MOUSE   = 1,   // 5-byte report: buttons, dx, dy, wheel
    RID_KEYBOARD = 2,  // 8-byte report: modifiers, reserved, keys[6]
    RID_GAMEPAD = 3,   // 2-byte report: 6 buttons
};

// Initialize USB and start the composite HID device. Returns immediately;
// USB events are handled in the USB interrupt. Call update() in loop().
void init();

// Service routine (kept for symmetry with usb_msd; the ISR does the work).
void update();

// Queue an input report on EP1 IN. The report ID is prepended automatically;
// `data` holds `len` payload bytes (max 8). Reports are coalesced if the
// endpoint is still busy with the previous report.
void sendReport(uint8_t id, const uint8_t *data, uint8_t len);

// Returns true once a host has enumerated us (bus reset seen).
bool isConnected();

// Number of times the USBFS interrupt fired since init() (diagnostic).
uint32_t isrCount();

} // namespace usb_hid
