#pragma once

#include <cstdint>
#include <span>
#include <string>

// NOT ORIGINAL - GPU crash markers (round GD): where in a frame the device's work was when the device was lost. The
// renderer's passes - the Gpu's own sections (a submission's uploads and stream, the world's surfaces, the ray-traced
// scene's builds, the present, the UI) and every view listener's call (rt_scene.h ViewListener: view_begins, sky_drawn,
// opaque_drawn, world_drawn, at its ViewOrder) - are marked where they begin and end. A backend that records markers in
// its command buffers sets the sink (the Vulkan Gpu: NVIDIA's diagnostic checkpoints or AMD's buffer markers -
// gpu_vulkan/vulkan_crash_markers.h); with no sink nothing happens. The names come from one table (gpu_markers.cpp).
namespace render::gpu_markers {

// What kind of pass a mark is of: the Gpu's own section (section below), or a view listener's call (its ViewOrder).
enum class Phase : uint8_t {
    kGpu = 0,
    kViewBegins = 1,
    kSky = 2,
    kOpaque = 3,
    kWorld = 4,
};
inline constexpr uint16_t kPhaseCount = 5;

// The Gpu's own sections (Phase::kGpu).
namespace section {
inline constexpr uint16_t kStream = 1;        // a submission's stream begun: its passes and draws, in order
inline constexpr uint16_t kUploads = 2;       // a submission's uploads (before its stream)
inline constexpr uint16_t kWorldSurfaces = 3; // a view's world surfaces and objects (rt::set_world_surfaces_drawing)
inline constexpr uint16_t kRtScene = 4;       // ray tracing's acceleration structures built for the frame
inline constexpr uint16_t kPresent = 5;       // the present: the frame copied into the swapchain's image
inline constexpr uint16_t kUi = 6;            // the UI's draws (Dear ImGui)
// (round PF) the steps inside the path tracer's passes - the profile's (vulkan_pass_profile.h) and a lost device's
// report's finer places
inline constexpr uint16_t kDiGrid = 7;        // ReSTIR DI: the emitters' scan, the light grid's bounds and cells
inline constexpr uint16_t kDiTemporal = 8;    // ReSTIR DI: the candidates and the temporal reuse
inline constexpr uint16_t kDiSpatial = 9;     // ReSTIR DI: the spatial reuse and the light inputs
inline constexpr uint16_t kGiCache = 10;      // ReSTIR GI: the radiance cache's update and resolve
inline constexpr uint16_t kGiTemporal = 11;   // ReSTIR GI: the samples and the temporal reuse
inline constexpr uint16_t kGiSpatial = 12;    // ReSTIR GI: the spatial reuse and the light inputs
inline constexpr uint16_t kFogMap = 13;       // the fog: the air's sun map
inline constexpr uint16_t kFogMarch = 14;     // the fog: the march
inline constexpr uint16_t kFogHistory = 15;   // the fog: the history
inline constexpr uint16_t kFogRays = 16;      // the fog: the god rays' mask and radial passes
inline constexpr uint16_t kFogComposite = 17; // the fog: the composite over the frame
inline constexpr uint16_t kLightPool = 18;    // the light list's presampled pool (vulkan_light_list.h kLightPoolSize)
inline constexpr uint16_t kCount = 19;
} // namespace section

struct Mark {
    Phase phase = Phase::kGpu;
    uint16_t id = 0; // the section, or the listener's order (0 - 1023)
    bool end = false;
};

// Who records the marks (null: nobody). Called on the thread that draws.
using Sink = void (*)(const Mark& mark);
void set_sink(Sink sink);
[[nodiscard]] bool marking();
void mark(const Mark& mark);

// A pass marked from here to the end of the scope.
class Scope {
public:
    Scope(Phase phase, uint16_t id) : phase_(phase), id_(id) { mark({.phase = phase_, .id = id_, .end = false}); }
    ~Scope() { mark({.phase = phase_, .id = id_, .end = true}); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

private:
    Phase phase_;
    uint16_t id_;
};

// A pass's name for the log: "opaque_drawn: ray-traced shadows (100)", "the Gpu: uploads" - its phase's name and the
// pass's own ("ray-traced shadows (100)", "uploads").
[[nodiscard]] std::string name(Phase phase, uint16_t id);
[[nodiscard]] const char* phase_name(Phase phase);
[[nodiscard]] std::string pass_name(Phase phase, uint16_t id);

// The table's view listeners: an order and what runs there (several listeners may share one).
struct ListenerName {
    int order = 0;
    const char* name = "";
};
[[nodiscard]] std::span<const ListenerName> listener_names();

} // namespace render::gpu_markers
