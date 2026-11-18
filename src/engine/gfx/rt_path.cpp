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

} // namespace

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
        std::string ev;
        in >> count >> ev;
        const long samples = std::strtol(count.c_str(), nullptr, 10);
        if (samples <= 0 || samples > static_cast<long>(kMostSamples)) {
            return "pt ref <samples a pixel, 1 - 1048576> [EV100]";
        }
        PathReferenceRequest request;
        request.samples = static_cast<uint32_t>(samples);
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
        get_log("pt: reference asked - %u samples a pixel, EV100 %s", request.samples,
                request.ev_given ? ev.c_str() : "the camera's metered one");
        return text::format("pt: reference of %u samples a pixel started (pause the game; the camera must stay still)",
                            request.samples);
    }
    if (verb == "off") {
        request_path_reference({});
        get_log("pt: reference off");
        return "pt: off";
    }
    if (!verb.empty() && verb != "status") {
        return "pt ref <samples> [EV100] | pt off | pt status";
    }
    return status_text(path_reference_status(), path_reference_request());
}

} // namespace render::rt::path
