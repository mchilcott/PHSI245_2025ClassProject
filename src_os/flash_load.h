#pragma once

#include <stdint.h>
#include <stdbool.h>

// Flash partition (must match linker/app_flash.ld and prog_tools/mkapp.py).
// Addresses here are the *execution* aliases at 0x0000_0000 (what the CPU runs
// and reads). The physical flash region is at 0x0800_0000; the alias cannot be
// erased or programmed, so every flash-controller access must add
// FLASH_REGION_BASE. See flashProgramPage() in flash_load.cpp.
//
//   0x0000_0000 - 0x0000_7FFF   bootloader (32 KB)
//   0x0000_8000 - 0x0000_F6FF   app code    (APP_BASE, up to APP_MAX_SIZE)
//   0x0000_F700 - 0x0000_F7FF   app descriptor page (magic/loadaddr/size/crc)
//   (device flash ends at 0x0000_F7FF = 62 KB; no tail beyond that)

// Physical flash region base (not the alias). Matches FLASH_BASE in the HAL.
#define FLASH_REGION_BASE 0x08000000u

#define APP_BASE         0x00008000u
#define APP_MAX_SIZE     0x7700u   // 30464 bytes (app region minus descriptor page)
#define DESC_ADDR        0x0000F700u
#define APP_MAGIC        0x35324850u  // "PH25" as little-endian u32

// SD image header size (see prog_tools/mkapp.py)
#define APP_HEADER_SIZE  20

struct AppHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t loadAddr;
    uint32_t codeSize;
    uint32_t crc;
};

// Result of parsing + validating the 20-byte header at the start of a FAT file.
enum AppHdrResult {
    APP_HDR_OK = 0,
    APP_HDR_OPEN_FAIL,    // file open failed
    APP_HDR_READ_FAIL,    // file shorter than the 20-byte header
    APP_HDR_BAD_MAGIC,    // missing "PH25" magic — not a PHSI245 image
    APP_HDR_BAD_VERSION,  // header version != 1
    APP_HDR_BAD_LOADADDR, // loadAddr != APP_BASE
    APP_HDR_BAD_SIZE,     // codeSize == 0 or > APP_MAX_SIZE
};

// Parse + validate the 20-byte header at the start of the FAT file `path`.
// Returns APP_HDR_OK and fills `hdr` on success, or a specific error code so
// the caller can report why the file was rejected.
AppHdrResult appHeaderFromFile(const char *path, AppHeader *hdr);

// Pass 1: stream the file, compute CRC-32 over the code, compare with hdr->crc.
// Calls progressCb(pct 0..100) as it goes. Returns APP_VERIFY_OK if the image
// is sound; a read error and a CRC mismatch are reported separately.
enum AppVerifyResult {
    APP_VERIFY_OK = 0,
    APP_VERIFY_READ_FAIL, // SD read error mid-stream
    APP_VERIFY_CRC_FAIL,  // computed CRC-32 != hdr->crc
};

AppVerifyResult appVerifyFile(const char *path, const AppHeader &hdr, void (*progressCb)(uint8_t));

// Pass 2: erase + program the app region from the file, write the descriptor
// page. Interrupts are disabled for the flash phase, so progressCb (which
// draws to the display) must not rely on interrupts. The app region may be
// partially written on failure; the descriptor is only written on success, so
// boot-time validation catches it. Returns a code so the caller can tell an
// SD read failure apart from a flash program failure; *failAddr receives the
// address being written when the failure happened (0 on APP_FLASH_OK).
enum AppFlashResult {
    APP_FLASH_OK = 0,
    APP_FLASH_SD_READ,   // SD read failed mid-stream (bad or short read)
    APP_FLASH_PAGE,      // code page program/read-back failed
    APP_FLASH_DESC,      // descriptor page program/read-back failed
};

AppFlashResult appFlashFromFile(const char *path, const AppHeader &hdr,
                                void (*progressCb)(uint8_t), uint32_t *failAddr);

// Jump to the flashed app at APP_BASE. Never returns.
void appJump();

// True if a valid app is installed: descriptor magic + loadaddr + size in
// range + CRC-32 over the flashed code matches the descriptor.
bool appValidInstalled();
