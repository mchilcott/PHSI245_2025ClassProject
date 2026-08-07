// USB Mass Storage Device — exposes SD card to host via USB
// Targets CH32X035 USBFS peripheral (device mode).
// Register bit definitions mirror the WCH ch32x035_usb.h header, which the
// Arduino framework build does not ship.

#include "usb_msd.h"
#include "storage.h"
#include <ch32x035.h>
#include <Arduino.h>
#include <string.h>

// --- USBFS register bits (CH32X035 device mode) ---

#define USBFS_UC_RESET_SIE    0x04
#define USBFS_UC_CLR_ALL      0x02
#define USBFS_UC_DEV_PU_EN    0x20
#define USBFS_UC_INT_BUSY     0x08
#define USBFS_UC_DMA_EN       0x01

#define USBFS_UIE_SUSPEND     0x04
#define USBFS_UIE_TRANSFER    0x02
#define USBFS_UIE_BUS_RST     0x01

#define USBFS_UIF_SUSPEND     0x04
#define USBFS_UIF_TRANSFER    0x02
#define USBFS_UIF_BUS_RST     0x01

#define USBFS_UIS_TOKEN_MASK  0x30
#define USBFS_UIS_TOKEN_OUT   0x00
#define USBFS_UIS_TOKEN_SOF   0x10
#define USBFS_UIS_TOKEN_IN    0x20
#define USBFS_UIS_TOKEN_SETUP 0x30
#define USBFS_UIS_ENDP_MASK   0x0F

#define USBFS_UD_PD_DIS       0x80
#define USBFS_UD_PORT_EN      0x01

#define USBFS_UEP_T_RES_MASK  (3 << 0)
#define USBFS_UEP_T_RES_ACK   (0 << 1)
#define USBFS_UEP_T_RES_NAK   (1 << 1)
#define USBFS_UEP_T_RES_STALL (3 << 0)
#define USBFS_UEP_T_TOG       (1 << 6)
#define USBFS_UEP_T_AUTO_TOG  (1 << 4)

#define USBFS_UEP_R_RES_MASK  (3 << 2)
#define USBFS_UEP_R_RES_ACK   (0 << 3)
#define USBFS_UEP_R_RES_NAK   (1 << 3)
#define USBFS_UEP_R_RES_STALL (3 << 2)
#define USBFS_UEP_R_TOG       (1 << 7)
#define USBFS_UEP_R_AUTO_TOG  (1 << 4)

#define USBFS_UIS_TOG_OK      0x40

#define USBFS_UEP1_TX_EN      0x40
#define USBFS_UEP2_RX_EN      0x08

// AFIO->CTLR USB config bits (PC16=UDM/D-, PC17=UDP/D+)
// UDP_PUE is a 2-bit field [3:2]: 00=disable, 01=35uA, 10=10k, 11=1k5.
// USB device attach needs the 1.5k D+ pull-up (0x0C), not the 35uA
// option (0x04) which leaves D+ effectively floating (no enumeration).
#define AFIO_USB_MASK         0x0F   // UDM_PUE [1:0] | UDP_PUE [3:2]
#define AFIO_USB_IOEN         0x80
#define AFIO_USB_PHY_V33      0x40
#define AFIO_UDP_PUE_1K5      0x0C

// Endpoint assignments
#define EP0     0   // Control
#define EP1_IN  1   // Bulk IN (device -> host)
#define EP2_OUT 2   // Bulk OUT (host -> device)

// Max packet sizes
#define EP0_SIZE    64
#define EP1_SIZE    64
#define EP2_SIZE    64

// --- USB standard request codes ---
#define REQ_GET_DESCRIPTOR      0x06
#define REQ_SET_ADDRESS         0x05
#define REQ_SET_CONFIGURATION   0x09
#define REQ_GET_CONFIGURATION   0x08
#define REQ_GET_STATUS          0x00
#define REQ_SET_FEATURE         0x03
#define REQ_CLEAR_FEATURE       0x01
#define REQ_GET_INTERFACE       0x0A
#define REQ_SET_INTERFACE       0x0B

#define DESC_DEVICE             0x01
#define DESC_CONFIG             0x02
#define DESC_STRING             0x03

// --- BOT (Bulk-Only Transport) ---
#define BOT_CBW_SIGNATURE   0x43425355  // "USBC"
#define BOT_CSW_SIGNATURE   0x53425355  // "USBS"
#define BOT_CBW_LEN         31
#define BOT_CSW_LEN         13
#define BOT_GET_MAX_LUN     0xFE
#define BOT_RESET           0xFF
#define BOT_DIR_IN          0x80

// --- SCSI commands ---
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_INQUIRY            0x12
#define SCSI_MODE_SENSE_6       0x1A
#define SCSI_START_STOP_UNIT    0x1B
#define SCSI_PREVENT_ALLOW      0x1E
#define SCSI_READ_CAPACITY_10   0x25
#define SCSI_READ_10            0x28
#define SCSI_WRITE_10           0x2A
#define SCSI_SYNCHRONIZE_CACHE  0x35

// --- Global state ---
static uint8_t  usbAddrPending = 0;
static uint8_t  usbConfigured  = 0;
static bool     hostPresent    = false;
static bool     usbInitialized = false;
static volatile uint32_t usbIsrFires = 0;

// MSD state
static uint8_t  msdBuf[2][512];      // double-buffered sectors (read path)
static uint32_t msdBlockCount = 0;
static uint16_t msdBlockSize  = 512;

// CBW/CSW
static struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} cbw;

static struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} csw;

// Request sense data
static uint8_t senseKey = 0;
static uint8_t senseASC = 0;

// --- USB descriptors (PROGMEM is empty on RISC-V; plain const) ---

static const PROGMEM uint8_t devDesc[] = {
    0x12,       // bLength
    0x01,       // bDescriptorType (Device)
    0x00, 0x02, // bcdUSB 2.00
    0x00,       // bDeviceClass (use interface)
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    EP0_SIZE,   // bMaxPacketSize0
    0x86, 0x1A, // idVendor (0x1A86 = WCH)
    0x10, 0x57, // idProduct (0x5710)
    0x00, 0x01, // bcdDevice 1.00
    1, 2, 3,    // iManufacturer, iProduct, iSerialNumber
    0x01,       // bNumConfigurations
};

static const PROGMEM uint8_t cfgDesc[] = {
    // Configuration descriptor
    0x09,       // bLength
    0x02,       // bDescriptorType (Configuration)
    0x20, 0x00, // wTotalLength (32 bytes)
    0x01,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0xC0,       // bmAttributes (self-powered)
    0x32,       // bMaxPower (100 mA)

    // Interface descriptor
    0x09,       // bLength
    0x04,       // bDescriptorType (Interface)
    0x00,       // bInterfaceNumber
    0x00,       // bAlternateSetting
    0x02,       // bNumEndpoints (2: Bulk IN + Bulk OUT)
    0x08,       // bInterfaceClass (Mass Storage)
    0x06,       // bInterfaceSubClass (SCSI transparent)
    0x50,       // bInterfaceProtocol (Bulk-Only)
    0x00,       // iInterface

    // Endpoint 1 IN
    0x07,       // bLength
    0x05,       // bDescriptorType (Endpoint)
    0x81,       // bEndpointAddress (EP1 IN)
    0x02,       // bmAttributes (Bulk)
    EP1_SIZE, 0x00, // wMaxPacketSize
    0x00,       // bInterval

    // Endpoint 2 OUT
    0x07,
    0x05,
    0x02,       // bEndpointAddress (EP2 OUT)
    0x02,       // bmAttributes (Bulk)
    EP2_SIZE, 0x00,
    0x00,
};

// String descriptors
static const PROGMEM uint8_t langID[] = {
    0x04, 0x03, 0x09, 0x04  // English (US)
};

static const PROGMEM uint8_t strManufacturer[] = {
    18, 0x03,
    'P',0,'H',0,'S',0,'I',0,'2',0,'4',0,'5',0
};

static const PROGMEM uint8_t strProduct[] = {
    18, 0x03,
    'U',0,'S',0,'B',0,' ',0,'M',0,'S',0,'D',0
};

static const PROGMEM uint8_t strSerial[] = {
    8, 0x03,
    '0',0,'0',0,'1',0
};

static const PROGMEM uint8_t *strDescs[] = {
    langID, strManufacturer, strProduct, strSerial
};

// --- Endpoint buffers (DMA targets, 4-byte aligned) ---

__attribute__((aligned(4))) static uint8_t ep0Buf[EP0_SIZE];  // EP0 RX + TX
__attribute__((aligned(4))) static uint8_t ep1Buf[EP1_SIZE];  // EP1 IN
__attribute__((aligned(4))) static uint8_t ep2Buf[EP2_SIZE];  // EP2 OUT

// --- Control transfer state ---
static const uint8_t *ctrlDesc  = 0;
static uint16_t       ctrlRemain = 0;

// --- BOT state ---
enum BotState {
    BOT_IDLE,
    BOT_DATA_OUT,      // receiving write data from host
    BOT_DATA_IN,       // sending read data to host
    BOT_CSW,           // CSW queued/sent
};

static volatile BotState botState = BOT_IDLE;
static volatile bool     cbwReady = false;
static volatile uint8_t  cbwPos   = 0;
static uint8_t           cbwBuf[32];

// Read (data-in) state — chunks/CSW pumped from ISR, SD refills from loop()
static uint32_t           txLBA;
static volatile uint32_t  txRemain;   // bytes the host still expects
static volatile uint16_t  txBufPos;   // offset within current sector
static volatile bool txNext  = false;
static volatile bool ep1Busy = false;

// Double-buffered read path: the ISR drains txDrainIdx while the loop reads
// the next sector into the other buffer (txPendIdx) and flags it txPendFull,
// hiding the ~350us card read behind the host's IN-token pacing.
static volatile uint8_t  txDrainIdx = 0;     // buffer currently drained by ISR
static volatile uint8_t  txPendIdx  = 0;     // loop-filled buffer awaiting handoff
static volatile bool     txPendFull = false; // true while txPendIdx holds a sector
static volatile bool     txWaitPend = false; // ISR needs a new sector — loop arms EP1
static uint32_t          txSectorsLeft;      // sectors not yet pulled this transfer

// Multi-block read (CMD18) state for the current READ_10 data-in phase.
static bool     multiReadActive = false;  // CMD18 stream in progress

// --- Diagnostic tracking (temporary) ---
static uint8_t  dbgLastOp  = 0xFF;
static uint32_t dbgLastLba = 0;
static uint16_t dbgSdFails = 0;
static uint16_t dbgWriteFails = 0;
// Persistent USB-level counters (survive BOT resets, cleared on init()).
static volatile uint32_t dbgBusResets = 0;   // host bus resets seen
static volatile uint32_t dbgCbwCount  = 0;   // CBWs received & processed
static volatile uint32_t dbgTogFails  = 0;   // EP2 OUT toggle mismatches (dropped pkts)
static volatile uint32_t dbgInPkts    = 0;   // EP1 IN packets sent (data+CSW)
static volatile uint32_t dbgTogByState[4] = {0,0,0,0};  // mismatches per BOT state
// Per-packet gap timing: time between consecutive EP1 data completions, to
// distinguish device-side NAK bubbles from the host's full-speed polling rate.
static volatile uint32_t dbgLastDataT = 0;   // micros() of last data completion
static volatile uint32_t dbgMaxGap    = 0;   // max inter-data-packet gap (us)
static volatile uint32_t dbgBigGaps   = 0;   // data-packet gaps > 200us
static uint32_t dbgBusyMs   = 0;   // total ms in a BOT transfer (loop-side)
static uint32_t dbgReadCmds = 0;   // READ_10 CBWs processed

// Write (data-out) state — ISR fills, loop writes to SD.
// Triple buffered: the fill slot is (writeHead + writeCount) % 3, which is
// never one of the full buffers pending write even when a packet straddles a
// 512-byte sector boundary (with 2 buffers the wrap back onto writeHead
// corrupted the sector being written out, causing file corruption over MSD).
static uint8_t          writeBufs[3][512];
static volatile uint16_t writeFill   = 0;   // bytes in the buffer being filled
static uint8_t           writeHead   = 0;   // FIFO head: oldest full buffer
static volatile uint8_t  writeCount  = 0;   // full buffers pending write (0..2)
static volatile uint32_t rxTotal     = 0;   // bytes received this transfer
static uint32_t          writeLBA;
static uint32_t          writeRemain;       // dCBWDataTransferLength
static bool              writeDiscard;      // true if LBA range invalid (drain only)

// --- Endpoint helpers ---

static void configureBulkEndpoints()
{
    USBFSD->UEP4_1_MOD = USBFS_UEP1_TX_EN;   // EP1 IN
    USBFSD->UEP2_3_MOD = USBFS_UEP2_RX_EN;   // EP2 OUT
    USBFSD->UEP1_DMA   = (uint32_t)(uint8_t *)ep1Buf;
    USBFSD->UEP2_DMA   = (uint32_t)(uint8_t *)ep2Buf;
    // AUTO_TOG: the hardware maintains the data toggle (continuous with the
    // host's toggle); manual toggling here drifts out of sync across BOT
    // transfers and made writes fail with host-side USB resets.
    USBFSD->UEP1_CTRL_H = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;
    USBFSD->UEP1_TX_LEN = 0;
    USBFSD->UEP2_CTRL_H = USBFS_UEP_R_RES_ACK | USBFS_UEP_R_AUTO_TOG;
    ep1Busy     = false;
}

static void usbEndpInit()
{
    USBFSD->UEP0_DMA     = (uint32_t)(uint8_t *)ep0Buf;
    USBFSD->UEP0_CTRL_H  = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;  // await SETUP
    configureBulkEndpoints();
}

// Queue one packet on EP1 IN. Caller must ensure the endpoint is not busy.
static void ep1Send(const void *data, uint16_t len)
{
    if (ep1Busy) return;
    if (len > EP1_SIZE) len = EP1_SIZE;
    memcpy(ep1Buf, data, len);
    USBFSD->UEP1_TX_LEN   = len;
    USBFSD->UEP1_CTRL_H   = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    ep1Busy       = true;
}

// Re-arm EP2 OUT to accept the next packet.
static void ep2Recv()
{
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
}

static void resetBOT()
{
    botState     = BOT_IDLE;
    cbwReady     = false;
    cbwPos       = 0;
    txNext       = false;
    txPendFull   = false;
    txWaitPend   = false;
    txSectorsLeft = 0;
    writeFill    = 0;
    writeHead    = 0;
    writeCount   = 0;
    rxTotal      = 0;
    if (multiReadActive) {
        sd::endReadMulti();
        multiReadActive = false;
    }
}

// --- Control transfer handling ---

static void handleSetupPacket()
{
    uint8_t setup[8];
    memcpy(setup, ep0Buf, 8);

    uint8_t  bmRequestType = setup[0];
    uint8_t  bRequest      = setup[1];
    uint16_t wValue        = setup[2] | ((uint16_t)setup[3] << 8);
    uint16_t wLength       = setup[6] | ((uint16_t)setup[7] << 8);

    // NAK both directions; data stage after SETUP is DATA1
    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK
                | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

    bool handled = true;
    ctrlDesc     = 0;
    ctrlRemain   = wLength;

    if ((bmRequestType & 0x60) == 0x00) {  // standard request
        switch (bRequest) {

        case REQ_SET_ADDRESS:
            usbAddrPending = wValue & 0x7F;
            ctrlRemain = 0;
            break;

        case REQ_SET_CONFIGURATION:
            usbConfigured = wValue;
            if (usbConfigured)
                configureBulkEndpoints();
            ctrlRemain = 0;
            break;

        case REQ_GET_CONFIGURATION:
            ep0Buf[0] = usbConfigured ? 1 : 0;
            ctrlDesc = ep0Buf;
            if (ctrlRemain > 1) ctrlRemain = 1;
            break;

        case REQ_GET_STATUS:
            ep0Buf[0] = 0;  // self-powered, remote wakeup off
            ep0Buf[1] = 0;
            ctrlDesc = ep0Buf;
            if (ctrlRemain > 2) ctrlRemain = 2;
            break;

        case REQ_CLEAR_FEATURE:
        case REQ_SET_FEATURE:
        case REQ_SET_INTERFACE:
            ctrlRemain = 0;
            break;

        case REQ_GET_INTERFACE:
            ep0Buf[0] = 0;
            ctrlDesc = ep0Buf;
            ctrlRemain = 1;
            break;

        case REQ_GET_DESCRIPTOR: {
            uint8_t descType = wValue >> 8;
            uint8_t descIdx  = wValue & 0xFF;
            switch (descType) {
            case DESC_DEVICE:
                ctrlDesc   = devDesc;
                ctrlRemain = (wLength < sizeof(devDesc)) ? wLength : (uint16_t)sizeof(devDesc);
                break;
            case DESC_CONFIG:
                ctrlDesc   = cfgDesc;
                ctrlRemain = (wLength < sizeof(cfgDesc)) ? wLength : (uint16_t)sizeof(cfgDesc);
                break;
            case DESC_STRING:
                if (descIdx < 4) {
                    ctrlDesc   = strDescs[descIdx];
                    uint8_t len = ctrlDesc[0];
                    ctrlRemain = (wLength < len) ? wLength : len;
                } else {
                    handled = false;
                }
                break;
            default:
                handled = false;
                break;
            }
            break;
        }

        default:
            handled = false;
            break;
        }
    } else if ((bmRequestType & 0x60) == 0x20) {  // class request
        if (bRequest == BOT_GET_MAX_LUN) {
            ep0Buf[0] = 0;      // single LUN
            ctrlDesc = ep0Buf;
            ctrlRemain = 1;
        } else if (bRequest == BOT_RESET) {
            resetBOT();
            ctrlRemain = 0;
        } else {
            handled = false;
        }
    } else {
        handled = false;
    }

    if (!handled) {
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL
                    | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
        return;
    }

    if (bmRequestType & BOT_DIR_IN) {
        // IN data stage (device -> host)
        if (ctrlRemain > 0) {
            uint16_t n = (ctrlRemain > EP0_SIZE) ? EP0_SIZE : ctrlRemain;
            memcpy(ep0Buf, ctrlDesc, n);
            ctrlDesc += n;
            ctrlRemain -= n;
            USBFSD->UEP0_TX_LEN = n;
            USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                        | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
        } else {
            USBFSD->UEP0_TX_LEN = 0;
            USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                        | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
        }
    } else if (ctrlRemain == 0) {
        // Status stage (zero-length IN)
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                    | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    } else {
        // OUT data stage (host -> device) — none expected, arm RX anyway
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                    | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    }
}

static void handleEP0In()
{
    if (ctrlRemain == 0) {
        // Data stage complete — arm RX for the status OUT
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                    | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    }
    if (usbAddrPending) {
        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & 0x80) | usbAddrPending;
        usbAddrPending = 0;
    }
    if (ctrlRemain > 0 && ctrlDesc != 0) {
        uint16_t n = (ctrlRemain > EP0_SIZE) ? EP0_SIZE : ctrlRemain;
        memcpy(ep0Buf, ctrlDesc, n);
        ctrlDesc += n;
        ctrlRemain -= n;
        USBFSD->UEP0_TX_LEN   = n;
        USBFSD->UEP0_CTRL_H  ^= USBFS_UEP_T_TOG;
    }
}

static void handleEP0Out()
{
    (void)(USBFSD->RX_LEN & 0x3FF);  // no control-OUT data accepted
    if (ctrlRemain == 0) {
        // Status stage — send zero-length IN
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                    | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
}

// --- SCSI command processing ---

static void msdFillTx();
static void sendCSW();

static void scsiTestUnitReady()
{
    if (storage::sdAvailable()) {
        senseKey = 0;
        csw.bCSWStatus = 0;
    } else {
        senseKey = 2;       // NOT READY
        senseASC = 0x3A;    // MEDIUM NOT PRESENT
        csw.bCSWStatus = 1;
    }
    csw.dCSWDataResidue = 0;
}

// Start a data-in transfer from a small payload (<=64 bytes, single packet,
// no SD access). Larger transfers use the SD-backed path in msdFillTx().
static void startDataIn(const void *data, uint16_t len, uint32_t transferLen)
{
    if (len > EP1_SIZE) len = EP1_SIZE;
    if (transferLen < len) len = (uint16_t)transferLen;
    txRemain = 0;   // CSW follows once this packet is sent
    txNext   = false;
    csw.dCSWDataResidue = transferLen - len;
    csw.bCSWStatus = 0;
    botState = BOT_DATA_IN;
    ep1Send(data, len);
}

static void scsiRequestSense()
{
    uint8_t sense[18];
    memset(sense, 0, 18);
    sense[0] = 0x70;    // response code
    sense[2] = senseKey;
    sense[7] = 10;      // additional sense length
    sense[12] = senseASC;

    uint16_t len = cbw.CBWCB[4];
    if (len > 18) len = 18;
    startDataIn(sense, len, cbw.dCBWDataTransferLength);
    senseKey = 0;
    senseASC = 0;
}

static void scsiInquiry()
{
    uint8_t inquiry[36];
    memset(inquiry, 0, 36);
    inquiry[0] = 0x00;         // SBC direct-access device
    inquiry[1] = 0x80;         // removable
    inquiry[2] = 0x04;         // SPC-2
    inquiry[3] = 0x02;         // response data format
    inquiry[4] = 31;           // additional length
    memcpy(inquiry + 8,  "PHSI245 ", 8);
    memcpy(inquiry + 16, "SD Card    ", 12);
    memcpy(inquiry + 28, "1.00", 4);

    uint16_t len = cbw.CBWCB[4];
    if (len > 36) len = 36;
    startDataIn(inquiry, len, cbw.dCBWDataTransferLength);
}

static void scsiModeSense6()
{
    uint8_t mode[4] = {3, 0, 0, 0};  // mode parameter header
    startDataIn(mode, 4, cbw.dCBWDataTransferLength);
}

static void scsiReadCapacity10()
{
    uint32_t lastBlock = sd::blockCount() - 1;
    if (lastBlock > 0xFFFFFFFF) lastBlock = 0xFFFFFFFF;

    uint8_t cap[8];
    cap[0] = (uint8_t)(lastBlock >> 24);
    cap[1] = (uint8_t)(lastBlock >> 16);
    cap[2] = (uint8_t)(lastBlock >> 8);
    cap[3] = (uint8_t)(lastBlock);
    cap[4] = (uint8_t)(msdBlockSize >> 24);
    cap[5] = (uint8_t)(msdBlockSize >> 16);
    cap[6] = (uint8_t)(msdBlockSize >> 8);
    cap[7] = (uint8_t)(msdBlockSize);

    startDataIn(cap, 8, cbw.dCBWDataTransferLength);
}

// Read the next sector for the data-in phase. Uses the active CMD18 stream
// if one is running, otherwise a single CMD17 read. Ends the stream once the
// last sector of the transfer has been pulled (the data is already in the
// caller's buffer, so stopping early is safe).
static bool readTxSector(uint8_t *buf)
{
    if (txSectorsLeft == 0) return false;
    txSectorsLeft--;
    if (multiReadActive) {
        bool ok = sd::readMultiBlock(buf);
        if (ok) txLBA++;
        if (txSectorsLeft == 0) {
            sd::endReadMulti();
            multiReadActive = false;
        }
        return ok;
    }
    return sd::readBlock(txLBA++, buf);
}

// Abort a running multi-block stream (error paths).
static void abortReadMulti()
{
    if (multiReadActive) {
        sd::endReadMulti();
        multiReadActive = false;
    }
}

// Start the next data-in transfer. Loop context only (may do SD I/O):
// reads the first sector and queues the first chunk. All following chunks
// and the CSW are pumped from the USB ISR; the loop keeps one full sector
// read-ahead so the ISR never waits on SD.
static void msdFillTx()
{
    if (botState != BOT_DATA_IN) return;

    if (txRemain == 0) {
        botState = BOT_CSW;
        sendCSW();
        return;
    }

    if (!readTxSector(msdBuf[0])) {
        abortReadMulti();
        dbgSdFails++;
        csw.bCSWStatus = 1;
        senseKey = 4;   // HARDWARE ERROR
        botState = BOT_CSW;
        sendCSW();
        return;
    }

    txDrainIdx = 0;
    dbgLastDataT = 0;  // sector start — only consecutive same-sector gaps count
    uint16_t n = (txRemain > 64) ? 64 : (uint16_t)txRemain;
    ep1Send(msdBuf[0], n);
    txBufPos = n;
    txRemain -= n;
}

static void scsiRead10()
{
    uint32_t lba = ((uint32_t)cbw.CBWCB[2] << 24) | ((uint32_t)cbw.CBWCB[3] << 16)
                 | ((uint32_t)cbw.CBWCB[4] << 8)  | cbw.CBWCB[5];
    uint16_t count = ((uint16_t)cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    if (count == 0) count = 256;
    dbgReadCmds++;

    if (lba + count > sd::blockCount()) {
        csw.dCSWDataResidue = cbw.dCBWDataTransferLength;
        csw.bCSWStatus = 1;
        senseKey = 5;   // ILLEGAL REQUEST
        senseASC = 0x21; // LBA OUT OF RANGE
        botState = BOT_CSW;
        sendCSW();
        return;
    }

    dbgLastLba = lba;
    uint32_t want = cbw.dCBWDataTransferLength;
    uint32_t avail = (uint32_t)count * 512;
    if (want > avail) want = avail;

    txLBA    = lba;
    txRemain = want;
    txBufPos = 0;
    txNext   = false;
    txSectorsLeft = (want + 511) / 512;
    txPendFull = false;
    txPendIdx  = 0;
    txDrainIdx = 0;
    txWaitPend = false;
    csw.dCSWDataResidue = cbw.dCBWDataTransferLength - want;
    csw.bCSWStatus = 0;
    botState = BOT_DATA_IN;

    // Multi-block (CMD18) for transfers of >= 2 full sectors: amortizes the
    // per-sector select/CMD17/token overhead that dominates mount. Falls back
    // to single CMD17 reads if the stream can't be started.
    multiReadActive = false;
    if (want >= 1024) {
        if (sd::beginReadMulti(lba) == 0) {
            multiReadActive = true;
        }
    }
    msdFillTx();
}

static void scsiWrite10()
{
    uint32_t lba = ((uint32_t)cbw.CBWCB[2] << 24) | ((uint32_t)cbw.CBWCB[3] << 16)
                 | ((uint32_t)cbw.CBWCB[4] << 8)  | cbw.CBWCB[5];
    uint16_t count = ((uint16_t)cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    if (count == 0) count = 256;

    dbgLastLba = lba;
    writeLBA    = lba;
    writeRemain = cbw.dCBWDataTransferLength;
    writeFill   = 0;
    writeHead   = 0;
    writeCount  = 0;
    rxTotal     = 0;
    writeDiscard = (lba + count > sd::blockCount());

    if (writeDiscard) {
        csw.dCSWDataResidue = 0;
        csw.bCSWStatus = 1;
        senseKey = 5;
        senseASC = 0x21;
    } else {
        csw.dCSWDataResidue = 0;
        csw.bCSWStatus = 0;
    }

    if (writeRemain > 0) {
        botState = BOT_DATA_OUT;
        ep2Recv();
    } else {
        botState = BOT_CSW;
        sendCSW();
    }
}

static void scsiPreventAllow()
{
    csw.dCSWDataResidue = 0;
    csw.bCSWStatus = 0;
}

static void scsiStopStart()
{
    csw.dCSWDataResidue = 0;
    csw.bCSWStatus = 0;
}

// Process a WRITE_10 CBW from ISR context. scsiWrite10() only assigns state
// and programs endpoint registers (sd::blockCount() is a cached read), so this
// is safe to run in the ISR. Setting up the data phase here — instead of
// leaving cbwReady for the loop — closes the race where the host's first data
// packet is received and ACKed by the SIE before the loop processes the CBW,
// then discarded, stalling the write forever waiting for rxTotal == writeRemain.
static void processWriteCBW()
{
    memcpy(&cbw, cbwBuf, BOT_CBW_LEN);
    dbgCbwCount++;
    csw.dCSWSignature   = BOT_CSW_SIGNATURE;
    csw.dCSWTag         = cbw.dCBWTag;
    csw.dCSWDataResidue = 0;
    csw.bCSWStatus      = 0;
    cbwReady            = false;
    txNext              = false;
    dbgLastOp           = cbw.CBWCB[0];
    scsiWrite10();
}

// --- BOT state machine ---

static void sendCSW()
{
    dbgLastDataT = 0;  // command boundary — start a fresh gap run
    botState = BOT_CSW;
    ep1Send(&csw, BOT_CSW_LEN);
}

static void processCBW()
{
    memcpy(&cbw, cbwBuf, BOT_CBW_LEN);
    dbgCbwCount++;
    csw.dCSWSignature  = BOT_CSW_SIGNATURE;
    csw.dCSWTag        = cbw.dCBWTag;
    csw.dCSWDataResidue = 0;
    csw.bCSWStatus      = 0;
    cbwReady            = false;
    txNext              = false;

    uint8_t op = cbw.CBWCB[0];
    dbgLastOp = op;

    switch (op) {
    case SCSI_TEST_UNIT_READY:
        scsiTestUnitReady();
        sendCSW();
        break;
    case SCSI_REQUEST_SENSE:
        scsiRequestSense();
        break;
    case SCSI_INQUIRY:
        scsiInquiry();
        break;
    case SCSI_MODE_SENSE_6:
        scsiModeSense6();
        break;
    case SCSI_READ_CAPACITY_10:
        scsiReadCapacity10();
        break;
    case SCSI_READ_10:
        scsiRead10();
        break;
    case SCSI_WRITE_10:
        scsiWrite10();
        break;
    case SCSI_PREVENT_ALLOW:
        scsiPreventAllow();
        sendCSW();
        break;
    case SCSI_START_STOP_UNIT:
        scsiStopStart();
        sendCSW();
        break;
    case SCSI_SYNCHRONIZE_CACHE:
        csw.dCSWDataResidue = 0;
        csw.bCSWStatus = 0;
        sendCSW();
        break;
    default:
        senseKey = 5;
        senseASC = 0x20;
        csw.bCSWStatus = 1;
        csw.dCSWDataResidue = cbw.dCBWDataTransferLength;
        sendCSW();
        break;
    }
}

// --- Public API ---

void usb_msd::init()
{
    // Clocks
    RCC->AHBPCENR |= RCC_USBFS;
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC;

    // GPIO: PC16 (UDM/D-) floating, PC17 (UDP/D+) pull-up
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = GPIO_Pin_16;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &g);
    g.GPIO_Pin   = GPIO_Pin_17;
    g.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &g);

    // AFIO: route USB IO, enable D+ 1.5K pull-up (device attach), PHY at 3.3V
    AFIO->CTLR = (AFIO->CTLR & ~AFIO_USB_MASK)
               | AFIO_USB_PHY_V33 | AFIO_UDP_PUE_1K5 | AFIO_USB_IOEN;

    // SIE
    USBFSD->BASE_CTRL = 0;
    usbEndpInit();
    USBFSD->DEV_ADDR  = 0;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;

    USBFSD->INT_FG   = 0xFF;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
    USBFSD->INT_EN   = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
    NVIC_EnableIRQ(USBFS_IRQn);

    usbInitialized = true;
    hostPresent    = false;
    usbAddrPending = 0;
    usbConfigured  = 0;
    resetBOT();

    msdBlockSize  = 512;
    msdBlockCount = storage::sdAvailable() ? sd::blockCount() : 0;
}

void usb_msd::deinit()
{
    NVIC_DisableIRQ(USBFS_IRQn);

    // Disconnect: release D+ pull-up and USB IO, pull the SIE out of operation
    AFIO->CTLR &= ~(AFIO_USB_MASK | AFIO_USB_IOEN);
    USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
    delay(10);
    USBFSD->BASE_CTRL = 0;

    hostPresent    = false;
    usbInitialized = false;
    resetBOT();
}

void usb_msd::linkAnchor()
{
    // Keep this library linked via os_libs_init() (AGENTS.md convention).
}

bool usb_msd::isConnected()
{
    return hostPresent;
}

uint32_t usb_msd::isrCount()
{
    return usbIsrFires;
}

bool usb_msd::isBusy()
{
    return cbwReady || botState == BOT_DATA_IN || botState == BOT_DATA_OUT;
}

// --- Temporary BOT diagnostics ---
uint8_t usb_msd::botStateDebug()
{
    return (uint8_t)botState;
}

uint16_t usb_msd::txRemainDebug()
{
    return txRemain;
}

uint8_t usb_msd::lastOpcode()
{
    return dbgLastOp;
}

uint32_t usb_msd::lastLBA()
{
    return dbgLastLba;
}

uint16_t usb_msd::sdFailCount()
{
    return dbgSdFails;
}

uint16_t usb_msd::writeFailCount()
{
    return dbgWriteFails;
}

uint32_t usb_msd::busResetCount()
{
    return dbgBusResets;
}

uint32_t usb_msd::cbwCount()
{
    return dbgCbwCount;
}

uint32_t usb_msd::togFailCount()
{
    return dbgTogFails;
}

uint16_t usb_msd::togByState(uint8_t state)
{
    if (state > 3) return 0;
    return dbgTogByState[state];
}

uint32_t usb_msd::inPktCount()
{
    return dbgInPkts;
}

uint32_t usb_msd::busyMs()
{
    return dbgBusyMs;
}

uint32_t usb_msd::readCmdCount()
{
    return dbgReadCmds;
}

uint32_t usb_msd::maxInGap()
{
    return dbgMaxGap;
}

uint32_t usb_msd::bigGapCount()
{
    return dbgBigGaps;
}

bool usb_msd::update()
{
    if (!usbInitialized) return false;

    // Accumulate wall-clock time spent inside BOT transfers. The loop runs
    // without its idle delay while busy, so the inter-update delta here is
    // real transfer time (incl. SD reads and waiting on the host's IN tokens).
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    uint32_t delta = (lastMs != 0) ? now - lastMs : 0;
    lastMs = now;
    if (cbwReady || botState == BOT_DATA_IN || botState == BOT_DATA_OUT)
        dbgBusyMs += delta;

    if (cbwReady) {
        processCBW();
        return true;
    }

    if (botState == BOT_DATA_IN && txRemain > 0) {
        if (!txPendFull && txSectorsLeft > 0) {
            uint8_t freeIdx = txDrainIdx ^ 1;
            if (!readTxSector(msdBuf[freeIdx])) {
                abortReadMulti();
                dbgSdFails++;
                csw.bCSWStatus = 1;
                senseKey = 4;   // HARDWARE ERROR
                botState = BOT_CSW;
                sendCSW();
                return true;
            }
            txPendIdx  = freeIdx;
            txPendFull = true;
        }
        if (txWaitPend && txPendFull) {
            txWaitPend = false;
            txPendFull = false;
            txDrainIdx = txPendIdx;
            dbgLastDataT = 0;  // sector start — fresh gap run
            uint16_t n = (txRemain > 64) ? 64 : (uint16_t)txRemain;
            ep1Send(msdBuf[txDrainIdx], n);
            txBufPos = n;
            txRemain -= n;
        }
    } else if (botState == BOT_DATA_OUT) {
        // Write all pending full sectors to the SD card (up to two).
        while (writeCount > 0) {
            bool ok = true;
            if (!writeDiscard)
                ok = sd::writeBlock(writeLBA, writeBufs[writeHead]);
            if (!ok) dbgWriteFails++;
            writeLBA++;
            writeHead = (uint8_t)((writeHead + 1) % 3);
            writeCount--;
            ep2Recv();  // buffer freed — unthrottle the host
        }
        if (rxTotal >= writeRemain && writeFill > 0) {
            // Final partial sector — zero pad and flush
            bool ok = true;
            if (!writeDiscard) {
                memset(writeBufs[writeHead] + writeFill, 0, 512 - writeFill);
                ok = sd::writeBlock(writeLBA, writeBufs[writeHead]);
            }
            if (!ok) dbgWriteFails++;
            writeLBA++;
            writeFill = 0;
        }
        if (rxTotal >= writeRemain && writeFill == 0 && writeCount == 0) {
            botState = BOT_CSW;
            sendCSW();
        }
    }
    return true;
}

// --- USB interrupt handler ---
// MUST be compiled with the WCH-Interrupt-fast attribute: a plain function
// prologue would return via `ret` to a stale `ra` and never restore mstatus,
// which dead-locks the first interrupt and hangs the main loop.
extern "C" void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void USBFS_IRQHandler(void)
{
    usbIsrFires++;
    uint8_t intflag = USBFSD->INT_FG;
    uint8_t intst   = USBFSD->INT_ST;

    if (intflag & USBFS_UIF_TRANSFER) {
        switch (intst & USBFS_UIS_TOKEN_MASK) {

        case USBFS_UIS_TOKEN_SETUP:
            handleSetupPacket();
            break;

        case USBFS_UIS_TOKEN_IN: {
            uint8_t ep = intst & USBFS_UIS_ENDP_MASK;
            if (ep == 0) {
                handleEP0In();
            } else if (ep == EP1_IN) {
                USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                                    | USBFS_UEP_T_RES_NAK;
                ep1Busy = false;
                if (botState == BOT_DATA_IN) {
                    if (txRemain == 0) {
                        dbgInPkts++;
                        sendCSW();              // data phase complete — queue CSW now
                    } else if (txBufPos != 0) {
                        dbgInPkts++;
                        uint32_t nt = micros();
                        if (dbgLastDataT != 0) {
                            uint32_t g = nt - dbgLastDataT;
                            if (g > dbgMaxGap) dbgMaxGap = g;
                            if (g > 200) dbgBigGaps++;
                        }
                        dbgLastDataT = nt;
                        uint16_t n = (txRemain > 64) ? 64 : (uint16_t)txRemain;
                        ep1Send(msdBuf[txDrainIdx] + txBufPos, n);
                        txBufPos += n;
                        txRemain -= n;
                        if (txBufPos >= 512) txBufPos = 0;  // sector done — loop hands off pending
                    } else {
                        // Endpoint NAK'd; a NAK'd token never completes so the
                        // ISR won't fire again. The loop refills and arms EP1
                        // itself (see update()).
                        txWaitPend = true;
                    }
                } else if (botState == BOT_CSW) {
                    dbgInPkts++;
                    botState = BOT_IDLE;
                    cbwPos   = 0;
                    ep2Recv();
                }
            }
            break;
        }

        case USBFS_UIS_TOKEN_OUT: {
            uint8_t ep = intst & USBFS_UIS_ENDP_MASK;
            if (ep == 0) {
                handleEP0Out();
            } else if (ep == EP2_OUT) {
                // Toggle mismatch: discard the packet, leave the endpoint
                // armed so the host's retry (same toggle) is accepted.
                if (!(intst & USBFS_UIS_TOG_OK)) {
                    dbgTogFails++;
                    uint8_t st = (uint8_t)botState;
                    if (st < 4) dbgTogByState[st]++;
                    break;
                }

                uint16_t len = USBFSD->RX_LEN & 0x3FF;
                if (botState == BOT_DATA_OUT) {
                    // Write data — accumulate into the fill slot
                    // (writeHead + writeCount) % 3. The push is immediate: a
                    // buffer reaching 512 bytes is committed to the FIFO right
                    // away, so writeFill never lingers at 512 and a completing
                    // sector is never left behind by the final flush in
                    // update(). With three buffers the fill slot is distinct
                    // from the full buffers even when a 64-byte OUT packet
                    // straddles a sector boundary.
                    uint16_t pos = 0;
                    while (pos < len) {
                        uint8_t  idx   = (uint8_t)(writeHead + writeCount) % 3;
                        uint16_t space = 512 - writeFill;
                        uint16_t n     = len - pos;
                        if (n > space) n = space;
                        memcpy(writeBufs[idx] + writeFill, ep2Buf + pos, n);
                        writeFill += n;
                        pos       += n;
                        if (writeFill >= 512) {      // buffer complete — push to FIFO
                            writeFill = 0;
                            writeCount++;
                        }
                    }
                    rxTotal += len;
                } else {
                    // CBW (may span several packets). If a CBW is already
                    // queued but not yet processed by the loop, this packet is
                    // the command's DATA phase — NAK it rather than misparse it
                    // as a second CBW.
                    if (cbwReady) {
                        USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                                            | USBFS_UEP_R_RES_NAK;
                        break;
                    }
                    if (len > BOT_CBW_LEN - cbwPos) len = BOT_CBW_LEN - cbwPos;
                    if (len > 0) {
                        memcpy(cbwBuf + cbwPos, ep2Buf, len);
                        cbwPos += len;
                        if (cbwPos >= BOT_CBW_LEN) {
                            cbwPos = 0;
                            // WRITE_10 CBWs get their data phase armed here
                            // (ISR-safe, see processWriteCBW) so the host's
                            // first data packet is never lost to the loop's
                            // cbwReady latency.
                            if (cbwBuf[15] == SCSI_WRITE_10 && !(cbwBuf[12] & 0x80))
                                processWriteCBW();
                            else
                                cbwReady = true;
                        }
                    }
                }
                // Re-arm for the next packet. If two buffers are full and the
                // SD writes are falling behind, NAK to throttle the host; the
                // loop re-arms with ACK once a buffer frees up. (One more
                // packet may still be in flight while writeCount == 2 — it
                // lands in the third buffer, which is always free here.)
                if (botState == BOT_DATA_OUT && writeCount == 2) {
                    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                                        | USBFS_UEP_R_RES_NAK;
                } else {
                    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                                        | USBFS_UEP_R_RES_ACK;
                }
            }
            break;
        }

        default:  // USBFS_UIS_TOKEN_SOF
            break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    } else if (intflag & USBFS_UIF_BUS_RST) {
        dbgBusResets++;
        hostPresent    = true;
        usbConfigured  = 0;
        usbAddrPending = 0;
        USBFSD->DEV_ADDR = 0;
        usbEndpInit();
        resetBOT();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    } else if (intflag & USBFS_UIF_SUSPEND) {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
    } else {
        USBFSD->INT_FG = intflag;
    }
}
