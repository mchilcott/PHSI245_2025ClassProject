// Force linker retention of all OS libraries.
// Even if main.cpp stops using a library directly, this file ensures
// the linker does not strip the library code. Add each new OS library here.
#include "os_libs.h"

void os_libs_init()
{
    // Init calls ensure each library's object files are pulled into the link.
    // These are normal init calls — they run once at boot and have no
    // side effects beyond what setup() already does elsewhere.
    gfx::init();
    input::init();
    led::init();
    ostime::init();
    beep::init();
    rng::init();
    pong::init();

    // Check SD availability and retain storage linker symbols
    volatile bool sdCheck = storage::sdAvailable();
    (void)sdCheck;
    storage::initSave();
    usb_msd::linkAnchor();  // USB MSD is started on demand from the main menu
    // Load stored touch calibration if present. Calibration is only run
    // interactively when the user picks "Calibrate Touch" from the menu, so
    // this never prompts at boot (and the values survive reflashes).
    touchCalibrate();
}
