#pragma once

#include <stdint.h>

#define GFX_WIDTH  128
#define GFX_HEIGHT 64
#define GFX_BLACK  0
#define GFX_WHITE  1
#define GFX_TRANSPARENT 0xFF  // sentinel for "no background fill"

namespace gfx {

extern uint8_t buffer[GFX_WIDTH * GFX_HEIGHT / 8];

void init();
void clear(uint8_t color = GFX_BLACK);
void display();
void display(bool clearBuffer);

void drawPixel(int16_t x, int16_t y, uint8_t color = GFX_WHITE);
uint8_t getPixel(int16_t x, int16_t y);

void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color = GFX_WHITE);
void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color = GFX_WHITE);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color = GFX_WHITE);

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color = GFX_WHITE);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color = GFX_WHITE);

void drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color = GFX_WHITE);
void fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color = GFX_WHITE);

void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, uint8_t color = GFX_WHITE);

// --- Sprite draw modes ---
#define GFX_SPRITE_UNMASKED    0   // overwrite: image copied directly
#define GFX_SPRITE_MASKED      1   // external mask: separate image+mask arrays
#define GFX_SPRITE_PLUS_MASK   2   // interleaved image+mask byte pairs
#define GFX_SPRITE_SELF_MASKED 3   // self-masked: 1=draw, 0=transparent
#define GFX_SPRITE_ERASE       4   // erase: 1=clear, 0=transparent

// Raw sprite (explicit width/height, no embedded size header)
void drawSprite(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, uint8_t mode = GFX_SPRITE_UNMASKED);

// External-mask sprite (separate image+mask, explicit w/h)
void drawSpriteMasked(int16_t x, int16_t y, const uint8_t *bitmap,
                      const uint8_t *mask, int16_t w, int16_t h);

// Data-first sprite sheet (width/height in first 2 bytes)
void drawSpriteSheet(int16_t x, int16_t y, const uint8_t *sheet, uint8_t frame,
                     uint8_t mode = GFX_SPRITE_UNMASKED);

// Data-first sprite sheet with external mask
void drawSpriteSheetMasked(int16_t x, int16_t y, const uint8_t *sheet,
                           const uint8_t *maskSheet, uint8_t frame,
                           uint8_t maskFrame = 0);

// Data-first plus-mask sprite (interleaved img+mask within sheet)
void drawPlusMask(int16_t x, int16_t y, const uint8_t *sheet, uint8_t frame);

// Text output (5x7 font)
void setCursor(int16_t x, int16_t y);
void setTextSize(uint8_t s);
void setTextColor(uint8_t c);
void setTextBgColor(uint8_t c);  // GFX_TRANSPARENT = no background (default)
void print(const char *str);
void print(int16_t n);
void print(uint16_t n);

// Right-aligned fixed-width number rendering (e.g. scores).
// Draws `n` at (x, y) with exactly `digits` columns, padding with spaces.
// The number is clipped if it exceeds the column count.
// Example: gfx::drawNumber(120, 0, score, 5) — 5-digit score, right edge at x=120
void drawNumber(int16_t x, int16_t y, uint16_t n, uint8_t digits);

// Simple vertical text menu.
// Renders `count` items starting at (x, y), highlighting `cursor` with ">".
// Items are spaced `lineH` pixels apart (use 12 for textSize 1, 24 for textSize 2).
void drawMenu(int16_t x, int16_t y, const char *items[], uint8_t count,
              uint8_t cursor, uint8_t lineH = 12);

// --- Scrollable lists ---

// Compute a scroll offset so `cursor` (0..count-1) stays visible in a window
// of `visible` rows. Returns 0 when everything fits. Callers persist the
// returned offset between frames (see drawScrollMenu).
uint8_t clampScroll(uint8_t cursor, uint8_t count, uint8_t visible);

// Scrollable menu. Renders at most `visible` rows starting at *scroll; *scroll
// is auto-clamped so the highlighted `cursor` row stays visible. The cursor
// row is prefixed with ">", item text at (x + 8, ...). Content is drawn below
// (x, y) and the caller is responsible for keeping the area clear of headers
// or footers. lineH defaults to 8px (textSize 1).
void drawScrollMenu(int16_t x, int16_t y, const char *items[], uint8_t count,
                    uint8_t cursor, uint8_t visible, uint8_t *scroll,
                    uint8_t lineH = 8);

} // namespace gfx
