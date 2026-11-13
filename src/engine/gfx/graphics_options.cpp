// NOT ORIGINAL - image-quality options (see graphics_options.h).

#include "engine/gfx/graphics_options.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <string>
#include <vector>

#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "base/core/ints.h"
#include "base/core/no_destroy.h"

namespace render::graphics_options {

constexpr std::size_t kOptionTextSize = 32;
constexpr int kMaxAnisotropy = 16;
constexpr int kMaxMultisample = 16;

namespace {

// NOT ORIGINAL grouping - graphics_options.cpp's state (were globals). Made on first use, never destroyed
// (graphics_options_state()).
struct GraphicsOptionsState {
    Options options_{};
    LiveReload live_reload_ = nullptr;
    CompareBackend compare_{};
};

GraphicsOptionsState& graphics_options_state()
{
    static core::NoDestroy<GraphicsOptionsState> state;
    return state.get();
}

constexpr float kMinScale = 0.25F;
constexpr float kMaxScale = 8.0F;

} // namespace

Options& get_pull() { return graphics_options_state().options_; }

void set_live_reload(LiveReload reload) { graphics_options_state().live_reload_ = reload; }

bool reload_live(const char* key)
{
    const LiveReload reload = graphics_options_state().live_reload_;
    return reload != nullptr && key != nullptr && reload(key);
}

void set_compare_backend(const CompareBackend& backend) { graphics_options_state().compare_ = backend; }

bool compare_prepare(const CompareSettings& overrides, std::vector<std::string>* both_sides)
{
    const CompareBackend& backend = graphics_options_state().compare_;
    return backend.prepare != nullptr && backend.prepare(overrides, both_sides);
}

void compare_apply(int side)
{
    if (const CompareBackend& backend = graphics_options_state().compare_; backend.apply != nullptr) {
        backend.apply(side);
    }
}

void compare_end()
{
    if (const CompareBackend& backend = graphics_options_state().compare_; backend.end != nullptr) {
        backend.end();
    }
}

void read_in()
{
    graphics_options_state().options_.anisotropy = parse_anisotropy(build_info::get_int(build_info::kGraphics, "anisotropy", 0));
    graphics_options_state().options_.msaa = parse_multisample(build_info::get_int(build_info::kGraphics, "msaa", 0));
    std::array<char, kOptionTextSize> text{};
    build_info::fetch_text(build_info::kGraphics, "draw_distance_scale", "1", text.data(), sizeof(text));
    graphics_options_state().options_.draw_distance_scale_ = parse_scale(text.data());
    build_info::fetch_text(build_info::kGraphics, "lod_distance_scale", "1", text.data(), sizeof(text));
    graphics_options_state().options_.lod_distance_scale_ = parse_scale(text.data());
    if ((graphics_options_state().options_.anisotropy != 0) || (graphics_options_state().options_.msaa != 0) ||
        graphics_options_state().options_.draw_distance_scale_ != 1.0F ||
        graphics_options_state().options_.lod_distance_scale_ != 1.0F) {
        get_log("graphics options: anisotropy=%d msaa=%d draw_distance_scale=%.3g lod_distance_scale=%.3g",
                graphics_options_state().options_.anisotropy, graphics_options_state().options_.msaa, graphics_options_state().options_.draw_distance_scale_, graphics_options_state().options_.lod_distance_scale_);
    }
}

int parse_anisotropy(int value) { return value < 2 ? 0 : std::min(value, kMaxAnisotropy); }

int parse_multisample(int value) { return value < 2 ? 0 : std::min(value, kMaxMultisample); }

float parse_scale(const char* text)
{
    if (!text || (*text == '\0')) {
        return 1.0F;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value) || !(value > 0.0)) {
        return 1.0F;
    }
    auto scale = static_cast<float>(value);
    return scale < kMinScale ? kMinScale : std::min(scale, kMaxScale);
}

int device_anisotropy(int option, bool device_can_filter, uint32_t max_anisotropy)
{
    if (option < 2 || !device_can_filter || max_anisotropy < 2) {
        return 0;
    }
    return ints::as_unsigned(option) > max_anisotropy ? static_cast<int>(max_anisotropy) : option;
}

int pick_samples(int option, bool (*supported)(int samples, void* context), void* context)
{
    for (int samples = option; samples >= 2; --samples) {
        if (supported(samples, context)) {
            return samples;
        }
    }
    return 0;
}

float get_scaled(float distance, float scale) { return scale == 1.0F ? distance : distance * scale; }

} // namespace render::graphics_options
