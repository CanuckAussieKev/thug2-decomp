#pragma once

#include <cstdint>

namespace render {

// NOT ORIGINAL (the class; its fields were the original's globals): what the graphics device can do, as the backend
// found when it made the device - stream offsets, a blend factor, border addressing, the most anisotropy, pixel
// shaders, and whether it is a Radeon 8500 (its own workarounds). The backend fills it; the draw code reads it.
// Held by the renderer.
class DeviceCaps {
public:
    [[nodiscard]] bool stream_offset_supported() const { return stream_offset_supported_; }
    void set_stream_offset_supported(bool value) { stream_offset_supported_ = value; }
    [[nodiscard]] bool blend_factor_supported() const { return blend_factor_supported_; }
    void set_blend_factor_supported(bool value) { blend_factor_supported_ = value; }
    [[nodiscard]] bool border_addressing_supported() const { return border_addressing_supported_; }
    void set_border_addressing_supported(bool value) { border_addressing_supported_ = value; }
    [[nodiscard]] uint32_t max_anisotropy() const { return max_anisotropy_; }
    void set_max_anisotropy(uint32_t value) { max_anisotropy_ = value; }
    [[nodiscard]] bool radeon8500() const { return radeon8500_; }
    void set_radeon8500(bool value) { radeon8500_ = value; }
    [[nodiscard]] bool shaders_available() const { return shaders_available_; }
    void set_shaders_available(bool value) { shaders_available_ = value; }

private:
    bool stream_offset_supported_{};
    bool blend_factor_supported_{};
    bool border_addressing_supported_{};
    uint32_t max_anisotropy_{};
    bool radeon8500_{};
    bool shaders_available_{};
};

} // namespace render
