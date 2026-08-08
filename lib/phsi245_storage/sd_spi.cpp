#include "storage.h"
#include <HAL.h>
#include <Arduino.h>

/*
 * SD/MMC card driver in SPI mode.
 *
 * This is a faithful port of ChaN's reference driver from the FatFs sample
 * projects: ffsample/stm32/mmc_stm32f1_spi.c ("MMCv3/SDv1/SDv2 (SPI mode)
 * control module", (C)ChaN 2018, released under the FatFs open-source terms).
 * The card init handshake, command framing, R1 decoding (byte-aligned, first
 * byte with bit7 clear, max 10 bytes), and the select/deselect + wait_ready
 * gating in send_cmd() are kept exactly as upstream. Keep the protocol intact
 * if you modify this file.
 *
 * Replacing the previous hand-written init driver: the old code held CS low
 * across ACMD41/CMD58 and decoded R1 by scanning the clocked bit stream for
 * the first 0 after the idle 0xFF run (up to 128 bytes). ChaN's proven
 * approach instead reselects the card and waits for it to be ready before
 * every command, and reads R1 byte-aligned.
 *
 * NOTE on bit-bang migration: if hardware SPI ever again proves unreliable on
 * this board (e.g. bit-shifted R1 bytes such as 0xE0 0x3F appearing at ACMD41),
 * migrate the byte-I/O layer below (xchg_spi) to ChaN's bit-bang variant
 * ffsample/generic/sdmm.c ("Foolproof MMCv3/SDv1/SDv2", (C)ChaN 2019). That
 * driver uses the exact same protocol and command set but drives SCK/MOSI and
 * reads MISO from plain GPIO pins, so no SPI-peripheral edge/timing bugs can
 * corrupt responses. Everything else in this file stays unchanged.
 */

// SD card commands (SPI mode)
#define CMD0    0          // GO_IDLE_STATE
#define CMD1    1          // SEND_OP_COND (MMC)
#define CMD8    8          // SEND_IF_COND
#define CMD9    9          // SEND_CSD
#define CMD12   12         // STOP_TRANSMISSION
#define CMD16   16         // SET_BLOCKLEN
#define CMD17   17         // READ_SINGLE_BLOCK
#define CMD18   18         // READ_MULTIPLE_BLOCK
#define CMD24   24         // WRITE_BLOCK
#define CMD55   55         // APP_CMD
#define CMD58   58         // READ_OCR
#define ACMD41  (0x80+41)  // SEND_OP_COND (SDC, via CMD55)

// Card type flags
#define CT_MMC   0x01
#define CT_SDC1  0x02
#define CT_SDC2  0x04
#define CT_BLOCK 0x08

// Data-transfer clock after init (APB2 = 48 MHz / prescaler). Prescaler_4 =
// ~12 MHz. A 6 MHz data clock was tested against the beep wedge and made no
// difference, so we keep the faster 12 MHz. Options: _8=~6MHz, _16=~3MHz.
static const uint16_t SD_DATA_CLOCK = SPI_BaudRatePrescaler_4;

namespace sd {

static uint8_t CardType = 0;  // b0:MMC, b1:SDv1, b2:SDv2, b3:block addressing
static uint32_t blocks = 0;   // total block count (read from CSD at init)

// --- Byte I/O layer (platform dependent) ---
// To migrate to ChaN's bit-bang driver (sdmm.c), replace xchg_spi with the
// GPIO xmit_mmc/rcvr_mmc routines; the protocol below stays identical.
static uint8_t xchg_spi(uint8_t dat)
{
    return spi_transfer(dat);
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static int wait_ready(uint32_t wt)  // 1:Ready, 0:Timeout (wt in ms)
{
    uint8_t d;
    uint32_t t0 = millis();
    do {
        d = xchg_spi(0xFF);
    } while (d != 0xFF && (uint32_t)(millis() - t0) < wt);

    return (d == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/
static void deselect(void)
{
    spi_cs_high();      // Set CS# high
    xchg_spi(0xFF);     // Dummy clock (force DO hi-z for multiple slave SPI)
}

/*-----------------------------------------------------------------------*/
/* Select the card and wait for ready                                    */
/*-----------------------------------------------------------------------*/
static int select(void)  // 1:OK, 0:Timeout
{
    spi_cs_low();       // Set CS# low
    xchg_spi(0xFF);     // Dummy clock (force DO enabled)
    if (wait_ready(500)) return 1;

    deselect();
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the card                                   */
/*-----------------------------------------------------------------------*/
static int rcvr_datablock(uint8_t *buff, uint32_t btr)  // 1:OK, 0:Error
{
    uint8_t token;
    uint32_t t0 = millis();

    do {  // Wait for DataStart token in timeout of 200ms
        token = xchg_spi(0xFF);
    } while (token == 0xFF && (uint32_t)(millis() - t0) < 200);
    if (token != 0xFE) return 0;  // Fail if not a valid data token

    for (uint32_t i = 0; i < btr; i++)
        buff[i] = xchg_spi(0xFF);  // Store trailing data to the buffer
    xchg_spi(0xFF); xchg_spi(0xFF);  // Discard CRC

    return 1;
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the card                                        */
/*-----------------------------------------------------------------------*/
static int xmit_datablock(const uint8_t *buff, uint8_t token)  // 1:OK, 0:Failed
{
    uint8_t resp;

    if (!wait_ready(500)) return 0;  // Wait for card ready

    xchg_spi(token);  // Send token
    if (token != 0xFD) {  // Send data if token is other than StopTran
        for (int i = 0; i < 512; i++)
            xchg_spi(buff[i]);  // Send the 512 byte data block
        xchg_spi(0xFF); xchg_spi(0xFF);  // Dummy CRC

        resp = xchg_spi(0xFF);  // Receive data response
        if ((resp & 0x1F) != 0x05) return 0;  // Fail if the data was not accepted
    }

    return 1;
}

/*-----------------------------------------------------------------------*/
/* Send a command packet to the card                                     */
/*-----------------------------------------------------------------------*/
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)  // R1 resp (bit7==1: failed)
{
    uint8_t n, res;

    if (cmd & 0x80) {  // ACMD<n> is the sequence CMD55 + CMD<n>
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    // Select the card and wait for ready except to stop multiple block read
    if (cmd != CMD12) {
        deselect();
        if (!select()) return 0xFF;
    }

    // Send command packet
    xchg_spi(0x40 | cmd);            // Start + command index
    xchg_spi((uint8_t)(arg >> 24));  // Argument[31..24]
    xchg_spi((uint8_t)(arg >> 16));  // Argument[23..16]
    xchg_spi((uint8_t)(arg >> 8));   // Argument[15..8]
    xchg_spi((uint8_t)arg);          // Argument[7..0]
    n = 0x01;                        // Dummy CRC + Stop
    if (cmd == CMD0) n = 0x95;       // Valid CRC for CMD0(0)
    if (cmd == CMD8) n = 0x87;       // Valid CRC for CMD8(0x1AA)
    xchg_spi(n);

    // Receive command response
    if (cmd == CMD12) xchg_spi(0xFF);  // Discard the following byte on CMD12
    n = 10;                            // Wait for a valid response (10 bytes max)
    do {
        res = xchg_spi(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

uint8_t spiRecv()
{
    return xchg_spi(0xFF);
}

/*-----------------------------------------------------------------------*/
/* Debug probe (temporary): raw CMD17 read at a block                     */
/*-----------------------------------------------------------------------*/
void probeReadCmd(uint32_t block, uint8_t *r1, uint8_t *token)
{
    *r1 = 0xFF;
    *token = 0;
    if (!CardType) return;

    uint32_t sect = block;
    if (!(CardType & CT_BLOCK)) sect <<= 9;  // Byte addressing for SDSC

    *r1 = send_cmd(CMD17, sect);
    if (*r1 == 0) {
        uint32_t t0 = millis();
        do {
            *token = xchg_spi(0xFF);
        } while (*token == 0xFF && (uint32_t)(millis() - t0) < 200);
    }
    deselect();
}

/*-----------------------------------------------------------------------*/
/* Initialize the card (shared core)                                     */
/*-----------------------------------------------------------------------*/
// Runs ChaN's card-identification sequence and re-arms the card exactly like
// init(): CardType/blocks set, SPI clock raised to 12 MHz on success.
static bool doInit()
{
    uint8_t ty = 0, cmd, ocr[4];
    uint32_t t0 = millis();

    uint8_t c0r1 = send_cmd(CMD0, 0);         // Enter Idle state
    if (c0r1 == 1) {
        uint8_t c8r1 = send_cmd(CMD8, 0x1AA); // SDv2?
        if (c8r1 == 1) {
            for (int n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);  // R7 response tail
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {  // Voltage range 2.7-3.6V ok
                while ((uint32_t)(millis() - t0) < 1000 && send_cmd(ACMD41, 1UL << 30))  // ACMD41(HCS)
                    ;
                if ((uint32_t)(millis() - t0) < 1000 && send_cmd(CMD58, 0) == 0) {  // Check CCS in OCR
                    for (int n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
                    ty = (ocr[0] & 0x40) ? CT_SDC2 | CT_BLOCK : CT_SDC2;
                }
            }
        } else {  // SDv1 or MMCv3
            if (send_cmd(ACMD41, 0) <= 1) {
                ty = CT_SDC1; cmd = ACMD41;  // SDv1
            } else {
                ty = CT_MMC;  cmd = CMD1;    // MMCv3
            }
            while ((uint32_t)(millis() - t0) < 1000 && send_cmd(cmd, 0)) ;  // Wait for end of init
            if ((uint32_t)(millis() - t0) >= 1000 || send_cmd(CMD16, 512) != 0)  // Set block length
                ty = 0;
        }
    }
    CardType = ty;
    deselect();

    // Read CSD to obtain the total block count (ChaN's GET_SECTOR_COUNT)
    uint32_t blk = 0;
    if (ty) {
        uint8_t csd[16];
        if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
            uint32_t cs;
            if ((csd[0] >> 6) == 1) {  // CSD ver 2.00 (SDHC/SDXC)
                cs = csd[9] + ((uint32_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
                blk = cs << 10;
            } else {                   // CSD ver 1.XX or MMC
                uint8_t n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                cs = (csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3) << 10) + 1;
                blk = cs << (n - 9);
            }
        }
        deselect();
    }
    blocks = blk;
    if (blk > 0)
        spi_set_prescaler(SD_DATA_CLOCK);  // data clock after init
    return blk > 0;
}

bool init()
{
    spi_init();

    // FCLK_SLOW() equivalent is already in effect: spi_init() runs SPI1 at
    // prescaler 256 (~187kHz, below the 400kHz SD init limit). Once the card
    // is identified below we raise the clock (ChaN's FCLK_FAST()) to
    // SD_DATA_CLOCK above (~12 MHz). A 6 MHz data clock showed no difference
    // against the beep-driven wedge, so the faster clock is kept.

    for (int n = 10; n; n--) xchg_spi(0xFF);  // 80 dummy clocks, card gets ready

    return doInit();
}

/*-----------------------------------------------------------------------*/
/* Read one 512-byte block                                               */
/*-----------------------------------------------------------------------*/
bool readBlock(uint32_t block, uint8_t *buf)
{
    if (!CardType) return false;

    uint32_t sect = block;
    if (!(CardType & CT_BLOCK)) sect <<= 9;  // Byte addressing for SDSC

    bool ok = (send_cmd(CMD17, sect) == 0) && rcvr_datablock(buf, 512);
    deselect();

    return ok;
}

/*-----------------------------------------------------------------------*/
/* Multi-block read (CMD18): stream several sectors with one command.     */
/* The card stays selected (CS low) from beginReadMulti() until           */
/* endReadMulti(), so the per-sector CMD17/select/token overhead is       */
/* amortized away. The caller paces readMultiBlock() against its own      */
/* consumer; the card merely pauses while we stop clocking.               */
/*-----------------------------------------------------------------------*/

// Start the stream. Returns R1 (0 = accepted, card left selected).
uint8_t beginReadMulti(uint32_t block)
{
    if (!CardType) return 0xFF;

    uint32_t sect = block;
    if (!(CardType & CT_BLOCK)) sect <<= 9;  // Byte addressing for SDSC

    return send_cmd(CMD18, sect);  // send_cmd() leaves the card selected
}

// Read the next 512-byte block from the stream (no CS toggling).
bool readMultiBlock(uint8_t *buf)
{
    return rcvr_datablock(buf, 512);
}

// Stop the stream and deselect the card. The next select() waits out any
// card busy from CMD12, so callers need no extra delay.
void endReadMulti()
{
    send_cmd(CMD12, 0);  // send_cmd() special-cases CMD12 (no select/deselect)
    deselect();
}

/*-----------------------------------------------------------------------*/
/* Write one 512-byte block                                              */
/*-----------------------------------------------------------------------*/
bool writeBlock(uint32_t block, const uint8_t *buf)
{
    if (!CardType) return false;

    uint32_t sect = block;
    if (!(CardType & CT_BLOCK)) sect <<= 9;  // Byte addressing for SDSC

    bool ok = (send_cmd(CMD24, sect) == 0) && xmit_datablock(buf, 0xFE);
    deselect();

    return ok;
}

uint32_t blockCount()
{
    return blocks;
}

const BlockDevice DEVICE = {
    init,
    readBlock,
    writeBlock,
    blockCount,
};

} // namespace sd
