// NOT ORIGINAL - NVIDIA DLSS Frame Generation and Reflex (vulkan_framegen.h; docs/design/vulkan.md "Frame generation"):
// Streamline 2.14's sl.dlss_g (with NVIDIA's nvngx_dlssg.dll), sl.reflex (with NvLowLatencyVk.dll) and sl.pcl, loaded
// by the upscaler's Streamline (vulkan_upscale_dlss.cpp, vulkan_streamline.h) when frame_generation or reflex asks -
// NVIDIA's RTX SDKs licence for the NGX and Reflex libraries (THIRD_PARTY_NOTICES.md). Streamline's swapchain hooks
// (already routed for the present) present the made frames; each frame it is given the common constants (the upscaler's
// camera, its reset), the depth and motion vectors (the upscaler's images), the HUD-less colour and the UI image, and
// DLSS-G turned on or off - off keeps its resources (eRetainResourcesWhenOff), so the pause menu costs no stutter.
// Reflex: the frame's start waits in slReflexSleep, the PCL markers time the simulation, the draws and the present.
#include "gpu_vulkan/vulkan_framegen.h"

#include <memory>
#include <string>

#ifdef THUG2_DLSS
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_streamline.h"
#include "platform/run_log.h"
#endif

namespace render::vulkan {

#ifdef THUG2_DLSS
namespace {

constexpr uint32_t kViewport = 0;
constexpr sl::ResourceLifecycle kKept = sl::ResourceLifecycle::eValidUntilPresent;
constexpr uint32_t kMostMultiplier = 4; // (the Options offer 4x at most)

// Reflex's and the markers' state.
struct LatencyState {
    bool simulation_started = false; // the frame's token had its simulation start marked
    uint64_t sleeps = 0;
};

LatencyState& latency()
{
    static LatencyState state;
    return state;
}

template <typename Function> Function* feature_function(sl::Feature feature, const char* name)
{
    void* function = nullptr;
    const SlFunctions& calls = streamline().functions;
    if (calls.feature_function == nullptr || calls.feature_function(feature, name, function) != sl::Result::eOk) {
        return nullptr;
    }
    return same_address<Function*>(function);
}

void set_marker(sl::PCLMarker marker)
{
    const SlFunctions& calls = streamline().functions;
    sl::FrameToken* token = streamline_frame_token();
    if (token != nullptr && calls.pcl_marker != nullptr) {
        calls.pcl_marker(marker, *token);
    }
}

class DlssgProvider final : public FrameGenProvider {
public:
    DlssgProvider() = default;
    DlssgProvider(const DlssgProvider&) = delete;
    DlssgProvider& operator=(const DlssgProvider&) = delete;
    DlssgProvider(DlssgProvider&&) = delete;
    DlssgProvider& operator=(DlssgProvider&&) = delete;
    ~DlssgProvider() override = default;

    [[nodiscard]] const char* name() const override { return "DLSS-G"; }
    [[nodiscard]] std::string describe() const override
    {
        const SlState& sl_state = streamline();
        return "NVIDIA DLSS Frame Generation, up to " + std::to_string(most_multiplier()) + "x" +
               (sl_state.version.empty() ? "" : " (" + sl_state.version + ")");
    }

    [[nodiscard]] uint32_t most_multiplier() const override
    {
        // (numFramesToGenerateMax: 1 on a device of 2x, 3 on one of 4x; asked again at the first frame on, when the
        // swapchain is Streamline's)
        return std::clamp(most_generated_ + 1, 2U, kMostMultiplier);
    }
    [[nodiscard]] bool wants_ui() const override { return true; }

    bool query_most()
    {
        const SlFunctions& calls = streamline().functions;
        sl::DLSSGState state{};
        if (calls.dlssg_state == nullptr || calls.dlssg_state(sl::ViewportHandle(kViewport), state, nullptr) != sl::Result::eOk) {
            return false;
        }
        if (state.numFramesToGenerateMax > 0) {
            most_generated_ = state.numFramesToGenerateMax;
        }
        return true;
    }

    bool inputs(Backend& state, const FrameGenFrame& frame, std::string* error) override
    {
        SlState& sl_state = streamline();
        const SlFunctions& calls = sl_state.functions;
        sl::FrameToken* token = streamline_frame_token();
        if (token == nullptr) {
            *error = "slGetNewFrameToken failed";
            return false;
        }
        const sl::ViewportHandle viewport(kViewport);
        const UpscaleFrame& upscaled = *frame.inputs;
        // the common constants (once a frame: DLSS's provider gave them when it is the upscaler)
        if (!sl_state.constants_given) {
            const sl::Constants constants = streamline_constants(state, upscaled, frame.reset || upscaled.reset);
            if (calls.constants(constants, *token, viewport) != sl::Result::eOk) {
                *error = "slSetConstants failed";
                return false;
            }
            sl_state.constants_given = true;
        }
        // the depth and the motion vectors: the upscaler's images, kept until its next frame
        const sl::Extent render{.top = 0, .left = 0, .width = upscaled.render.width, .height = upscaled.render.height};
        sl::Resource depth = streamline_resource(*upscaled.depth);
        sl::Resource motion = streamline_resource(*upscaled.motion);
        const std::array<sl::ResourceTag, 2> tags{
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, kKept, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, kKept, &render),
        };
        auto* commands = same_address<sl::CommandBuffer*>(frame.commands);
        if (calls.tag(*token, viewport, tags.data(), static_cast<uint32_t>(tags.size()), commands) != sl::Result::eOk) {
            *error = "slSetTagForFrame failed (the depth and motion)";
            return false;
        }
        return true;
    }

    bool before_present(Backend& /*state*/, const FrameGenFrame& frame, std::string* error) override
    {
        const SlFunctions& calls = streamline().functions;
        sl::FrameToken* token = streamline_frame_token();
        if (token == nullptr) {
            *error = "slGetNewFrameToken failed";
            return false;
        }
        const sl::ViewportHandle viewport(kViewport);
        auto* commands = same_address<sl::CommandBuffer*>(frame.commands);
        if (frame.on) {
            const sl::Extent shown{.top = 0, .left = 0, .width = frame.shown.width, .height = frame.shown.height};
            sl::Resource hudless = streamline_resource(*frame.hudless);
            sl::Resource ui = streamline_resource(*frame.ui);
            const std::array<sl::ResourceTag, 2> tags{
                sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor, kKept, &shown),
                sl::ResourceTag(&ui, sl::kBufferTypeUIColorAndAlpha, kKept, &shown),
            };
            if (calls.tag(*token, viewport, tags.data(), static_cast<uint32_t>(tags.size()), commands) != sl::Result::eOk) {
                *error = "slSetTagForFrame failed (the HUD-less colour and the UI)";
                return false;
            }
        } else if (on_) {
            // (off: the tags let go - nothing of this frame is valid for frame generation)
            const std::array<sl::ResourceTag, 4> tags{
                sl::ResourceTag(nullptr, sl::kBufferTypeDepth, kKept), sl::ResourceTag(nullptr, sl::kBufferTypeMotionVectors, kKept),
                sl::ResourceTag(nullptr, sl::kBufferTypeHUDLessColor, kKept),
                sl::ResourceTag(nullptr, sl::kBufferTypeUIColorAndAlpha, kKept)};
            calls.tag(*token, viewport, tags.data(), static_cast<uint32_t>(tags.size()), commands);
        }
        // the options (taken at the next present): changed only when on / off or the multiplier changes
        if (frame.on != on_ || (frame.on && frame.multiplier != multiplier_)) {
            if (frame.on && made_ == released_) {
                query_most(); // (Streamline's swapchain now made: the device's real most)
                if (frame.multiplier > most_multiplier()) {
                    get_log("vulkan: frame generation: DLSS-G makes at most %ux on this device - %ux", most_multiplier(),
                            most_multiplier());
                }
            }
            const uint32_t multiplier = std::min(frame.multiplier, most_multiplier());
            sl::DLSSGOptions options{};
            options.mode = frame.on ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
            options.numFramesToGenerate = multiplier - 1;
            options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff | sl::DLSSGFlags::eEnableFullscreenMenuDetection;
            options.colorWidth = frame.shown.width;
            options.colorHeight = frame.shown.height;
            const sl::Result set = calls.dlssg_options(viewport, options);
            if (set != sl::Result::eOk) {
                *error = "slDLSSGSetOptions returned " + std::to_string(static_cast<int>(set));
                return false;
            }
            if (frame.on && made_ == released_) {
                ++made_; // (its resources made at the next present)
            }
            on_ = frame.on;
            multiplier_ = frame.multiplier;
        }
        return true;
    }

    uint64_t after_present(Backend& /*state*/) override
    {
        const SlFunctions& calls = streamline().functions;
        sl::DLSSGState state{};
        if (made_ == released_ || calls.dlssg_state == nullptr ||
            calls.dlssg_state(sl::ViewportHandle(kViewport), state, nullptr) != sl::Result::eOk) {
            return 1;
        }
        if (state.status != sl::DLSSGStatus::eOk && !status_reported_) {
            status_reported_ = true;
            get_log("vulkan: frame generation: DLSS-G reports status 0x%x (Reflex not seen, the window too small, the "
                    "constants...) - see Streamline's lines above",
                    static_cast<unsigned>(state.status));
        }
        return state.numFramesActuallyPresented;
    }

    void release(Backend& /*state*/) override
    {
        const SlState& sl_state = streamline();
        if (made_ != released_ && sl_state.device_set) {
            sl::DLSSGOptions options{};
            options.mode = sl::DLSSGMode::eOff;
            sl_state.functions.dlssg_options(sl::ViewportHandle(kViewport), options);
            if (sl_state.functions.free_resources != nullptr) {
                sl_state.functions.free_resources(sl::kFeatureDLSS_G, sl::ViewportHandle(kViewport));
            }
            released_ = made_;
        }
        on_ = false;
    }

    [[nodiscard]] uint64_t made() const override { return made_; }
    [[nodiscard]] uint64_t released() const override { return released_; }

private:
    uint32_t most_generated_ = kMostMultiplier - 1; // (until the device says: Streamline knows it once its swapchain is made)
    bool on_ = false;
    uint32_t multiplier_ = 0;
    uint64_t made_ = 0;
    uint64_t released_ = 0;
    bool status_reported_ = false;
};

} // namespace

void streamline_latency_device_made(const sl::AdapterInfo& adapter)
{
    SlState& sl_state = streamline();
    SlFunctions& calls = sl_state.functions;
    sl_state.reflex = false;
    sl_state.frame_generation = false;
    if (sl_state.latency_loaded && calls.supported != nullptr) {
        calls.reflex_options = feature_function<PFun_slReflexSetOptions>(sl::kFeatureReflex, "slReflexSetOptions");
        calls.reflex_state = feature_function<PFun_slReflexGetState>(sl::kFeatureReflex, "slReflexGetState");
        calls.reflex_sleep = feature_function<PFun_slReflexSleep>(sl::kFeatureReflex, "slReflexSleep");
        calls.pcl_marker = feature_function<PFun_slPCLSetMarker>(sl::kFeaturePCL, "slPCLSetMarker");
        const bool supported = calls.supported(sl::kFeatureReflex, adapter) == sl::Result::eOk;
        if (supported && calls.reflex_options != nullptr && calls.reflex_sleep != nullptr && calls.pcl_marker != nullptr) {
            // (DLSS-G needs Reflex on: at least low latency)
            const Reflex asked = frame_generation_settings_locked().reflex;
            sl::ReflexOptions options{};
            options.mode = asked == Reflex::kBoost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
            options.frameLimitUs = 0;
            sl_state.reflex = calls.reflex_options(options) == sl::Result::eOk;
        }
        get_log("vulkan: Reflex: %s", sl_state.reflex ? (frame_generation_settings_locked().reflex == Reflex::kBoost
                                                             ? "low latency + boost"
                                                             : "low latency")
                                                       : "not supported here (an NVIDIA device and driver have it)");
    }
    if (sl_state.generation_loaded && calls.supported != nullptr) {
        calls.dlssg_options = feature_function<PFun_slDLSSGSetOptions>(sl::kFeatureDLSS_G, "slDLSSGSetOptions");
        calls.dlssg_state = feature_function<PFun_slDLSSGGetState>(sl::kFeatureDLSS_G, "slDLSSGGetState");
        const sl::Result supported = calls.supported(sl::kFeatureDLSS_G, adapter);
        sl_state.frame_generation = supported == sl::Result::eOk && sl_state.reflex && calls.dlssg_options != nullptr &&
                                    calls.dlssg_state != nullptr;
        if (!sl_state.frame_generation) {
            sl_state.problem = supported != sl::Result::eOk
                                   ? "slIsFeatureSupported(DLSS-G) returned " + std::to_string(static_cast<int>(supported)) +
                                         " (an RTX 40 or 50, a recent driver and hardware-accelerated GPU scheduling have it)"
                                   : (sl_state.reflex ? "DLSS-G's functions are missing" : "Reflex isn't running");
        }
    }
}

bool reflex_running() { return streamline().reflex; }

void reflex_marker(GpuContext::LatencyMarker marker)
{
    SlState& sl_state = streamline();
    LatencyState& own = latency();
    switch (marker) {
    case GpuContext::LatencyMarker::kSimulationStart: {
        // (a frame that was never presented - drawing skipped - gives its token up: a new frame starts)
        if (own.simulation_started) {
            end_streamline_frame();
        }
        sl::FrameToken* token = streamline_frame_token();
        if (token != nullptr && sl_state.functions.reflex_sleep != nullptr) {
            sl_state.functions.reflex_sleep(*token);
            ++own.sleeps;
        }
        set_marker(sl::PCLMarker::eSimulationStart);
        own.simulation_started = true;
        break;
    }
    case GpuContext::LatencyMarker::kSimulationEnd:
        set_marker(sl::PCLMarker::eSimulationEnd);
        break;
    case GpuContext::LatencyMarker::kRenderSubmitStart:
        set_marker(sl::PCLMarker::eRenderSubmitStart);
        break;
    case GpuContext::LatencyMarker::kRenderSubmitEnd:
        set_marker(sl::PCLMarker::eRenderSubmitEnd);
        break;
    }
}

void reflex_present_marker(bool start) { set_marker(start ? sl::PCLMarker::ePresentStart : sl::PCLMarker::ePresentEnd); }

void streamline_frame_done()
{
    if (streamline_frames_shared()) {
        end_streamline_frame();
        latency().simulation_started = false;
    }
}

void dlssg_other_swapchain(bool making)
{
    const SlState& sl_state = streamline();
    if (sl_state.frame_generation && sl_state.functions.set_loaded != nullptr) {
        sl_state.functions.set_loaded(sl::kFeatureDLSS_G, !making);
    }
}

std::unique_ptr<FrameGenProvider> make_dlssg_provider(Backend& /*state*/, std::string* why)
{
    const SlState& sl_state = streamline();
    if (!sl_state.initialised) {
        *why = sl_state.problem.empty() ? "Streamline wasn't initialised before the device" : sl_state.problem;
        return nullptr;
    }
    if (!sl_state.nvidia) {
        *why = "not an NVIDIA device";
        return nullptr;
    }
    if (!sl_state.device_set || !sl_state.frame_generation) {
        *why = sl_state.problem.empty() ? "DLSS Frame Generation isn't supported on this device or driver" : sl_state.problem;
        return nullptr;
    }
    auto provider = std::make_unique<DlssgProvider>();
    provider->query_most();
    return provider;
}

#else

std::unique_ptr<FrameGenProvider> make_dlssg_provider(Backend& /*state*/, std::string* why)
{
    *why = "this build has no Streamline (a 64-bit build with THUG2_DLSS on has it)";
    return nullptr;
}

bool reflex_running() { return false; }
void reflex_marker(GpuContext::LatencyMarker /*marker*/) {}
void reflex_present_marker(bool /*start*/) {}
void streamline_frame_done() {}
void dlssg_other_swapchain(bool /*making*/) {}

#endif

} // namespace render::vulkan
