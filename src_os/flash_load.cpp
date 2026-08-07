#include "flash_load.h"

#include <string.h>

#include <Arduino.h>
#include <ch32x035_flash.h>

#include "storage.h"

// --- CRC-32 (IEEE 802.3, bitwise — matches zlib.crc32 in prog_tools/mkapp.py) ---

static uint32_t crc32Update(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    return crc;
}

static uint32_t crc32Finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t crc32Flash(uint32_t addr, uint32_t len)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        crc = crc32Update(crc, p[i]);
    return crc32Finish(crc);
}

// --- Header ---

AppHdrResult appHeaderFromFile(const char *path, AppHeader *hdr)
{
    fat::Result r = fat::open(path);
    if (r != fat::OK)
        return APP_HDR_OPEN_FAIL;

    uint8_t buf[APP_HEADER_SIZE];
    uint16_t br = 0;
    r = fat::read(buf, APP_HEADER_SIZE, &br);
    fat::close();

    if (r != fat::OK || br != APP_HEADER_SIZE)
        return APP_HDR_READ_FAIL;

    hdr->magic    = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    hdr->version  = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    hdr->flags    = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    hdr->loadAddr = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    hdr->codeSize = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) | ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
    hdr->crc      = (uint32_t)buf[16] | ((uint32_t)buf[17] << 8) | ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24);

    if (hdr->magic != APP_MAGIC)
        return APP_HDR_BAD_MAGIC;
    if (hdr->version != 1)
        return APP_HDR_BAD_VERSION;
    if (hdr->loadAddr != APP_BASE)
        return APP_HDR_BAD_LOADADDR;
    if (hdr->codeSize == 0 || hdr->codeSize > APP_MAX_SIZE)
        return APP_HDR_BAD_SIZE;
    return APP_HDR_OK;
}

// --- Pass 1: verify the SD image against its header CRC ---

AppVerifyResult appVerifyFile(const char *path, const AppHeader &hdr, void (*progressCb)(uint8_t))
{
    fat::Result r = fat::open(path);
    if (r != fat::OK)
        return APP_VERIFY_READ_FAIL;

    uint8_t skip[APP_HEADER_SIZE];
    uint16_t br = 0;
    r = fat::read(skip, APP_HEADER_SIZE, &br);
    if (r != fat::OK || br != APP_HEADER_SIZE) {
        fat::close();
        return APP_VERIFY_READ_FAIL;
    }

    uint32_t crc    = 0xFFFFFFFFu;
    uint32_t left   = hdr.codeSize;
    uint32_t done   = 0;
    uint8_t  buf[64];

    while (left > 0) {
        uint16_t want = left > sizeof(buf) ? (uint16_t)sizeof(buf) : (uint16_t)left;
        br = 0;
        r = fat::read(buf, want, &br);
        if (r != fat::OK || br != want) {
            fat::close();
            return APP_VERIFY_READ_FAIL;
        }
        for (uint16_t i = 0; i < br; i++)
            crc = crc32Update(crc, buf[i]);
        left -= br;
        done += br;

        if (progressCb && done < hdr.codeSize)
            progressCb((uint8_t)((done * 100u) / hdr.codeSize));
    }

    fat::close();
    return crc32Finish(crc) == hdr.crc ? APP_VERIFY_OK : APP_VERIFY_CRC_FAIL;
}

// --- Pass 2: program flash ---

// Erase + program one 256-byte page, then read it back and compare.
// `addr` is an execution alias; the flash controller only accepts physical
// (0x08000000-based) addresses, so translate before every write operation.
// The read-back comparison is a plain read and works through the alias.
//
// Order matters: the erase must happen BEFORE the buffer is loaded — it also
// clears the page buffer, so loading first and then erasing programs an empty
// (all-0xFF) page and the read-back fails. This is the erase -> BufReset ->
// BufLoad x64 -> ProgramPage sequence used by WCH's own CH32X035 IAP example
// (EVT/EXAM/IAP/USB_UART/CH32X035_APP/User/iap.c).
static bool flashProgramPage(uint32_t addr, const uint8_t *page)
{
    uint32_t phy = FLASH_REGION_BASE + addr;

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(phy);
    FLASH_BufReset();

    for (uint8_t i = 0; i < 64; i++) {
        uint32_t word;
        memcpy(&word, page + i * 4, 4);
        FLASH_BufLoad(phy + i * 4, word);
    }

    FLASH_ProgramPage_Fast(phy);
    FLASH_Lock_Fast();

    for (uint16_t i = 0; i < 256; i++) {
        if (*(const volatile uint8_t *)(addr + i) != page[i])
            return false;
    }
    return true;
}

static bool flashWriteDescriptor(uint32_t codeSize, uint32_t crc)
{
    uint8_t page[256];
    memset(page, 0xFF, sizeof(page));

    uint32_t words[4];
    words[0] = APP_MAGIC;
    words[1] = APP_BASE;
    words[2] = codeSize;
    words[3] = crc;
    memcpy(page, words, sizeof(words));

    return flashProgramPage(DESC_ADDR, page);
}

AppFlashResult appFlashFromFile(const char *path, const AppHeader &hdr,
                                void (*progressCb)(uint8_t), uint32_t *failAddr)
{
    if (failAddr) *failAddr = 0;

    fat::Result r = fat::open(path);
    if (r != fat::OK)
        return APP_FLASH_SD_READ;

    uint8_t skip[APP_HEADER_SIZE];
    uint16_t br = 0;
    r = fat::read(skip, APP_HEADER_SIZE, &br);
    if (r != fat::OK || br != APP_HEADER_SIZE) {
        fat::close();
        return APP_FLASH_SD_READ;
    }

    uint8_t page[256];
    uint32_t addr = APP_BASE;
    uint32_t left = hdr.codeSize;
    uint32_t done = 0;

    while (left > 0) {
        uint16_t n = left > 256 ? 256 : (uint16_t)left;
        br = 0;
        r = fat::read(page, n, &br);  // IRQs stay on: SD timeouts rely on millis()
        if (r != fat::OK || br != n) {
            fat::close();
            if (failAddr) *failAddr = addr;
            return APP_FLASH_SD_READ;
        }
        if (n < 256)
            memset(page + n, 0xFF, 256 - n);

        __disable_irq();
        bool ok = flashProgramPage(addr, page);
        __enable_irq();
        if (!ok) {
            fat::close();
            if (failAddr) *failAddr = addr;
            return APP_FLASH_PAGE;
        }

        left -= n;
        done += n;
        addr += 256;

        if (progressCb && done < hdr.codeSize)
            progressCb((uint8_t)((done * 100u) / hdr.codeSize));
    }

    // IRQs stay off on success: the caller jumps straight into the app.
    __disable_irq();
    bool ok = flashWriteDescriptor(hdr.codeSize, hdr.crc);
    if (!ok)
        __enable_irq();

    fat::close();
    if (!ok) {
        if (failAddr) *failAddr = DESC_ADDR;
        return APP_FLASH_DESC;
    }
    return APP_FLASH_OK;
}

// --- Boot-time validation & jump ---

bool appValidInstalled()
{
    const volatile uint32_t *desc = (const volatile uint32_t *)DESC_ADDR;
    if (desc[0] != APP_MAGIC || desc[1] != APP_BASE)
        return false;

    uint32_t size = desc[2];
    if (size == 0 || size > APP_MAX_SIZE)
        return false;

    return crc32Flash(APP_BASE, size) == desc[3];
}

void appJump()
{
    __disable_irq();

    // Stop the 1 ms SysTick and clear its pending flag so no interrupt fires
    // into the app during handle_reset, before the app's own mtvec is set.
    // (handle_reset enables MIE before it writes mtvec; a pending SysTick
    // would dispatch through the bootloader's vector table in that window.)
    SysTick->CTLR = 0;
    SysTick->SR = 0;

    typedef void (*Entry)(void);
    Entry entry = (Entry)(uintptr_t)APP_BASE;
    entry();
    while (1) {
    }
}
