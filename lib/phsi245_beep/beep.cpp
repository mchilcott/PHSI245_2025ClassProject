#include "beep.h"
#include <Arduino.h>
#include <HAL.h>

namespace beep {

// Melody state (non-blocking playback)
static const uint8_t *melodyNotes = nullptr;
static uint16_t      melodyIndex = 0;
static uint32_t      noteStartMs = 0;
static uint16_t      noteFreq    = 0;
static uint16_t      noteDur     = 0;

void init()
{
    pinMode(PIN_BEEP, OUTPUT);
    digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
}

// Busy-wait `us` microseconds using the SysTick counter. 32-bit only: the
// core's delayMicroseconds() does 64-bit math and drags in __udivdi3, but
// (us-1) * ticksPerMs stays below 2^32 for us < ~89 ms at 48 MHz — which
// covers every period the beep produces (freq >= ~12 Hz).
static void delayUs(uint32_t us)
{
    uint32_t tickPerMs = SysTick->CMP + 1;
    uint32_t nbTicks   = ((us - 1) * tickPerMs) / 1000;
    uint32_t elapsed   = 0;
    uint32_t oldTicks  = SysTick->CNT;
    while (elapsed < nbTicks) {
        uint32_t t = SysTick->CNT;
        elapsed += (oldTicks <= t) ? t - oldTicks : tickPerMs - oldTicks + t;
        oldTicks = t;
    }
}

void beep_ms(uint16_t duration_ms)
{
    // Toggle at ~2 kHz for audible beep
    uint32_t end = millis() + duration_ms;
    while (millis() < end) {
        digitalWrite(PIN_BEEP, LOW);
        delayUs(250);
        digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
        delayUs(250);
    }
}

void beep_error()
{
    for (uint8_t i = 0; i < 3; i++) {
        beep_ms(100);
        delay(100);
    }
}

void beep_startup()
{
    beep_ms(50);
}

void tone(uint16_t freqHz, uint16_t durationMs)
{
    if (freqHz == 0) {
        digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
        delay(durationMs);
        return;
    }

    uint32_t halfPeriodUs = 500000UL / freqHz;  // half period in microseconds
    uint32_t end = millis() + durationMs;

    while (millis() < end) {
        digitalWrite(PIN_BEEP, LOW);
        delayUs(halfPeriodUs);
        digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
        delayUs(halfPeriodUs);
    }
}

bool playMelody(const uint8_t *notes)
{
    // Start a new melody
    if (melodyNotes != notes) {
        melodyNotes = notes;
        melodyIndex = 0;
        noteFreq = 0;
        noteDur  = 0;
        noteStartMs = millis();
    }

    if (!melodyNotes) return false;

    // Check if current note is done
    uint32_t now = millis();
    if (noteDur > 0 && (now - noteStartMs) >= noteDur) {
        // Move to next note
        melodyIndex++;
        noteStartMs = now;

        // Speaker off between notes (small gap)
        digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
    }

    // Load next note if needed
    if (noteDur == 0 || (now - noteStartMs) < 2) {
        uint8_t fh = pgm_read_byte(&melodyNotes[melodyIndex]);
        uint8_t fl = pgm_read_byte(&melodyNotes[melodyIndex + 1]);
        uint8_t dh = pgm_read_byte(&melodyNotes[melodyIndex + 2]);
        uint8_t dl = pgm_read_byte(&melodyNotes[melodyIndex + 3]);

        noteFreq = ((uint16_t)fh << 8) | fl;
        noteDur  = ((uint16_t)dh << 8) | dl;

        // Check for end marker
        if (noteFreq == 0 && noteDur == 0) {
            melodyNotes = nullptr;
            digitalWrite(PIN_BEEP, PIN_BEEP_OFF);
            return false;
        }

        noteStartMs = now;
    }

    // Toggle the speaker at the note frequency
    if (noteFreq > 0 && noteDur > 0) {
        uint32_t halfUs = 500000UL / noteFreq;
        uint32_t phase = (now - noteStartMs) * 1000UL;  // ms → us
        bool high = ((phase / halfUs) & 1) == 0;
        digitalWrite(PIN_BEEP, high ? LOW : PIN_BEEP_OFF);
    }

    return true;
}

} // namespace beep
