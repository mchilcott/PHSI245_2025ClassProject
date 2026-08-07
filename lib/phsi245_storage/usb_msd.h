#pragma once

#include <stdint.h>

namespace usb_msd {

// Initialize USB and start MSD class. Returns immediately; USB events
// are handled in the USB interrupt. Call update() in loop().
void init();

// Stop MSD and disconnect from the host (drops D+ pull-up).
void deinit();

// No-op used by os_libs_init() to keep this library linked.
void linkAnchor();

// Must be called regularly from loop() to process SCSI commands.
// Returns true if a read or write was processed this call.
bool update();

// Returns true if USB is connected to a host (VBUS detected).
bool isConnected();

// Returns true while a BOT transfer is active (data in/out pending or a CBW
// queued). The main loop can spin instead of sleeping to keep the SD pipe full.
bool isBusy();

// Number of times the USBFS interrupt fired since init() (diagnostic).
uint32_t isrCount();

// --- Temporary BOT diagnostics ---
uint8_t  botStateDebug();   // 0=idle 1=data_out 2=data_in 3=csw
uint16_t txRemainDebug();   // bytes still owed to the host in data-in
uint8_t  lastOpcode();      // last SCSI opcode processed
uint32_t lastLBA();         // last LBA read/written
uint16_t sdFailCount();     // SD read failures since init()
uint16_t writeFailCount();  // SD write failures since init()
uint32_t busResetCount();   // USB bus resets seen since init()
uint32_t cbwCount();        // CBWs received & processed since init()
uint32_t togFailCount();    // EP2 OUT toggle-mismatch packets since init()
uint16_t togByState(uint8_t state); // mismatches broken down by BOT state (0..3)
uint32_t inPktCount();      // EP1 IN packets sent (data+CSW) since init()
uint32_t busyMs();          // total ms spent inside BOT transfers since init()
uint32_t readCmdCount();    // READ_10 CBWs processed since init()
uint32_t maxInGap();        // max gap between consecutive EP1 data packets (us)
uint32_t bigGapCount();     // EP1 data-packet gaps > 200us (host polling pauses)

} // namespace usb_msd
