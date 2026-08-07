#include <Arduino.h>
#include <string.h>
#include "os_libs.h"
#include "flash_load.h"
#include "ch32x035.h"

// --- Diagnostics ---
// printHex: 2-digit hex for the USB register readback on the MSD screen.
static void printHex(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[3] = { hex[(v >> 4) & 0xF], hex[v & 0xF], 0 };
    gfx::print(buf);
}

// printHex16: fixed 4-digit hex so diagnostics rows never overflow 128px.
static void printHex16(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[5];
    for (uint8_t i = 0; i < 4; i++) {
        buf[3 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[4] = '\0';
    gfx::print(buf);
}

// --- Constants ---

#define MAX_FILES   64
#define MAX_PATH    64
#define LIST_Y      12
#define ITEM_H      8
#define MAX_VISIBLE 5

// How long setup() samples the touch buttons before deciding whether to
// auto-boot a flashed game. Must be long enough for the touch-key ADC to
// warm up so a held B is never missed.
#define BOOT_BUTTON_WINDOW_MS 500

// --- File entry ---

struct FileEntry {
    char name[13];
    bool isDir;
    uint32_t size;
};

static FileEntry files[MAX_FILES];
static uint8_t  fileCount = 0;

// --- Directory navigation ---

static char currentPath[MAX_PATH] = "/";

static bool isAtRoot()
{
    return currentPath[0] == '/' && currentPath[1] == '\0';
}

static uint8_t pathLen()
{
    uint8_t n = 0;
    while (currentPath[n]) n++;
    return n;
}

// Drop the last path component; leaves "/" for root.
static void pathPop()
{
    uint8_t len = pathLen();
    while (len > 1) {
        len--;
        if (currentPath[len] == '/') {
            currentPath[len] = '\0';
            return;
        }
    }
    currentPath[0] = '/';
    currentPath[1] = '\0';
}

// Append a directory entry's name to currentPath. Returns false if it
// won't fit in the buffer (caller should keep browsing current dir).
static bool pathPush(const FileEntry *e)
{
    uint8_t base = pathLen();
    if (base > 0 && currentPath[base - 1] != '/') {
        if (base + 1 >= MAX_PATH) return false;
        currentPath[base++] = '/';
    }
    uint8_t len = 0;
    while (e->name[len]) len++;
    if (base + len >= MAX_PATH) return false;
    for (uint8_t i = 0; i < len; i++)
        currentPath[base + i] = e->name[i];
    currentPath[base + len] = '\0';
    return true;
}

// Write the full path of entry e into out (bounds-checked).
static void buildFilePath(char *out, uint8_t outSize, const FileEntry *e)
{
    uint8_t base = pathLen();
    if (base >= outSize) base = outSize - 1;
    for (uint8_t i = 0; i < base; i++)
        out[i] = currentPath[i];
    uint8_t pos = base;
    if (pos > 0 && out[pos - 1] != '/') {
        if (pos + 1 >= outSize) { out[pos] = '\0'; return; }
        out[pos++] = '/';
    }
    uint8_t len = 0;
    while (e->name[len]) len++;
    if (pos + len >= outSize) len = outSize - pos - 1;
    for (uint8_t i = 0; i < len; i++)
        out[pos + i] = e->name[i];
    out[pos + len] = '\0';
}

// Only .BIN files are executable.
static bool isBinFile(const FileEntry *e)
{
    if (e->isDir) return false;
    uint8_t len = 0;
    while (e->name[len]) len++;
    if (len < 5) return false;             // "X.BIN" is 5 chars
    const char *ext = e->name + len - 4;   // ".BIN"
    return ext[0] == '.' &&
           (ext[1] == 'B' || ext[1] == 'b') &&
           (ext[2] == 'I' || ext[2] == 'i') &&
           (ext[3] == 'N' || ext[3] == 'n');
}

// --- UI State ---

enum State {
    STATE_LOADING,
    STATE_BROWSE,
    STATE_FILE_INFO,
    STATE_EXECUTING,
    STATE_EXEC_ERROR,
    STATE_ERROR,
    STATE_NO_SD,
    STATE_MENU,
    STATE_PONG,
    STATE_CALIBRATE,
    STATE_FORMAT,
    STATE_MSD,
};

static State   state      = STATE_LOADING;
static uint8_t cursor     = 0;
static uint8_t scrollOff  = 0;
static uint8_t menuScroll = 0;
static uint8_t loadingPct = 0;

// Real load-and-run bookkeeping (see loadGame / drawExecProgress).
static bool         execRun    = false;
static const char  *execPhase  = "";
static char         execError[40];

// --- Helpers ---

static void drawHeader(const char *title)
{
    gfx::drawFastHLine(0, 10, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(1, 1);
    gfx::print(title);
}

// Browse header shows the current directory; truncate long paths with ".."
static void drawPathHeader()
{
    gfx::drawFastHLine(0, 10, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(1, 1);

    uint8_t len = 0;
    while (currentPath[len]) len++;
    if (len <= 20) {
        gfx::print(currentPath);
    } else {
        char buf[22];
        uint8_t keep = 18;
        for (uint8_t i = 0; i < keep; i++)
            buf[i] = currentPath[i];
        buf[keep]     = '.';
        buf[keep + 1] = '.';
        buf[keep + 2] = '\0';
        gfx::print(buf);
    }
}

static void drawProgressBar(uint8_t pct)
{
    int16_t barW = 100;
    int16_t barX = (GFX_WIDTH - barW) / 2;
    int16_t barY = 40;
    int16_t barH = 8;

    gfx::drawRect(barX - 1, barY - 1, barW + 2, barH + 2, GFX_WHITE);
    int16_t fill = (barW * pct) / 100;
    if (fill > 0)
        gfx::fillRect(barX, barY, fill, barH, GFX_WHITE);
}

// List `path` into files[], skipping "." and "..". Returns the entry count.
static uint8_t listDir(const char *path)
{
    uint8_t count = 0;
    char name[13];
    bool isDir;
    uint32_t size;

    if (fat::openDir(path) != fat::OK)
        return 0;

    while (fat::readDir(name, &isDir, &size) && count < MAX_FILES) {
        // Skip "." and ".." entries so they don't clutter the list
        if (name[0] == '.')
            continue;
        FileEntry *e = &files[count];
        for (uint8_t i = 0; i < 13; i++) {
            e->name[i] = name[i];
            if (name[i] == '\0') break;
        }
        e->isDir = isDir;
        e->size  = size;
        count++;
    }
    return count;
}

static void collectFiles(const char *path)
{
    fileCount = listDir(path);

    // The flash programming during a load can leave the SD card unresponsive
    // (probe reads all fail). Re-init + re-mount once to recover it before
    // giving up and showing the diagnostics screen.
    if (fileCount == 0 && fat::mount(&sd::DEVICE) == fat::OK)
        fileCount = listDir(path);
}

static void formatSize(char *buf, uint32_t size)
{
    if (size < 1024) {
        uint8_t pos = 0;
        uint16_t n = (uint16_t)size;
        if (n >= 100) { buf[pos++] = '0' + (n / 100); n %= 100; }
        if (n >= 10 || size >= 100) { buf[pos++] = '0' + (n / 10); n %= 10; }
        buf[pos++] = '0' + n;
        buf[pos++] = 'B';
        buf[pos] = '\0';
    } else if (size < 1024 * 1024) {
        uint16_t k = (uint16_t)(size / 1024);
        uint8_t pos = 0;
        if (k >= 100) { buf[pos++] = '0' + (k / 100); k %= 100; }
        if (k >= 10)   { buf[pos++] = '0' + (k / 10); k %= 10; }
        buf[pos++] = '0' + k;
        buf[pos++] = 'K';
        buf[pos] = '\0';
    } else {
        buf[0] = '>'; buf[1] = '1'; buf[2] = 'M'; buf[3] = '\0';
    }
}

// Temporary debug helper (remove before release)
static void printU32(uint32_t v)
{
    char buf[11];
    uint8_t n = 0;
    do { buf[n++] = '0' + (char)(v % 10); v /= 10; } while (v && n < 10);
    char tmp;
    for (uint8_t i = 0; i < n / 2; i++) {
        tmp = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = tmp;
    }
    buf[n] = '\0';
    gfx::print(buf);
}

// --- Drawing ---

static void drawLoading()
{
    gfx::clear();
    gfx::setCursor(8, 22);
    gfx::print("PHSI245 OS Bootloader");

    drawProgressBar(loadingPct);

    gfx::setCursor(20, 52);
    gfx::print("Mounting SD card...");
    gfx::display();
}

// Temporary debug screen: partition layout and raw SD read probes.
static void drawDiagnostic()
{
    DiagInfo d = fat::getDiag();

    gfx::clear();
    drawHeader("SD Diag");

    // Filesystem type + card size in blocks (from CSD)
    gfx::setCursor(2, LIST_Y);
    gfx::print("FS");
    printU32(d.fatType);
    gfx::print(" ");
    printU32(d.totalBlocks);
    gfx::print("blk");

    // Partition / FAT layout (LBAs)
    gfx::setCursor(2, LIST_Y + 8);
    gfx::print("p");
    printU32(d.partStart);
    gfx::print(" f");
    printU32(d.firstFATSector);
    gfx::print(" d");
    printU32(d.firstDataSector);

    gfx::setCursor(2, LIST_Y + 16);
    gfx::print("root");
    printU32(d.rootDirCluster);
    gfx::print(" s");
    printU32(d.sectorsPerCluster);

    // Raw read probes across the card (bit i = LBA probes[i] read OK)
    gfx::setCursor(2, LIST_Y + 24);
    gfx::print("probe ");
    char s[3];
    s[0] = "0123456789ABCDEF"[d.probeMask >> 4];
    s[1] = "0123456789ABCDEF"[d.probeMask & 0x0F];
    s[2] = '\0';
    gfx::print(s);

    // Raw CMD17 at the root dir sector (R1 0 = accepted, token FE = data)
    gfx::setCursor(2, LIST_Y + 32);
    gfx::print("root R1=");
    s[0] = "0123456789ABCDEF"[d.rootR1 >> 4];
    s[1] = "0123456789ABCDEF"[d.rootR1 & 0x0F];
    s[2] = '\0';
    gfx::print(s);
    gfx::print(" T=");
    s[0] = "0123456789ABCDEF"[d.rootToken >> 4];
    s[1] = "0123456789ABCDEF"[d.rootToken & 0x0F];
    s[2] = '\0';
    gfx::print(s);

    gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, GFX_HEIGHT - 7);
    gfx::print("B:Menu");

    gfx::display();
}

static void drawBrowse()
{
    gfx::clear();
    drawPathHeader();

    if (fileCount == 0) {
        drawDiagnostic();
        return;
    }

    for (uint8_t i = 0; i < MAX_VISIBLE; i++) {
        uint8_t fi = scrollOff + i;
        if (fi >= fileCount) break;

        int16_t y = LIST_Y + i * ITEM_H;
        FileEntry *e = &files[fi];

        if (fi == cursor) {
            gfx::setCursor(0, y);
            gfx::print(">");
        }

        gfx::setCursor(8, y);
        if (e->isDir)
            gfx::print("[");
        gfx::print(e->name);
        if (e->isDir)
            gfx::print("]");

        if (!e->isDir) {
            char sz[6];
            formatSize(sz, e->size);
            gfx::setCursor(102, y);
            gfx::print(sz);
        }
    }

    if (fileCount > MAX_VISIBLE) {
        gfx::setCursor(GFX_WIDTH - 6, LIST_Y);
        gfx::print("+");
        if (scrollOff + MAX_VISIBLE < fileCount) {
            gfx::setCursor(GFX_WIDTH - 6, LIST_Y + (MAX_VISIBLE - 1) * ITEM_H);
            gfx::print("+");
        }
    }

    gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, GFX_HEIGHT - 7);
    gfx::print("A:Open  B:Back");

    gfx::display();
}

static void drawFileInfo()
{
    FileEntry *e = &files[cursor];

    gfx::clear();
    drawHeader("File Info");

    gfx::setCursor(2, LIST_Y);
    gfx::print("Name: ");
    gfx::print(e->name);

    gfx::setCursor(2, LIST_Y + 8);
    gfx::print("Type: ");
    if (e->isDir)
        gfx::print("Directory");
    else
        gfx::print("File");

    gfx::setCursor(2, LIST_Y + 16);
    gfx::print("Size: ");
    if (e->isDir) {
        gfx::print("--");
    } else {
        char sz[6];
        formatSize(sz, e->size);
        gfx::print(sz);
    }

    gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
    gfx::setCursor(2, GFX_HEIGHT - 7);
    if (isBinFile(e))
        gfx::print("A:Exec  B:Back");
    else
        gfx::print("B:Back");

    gfx::display();
}

// Progress redraw for the real load-and-run path. Runs with IRQs disabled
// during the flash phase, so it must only use polling I/O (gfx::display is).
static void drawExecProgress(uint8_t pct)
{
    gfx::clear();
    drawHeader("Executing...");

    gfx::setCursor(2, LIST_Y);
    gfx::print(execPhase);

    gfx::setCursor(2, LIST_Y + 10);
    gfx::print(files[cursor].name);

    drawProgressBar(pct);

    gfx::display();
}

// Format the load failure into execError: "Flash err @0800A400" style, with
// the 8-hex-digit alias address that was being written when it failed.
static const char *loadErrorMsg(enum AppFlashResult r, uint32_t addr)
{
    const char *prefix = (r == APP_FLASH_SD_READ) ? "SD err @"
                       : (r == APP_FLASH_DESC)    ? "Desc err @"
                                                  : "Flash err @";
    uint8_t p = 0;
    for (uint8_t i = 0; prefix[i] && p < sizeof(execError) - 9; i++)
        execError[p++] = prefix[i];
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        uint8_t nyb = (uint8_t)(addr >> shift) & 0x0F;
        execError[p++] = "0123456789ABCDEF"[nyb];
    }
    execError[p] = '\0';
    return execError;
}

// Flash the selected .bin into the app region and jump to it. Never returns
// on success; returns an error string (shown on the exec-error screen).
static const char *loadGame(const FileEntry *e)
{
    char path[MAX_PATH + 16];
    buildFilePath(path, sizeof(path), e);

    AppHeader hdr;
    switch (appHeaderFromFile(path, &hdr)) {
    case APP_HDR_OK:            break;
    case APP_HDR_OPEN_FAIL:     return "Can't open file.";
    case APP_HDR_READ_FAIL:     return "File too small.";
    case APP_HDR_BAD_MAGIC:     return "Missing PH25 header.";
    case APP_HDR_BAD_VERSION:   return "Bad image version.";
    case APP_HDR_BAD_LOADADDR:  return "Bad load address.";
    case APP_HDR_BAD_SIZE:      return "Image too large.";
    default:                    return "Not a PHSI245 game image.";
    }

    execPhase = "Verifying...";
    switch (appVerifyFile(path, hdr, drawExecProgress)) {
    case APP_VERIFY_OK:         break;
    case APP_VERIFY_READ_FAIL:  return "SD read error.";
    case APP_VERIFY_CRC_FAIL:   return "Image failed CRC check.";
    default:                    return "Image failed CRC check.";
    }

    execPhase = "Loading...";
    uint32_t failAddr = 0;
    enum AppFlashResult r = appFlashFromFile(path, hdr, drawExecProgress, &failAddr);
    if (r != APP_FLASH_OK)
        return loadErrorMsg(r, failAddr);

    // DIAGNOSTIC: this LED lights only after the descriptor page wrote OK and
    // right before we jump into the app. If it lights and stays lit at ~98%,
    // the app was entered and faulted before setup(). If it NEVER lights, the
    // descriptor write (or a preceding flash op) is hanging before appJump().
    led::on(PIN_LED_0);

    appJump();  // never returns
    return NULL;
}



void jump_to_programming_mode(){
    SystemReset_StartMode(Start_Mode_BOOT);
    NVIC_SystemReset();
    while(1);
}

// --- Setup & Loop ---

void setup()
{
    os_libs_init();  // ensure all OS libraries are linked (even if unused elsewhere)

    // If a game is flashed it auto-boots on the next power-up. Holding B
    // must reliably cancel that, so sample the touch buttons for a full
    // debounce window instead of once: the touch-key ADC needs a warm-up
    // read or two, and a single early sample can miss a held B and trap the
    // user inside the auto-booted game (B and A holds would then both fail).
    bool valid = appValidInstalled();
    bool wantA = false;
    bool wantB = false;

    if (valid) {
        gfx::clear();
        gfx::setCursor(8, 22);
        gfx::print("Hold B for Menu...");
        gfx::display();

        for (uint32_t t0 = millis(); millis() - t0 < BOOT_BUTTON_WINDOW_MS; ) {
            input::update();
            if (input::pressed(PIN_BTN_A)) wantA = true;
            if (input::pressed(PIN_BTN_B)) wantB = true;
            ostime::delay_ms(10);
        }
    } else {
        // No game installed: nothing to auto-boot, so keep the original
        // single sample (still needed for the A-hold programming mode).
        input::update();
        wantA = input::pressed(PIN_BTN_A);
        wantB = input::pressed(PIN_BTN_B);
    }

    // Jump to programming mode if A is held at power-up
    if (wantA) {
        led::on(PIN_LED_1);
        delay(100);
        jump_to_programming_mode();
    }

    // Auto-boot the flashed game unless B is held at power-up. The app's own
    // startup (handle_reset) re-inits SP/.data/.bss/mtvec and runs SystemInit.
    // LED0 lights right before the jump: lit + frozen screen afterwards means
    // the app was entered and faulted; never lit means we didn't reach here.
    if (!wantB && valid) {
        led::on(PIN_LED_0);
        appJump();
    }

    // Blink test: 3 quick flashes to confirm the MCU is alive
    led::init();
    ostime::init();
    for (uint8_t i = 0; i < 3; i++) {
        led::on(PIN_LED_0);
        ostime::delay_ms(150);
        led::off(PIN_LED_0);
        ostime::delay_ms(150);
    }

    beep::init();
    beep::beep_startup();

    gfx::init();
    beep::beep_startup();  // confirm display init completed
    gfx::setTextSize(1);
    gfx::setTextColor(GFX_WHITE);

    // Note: input::init() runs inside os_libs_init(); do NOT re-init here —
    // it would clobber the touch thresholds touchCalibrate() just loaded.

    // Wait for user to release all buttons (debounce from boot)
    // before attempting SD mount (touch ADC may interfere with SPI)
    ostime::delay_ms(100);
}

void loop()
{
    input::update();
    usb_msd::update();  // process USB MSD commands

    switch (state) {

    case STATE_LOADING:
        drawLoading();
        ostime::delay_ms(50);

        if (loadingPct < 30) {
            loadingPct += 4;
        } else if (loadingPct < 60) {
            if (storage::sdAvailable())
                loadingPct += 6;
            else
                loadingPct = 100;  // skip to end — no SD card
        } else if (loadingPct < 90) {
            loadingPct += 2;
        } else if (loadingPct < 100) {
            loadingPct += 1;
        } else {
            if (storage::sdAvailable()) {
                currentPath[0] = '/';
                currentPath[1] = '\0';
                collectFiles(currentPath);
                scrollOff = 0;
                cursor    = 0;
                state     = STATE_BROWSE;
            } else {
                state = STATE_NO_SD;
            }
        }
        break;

    case STATE_BROWSE:
        drawBrowse();

        if (input::justPressed(PIN_BTN_UP)) {
            if (cursor > 0)
                cursor--;
        }
        if (input::justPressed(PIN_BTN_DOWN)) {
            if (cursor + 1 < fileCount)
                cursor++;
        }
        scrollOff = gfx::clampScroll(cursor, fileCount, MAX_VISIBLE);
        if (input::justPressed(PIN_BTN_A)) {
            if (fileCount > 0) {
                if (files[cursor].isDir) {
                    if (pathPush(&files[cursor])) {
                        collectFiles(currentPath);
                        scrollOff = 0;
                        cursor    = 0;
                    }
                } else {
                    state = STATE_FILE_INFO;
                }
            }
        }
        if (input::justPressed(PIN_BTN_B)) {
            if (isAtRoot()) {
                cursor = 0;
                state = STATE_MENU;
            } else {
                pathPop();
                collectFiles(currentPath);
                scrollOff = 0;
                cursor    = 0;
            }
        }

        ostime::delay_ms(80);
        break;

    case STATE_FILE_INFO:
        drawFileInfo();

        if (input::justPressed(PIN_BTN_A)) {
            if (isBinFile(&files[cursor])) {
                execRun = false;
                state = STATE_EXECUTING;
            }
        }
        if (input::justPressed(PIN_BTN_B)) {
            state = STATE_BROWSE;
        }

        ostime::delay_ms(80);
        break;

    case STATE_EXECUTING:
        if (!execRun) {
            execRun = true;
            const char *err = loadGame(&files[cursor]);  // blocks; jumps on success
            if (err) {
                if (err != execError)
                    strncpy(execError, err, sizeof(execError) - 1);
                execError[sizeof(execError) - 1] = '\0';
                state = STATE_EXEC_ERROR;
            }
        }

        ostime::delay_ms(80);
        break;

    case STATE_EXEC_ERROR:
        beep::beep_error();
        gfx::clear();
        drawHeader("Load Failed");

        gfx::setCursor(2, LIST_Y);
        gfx::print(files[cursor].name);

        gfx::setCursor(2, LIST_Y + 12);
        gfx::print(execError);

        gfx::setCursor(2, LIST_Y + 26);
        gfx::print("Press B to continue");

        gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(2, GFX_HEIGHT - 7);
        gfx::print("B:Back");

        gfx::display();

        if (input::justPressed(PIN_BTN_B)) {
            state = STATE_BROWSE;
        }

        ostime::delay_ms(80);
        break;

    case STATE_ERROR:
        beep::beep_error();
        gfx::clear();
        drawHeader("No SD Card");

        gfx::setCursor(4, LIST_Y + 4);
        gfx::print("No SD card detected.");
        gfx::setCursor(4, LIST_Y + 14);
        gfx::print("Insert a FAT-formatted");
        gfx::setCursor(4, LIST_Y + 22);
        gfx::print("SD card for storage.");
        gfx::setCursor(4, LIST_Y + 30);
        gfx::print("Press B for menu.");

        gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(2, GFX_HEIGHT - 7);
        gfx::print("B:Menu");

        gfx::display();

        if (input::justPressed(PIN_BTN_B)) {
            cursor = 0;
            state = STATE_MENU;
        }
        ostime::delay_ms(80);
        break;

    case STATE_NO_SD:
        gfx::clear();
        drawHeader("PHSI245 OS");

        gfx::setCursor(4, LIST_Y + 4);
        gfx::print("No SD card detected.");
        gfx::setCursor(4, LIST_Y + 14);
        gfx::print("Insert a FAT-formatted");
        gfx::setCursor(4, LIST_Y + 22);
        gfx::print("SD card for storage.");
        gfx::setCursor(4, LIST_Y + 30);
        gfx::print("Press A for menu.");

        gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(2, GFX_HEIGHT - 7);
        gfx::print("A:Menu");

        gfx::display();

        if (input::justPressed(PIN_BTN_A)) {
            cursor = 0;
            state = STATE_MENU;
        }
        ostime::delay_ms(80);
        break;

    case STATE_MENU: {
        enum MenuAction { M_PONG, M_CALIBRATE, M_FORMAT, M_MSD, M_PROGRAMMING, M_RESTART };
        const char *items[6];
        uint8_t menuActions[6];
        uint8_t itemCount = 0;
        items[itemCount] = "Play Pong";       menuActions[itemCount] = M_PONG;       itemCount++;
        items[itemCount] = "Calibrate Touch"; menuActions[itemCount] = M_CALIBRATE; itemCount++;
        if (storage::sdAvailable()) {
            items[itemCount] = "Format SD Card";    menuActions[itemCount] = M_FORMAT; itemCount++;
            items[itemCount] = "USB Mass Storage";  menuActions[itemCount] = M_MSD;   itemCount++;
        }
        items[itemCount] = "Programming Mode";  menuActions[itemCount] = M_PROGRAMMING; itemCount++;
        items[itemCount] = "Restart";         menuActions[itemCount] = M_RESTART;   itemCount++;

        gfx::clear();
        drawHeader("Main Menu");

        gfx::drawScrollMenu(0, LIST_Y, items, itemCount, cursor,
                            MAX_VISIBLE, &menuScroll, ITEM_H);

        gfx::drawFastHLine(0, GFX_HEIGHT - 9, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(2, GFX_HEIGHT - 7);
        gfx::print("A:Select  B:Back");

        gfx::display();

        if (input::justPressed(PIN_BTN_UP) && cursor > 0) {
            cursor--;
        }
        if (input::justPressed(PIN_BTN_DOWN) && cursor + 1 < itemCount) {
            cursor++;
        }
        if (input::justPressed(PIN_BTN_A)) {
            switch (menuActions[cursor]) {
            case M_PONG:
                state = STATE_PONG;
                break;
            case M_CALIBRATE:
                state = STATE_CALIBRATE;
                break;
            case M_FORMAT:
                state = STATE_FORMAT;
                break;
            case M_MSD:
                usb_msd::init();
                state = STATE_MSD;
                break;
            case M_PROGRAMMING:
                gfx::clear();
                gfx::setCursor(8, 22);
                gfx::print("Entering");
                gfx::setCursor(8, 32);
                gfx::print("Programming Mode");
                gfx::display();
                ostime::delay_ms(500);

                jump_to_programming_mode();
                break;
            case M_RESTART:
            default:
                NVIC_SystemReset();
                break;
            }
        }
        if (input::justPressed(PIN_BTN_B)) {
            if (storage::sdAvailable()) {
                // Re-enter the browser from the menu: re-list the root so
                // files changed by USB-MSD / Format / card swaps show up.
                currentPath[0] = '/';
                currentPath[1] = '\0';
                collectFiles(currentPath);
                scrollOff = 0;
                cursor    = 0;
                state     = STATE_BROWSE;
            } else {
                state = STATE_NO_SD;
            }
        }
        ostime::delay_ms(80);
        break;
    }

    case STATE_PONG: {
        pong::update();
        pong::draw();
        gfx::display();

        if (!pong::isPlaying() && input::justPressed(PIN_BTN_B)) {
            state = STATE_MENU;
            cursor = 0;
        }
        ostime::delay_ms(25);
        break;
    }

    case STATE_CALIBRATE:
        touchCalibrateNow();
        cursor = 1;
        state = STATE_MENU;
        break;

    case STATE_FORMAT: {
        static bool   confirmed = false;
        static bool   fmtDone   = false;
        static bool   aReleased = true;
        static fat::Result fmtResult = fat::OK;

        if (!confirmed) {
            gfx::clear();
            drawHeader("Format SD Card");
            gfx::setCursor(4, LIST_Y + 2);
            gfx::print("This will erase ALL");
            gfx::setCursor(4, LIST_Y + 12);
            gfx::print("data on the card!");
            gfx::setCursor(4, LIST_Y + 28);
            gfx::print("A: Confirm format");
            gfx::setCursor(4, LIST_Y + 38);
            gfx::print("B: Cancel");
            gfx::display();

            if (input::justPressed(PIN_BTN_A)) {
                confirmed = true;
                fmtDone   = false;
                aReleased = false;
            }
            if (input::justPressed(PIN_BTN_B)) {
                confirmed = false;
                fmtDone   = false;
                aReleased = true;
                state = STATE_MENU;
                cursor = 2;
            }
            ostime::delay_ms(80);
            break;
        }

        if (!fmtDone) {
            // Run the format exactly once (it blocks for a long time).
            gfx::clear();
            drawHeader("Formatting...");
            gfx::setCursor(4, LIST_Y + 8);
            gfx::print("Please wait...");
            gfx::display();

            fmtResult = fat::format();
            fmtDone   = true;
        }

        gfx::clear();
        drawHeader("Format");
        gfx::setCursor(4, LIST_Y + 8);
        if (fmtResult == fat::OK) {
            gfx::print("Format successful!");
            gfx::setCursor(4, LIST_Y + 18);
            gfx::print("Card is ready to use.");
        } else {
            gfx::print("Format failed!");
            gfx::setCursor(4, LIST_Y + 18);
            gfx::print("Error: ");
            gfx::print((int16_t)fmtResult);
        }
        gfx::setCursor(4, LIST_Y + 34);
        gfx::print("Press A to continue");
        gfx::display();

        // Only accept A after it has been cleanly released, so a held/confirm
        // press can't stick the state or silently loop.
        if (!input::pressed(PIN_BTN_A))
            aReleased = true;
        if (aReleased && input::justPressed(PIN_BTN_A)) {
            confirmed = false;
            fmtDone   = false;
            aReleased = true;
            state = STATE_MENU;
            cursor = 2;
        }
        ostime::delay_ms(80);
        break;
    }

    case STATE_MSD: {
        static uint32_t lastDraw = 0;
        static bool     lastConnected = false;
        static uint16_t dbgTick  = 0;
        dbgTick++;
        uint32_t now    = millis();
        bool     connected = usb_msd::isConnected();

        // Skip the full-screen redraw while a transfer is active: it's an
        // SSD1306 over I2C and takes ~12ms, stalling SD refills and CSW
        // processing (which is what slows mount/unmount down).
        bool busy = usb_msd::isBusy();
        if ((!busy && now - lastDraw >= 250) || connected != lastConnected) {
            lastDraw      = now;
            lastConnected = connected;
            gfx::clear();
            drawHeader("USB Mass Storage");

            // --- Temporary diagnostics ---
            // All counts hex (fixed width, never overflows 128px).
            // C=connected, T=loop tick (freezes if the loop dies), BR=bus
            // resets, OP=last opcode, L=last LBA, CB=CBWs, TG=EP2 OUT
            // toggle-mismatch drops, BT=total ms in BOT transfers, SD=total
            // ms in SD reads, RC=READ_10 commands, RD=SD readBlock calls,
            // B=512 dummy SPI bytes in ms (byte-loop floor), F=SD read
            // failures.
            gfx::setCursor(4, LIST_Y + 4);
            gfx::print("C:");
            gfx::print(connected ? (int16_t)1 : (int16_t)0);
            gfx::print(" T:");
            printHex16(dbgTick);
            gfx::print(" BR:");
            printHex16(usb_msd::busResetCount());

            gfx::setCursor(4, LIST_Y + 12);
            gfx::print("OP:");
            printHex(usb_msd::lastOpcode());
            gfx::print(" L:");
            printHex16(usb_msd::lastLBA());

            gfx::setCursor(4, LIST_Y + 20);
            gfx::print("CB:");
            printHex16(usb_msd::cbwCount());
            gfx::print(" TG:");
            printHex16(usb_msd::togFailCount());

            gfx::setCursor(4, LIST_Y + 28);
            gfx::print("BT:");
            printHex16(usb_msd::busyMs());
            gfx::print(" SD:");
            printHex16(sd::readUs() / 1000);

            gfx::setCursor(4, LIST_Y + 36);
            gfx::print("RC:");
            printHex16(usb_msd::readCmdCount());
            gfx::print(" RD:");
            printHex16(sd::readCalls());

            gfx::setCursor(4, LIST_Y + 44);
            gfx::print("G:");
            printHex16(usb_msd::maxInGap());   // max inter-packet gap (us)
            gfx::print(" S:");
            printHex16(usb_msd::bigGapCount()); // data-packet gaps > 200us
            gfx::print(" F:");
            printHex16(usb_msd::sdFailCount());
            gfx::display();
        }

        if (input::justPressed(PIN_BTN_B)) {
            usb_msd::deinit();
            cursor = 3;  // "USB Mass Storage" menu item
            state  = STATE_MENU;
        }
        // Spin (no delay) while a BOT transfer is in progress so the loop
        // refills SD sectors as fast as the USB ISR drains them; idle back
        // off to 2ms. update() is called at the top of loop().
        if (!usb_msd::isBusy())
            ostime::delay_ms(2);
        break;
    }
    }
}
