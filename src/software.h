#pragma once
#include <QString>

namespace NickelModManager {
enum class SoftwareFamily { Kobo, Tolino };

// detectSoftwareFamily queries Nickel's active device after its UI is ready.
// Missing symbols or a missing current device default to Kobo. It does not
// infer software from the hardware name, installed fonts or affiliate data.
SoftwareFamily detectSoftwareFamily();

// softwareFamilyId returns the stable manifest condition value.
QString softwareFamilyId(SoftwareFamily family);
} // namespace NickelModManager
