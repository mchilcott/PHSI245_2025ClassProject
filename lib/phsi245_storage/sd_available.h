#pragma once

#include <stdbool.h>

namespace storage {

// Returns true if an SD card is present and mounted.
// Mounts the card on first call, returns result.
bool sdAvailable();

} // namespace storage
