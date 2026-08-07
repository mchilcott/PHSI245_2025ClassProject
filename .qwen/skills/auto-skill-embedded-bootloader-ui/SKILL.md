---
name: embedded-bootloader-ui
description: Design a minimal embedded bootloader with loading screen, file browser, and audio feedback — state-machine architecture on a 128×64 monochrome OLED with touch buttons and SD card storage
source: auto-skill
extracted_at: '2026-07-16T13:30:00.000Z'
---

# Embedded Bootloader with File Browser UI

Build a bootloader that fits on a 128×64 monochrome OLED display and uses capacitive
touch buttons for navigation. The bootloader detects available storage, shows a loading
animation, browses files, and provides audio feedback for error conditions.

## State machine architecture

Use a flat C-style enum-driven state machine with one draw function per state. Each
loop iteration calls `input::update()` once, renders the current state's screen, and
checks for transitions:

```cpp
enum State {
    STATE_LOADING,
    STATE_BROWSE,
    STATE_FILE_INFO,
    STATE_EXECUTING,
    STATE_ERROR,
};

static State state = STATE_LOADING;

void loop() {
    input::update();

    switch (state) {
    case STATE_LOADING:   /* draw loading bar + mount SD */   break;
    case STATE_BROWSE:    /* draw file list + handle nav */   break;
    case STATE_FILE_INFO: /* draw file details */           break;
    case STATE_EXECUTING: /* draw execution screen */       break;
    case STATE_ERROR:     /* draw error + retry prompt */   break;
    }
}
```

No inheritance, no virtual dispatch, no dynamic allocation. Each state is a `case`
block that draws, checks input, and may transition to another state. A per-state
`delay_ms()` at the end of each case prevents excessive redraws and sets the polling
rate (~12 Hz at 80ms is comfortable for button response).

## Loading screen with animated progress bar

Show a title and a progress bar that fills as initialization proceeds. The mount
attempt happens during the bar animation rather than before it — this avoids a
blank-screen pause:

```cpp
case STATE_LOADING:
    drawLoading();                    // title + progress bar at current loadingPct
    ostime::delay_ms(50);

    if (loadingPct < 30) {
        loadingPct += 2;             // fast initial fill
    } else if (loadingPct < 60) {
        fat::Result r = fat::mount(&sd::DEVICE);  // attempt SD mount mid-animation
        if (r == fat::OK) {
            sdReady = true;
            loadingPct += 3;
        } else {
            loadingPct = 100;        // skip remaining animation, go to error
            sdReady = false;
        }
    } else if (loadingPct < 90) {
        loadingPct += 2;
    } else {
        // Done. Transition to browse or error.
        state = sdReady ? STATE_BROWSE : STATE_ERROR;
    }
    break;
```

Key detail: when the mount fails, set `loadingPct = 100` to jump to the end without
waiting through the remaining animation frames. The user sees the bar jump to full and
the error screen appears immediately.

### Progress bar drawing (on 128×64 monochrome)

```cpp
static void drawProgressBar(uint8_t pct) {
    int16_t barW = 100;
    int16_t barX = (GFX_WIDTH - barW) / 2;   // center horizontally
    int16_t barY = 40;
    int16_t barH = 8;

    gfx::drawRect(barX - 1, barY - 1, barW + 2, barH + 2, GFX_WHITE);  // border
    int16_t fill = (barW * pct) / 100;
    if (fill > 0)
        gfx::fillRect(barX, barY, fill, barH, GFX_WHITE);              // fill
}
```

## File browser with scrollable list

On a 128×64 display you can show ~6 items (8px each) below a 10px header.

### Collect files into a static array

Read the root directory once on mount success, storing names and metadata:

```cpp
#define MAX_FILES 64

struct FileEntry {
    char name[13];       // 8.3 + null
    bool isDir;
    uint32_t size;
};

static FileEntry files[MAX_FILES];
static uint8_t  fileCount = 0;

static void collectFiles() {
    fileCount = 0;
    char name[13]; bool isDir; uint32_t size;
    fat::openDir("/");
    while (fat::readDir(name, &isDir, &size) && fileCount < MAX_FILES) {
        // copy into files[fileCount]
        fileCount++;
    }
}
```

### Scroll with cursor

Track a cursor index and a scroll offset. Keep the cursor visible by adjusting
`scrollOff` when it moves beyond the visible window:

```cpp
static uint8_t cursor    = 0;   // which file is selected
static uint8_t scrollOff = 0;   // first visible file index

if (input::justPressed(PIN_BTN_UP)) {
    if (cursor > 0) {
        cursor--;
        if (cursor < scrollOff)
            scrollOff = cursor;        // scroll up to keep cursor visible
    }
}
if (input::justPressed(PIN_BTN_DOWN)) {
    if (cursor + 1 < fileCount) {
        cursor++;
        if (cursor >= scrollOff + MAX_VISIBLE)
            scrollOff = cursor - MAX_VISIBLE + 1;  // scroll down
    }
}
```

### Render the visible slice

```cpp
for (uint8_t i = 0; i < MAX_VISIBLE; i++) {
    uint8_t fi = scrollOff + i;
    if (fi >= fileCount) break;

    int16_t y = LIST_Y + i * ITEM_H;
    FileEntry *e = &files[fi];

    if (fi == cursor) {
        gfx::setCursor(0, y);
        gfx::print(">");               // selection indicator
    }
    gfx::setCursor(8, y);
    if (e->isDir) gfx::print("[");
    gfx::print(e->name);
    if (e->isDir) gfx::print("]");
    // ... right-align file size
}
```

## File info and simulated execution

**File info screen** (A on a file): shows name, type (File/Directory), formatted size.
A second press of A triggers execution. B returns to browse.

**Executing screen**: for a simulated "execute" action (since a microcontroller can't
execute arbitrary binaries), read and display the first 8 bytes of the file as hex:

```cpp
fat::Result r = fat::open(path);
if (r == fat::OK) {
    uint8_t header[16];
    uint16_t br;
    fat::read(header, 16, &br);
    fat::close();
    // Display header bytes as hex
    for (uint8_t i = 0; i < 8 && i < br; i++) {
        char hex[3];
        hex[0] = "0123456789ABCDEF"[header[i] >> 4];
        hex[1] = "0123456789ABCDEF"[header[i] & 0x0F];
        hex[2] = '\0';
        gfx::print(hex);
    }
}
```

Draw a full progress bar (100%) on this screen to indicate "loading complete."

## Error screen with retry

When storage is unavailable, show a clear message and a retry prompt. The B button
resets `loadingPct = 0` and returns to `STATE_LOADING`, which re-runs the mount:

```cpp
case STATE_ERROR:
    gfx::clear();
    drawHeader("No SD Card");
    gfx::setCursor(4, LIST_Y + 4);
    gfx::print("No SD card detected.");
    gfx::setCursor(4, LIST_Y + 14);
    gfx::print("Insert a FAT-formatted");
    gfx::setCursor(4, LIST_Y + 22);
    gfx::print("SD card and press B");
    gfx::setCursor(4, LIST_Y + 30);
    gfx::print("to try again.");
    // Footer: "B:Retry"
    if (input::justPressed(PIN_BTN_B)) {
        loadingPct = 0;
        state = STATE_LOADING;
    }
    break;
```

## Audio feedback integration

Use a beep library to provide audio cues — especially useful when the display isn't
working and you need to confirm the MCU is running:

```cpp
void setup() {
    beep::init();
    beep::beep_startup();        // "I'm alive" beep

    gfx::init();
    beep::beep_startup();        // "display init completed" beep
    // ...
}
```

In the error state, add an attention-getting error beep sequence (3 short beeps):

```cpp
case STATE_ERROR:
    beep::beep_error();          // 3 rapid beeps
    // ... draw error screen ...
```

This gives the user an immediate audio indicator that something is wrong, even if
they're not looking at the display.

## Header/footer convention

Use consistent header (top 10px with a horizontal rule) and footer (bottom 9px with a
horizontal rule) across all screens:

```
┌─────────────────────────┐
│ Title              (1px)│
│─────────────────────────│ ← drawFastHLine(0, 10, 128, WHITE)
│                         │
│     screen content      │
│                         │
│─────────────────────────│ ← drawFastHLine(0, 55, 128, WHITE)
│ A:Action  B:Back   (7px)│
└─────────────────────────┘
```

This gives the user a consistent mental model: the top bar is "where am I," the bottom
bar is "what can I do."

## Size formatting helper

Format file sizes in human-readable form on a memory-constrained MCU. Avoid `printf`
(which pulls in a large library). Use a simple function that produces "123B", "45K",
or ">1M":

```cpp
static void formatSize(char *buf, uint32_t size) {
    if (size < 1024) {
        // output as decimal bytes: "512B"
    } else if (size < 1024 * 1024) {
        // output as decimal KB: "32K"
    } else {
        buf[0] = '>'; buf[1] = '1'; buf[2] = 'M'; buf[3] = '\0';
    }
}
```

Right-align the size string at the right edge of the 128px display (e.g. `gfx::setCursor(102, y)`).
