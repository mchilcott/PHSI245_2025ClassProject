#pragma once

#include <stdint.h>
#include <stdbool.h>

// Minimal text-script interpreter for the USBHID app. Reads a text file from
// the SD card and translates it into HID reports (keyboard/mouse) plus
// device-side actions (delay, beep, LED, on-screen message, pause).
//
// Line grammar (see HID_SCRIPT.md):
//   ## comment (whole line)
//   #CMD args    command (uppercase, case-insensitive)
//   <any other>  literal text typed character by character, NO trailing Enter
//                escapes: \n = Enter, \t = Tab, \\ = backslash, \# = '#'
// Commands: DELAY, BEEP, LED, MSG, PAUSE, SPEED, STOP, KEY, MOVE, CLICK,
//           PRESS, RELEASE, SCROLL

namespace script {

// Reset the engine to idle and drop any held keys/buttons (abort-safe).
void init();

// Begin running the script in `path` (an SD file, e.g. "/SCRIPT.TXT").
// Returns false if the file cannot be opened (caller shows the error).
bool start(const char *path);

// Stop the script now, releasing every key/button and closing the file.
void abort();

bool isRunning();
bool isPaused();      // waiting for A on #PAUSE

// Current #MSG text, or empty string when none is showing.
const char *message();

// Advance the machine. Call from loop() — one step per iteration.
void tick();

} // namespace script
