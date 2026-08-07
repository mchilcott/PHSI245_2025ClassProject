#pragma once

#include <stdint.h>
#include <stdbool.h>

// --- Block Device Interface ---

struct BlockDevice {
    bool     (*init)();
    bool     (*readBlock)(uint32_t block, uint8_t *buf);
    bool     (*writeBlock)(uint32_t block, const uint8_t *buf);
    uint32_t (*blockCount)();
};

// --- SD Card (SPI mode) ---

namespace sd {

extern const BlockDevice DEVICE;

bool init();
bool readBlock(uint32_t block, uint8_t *buf);
bool writeBlock(uint32_t block, const uint8_t *buf);
uint32_t blockCount();

// Raw byte read (bypasses block buffer, for internal use)
uint8_t spiRecv();

// Diagnostic timing (temporary): readBlock() invocation count, total µs spent
// inside it, the longest single call, and a one-shot benchmark of 512 dummy
// SPI bytes (the byte-loop floor), since init().
uint32_t readCalls();
uint32_t readUs();
uint32_t maxReadUs();
uint32_t benchUs();

// Multi-block read (CMD18): beginReadMulti() streams from `block` with the
// card left selected; readMultiBlock() pulls each 512-byte sector; endReadMulti()
// stops the stream and deselects. beginReadMulti returns R1 (0 = accepted).
uint8_t beginReadMulti(uint32_t block);
bool   readMultiBlock(uint8_t *buf);
void   endReadMulti();

// Debug (temporary): send CMD17 for `block`, report the raw R1 response and
// whether a data token (0xFE) arrives within 200 ms. r1 == 0 and token == 0xFE
// means the read fully succeeded.
void probeReadCmd(uint32_t block, uint8_t *r1, uint8_t *token);

} // namespace sd

// --- Minimal FAT Filesystem ---

namespace fat {

// Result codes
enum Result {
    OK = 0,
    DISK_ERR,
    NOT_READY,
    NO_FILE,
    NOT_OPENED,
    NOT_ENABLED,
    NO_FILESYSTEM,
};

Result mount(const BlockDevice *dev);
Result open(const char *path);
Result read(void *buf, uint16_t btr, uint16_t *br);
Result close();

// Directory listing: iterates entries in the current directory.
// Call openDir() first, then readDir() repeatedly until it returns false.
// Each call fills `name` (max 13 bytes: 8.3 + null) and `isDir`, `fileSize`.
Result openDir(const char *path);
bool   readDir(char *name, bool *isDir, uint32_t *fileSize);

// Write support: create/overwrite a file and write data
Result create(const char *path);
Result write(const void *buf, uint16_t btw, uint16_t *bw);

// Format the SD card with a fresh FAT32 filesystem.
// All existing data is lost. Returns OK on success.
Result format();

} // namespace fat

// --- Temporary debug diagnostics (remove before release) ---

struct DiagInfo {
    uint32_t partStart;        // LBA of the FAT partition (0 = BPB at sector 0)
    uint32_t firstFATSector;   // absolute LBA of first FAT sector
    uint32_t firstDataSector;  // absolute LBA of first data sector
    uint32_t rootDirCluster;   // FAT32 root dir cluster
    uint32_t sectorsPerCluster;
    uint32_t totalBlocks;      // card block count from CSD
    uint8_t  fatType;          // 12 / 16 / 32
    uint8_t  probeMask;        // bit i set = probe[i] read OK
    uint8_t  rootR1;           // CMD17 R1 at the root dir sector (0 = accepted)
    uint8_t  rootToken;        // 0xFE = data token seen, 0xFF = timeout
};

namespace fat {

DiagInfo getDiag();

} // namespace fat

// EEPROM save/load API (high scores, game state)
#include "eeprom_save.h"

// SD card detection — call once during boot, returns true if a card is present.
// Mounts the card on first call and returns the result.
namespace storage {
bool sdAvailable();
}
