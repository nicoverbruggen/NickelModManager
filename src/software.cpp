#include "software.h"
#include <dlfcn.h>

namespace NickelModManager {
SoftwareFamily detectSoftwareFamily() {
    // These optional, read-only Device methods report the running software,
    // including a device whose owner changed its brand. Query only when the
    // manager opens, after Nickel has created its current device.
    void *library = dlopen("libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!library) {
        return SoftwareFamily::Kobo;
    }
    const auto currentDevice = reinterpret_cast<void *(*)()>(
        dlsym(library, "_ZN6Device16getCurrentDeviceEv"));
    const auto isTolino = reinterpret_cast<bool (*)(const void *)>(
        dlsym(library, "_ZNK6Device8isTolinoEv"));
    SoftwareFamily family = SoftwareFamily::Kobo;
    if (currentDevice && isTolino) {
        if (const void *device = currentDevice()) {
            family = isTolino(device) ? SoftwareFamily::Tolino : SoftwareFamily::Kobo;
        }
    }
    dlclose(library);
    return family;
}

QString softwareFamilyId(SoftwareFamily family) {
    switch (family) {
    case SoftwareFamily::Tolino:
        return "tolino";
    default:
        return "kobo";
    }
}
} // namespace NickelModManager
