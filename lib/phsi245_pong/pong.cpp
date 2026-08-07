#include "pong.h"
#include <HAL.h>
#include "gfx.h"
#include "input.h"
#include "beep.h"
#include "random.h"

namespace pong {

static int16_t ballX   = 64, ballY   = 32;
static int8_t  ballDX  = 2,  ballDY  = 1;
static int16_t paddleY = 24;
static uint8_t score   = 0;
static bool    playing = false;

// Ball speed: frames between moves. Starts slow (8), speeds up with score.
static uint8_t ballSpeed = 8;
static uint8_t ballTick  = 0;

static const int16_t PADDLE_H = 16;
static const int16_t PADDLE_X = 120;

void init()
{
    ballX = 64; ballY = 32;
    ballDX = 1; ballDY = 1;
    paddleY = 24;
    score = 0;
    playing = false;
    ballSpeed = 4;
    ballTick  = 0;
}

static void randomDirection()
{
    ballDX = 1;                            // 1 pixel per tick
    ballDY = (rng::next(2) ? 1 : -1);     // random up or down
}

bool isPlaying()
{
    return playing;
}

void update()
{
    if (!playing) {
        if (input::justPressed(PIN_BTN_A)) {
            ballX = 64; ballY = 32;
            randomDirection();
            paddleY = 24;
            score = 0;
            ballSpeed = 4;
            ballTick  = 0;
            playing = true;
        }
        return;
    }

    ballTick++;
    if (ballTick < ballSpeed) return;
    ballTick = 0;

    ballX += ballDX;
    ballY += ballDY;

    if (ballY <= 1)  { ballY = 1;  ballDY = -ballDY; }
    if (ballY >= 62) { ballY = 62; ballDY = -ballDY; }
    if (ballX <= 1)  { ballX = 1;  ballDX = -ballDX; }

    if (ballX >= PADDLE_X - 2 && ballX <= PADDLE_X + 1 &&
        ballY >= paddleY && ballY <= paddleY + PADDLE_H) {
        ballX = PADDLE_X - 2;
        ballDX = -ballDX;
        score++;
        if (ballSpeed > 2) ballSpeed--;
        beep::tone(880, 20);
    }

    if (ballX >= 127) {
        playing = false;
        beep::tone(220, 120);
    }

    if (input::pressed(PIN_BTN_UP)   && paddleY > 2)           paddleY -= 2;
    if (input::pressed(PIN_BTN_DOWN) && paddleY < 62 - PADDLE_H) paddleY += 2;

    if (input::pressed(PIN_BTN_B)) {
        playing = false;
    }
}

void draw()
{
    if (!playing) {
        gfx::clear();
        gfx::drawFastHLine(0, 9, GFX_WIDTH, GFX_WHITE);
        gfx::setCursor(20, 1);
        gfx::print("PONG");
        gfx::setCursor(14, 22);
        gfx::print("UP / DOWN: move");
        gfx::setCursor(14, 32);
        gfx::print("A: start game");
        gfx::setCursor(14, 42);
        gfx::print("B: back");
        return;
    }

    gfx::clear();
    gfx::drawPixel(ballX, ballY, GFX_WHITE);
    gfx::drawFastVLine(PADDLE_X, paddleY, PADDLE_H, GFX_WHITE);

    // Walls (top, left, bottom)
    gfx::drawFastHLine(0, 0, GFX_WIDTH, GFX_WHITE);
    gfx::drawFastVLine(0, 0, GFX_HEIGHT, GFX_WHITE);
    gfx::drawFastHLine(0, GFX_HEIGHT - 1, GFX_WIDTH, GFX_WHITE);

    gfx::drawNumber(120, GFX_HEIGHT - 7, score, 3);
    gfx::setCursor(90, GFX_HEIGHT - 7);
    gfx::print("B:Quit");
}

} // namespace pong
