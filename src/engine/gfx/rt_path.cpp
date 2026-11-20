// NOT ORIGINAL - the path tracer's reference request and the console's `pt` (rt_path.h).
#include "engine/gfx/rt_path.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include "base/core/format.h"
#include "platform/run_log.h"

namespace render::rt::path {

namespace {

struct ReferenceSlot {
    std::mutex mutex;
    PathReferenceRequest request;
    PathReferenceStatus status;
    uint64_t serial = 0;
};

ReferenceSlot& slot()
{
    static ReferenceSlot made;
    return made;
}

constexpr uint32_t kMostSamples = 1U << 20U;

std::string status_text(const PathReferenceStatus& status, const PathReferenceRequest& request)
{
    if (request.samples == 0) {
        return status.saved.empty() ? std::string("pt: off") : "pt: off (last saved " + status.saved + ")";
    }
    std::string text = text::format("pt: reference %s - %u of %u samples a pixel, EV100 %.2f", status.running ? "running" : "waiting",
                                    status.samples, request.samples, static_cast<double>(status.ev100));
    if (!status.saved.empty()) {
        text += ", saved " + status.saved;
    }
    if (!status.note.empty()) {
        text += " (" + status.note + ")";
    }
    return text;
}

constexpr const char* kRefUsage = "pt ref <samples a pixel, 1 - 1048576> [EV100] [even] [analytic] [open] [only disc,sky,emit,lamps] [direct | bounces <n>] [nee | bsdf] [as <name>]";
constexpr uint32_t kMostLinearFrames = 4096;

} // namespace

uint32_t light_kinds_of(const std::string& words)
{
    uint32_t kinds = 0;
    std::istringstream in(words);
    std::string word;
    while (std::getline(in, word, ',')) {
        if (word == "disc" || word == "sun" || word == "moon") {
            kinds |= kLightKindDisc;
        } else if (word == "sky") {
            kinds |= kLightKindSky;
        } else if (word == "emit" || word == "emitters") {
            kinds |= kLightKindEmitters;
        } else if (word == "lamps") {
            kinds |= kLightKindLamps;
        } else {
            return 0;
        }
    }
    return kinds;
}

std::string light_kinds_text(uint32_t kinds)
{
    if ((kinds & kAllLightKinds) == kAllLightKinds) {
        return "all";
    }
    std::string text;
    const auto add = [&text](const char* word) { text += (text.empty() ? "" : ",") + std::string(word); };
    if ((kinds & kLightKindDisc) != 0) {
        add("disc");
    }
    if ((kinds & kLightKindSky) != 0) {
        add("sky");
    }
    if ((kinds & kLightKindEmitters) != 0) {
        add("emit");
    }
    if ((kinds & kLightKindLamps) != 0) {
        add("lamps");
    }
    return text.empty() ? "none" : text;
}

namespace {
std::atomic<uint32_t>& upscaler_probe_asked()
{
    static std::atomic<uint32_t> asked{0};
    return asked;
}
} // namespace

void request_upscaler_probe(uint32_t frames) { upscaler_probe_asked().store(frames); }

uint32_t take_upscaler_probe() { return upscaler_probe_asked().exchange(0); }

namespace {
struct LinearFramesSlot {
    std::mutex mutex;
    LinearFramesRequest request;
    bool asked = false;
};
LinearFramesSlot& linear_frames_slot()
{
    static LinearFramesSlot made;
    return made;
}
} // namespace

void request_linear_frames(LinearFramesRequest request)
{
    LinearFramesSlot& own = linear_frames_slot();
    const std::scoped_lock lock(own.mutex);
    own.request = std::move(request);
    own.asked = true;
}

LinearFramesRequest take_linear_frames()
{
    LinearFramesSlot& own = linear_frames_slot();
    const std::scoped_lock lock(own.mutex);
    if (!own.asked) {
        return {};
    }
    own.asked = false;
    return own.request;
}

void request_path_reference(PathReferenceRequest request)
{
    ReferenceSlot& own = slot();
    const std::scoped_lock lock(own.mutex);
    request.serial = ++own.serial;
    own.request = request;
}

PathReferenceRequest path_reference_request()
{
    ReferenceSlot& own = slot();
    const std::scoped_lock lock(own.mutex);
    return own.request;
}

void set_path_reference_status(PathReferenceStatus status)
{
    ReferenceSlot& own = slot();
    const std::scoped_lock lock(own.mutex);
    own.status = std::move(status);
}

PathReferenceStatus path_reference_status()
{
    ReferenceSlot& own = slot();
    const std::scoped_lock lock(own.mutex);
    return own.status;
}

std::string path_console_command(const std::string& arguments)
{
    std::istringstream in(arguments);
    std::string verb;
    in >> verb;
    if (verb == "ref") {
        std::string count;
        in >> count;
        const long samples = std::strtol(count.c_str(), nullptr, 10);
        if (samples <= 0 || samples > static_cast<long>(kMostSamples)) {
            return kRefUsage;
        }
        PathReferenceRequest request;
        request.samples = static_cast<uint32_t>(samples);
        std::string ev;
        std::string word;
        while (in >> word) {
            if (word == "even") {
                request.even_lights = true;
            } else if (word == "analytic") {
                request.analytic_lamps = true;
            } else if (word == "open") {
                request.blended_open = true;
            } else if (word == "nee") {
                request.strategy = 1; // (round LG: the lights' samples alone)
            } else if (word == "bsdf") {
                request.strategy = 2; // (round LG: the BSDF's rays alone)
            } else if (word == "direct") {
                request.bounces = 1; // (path tracing, stage 4: the direct light alone - ReSTIR DI's yardstick)
            } else if (word == "bounces") {
                // (round MB) the paths cut at that many bounces: `bounces 2` is what a one-bounce indirect light reaches -
                // its difference from the whole is the further bounces' share
                std::string most;
                in >> most;
                const long bounces = std::strtol(most.c_str(), nullptr, 10);
                if (bounces <= 0 || bounces > static_cast<long>(kMostBounces)) {
                    return kRefUsage;
                }
                request.bounces = static_cast<uint32_t>(bounces);
            } else if (word == "as") {
                in >> request.name; // (round MV: the captures' name)
                if (request.name.empty() || request.name.find_first_of("\\/:") != std::string::npos) {
                    return kRefUsage;
                }
            } else if (word == "only") {
                std::string kinds;
                in >> kinds;
                request.light_kinds = light_kinds_of(kinds);
                if (request.light_kinds == 0) {
                    return kRefUsage;
                }
            } else if (ev.empty()) {
                ev = word;
            } else {
                return kRefUsage;
            }
        }
        if (!ev.empty()) {
            char* end = nullptr;
            const float value = std::strtof(ev.c_str(), &end);
            if (end == ev.c_str()) {
                return "pt ref <samples a pixel> [EV100]: the EV100 isn't a number";
            }
            request.ev_given = true;
            request.ev100 = value;
        }
        request_path_reference(request);
        const std::string cut = request.bounces == 1                ? ", the direct light alone (one bounce)"
                                : request.bounces < kMostBounces ? text::format(", %u bounces at most", request.bounces)
                                                                  : std::string();
        get_log("pt: reference asked - %u samples a pixel, EV100 %s, lights %s, light counted: %s%s%s%s%s", request.samples,
                request.ev_given ? ev.c_str() : "the camera's metered one",
                request.even_lights ? "evenly by kind" : "by power", light_kinds_text(request.light_kinds).c_str(),
                request.analytic_lamps ? ", every lamp analytic (no surface's emission)" : "",
                request.blended_open ? ", the light's rays pass the blended surfaces" : "",
                cut.c_str(),
                request.strategy == 1   ? ", the lights' samples alone"
                : request.strategy == 2 ? ", the BSDF's rays alone"
                                        : "");
        return text::format("pt: reference of %u samples a pixel started (pause the game; the camera must stay still)",
                            request.samples);
    }
    if (verb == "upscaler") {
        std::string count;
        in >> count;
        const long frames = count.empty() ? static_cast<long>(kUpscalerProbeFrames) : std::strtol(count.c_str(), nullptr, 10);
        if (frames <= 0 || frames > static_cast<long>(kUpscalerProbeMostFrames)) {
            return "pt upscaler [frames, 1 - 1024]";
        }
        request_upscaler_probe(static_cast<uint32_t>(frames));
        return text::format("pt: the upscaler's input and output compared over %ld frames (the log's \"upscaler probe\")",
                            frames);
    }
    if (verb == "frames") {
        // (round MV) the next frames' linear light, for the motion measurements
        LinearFramesRequest frames;
        std::string count;
        std::string every;
        std::string extra;
        in >> count >> frames.folder >> every >> extra;
        if (every == "inputs") {
            every.clear();
            extra = "inputs";
        }
        frames.inputs = extra == "inputs";
        const long wanted = std::strtol(count.c_str(), nullptr, 10);
        const long step = every.empty() ? 1 : std::strtol(every.c_str(), nullptr, 10);
        if (wanted <= 0 || wanted > static_cast<long>(kMostLinearFrames) || step <= 0 || frames.folder.empty() ||
            frames.folder.find_first_of(":") != std::string::npos || frames.folder.find("..") != std::string::npos) {
            return "pt frames <count, 1 - 4096> <folder under Screenshots> [every] [inputs]";
        }
        frames.count = static_cast<uint32_t>(wanted);
        frames.every = static_cast<uint32_t>(step);
        get_log("pt: the next %ld frames' linear light (each %ld-th) into Screenshots\\%s", wanted, step, frames.folder.c_str());
        request_linear_frames(frames);
        return text::format("pt: %ld frames' linear light asked", wanted);
    }
    if (verb == "off") {
        request_path_reference({});
        get_log("pt: reference off");
        return "pt: off";
    }
    if (!verb.empty() && verb != "status") {
        return "pt ref <samples> [EV100] [even] [analytic] [open] [only <kinds>] [as <name>] | pt frames <count> <folder> [every] | pt off | pt status | pt upscaler";
    }
    return status_text(path_reference_status(), path_reference_request());
}

} // namespace render::rt::path
