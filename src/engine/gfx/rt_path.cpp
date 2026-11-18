// NOT ORIGINAL - the path tracer's reference request and the console's `pt` (rt_path.h).
#include "engine/gfx/rt_path.h"

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

constexpr const char* kRefUsage = "pt ref <samples a pixel, 1 - 1048576> [EV100] [even] [analytic] [open] [only disc,sky,emit,lamps] [direct] [nee | bsdf]";

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
        get_log("pt: reference asked - %u samples a pixel, EV100 %s, lights %s, light counted: %s%s%s%s%s", request.samples,
                request.ev_given ? ev.c_str() : "the camera's metered one",
                request.even_lights ? "evenly by kind" : "by power", light_kinds_text(request.light_kinds).c_str(),
                request.analytic_lamps ? ", every lamp analytic (no surface's emission)" : "",
                request.blended_open ? ", the light's rays pass the blended surfaces" : "",
                request.bounces == 1 ? ", the direct light alone (one bounce)" : "",
                request.strategy == 1   ? ", the lights' samples alone"
                : request.strategy == 2 ? ", the BSDF's rays alone"
                                        : "");
        return text::format("pt: reference of %u samples a pixel started (pause the game; the camera must stay still)",
                            request.samples);
    }
    if (verb == "off") {
        request_path_reference({});
        get_log("pt: reference off");
        return "pt: off";
    }
    if (!verb.empty() && verb != "status") {
        return "pt ref <samples> [EV100] [even] [analytic] [open] [only <kinds>] | pt off | pt status";
    }
    return status_text(path_reference_status(), path_reference_request());
}

} // namespace render::rt::path
