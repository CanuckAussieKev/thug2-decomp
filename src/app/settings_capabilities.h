#pragma once

// NOT ORIGINAL - this machine's capabilities for the settings model (engine/config/settings_model.h Capabilities): the
// Vulkan device the game would draw with (its probe - asked before the window, whatever renderer the ini picks, so the
// Options screen knows what switching would bring), the build (64-bit), and the data in the game folder (surface maps
// made by tools/pbr_maps, the detail-material library, the texture sets installed). Found once at start-up.

#include <string>

#include "engine/config/settings_model.h"

namespace app {

struct FoundCapabilities {
    config::Capabilities caps;
    std::string device;  // the Vulkan device's name (empty: none)
    std::string problem; // why Vulkan can't draw here (empty when it can)
};

// Found and handed to the settings model (config::set_capabilities); asked again later, the first answer.
const FoundCapabilities& find_capabilities();

} // namespace app
