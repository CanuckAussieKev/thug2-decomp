// NOT ORIGINAL - GPU crash markers (gpu_markers.h): the sink and the passes' names.
#include "engine/gfx/gpu_markers.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_scene.h"

namespace render::gpu_markers {

namespace {

std::atomic<Sink>& sink_slot()
{
    static std::atomic<Sink> sink{nullptr};
    return sink;
}

// The one table of the passes' names: the view listeners at their orders (rt_scene.h ViewOrder - several listeners
// sharing an order are named together; the ReSTIR passes sit either side of the lamps), then the Gpu's sections.
constexpr std::array kListeners{
    ListenerName{.order = rt::ViewOrder::kSky, .name = "sky / Ray Reconstruction's albedo guide"},
    ListenerName{.order = rt::ViewOrder::kSurfaceInputs, .name = "per-surface light inputs"},
    ListenerName{.order = rt::ViewOrder::kShadows, .name = "ray-traced sun shadows"},
    ListenerName{.order = rt::ViewOrder::kOcclusion, .name = "ambient occlusion"},
    ListenerName{.order = rt::ViewOrder::kIndirect, .name = "indirect light (one bounce)"},
    ListenerName{.order = rt::ViewOrder::kLights - 1, .name = "ReSTIR direct light"},
    ListenerName{.order = rt::ViewOrder::kLights, .name = "lamps"},
    ListenerName{.order = rt::ViewOrder::kLights + 1, .name = "ReSTIR indirect light"},
    ListenerName{.order = rt::ViewOrder::kDecals, .name = "decals"},
    ListenerName{.order = rt::ViewOrder::kSurfaceShading, .name = "per-surface shading"},
    ListenerName{.order = rt::ViewOrder::kRain, .name = "rain"},
    ListenerName{.order = rt::ViewOrder::kReflections, .name = "reflections"},
    ListenerName{.order = rt::ViewOrder::kSnow, .name = "snow / paint trails"},
    ListenerName{.order = rt::ViewOrder::kClutter, .name = "clutter"},
    ListenerName{.order = rt::ViewOrder::kGrass, .name = "grass"},
    ListenerName{.order = rt::ViewOrder::kFog, .name = "volumetric fog"},
    ListenerName{.order = rt::ViewOrder::kSun, .name = "sun glare and bloom"},
    ListenerName{.order = rt::ViewOrder::kUpscale, .name = "upscaler"},
    ListenerName{.order = rt::ViewOrder::kCel, .name = "cel outlines"},
    ListenerName{.order = rt::ViewOrder::kCameraEffects, .name = "depth of field / motion blur"},
    ListenerName{.order = rt::ViewOrder::kLens, .name = "fisheye lens"},
    ListenerName{.order = rt::ViewOrder::kToneMapping, .name = "tone mapping"},
    ListenerName{.order = rt::ViewOrder::kStyle, .name = "art style"},
    ListenerName{.order = rt::ViewOrder::kGrade, .name = "colour grade"},
    ListenerName{.order = rt::ViewOrder::kFrameGeneration, .name = "frame generation's copy"},
};

constexpr std::array<const char*, section::kCount> kSections{
    "",
    "stream",
    "uploads",
    "world surfaces",
    "ray tracing scene builds",
    "present",
    "UI",
    "ReSTIR DI's light grid",
    "ReSTIR DI's candidates and temporal reuse",
    "ReSTIR DI's spatial reuse",
    "ReSTIR GI's radiance cache",
    "ReSTIR GI's temporal reuse",
    "ReSTIR GI's spatial reuse",
    "the fog's air sun map",
    "the fog's froxel light",
    "the fog's froxel integration",
    "the fog's god rays",
    "the fog's composite",
    "the light list's presampled pool",
    "ReSTIR GI's samples",
    "ReSTIR GI's continued paths",
    "ReSTIR DI's primary surfaces",
    "ReSTIR DI's initial candidates",
    "ReSTIR GI's upsample",
};

constexpr std::array<const char*, kPhaseCount> kPhases{
    "the Gpu", "view_begins", "sky_drawn", "opaque_drawn", "world_drawn",
};

} // namespace

void set_sink(Sink sink) { sink_slot().store(sink, std::memory_order_release); }

bool marking() { return sink_slot().load(std::memory_order_acquire) != nullptr; }

void mark(const Mark& mark)
{
    if (const Sink sink = sink_slot().load(std::memory_order_acquire)) {
        sink(mark);
    }
}

std::span<const ListenerName> listener_names() { return kListeners; }

const char* phase_name(Phase phase)
{
    const auto index = static_cast<std::size_t>(phase);
    return index < kPhases.size() ? core::at(kPhases, index) : "?";
}

std::string pass_name(Phase phase, uint16_t id)
{
    if (phase == Phase::kGpu) {
        return id > 0 && id < kSections.size() ? std::string(core::at(kSections, id))
                                               : text::format("section %u", static_cast<unsigned>(id));
    }
    for (const ListenerName& listener : kListeners) {
        if (listener.order == static_cast<int>(id)) {
            return text::format("%s (%u)", listener.name, static_cast<unsigned>(id));
        }
    }
    return text::format("the listener at order %u", static_cast<unsigned>(id));
}

std::string name(Phase phase, uint16_t id)
{
    return text::format("%s: %s", phase_name(phase), pass_name(phase, id).c_str());
}

} // namespace render::gpu_markers
