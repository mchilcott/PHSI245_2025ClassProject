#include "storage.h"   // for fat::mount, sd::DEVICE
#include "sd_available.h"

namespace storage {

static bool sdReady = false;

bool sdAvailable()
{
    if (sdReady) return true;

    fat::Result r = fat::mount(&sd::DEVICE);
    if (r == fat::OK) {
        sdReady = true;
        return true;
    }
    return false;
}

} // namespace storage
