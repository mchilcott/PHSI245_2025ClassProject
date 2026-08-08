#include "random.h"
#include <HAL.h>

namespace rng {

static uint32_t state = 1;

void init()
{
    // Seed from touch-key ADC noise on unused channels.
    // Read multiple ADC channels and mix the low bits for entropy.
    uint32_t seed = 0;
    for (uint8_t ch = 0; ch < 10; ch++) {
        seed ^= (uint32_t)Touch_Key_Adc(ch) << (ch & 3);
    }
    // Ensure seed is non-zero (xorshift requires it)
    if (seed == 0) seed = 123456789;
    state = seed;
}

uint16_t next(uint16_t max)
{
    // xorshift32 — fast, 32-bit state, good distribution
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;

    // Map to [0, max-1] without division. Take the top 16 bits of x
    // before multiplying so the product fits in 32 bits; a direct
    // (x * max) >> 16 would wrap modulo 2^32 and ignore max.
    return (uint16_t)(((uint32_t)(x >> 16) * max) >> 16);
}

uint16_t next(uint16_t min, uint16_t max)
{
    if (min >= max) return min;
    return min + next((uint16_t)(max - min + 1));
}

} // namespace rng
