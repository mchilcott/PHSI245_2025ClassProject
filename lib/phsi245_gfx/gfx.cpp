#include "gfx.h"
#include <HAL.h>
#include <Arduino.h>

namespace gfx {

uint8_t buffer[GFX_WIDTH * GFX_HEIGHT / 8];

// --- Text state ---
static int16_t cursorX = 0;
static int16_t cursorY = 0;
static uint8_t textSize = 1;
static uint8_t textColor = GFX_WHITE;
static uint8_t textBgColor = GFX_TRANSPARENT;

// --- 5x7 font (ASCII 32-127, 96 glyphs × 5 bytes) ---
static const PROGMEM uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // (space)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // backslash
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ->
    0x08, 0x1C, 0x2A, 0x08, 0x08, // <-
};

// --- Display init ---

void init()
{
    i2c_init();

    // SSD1306 init sequence
    static const PROGMEM uint8_t initSeq[] = {
        0xAE,           // display off
        0xD5, 0xF0,     // clock div
        0xA8, 0x3F,     // multiplex
        0xD3, 0x00,     // display offset
        0x40,           // start line
        0x8D, 0x14,     // charge pump
        0xA1,           // segment remap
        0xC8,           // COM scan direction
        0xDA, 0x12,     // COM pins
        0x81, 0xCF,     // contrast
        0xD9, 0xF1,     // precharge
        0xDB, 0x40,     // VCOM detect
        0xA4,           // display on (from RAM)
        0xA6,           // normal (not inverted)
        0xAF,           // display on
        0x20, 0x00,     // horizontal addressing
    };

    delay(50);
    i2c_start();
    i2c_send_byte(0x00); // command mode
    for (uint8_t i = 0; i < sizeof(initSeq); i++)
        i2c_send_byte(pgm_read_byte(initSeq + i));
    i2c_stop();

    clear();
    display();
}

void clear(uint8_t color)
{
    uint8_t fill = (color == GFX_WHITE) ? 0xFF : 0x00;
    for (uint16_t i = 0; i < sizeof(buffer); i++)
        buffer[i] = fill;
}

void display()
{
    for (uint16_t i = 0; i < sizeof(buffer); ) {
        i2c_start();
        i2c_send_byte(0x40); // data mode
        for (uint8_t x = 0; x < 16; x++, i++)
            i2c_send_byte(buffer[i]);
        i2c_stop();
    }
}

void display(bool clearBuffer)
{
    display();
    if (clearBuffer)
        clear();
}

// --- Pixel ---

void drawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT)
        return;
    uint16_t idx = ((y >> 3) * GFX_WIDTH) + x;
    uint8_t bit = 1 << (y & 7);
    if (color == GFX_WHITE)
        buffer[idx] |= bit;
    else
        buffer[idx] &= ~bit;
}

uint8_t getPixel(int16_t x, int16_t y)
{
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT)
        return 0;
    return (buffer[((y >> 3) * GFX_WIDTH) + x] >> (y & 7)) & 1;
}

// --- Lines ---

void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color)
{
    if (x < 0 || x >= GFX_WIDTH) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > GFX_HEIGHT) h = GFX_HEIGHT - y;
    if (h <= 0) return;

    for (int16_t i = 0; i < h; i++)
        drawPixel(x, y + i, color);
}

void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color)
{
    if (y < 0 || y >= GFX_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > GFX_WIDTH) w = GFX_WIDTH - x;
    if (w <= 0) return;

    uint16_t idx = ((y >> 3) * GFX_WIDTH) + x;
    uint8_t mask = 1 << (y & 7);
    for (int16_t i = 0; i < w; i++) {
        if (color == GFX_WHITE)
            buffer[idx + i] |= mask;
        else
            buffer[idx + i] &= ~mask;
    }
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int8_t sx = (x0 < x1) ? 1 : -1;
    int8_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// --- Rectangles ---

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_WIDTH)  w = GFX_WIDTH - x;
    if (y + h > GFX_HEIGHT) h = GFX_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int16_t i = 0; i < h; i++)
        drawFastHLine(x, y + i, w, color);
}

// --- Circles ---

void drawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    drawPixel(x0, y0 + r, color);
    drawPixel(x0, y0 - r, color);
    drawPixel(x0 + r, y0, color);
    drawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void fillCircle(int16_t x0, int16_t y0, int16_t r, uint8_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    drawFastVLine(x0, y0 - r, 2 * r + 1, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawFastVLine(x0 + x, y0 - y, 2 * y + 1, color);
        drawFastVLine(x0 - x, y0 - y, 2 * y + 1, color);
        drawFastVLine(x0 + y, y0 - x, 2 * x + 1, color);
        drawFastVLine(x0 - y, y0 - x, 2 * x + 1, color);
    }
}

// --- Bitmap ---

void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, uint8_t color)
{
    int16_t byteWidth = (w + 7) / 8;
    uint8_t byte = 0;

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (i & 7) {
                byte <<= 1;
            } else {
                byte = pgm_read_byte(bitmap + (j * byteWidth) + (i / 8));
            }
            if (byte & 0x80) {
                drawPixel(x + i, y + j, color);
            }
        }
    }
}

// --- Sprites ---

void drawSprite(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, uint8_t mode)
{
    if (w <= 0 || h <= 0) return;
    int16_t byteWidth = (w + 7) / 8;

    for (int16_t j = 0; j < h; j++) {
        int16_t py = y + j;
        if (py < 0 || py >= GFX_HEIGHT) continue;
        uint16_t bufRowOff = (py >> 3) * GFX_WIDTH;
        uint8_t bufBit = 1 << (py & 7);

        for (int16_t i = 0; i < w; i++) {
            int16_t px = x + i;
            if (px < 0 || px >= GFX_WIDTH) continue;

            uint8_t spriteByte = pgm_read_byte(bitmap + (j * byteWidth) + (i >> 3));
            uint8_t spriteBit = spriteByte & (0x80 >> (i & 7));

            if (!spriteBit) continue;

            uint16_t bufIdx = bufRowOff + px;

            switch (mode) {
            case GFX_SPRITE_UNMASKED:
                if (spriteBit)
                    buffer[bufIdx] |= bufBit;
                else
                    buffer[bufIdx] &= ~bufBit;
                break;
            case GFX_SPRITE_SELF_MASKED:
                if (spriteBit)
                    buffer[bufIdx] |= bufBit;
                break;
            case GFX_SPRITE_ERASE:
                if (spriteBit)
                    buffer[bufIdx] &= ~bufBit;
                break;
            }
        }
    }
}

void drawSpriteMasked(int16_t x, int16_t y, const uint8_t *bitmap,
                      const uint8_t *mask, int16_t w, int16_t h)
{
    if (w <= 0 || h <= 0) return;
    int16_t byteWidth = (w + 7) / 8;

    for (int16_t j = 0; j < h; j++) {
        int16_t py = y + j;
        if (py < 0 || py >= GFX_HEIGHT) continue;
        uint16_t bufRowOff = (py >> 3) * GFX_WIDTH;
        uint8_t bufBit = 1 << (py & 7);

        for (int16_t i = 0; i < w; i++) {
            int16_t px = x + i;
            if (px < 0 || px >= GFX_WIDTH) continue;

            uint8_t maskByte = pgm_read_byte(mask + (j * byteWidth) + (i >> 3));
            if (!(maskByte & (0x80 >> (i & 7)))) continue;

            uint8_t imgByte = pgm_read_byte(bitmap + (j * byteWidth) + (i >> 3));
            uint16_t bufIdx = bufRowOff + px;

            if (imgByte & (0x80 >> (i & 7)))
                buffer[bufIdx] |= bufBit;
            else
                buffer[bufIdx] &= ~bufBit;
        }
    }
}

// Data-first sprite sheet helpers

static int16_t sheetWidth(const uint8_t *sheet)
{
    return pgm_read_byte(sheet);
}

static int16_t sheetHeight(const uint8_t *sheet)
{
    return pgm_read_byte(sheet + 1);
}

static const uint8_t *sheetFrame(const uint8_t *sheet, uint8_t frame,
                                 int16_t w, int16_t h)
{
    int16_t byteWidth = (w + 7) / 8;
    return sheet + 2 + (frame * byteWidth * h);
}

void drawSpriteSheet(int16_t x, int16_t y, const uint8_t *sheet, uint8_t frame,
                     uint8_t mode)
{
    int16_t w = sheetWidth(sheet);
    int16_t h = sheetHeight(sheet);
    const uint8_t *data = sheetFrame(sheet, frame, w, h);
    drawSprite(x, y, data, w, h, mode);
}

void drawSpriteSheetMasked(int16_t x, int16_t y, const uint8_t *sheet,
                           const uint8_t *maskSheet, uint8_t frame,
                           uint8_t maskFrame)
{
    int16_t w = sheetWidth(sheet);
    int16_t h = sheetHeight(sheet);
    const uint8_t *data = sheetFrame(sheet, frame, w, h);
    const uint8_t *maskData = sheetFrame(maskSheet, maskFrame, w, h);
    drawSpriteMasked(x, y, data, maskData, w, h);
}

void drawPlusMask(int16_t x, int16_t y, const uint8_t *sheet, uint8_t frame)
{
    int16_t w = sheetWidth(sheet);
    int16_t h = sheetHeight(sheet);
    int16_t byteWidth = (w + 7) / 8;
    // Plus-mask: pairs of (image byte, mask byte), so stride is 2 * byteWidth
    const uint8_t *frameData = sheet + 2 + (frame * byteWidth * h * 2);

    if (w <= 0 || h <= 0) return;

    for (int16_t j = 0; j < h; j++) {
        int16_t py = y + j;
        if (py < 0 || py >= GFX_HEIGHT) continue;
        uint16_t bufRowOff = (py >> 3) * GFX_WIDTH;
        uint8_t bufBit = 1 << (py & 7);

        for (int16_t i = 0; i < w; i++) {
            int16_t px = x + i;
            if (px < 0 || px >= GFX_WIDTH) continue;

            // Plus-mask: row stride is 2 * byteWidth, byte pair at (i>>3)*2
            uint8_t pairIdx = (j * byteWidth * 2) + ((i >> 3) * 2);
            uint8_t maskByte = pgm_read_byte(frameData + pairIdx + 1);
            if (!(maskByte & (0x80 >> (i & 7)))) continue;

            uint8_t imgByte = pgm_read_byte(frameData + pairIdx);
            uint16_t bufIdx = bufRowOff + px;

            if (imgByte & (0x80 >> (i & 7)))
                buffer[bufIdx] |= bufBit;
            else
                buffer[bufIdx] &= ~bufBit;
        }
    }
}

// --- Text ---

void setCursor(int16_t x, int16_t y)
{
    cursorX = x;
    cursorY = y;
}

void setTextSize(uint8_t s)
{
    textSize = (s > 0) ? s : 1;
}

void setTextColor(uint8_t c)
{
    textColor = c;
}

void setTextBgColor(uint8_t c)
{
    textBgColor = c;
}

static void drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t size)
{
    if (c < 32 || c > 127) c = 32;
    c -= 32;

    // Fill character cell background if a background color is set
    if (textBgColor != GFX_TRANSPARENT) {
        fillRect(x, y, 6 * size, 8 * size, textBgColor);
    }

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = pgm_read_byte(font5x7 + (c * 5) + col);
        for (uint8_t row = 0; row < 8; row++) {
            if (line & 1) {
                if (size == 1) {
                    drawPixel(x + col, y + row, color);
                } else {
                    fillRect(x + col * size, y + row * size, size, size, color);
                }
            }
            line >>= 1;
        }
    }
}

void print(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            cursorX = 0;
            cursorY += 8 * textSize;
        } else {
            drawChar(cursorX, cursorY, *str, textColor, textSize);
            cursorX += 6 * textSize;
            if (cursorX + 5 * textSize >= GFX_WIDTH) {
                cursorX = 0;
                cursorY += 8 * textSize;
            }
        }
        str++;
    }
}

void print(int16_t n)
{
    if (n < 0) {
        print("-");
        n = -n;
    }
    print((uint16_t)n);
}

void print(uint16_t n)
{
    if (n == 0) {
        drawChar(cursorX, cursorY, '0', textColor, textSize);
        cursorX += 6 * textSize;
        return;
    }
    char buf[6];
    uint8_t i = 0;
    while (n > 0 && i < 5) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        drawChar(cursorX, cursorY, buf[--i], textColor, textSize);
        cursorX += 6 * textSize;
    }
}

void drawNumber(int16_t x, int16_t y, uint16_t n, uint8_t digits)
{
    // Build the number string right-to-left
    char buf[6];  // max 5 digits + null
    int8_t pos = digits;
    buf[pos] = '\0';
    while (pos > 0) {
        pos--;
        if (n > 0 || pos == (int8_t)(digits - 1)) {
            buf[pos] = '0' + (n % 10);
            n /= 10;
        } else {
            buf[pos] = ' ';  // leading space
        }
    }

    // Right-align: compute pixel width and position cursor
    int16_t charW = 6 * textSize;
    int16_t cx = x - (digits * charW);

    for (uint8_t i = 0; i < digits; i++) {
        drawChar(cx, y, buf[i], textColor, textSize);
        cx += charW;
    }
}

void drawMenu(int16_t x, int16_t y, const char *items[], uint8_t count,
              uint8_t cursor, uint8_t lineH)
{
    int16_t prevX = cursorX, prevY = cursorY;
    for (uint8_t i = 0; i < count; i++) {
        int16_t lineY = y + i * lineH;
        setCursor(x, lineY);
        if (i == cursor)
            print(">");
        setCursor(x + 8, lineY);
        print(items[i]);
    }
    cursorX = prevX;
    cursorY = prevY;
}

uint8_t clampScroll(uint8_t cursor, uint8_t count, uint8_t visible)
{
    if (count <= visible)
        return 0;
    uint8_t maxScroll = count - visible;
    uint8_t s = (cursor >= visible) ? (cursor - visible + 1) : 0;
    return (s > maxScroll) ? maxScroll : s;
}

void drawScrollMenu(int16_t x, int16_t y, const char *items[], uint8_t count,
                    uint8_t cursor, uint8_t visible, uint8_t *scroll,
                    uint8_t lineH)
{
    if (count == 0)
        return;
    uint8_t s = clampScroll(cursor, count, visible);
    if (scroll)
        *scroll = s;
    int16_t prevX = cursorX, prevY = cursorY;
    for (uint8_t i = 0; i < visible && (s + i) < count; i++) {
        int16_t lineY = y + i * lineH;
        setCursor(x, lineY);
        if (s + i == cursor)
            print(">");
        setCursor(x + 8, lineY);
        print(items[s + i]);
    }
    cursorX = prevX;
    cursorY = prevY;
}

} // namespace gfx
