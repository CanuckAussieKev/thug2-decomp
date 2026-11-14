// NOT ORIGINAL - the vhs, retro and watercolour art styles: their settings, the passes handed to each style's file, the
// retro geometry's switch (vulkan_style_film.h; docs/design/vulkan.md "Art style").
#include "gpu_vulkan/vulkan_style_film.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style_kit.h"

namespace render::vulkan {

namespace {

constexpr float kLeastLines = 60.0F;
constexpr float kMostLines = 720.0F;

struct FilmState {
    FilmSettings settings;
    bool in_world = false; // between a view's view_begins and its world_drawn (the retro geometry's draws)
    uint64_t passes = 0;
};

FilmState& film()
{
    static FilmState state;
    return state;
}

// The retro geometry's switch: on from a view's start to its whole world drawn (the 2D after it untouched).
class RetroListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kStyle; }
    void view_begins(const rt::ViewDrawn& /*view*/) override
    {
        const std::scoped_lock lock(backend().mutex);
        film().in_world = true;
    }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void world_drawn(const rt::ViewDrawn& /*view*/) override
    {
        const std::scoped_lock lock(backend().mutex);
        film().in_world = false;
    }
};

RetroListener& retro_listener()
{
    static RetroListener instance;
    return instance;
}

} // namespace

void set_film_settings(const FilmSettings& settings)
{
    {
        const std::scoped_lock lock(backend().mutex);
        film().settings = settings;
        film().in_world = false;
    }
    if (settings.style == ArtStyle::kRetro) {
        rt::add_view_listener(&retro_listener());
    } else {
        rt::remove_view_listener(&retro_listener());
    }
}

FilmSettings film_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return film().settings;
}

bool record_film_pass(Backend& state, const rt::ViewDrawn& view, const StyleSettings& settings, const FilmTarget& target)
{
    FilmState& own = film();
    if (!is_film_look(settings.style) || !(settings.strength > 0.0F) || target.colour == nullptr ||
        core::at(target.shown, 2) == 0 || core::at(target.shown, 3) == 0) {
        return false;
    }
    const float strength = std::min(settings.strength, 1.0F);
    bool recorded = false;
    switch (settings.style) {
    case ArtStyle::kVhs:
        recorded = film_kit::record_vhs(state, view, own.settings.vhs, strength, target);
        break;
    case ArtStyle::kFisheyeVhs: {
        // (the vhs under the real lens - vulkan_fisheye.h, before this pass: its own barrel and the corners' darkening
        // that goes with it left out; every other knob as set)
        VhsSettings vhs = own.settings.vhs;
        vhs.fisheye = 0.0F;
        recorded = film_kit::record_vhs(state, view, vhs, strength, target);
        break;
    }
    case ArtStyle::kRetro:
        recorded = film_kit::record_retro(state, own.settings.retro, strength, target);
        break;
    case ArtStyle::kWatercolour:
        recorded = film_kit::record_watercolour(state, own.settings.watercolour, strength, target);
        break;
    default:
        break;
    }
    own.passes += recorded ? 1 : 0;
    return recorded;
}

uint64_t film_passes() { return film().passes; }

bool read_film_paint(Backend& state, std::vector<uint16_t>* halves, Extent* size)
{
    return film_kit::read_watercolour_paint(state, halves, size);
}

bool retro_geometry()
{
    const FilmState& own = film();
    return own.in_world && own.settings.style == ArtStyle::kRetro;
}

uint8_t retro_key_bit(uint32_t fvf) { return retro_geometry() && !fvf_pre_transformed(fvf) ? 1U : 0U; }

std::array<int32_t, 2> retro_cells(float lines, Extent viewport)
{
    const float rows = std::isfinite(lines) ? std::clamp(std::round(lines), kLeastLines, kMostLines) : kRetroLines;
    const float aspect =
        viewport.height > 0 ? static_cast<float>(viewport.width) / static_cast<float>(viewport.height) : 1.0F;
    const int32_t across = std::max(1, static_cast<int32_t>(std::lround(rows * aspect)));
    return {across, static_cast<int32_t>(rows)};
}

std::array<float, 4> retro_vertex_uniform(Extent viewport)
{
    const RetroSettings& settings = film().settings.retro;
    const std::array<int32_t, 2> cells = retro_cells(settings.lines, viewport);
    const auto unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 1.0F; };
    return {static_cast<float>(core::at(cells, 0)), static_cast<float>(core::at(cells, 1)), unit(settings.snap),
            unit(settings.affine)};
}

void release_film_looks(Backend& state)
{
    film_kit::release_vhs(state);
    film_kit::release_retro(state);
    film_kit::release_watercolour(state);
    film_kit::release_common(state);
    film().in_world = false;
}

namespace film_kit {

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

} // namespace film_kit

} // namespace render::vulkan
