#include "software.h"
#include <cstdio>

int main() {
    using namespace NickelModManager;
    // Outside Nickel there is no current device. The manager defaults to
    // Kobo when the active software cannot be identified.
    if (detectSoftwareFamily() != SoftwareFamily::Kobo ||
        softwareFamilyId(SoftwareFamily::Kobo) != "kobo" ||
        softwareFamilyId(SoftwareFamily::Tolino) != "tolino") {
        std::fprintf(stderr, "Software family fallback or identifiers are incorrect\n");
        return 1;
    }
    return 0;
}
