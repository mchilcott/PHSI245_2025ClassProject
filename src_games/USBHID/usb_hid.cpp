// USB HID device — composite mouse + keyboard + gamepad over a single
// interrupt IN endpoint (Report IDs 1/2/3). Targets the CH32X035 USBFS
// peripheral (device mode). Register bit definitions mirror the WCH
// ch32x035_usb.h header, which the Arduino framework build does not ship;
// the USBFSD register struct itself comes from ch32x035.h.

#include "usb_hid.h"
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

#define USBFS_UEP1_TX_EN      0x40

// AFIO->CTLR USB config bits (PC16=UDM/D-, PC17=UDP/D+). The device attach
// needs the 1.5k D+ pull-up (0x0C), not the 35uA option (0x04).
#define AFIO_USB_MASK         0x0F
#define AFIO_USB_IOEN         0x80
#define AFIO_USB_PHY_V33      0x40
#define AFIO_UDP_PUE_1K5      0x0C

#define USBFS_UD_PD_DIS       0x80
#define USBFS_UD_PORT_EN      0x01

#define EP0     0
#define EP1_IN  1

#define EP0_SIZE    64
#define EP1_SIZE    16

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
#define DESC_HID                0x21
#define DESC_REPORT             0x22

// --- HID class request codes ---
#define HID_GET_REPORT          0x01
#define HID_GET_IDLE            0x02
#define HID_GET_PROTOCOL        0x03
#define HID_SET_REPORT          0x09
#define HID_SET_IDLE            0x0A
#define HID_SET_PROTOCOL        0x0B

// --- Global state ---
static uint8_t  usbAddrPending = 0;
static uint8_t  usbConfigured  = 0;
static bool     hostPresent    = false;
static bool     usbInitialized = false;
static volatile uint32_t usbIsrFires = 0;

// --- Endpoint buffers (DMA targets, 4-byte aligned) ---
__attribute__((aligned(4))) static uint8_t ep0Buf[EP0_SIZE];  // EP0 RX + TX
__attribute__((aligned(4))) static uint8_t ep1Buf[EP1_SIZE];  // EP1 IN

// --- Control transfer state ---
static const uint8_t *ctrlDesc     = 0;
static uint16_t       ctrlRemain   = 0;
static bool           ctrlSetReport = false;

// --- Report state ---
static uint8_t lastReport[9];   // id + payload, for GET_REPORT
static uint8_t lastReportLen = 0;

// --- EP1 IN state (single-slot pending queue) ---
static volatile bool ep1Busy    = false;
static volatile bool pendValid  = false;
static uint8_t       pendBuf[9];
static uint8_t       pendLen    = 0;

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
    0x48, 0x55, // idProduct (0x5548)
    0x00, 0x01, // bcdDevice 1.00
    1, 2, 3,    // iManufacturer, iProduct, iSerialNumber
    0x01,       // bNumConfigurations
};

static const PROGMEM uint8_t cfgDesc[] = {
    // Configuration descriptor
    0x09,       // bLength
    0x02,       // bDescriptorType (Configuration)
    0x22, 0x00, // wTotalLength (34 bytes)
    0x01,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0x80,       // bmAttributes (bus-powered)
    0x32,       // bMaxPower (100 mA)

    // Interface descriptor
    0x09,       // bLength
    0x04,       // bDescriptorType (Interface)
    0x00,       // bInterfaceNumber
    0x00,       // bAlternateSetting
    0x01,       // bNumEndpoints (1: interrupt IN)
    0x03,       // bInterfaceClass (HID)
    0x00,       // bInterfaceSubClass (none)
    0x00,       // bInterfaceProtocol (none)
    0x00,       // iInterface

    // HID descriptor
    0x09,       // bLength
    0x21,       // bDescriptorType (HID)
    0x10, 0x01, // bcdHID 1.10
    0x00,       // bCountryCode
    0x01,       // bNumDescriptors
    0x22,       // bDescriptorType (Report)
    0x84, 0x00, // wDescriptorLength (132)

    // Endpoint 1 IN (interrupt)
    0x07,       // bLength
    0x05,       // bDescriptorType (Endpoint)
    0x81,       // bEndpointAddress (EP1 IN)
    0x03,       // bmAttributes (Interrupt)
    EP1_SIZE, 0x00, // wMaxPacketSize
    0x01,       // bInterval (1 ms polling)
};

static const PROGMEM uint8_t reportDesc[] = {
    // ---------------- Report ID 1: Mouse ----------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Const)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    // ---------------- Report ID 2: Keyboard ----------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x08,        //   Report Count (8)
    0x75, 0x01,        //   Report Size (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)    modifiers
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const)           reserved
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x00,        //   Input (Data,Array)      keys[6]
    0xC0,              // End Collection

    // ---------------- Report ID 3: Gamepad ----------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x06,        //   Usage Maximum (Button 6)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x01,        //   Report Size (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)    buttons
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x02,        //   Report Size (2)
    0x81, 0x01,        //   Input (Const)           padding
    0xC0,              // End Collection
};

// String descriptors
static const PROGMEM uint8_t langID[] = {
    0x04, 0x03, 0x09, 0x04  // English (US)
};

// Descriptor integrity checks: lengths are asserted in the descriptors and
// must stay in sync (wTotalLength, HID descriptor offset, report length).
static_assert(sizeof(cfgDesc) == 34, "cfgDesc must be 34 bytes");
static_assert(sizeof(reportDesc) == 132, "reportDesc must be 132 bytes");

static const PROGMEM uint8_t strManufacturer[] = {
    18, 0x03,
    'P',0,'H',0,'S',0,'I',0,'2',0,'4',0,'5',0
};

static const PROGMEM uint8_t strProduct[] = {
    18, 0x03,
    'U',0,'S',0,'B',0,' ',0,'H',0,'I',0,'D',0
};

static const PROGMEM uint8_t strSerial[] = {
    8, 0x03,
    '0',0,'0',0,'1',0
};

static const PROGMEM uint8_t *strDescs[] = {
    langID, strManufacturer, strProduct, strSerial
};

// --- Endpoint helpers ---

static void ep1Send(const void *data, uint16_t len)
{
    if (len > EP1_SIZE) len = EP1_SIZE;
    memcpy(ep1Buf, data, len);
    USBFSD->UEP1_TX_LEN   = len;
    USBFSD->UEP1_CTRL_H   = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    ep1Busy       = true;
}

static void configureHidEndpoint()
{
    USBFSD->UEP4_1_MOD = USBFS_UEP1_TX_EN;   // EP1 IN
    USBFSD->UEP1_DMA   = (uint32_t)(uint8_t *)ep1Buf;
    // AUTO_TOG: the hardware maintains the data toggle (continuous with the
    // host's toggle), matching the proven usb_msd endpoint setup.
    USBFSD->UEP1_CTRL_H = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;
    USBFSD->UEP1_TX_LEN = 0;
    ep1Busy     = false;
    pendValid   = false;
}

static void usbEndpInit()
{
    USBFSD->UEP0_DMA     = (uint32_t)(uint8_t *)ep0Buf;
    USBFSD->UEP0_CTRL_H  = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;  // await SETUP
    configureHidEndpoint();
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
    ctrlSetReport = false;

    if ((bmRequestType & 0x60) == 0x00) {  // standard request
        switch (bRequest) {

        case REQ_SET_ADDRESS:
            usbAddrPending = wValue & 0x7F;
            ctrlRemain = 0;
            break;

        case REQ_SET_CONFIGURATION:
            usbConfigured = wValue;
            if (usbConfigured)
                configureHidEndpoint();
            ctrlRemain = 0;
            break;

        case REQ_GET_CONFIGURATION:
            ep0Buf[0] = usbConfigured ? 1 : 0;
            ctrlDesc = ep0Buf;
            if (ctrlRemain > 1) ctrlRemain = 1;
            break;

        case REQ_GET_STATUS:
            ep0Buf[0] = 0;  // bus-powered, remote wakeup off
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
            case DESC_HID:
                ctrlDesc   = &cfgDesc[18];   // HID descriptor inside cfgDesc
                ctrlRemain = 9;
                break;
            case DESC_REPORT:
                ctrlDesc   = reportDesc;
                ctrlRemain = (wLength < sizeof(reportDesc)) ? wLength : (uint16_t)sizeof(reportDesc);
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
    } else if ((bmRequestType & 0x60) == 0x20) {  // HID class request
        switch (bRequest) {
        case HID_GET_REPORT: {
            // Return the last report for the requested Report ID, or an
            // empty report of the right size. Data is >64B never (<=9B).
            uint8_t rid = (uint8_t)(wValue & 0xFF);
            uint8_t data[9] = {0};
            uint8_t len = 0;
            if (rid == 0 || (lastReportLen > 0 && lastReport[0] == rid)) {
                len = lastReportLen;
                for (uint8_t i = 0; i < len; i++) data[i] = lastReport[i];
            } else if (rid == usb_hid::RID_MOUSE) {
                len = 5;   data[0] = usb_hid::RID_MOUSE;
            } else if (rid == usb_hid::RID_KEYBOARD) {
                len = 9;   data[0] = usb_hid::RID_KEYBOARD;
            } else if (rid == usb_hid::RID_GAMEPAD) {
                len = 2;   data[0] = usb_hid::RID_GAMEPAD;
            } else {
                handled = false;
            }
            if (handled) {
                for (uint8_t i = 0; i < len; i++) ep0Buf[i] = data[i];
                ctrlDesc = ep0Buf;
                ctrlRemain = (wLength < len) ? wLength : len;
            }
            break;
        }
        case HID_GET_IDLE:
            ep0Buf[0] = 0;
            ctrlDesc = ep0Buf;
            ctrlRemain = 1;
            break;
        case HID_GET_PROTOCOL:
            ep0Buf[0] = 0;
            ctrlDesc = ep0Buf;
            ctrlRemain = 1;
            break;
        case HID_SET_REPORT:
            // Host wants to push a report; accept it in the OUT data stage.
            // A zero-length SET_REPORT has no data stage (status only).
            ctrlSetReport = (wLength > 0);
            ctrlRemain    = 0;
            break;
        case HID_SET_IDLE:
            // Acknowledged and ignored: EP1 simply NAKs when idle.
            ctrlRemain = 0;
            break;
        case HID_SET_PROTOCOL:
            // No boot protocol (interface protocol 0); accept and ignore.
            ctrlRemain = 0;
            break;
        default:
            handled = false;
            break;
        }
    } else {
        handled = false;
    }

    if (!handled) {
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL
                    | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
        return;
    }

    if (ctrlSetReport) {
        // OUT data stage (host -> device): arm RX with DATA1 toggle.
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK)
                    | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
        return;
    }

    if (ctrlRemain == 0) {
        // No data stage (SET_REPORT with zero length, or any pure status
        // request) — status stage is a zero-length IN.
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                    | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
        return;
    }

    if (bmRequestType & 0x80) {
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
        // OUT data stage — not expected for any handled standard request
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
    uint16_t len = USBFSD->RX_LEN & 0x3FF;
    if (ctrlSetReport) {
        ctrlSetReport = false;
        if (len > sizeof(lastReport)) len = (uint16_t)sizeof(lastReport);
        memcpy(lastReport, ep0Buf, len);
        lastReportLen = len;
        // Status stage — send zero-length IN
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                    | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
        return;
    }
    (void)len;
    if (ctrlRemain == 0) {
        // Status stage — send zero-length IN
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK)
                    | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
}

// --- Public API ---

void usb_hid::init()
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
    lastReportLen  = 0;
}

void usb_hid::update()
{
    // Nothing periodic to do: the ISR drives descriptor/data transfers and
    // sendReport() queues reports. Kept so loop() can call it symmetrically
    // with usb_msd::update().
}

void usb_hid::sendReport(uint8_t id, const uint8_t *data, uint8_t len)
{
    if (!usbInitialized || !usbConfigured) return;
    if (len > 8) len = 8;   // largest report payload (keyboard = 8 bytes)

    uint8_t buf[EP1_SIZE];
    buf[0] = id;
    memcpy(buf + 1, data, len);
    uint8_t total = len + 1;

    // Guard the pending slot: the ISR can copy pending->ep1Buf at any point.
    __disable_irq();
    memcpy(lastReport, buf, total);
    lastReportLen = total;
    if (ep1Busy) {
        memcpy(pendBuf, buf, total);
        pendLen   = total;
        pendValid = true;
    } else {
        ep1Send(buf, total);
    }
    __enable_irq();
}

bool usb_hid::isConnected()
{
    return hostPresent;
}

uint32_t usb_hid::isrCount()
{
    return usbIsrFires;
}

// --- USB interrupt handler ---
// MUST be compiled with the WCH-Interrupt-fast attribute: a plain function
// prologue would return via `ret` to a stale `ra` and never restore mstatus,
// dead-locking the first interrupt (see usb_msd.cpp).
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
                if (pendValid) {
                    pendValid = false;
                    ep1Send(pendBuf, pendLen);
                }
            }
            break;
        }

        case USBFS_UIS_TOKEN_OUT: {
            uint8_t ep = intst & USBFS_UIS_ENDP_MASK;
            if (ep == 0) {
                handleEP0Out();
            }
            break;
        }

        default:  // USBFS_UIS_TOKEN_SOF
            break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    } else if (intflag & USBFS_UIF_BUS_RST) {
        hostPresent    = true;
        usbConfigured  = 0;
        usbAddrPending = 0;
        USBFSD->DEV_ADDR = 0;
        usbEndpInit();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    } else if (intflag & USBFS_UIF_SUSPEND) {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
    } else {
        USBFSD->INT_FG = intflag;
    }
}
