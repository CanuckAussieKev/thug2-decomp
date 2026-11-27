// NOT ORIGINAL - (round FA) the lighting's debug views (vulkan_debug_view.h; docs/design/vulkan.md "The lighting's
// debug views"): the setting read, each view's terms, the exposure held, and the flicker view's heat map.
#include "gpu_vulkan/vulkan_debug_view.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_style_kit.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0xFA00'0001ULL;
constexpr int32_t kFlagRestart = 1;

struct ViewName {
    DebugView view;
    const char* name;
};
constexpr std::array<ViewName, 12> kNames{{{.view = DebugView::kOff, .name = "off"},
                                           {.view = DebugView::kLamps, .name = "lamps"},
                                           {.view = DebugView::kLampsSpecular, .name = "lamps_specular"},
                                           {.view = DebugView::kGi, .name = "gi"},
                                           {.view = DebugView::kGiSpecular, .name = "gi_specular"},
                                           {.view = DebugView::kSky, .name = "sky"},
                                           {.view = DebugView::kSun, .name = "sun"},
                                           {.view = DebugView::kReflections, .name = "reflections"},
                                           {.view = DebugView::kFog, .name = "fog"},
                                           {.view = DebugView::kFront, .name = "front"},
                                           {.view = DebugView::kEmission, .name = "emission"},
                                           {.view = DebugView::kFlicker, .name = "flicker"}}};

// The view in use (set_debug_view: the camera effects' settings), and how many times it changed.
std::atomic<DebugView>& view_now()
{
    static std::atomic<DebugView> made{DebugView::kOff};
    return made;
}
std::atomic<uint64_t>& view_changes()
{
    static std::atomic<uint64_t> made{0};
    return made;
}

// The flicker view's pass: a running mean and variance of each pixel's display luma (0 - 255) over kFlickerFrames
// (Welford's recurrence with weights 1 / n, n held at the window: an exponential window once full), then the heat map.
constexpr const char* kHistoryBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3, rgba16f) uniform image2D u_history;
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.rect.zw))) {
        return;
    }
    vec3 c = finite3(texelFetch(u_source, pc.rect.xy + local, 0).rgb);
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722)) * 255.0;
    vec4 h = (pc.info.x & 1) != 0 ? vec4(0.0) : imageLoad(u_history, local);
    float n = min(h.z + 1.0, pc.a.x);
    float d = y - h.x;
    float mean = h.x + d / n;
    float variance = (1.0 - 1.0 / n) * (h.y + d * d / n);
    imageStore(u_history, local, vec4(mean, variance, n, 0.0));
}
)";

constexpr const char* kHeatBody = R"(
layout(location = 0) out vec4 o_colour;
void main()
{
    ivec2 local = ivec2(gl_FragCoord.xy) - pc.rect.xy;
    vec4 h = texelFetch(u_a, local, 0);
    float t = clamp(sqrt(max(h.y, 0.0)) / pc.a.y, 0.0, 1.0) * 3.0;
    vec3 heat = clamp(vec3(t, t - 1.0, t - 2.0), 0.0, 1.0);
    // (the picture's luma, dimmed, where nothing flickers: where the heat is)
    vec3 c = finite3(texelFetch(u_source, pc.rect.xy + local, 0).rgb);
    float grey = dot(c, vec3(0.2126, 0.7152, 0.0722)) * 0.25;
    o_colour = vec4(max(heat, vec3(grey)), 1.0);
}
)";

struct FlickerState {
    VkPipeline history = VK_NULL_HANDLE;
    std::vector<film_kit::FragmentPipeline> heat;
    std::string heat_source;
    Image image;
    bool failed = false;
    bool running = false; // (its history holds this view's frames)
    uint64_t changes = 0; // (view_changes when its history began: another since - begun again)
};
FlickerState& flicker()
{
    static FlickerState made;
    return made;
}

// The exposure held while a term view is on (keep_debug_view).
struct HeldState {
    std::mutex mutex;
    DebugView logged = DebugView::kOff;
    bool held = false;
};
HeldState& held()
{
    static HeldState made;
    return made;
}

} // namespace

const char* debug_view_name(DebugView view)
{
    for (const ViewName& one : kNames) {
        if (one.view == view) {
            return one.name;
        }
    }
    return "off";
}

bool parse_debug_view(std::string_view text, DebugView* view)
{
    for (const ViewName& one : kNames) {
        if (text == one.name) {
            *view = one.view;
            return true;
        }
    }
    if (text.empty() || text == "0") {
        *view = DebugView::kOff;
        return true;
    }
    return false;
}

std::string debug_view_names()
{
    std::string out;
    for (const ViewName& one : kNames) {
        out += out.empty() ? "" : " | ";
        out += one.name;
    }
    return out;
}

uint32_t debug_view_terms(DebugView view)
{
    switch (view) {
    case DebugView::kLamps: return debug_term::kLampsDiffuse;
    case DebugView::kLampsSpecular: return debug_term::kLampsSpecular;
    case DebugView::kGi: return debug_term::kGiDiffuse;
    case DebugView::kGiSpecular: return debug_term::kGiSpecular;
    case DebugView::kSky: return debug_term::kSky | debug_term::kSun;
    case DebugView::kSun: return debug_term::kSun;
    case DebugView::kReflections: return debug_term::kReflections;
    case DebugView::kFog: return debug_term::kFog;
    case DebugView::kFront: return debug_term::kFront;
    case DebugView::kEmission: return 0U;
    case DebugView::kOff:
    case DebugView::kFlicker: break;
    }
    return debug_term::kAll;
}

DebugView debug_view() { return view_now().load(std::memory_order_relaxed); }

uint32_t debug_terms() { return debug_view_terms(debug_view()); }

void set_debug_view(DebugView view)
{
    if (view != view_now().exchange(view, std::memory_order_relaxed)) {
        view_changes().fetch_add(1, std::memory_order_relaxed); // (the flicker's history starts again)
    }
    HeldState& own = held();
    const std::scoped_lock lock(own.mutex);
    if (view != own.logged) {
        own.logged = view;
        get_log("vulkan: rt_debug_view %s%s", debug_view_name(view),
                view == DebugView::kOff       ? ""
                : view == DebugView::kFlicker ? " (each pixel's temporal deviation, display luma, as a heat map)"
                                              : " (that term of the light alone; the exposure held at the whole frame's)");
    }
    if (view == DebugView::kOff && own.held) {
        set_manual_ev100(false, 0.0F); // (the listener stops with the view: the meter's again)
        own.held = false;
    }
}

void keep_debug_view(Backend& state)
{
    HeldState& own = held();
    const std::scoped_lock lock(own.mutex);
    const DebugView view = debug_view();
    const bool term_view = view != DebugView::kOff && view != DebugView::kFlicker;
    if (term_view && !own.held) {
        // (the whole frame's exposure, held: the term as bright as it is in the picture)
        float ev100 = 0.0F;
        if (last_metered_ev100(state, &ev100)) {
            set_manual_ev100(true, ev100);
            own.held = true;
        }
    } else if (!term_view && own.held) {
        set_manual_ev100(false, 0.0F);
        own.held = false;
    }
}

bool record_flicker_view(Backend& state, const FilmTarget& target)
{
    FlickerState& own = flicker();
    if (debug_view() != DebugView::kFlicker || own.failed) {
        return false;
    }
    std::string error;
    if (own.heat_source.empty()) {
        own.heat_source = std::string(film_kit::glsl_common()) + kHeatBody;
    }
    if (film_kit::ensure_common(state, &error) && own.history == VK_NULL_HANDLE) {
        own.history = film_kit::make_compute(state, std::string(film_kit::glsl_common()) + kHistoryBody, &error);
    }
    const VkPipeline heat = own.history != VK_NULL_HANDLE
                                ? film_kit::fragment_pipeline(state, &own.heat, own.heat_source, target.colour->format,
                                                              target.colour->samples, &error)
                                : VK_NULL_HANDLE;
    if (heat == VK_NULL_HANDLE) {
        own.failed = true;
        if (film_kit::first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_debug_view flicker: the pass couldn't be made (%s)", error.c_str());
        }
        return false;
    }
    const Extent extent{.width = core::at(target.shown, 2), .height = core::at(target.shown, 3)};
    const bool sized = own.image.image != VK_NULL_HANDLE && own.image.extent.width == extent.width &&
                       own.image.extent.height == extent.height;
    if (!film_kit::ensure_work_image(state, &own.image, extent)) {
        return false;
    }
    FilmConstants constants;
    constants.rect = {static_cast<int32_t>(core::at(target.shown, 0)), static_cast<int32_t>(core::at(target.shown, 1)),
                      static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height)};
    const uint64_t changes = view_changes().load(std::memory_order_relaxed);
    constants.info = {own.running && sized && own.changes == changes ? 0 : kFlagRestart, 0, 0, 0};
    own.changes = changes;
    core::at(constants.values, 0) = {static_cast<float>(kFlickerFrames), kFlickerFull, 0.0F, 0.0F};
    const VkCommandBuffer commands = film_kit::begin(state, target, constants, {&own.image});
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    film_kit::dispatch(commands, own.history, {.source = film_kit::source(), .out = &own.image}, extent.width, extent.height);
    film_kit::draw(commands, heat, {.source = film_kit::source(), .a = &own.image}, target);
    film_kit::end(state, commands, target, "debug view flicker", {&own.image});
    own.running = true;
    return true;
}

void release_debug_view(Backend& state)
{
    FlickerState& own = flicker();
    film_kit::destroy_pipeline(state, &own.history);
    film_kit::destroy_pipelines(state, &own.heat);
    film_kit::destroy_image(state, &own.image);
    own.failed = false;
    own.running = false;
    HeldState& kept = held();
    const std::scoped_lock lock(kept.mutex);
    if (kept.held) {
        set_manual_ev100(false, 0.0F);
        kept.held = false;
    }
}

} // namespace render::vulkan
