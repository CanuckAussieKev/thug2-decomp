// NOT ORIGINAL - the render-only frames' schedule (see frame_pacing.h). Nothing here touches the game.

#include "engine/sys/frame_pacing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace frame_pacing {

namespace {

std::string lowered(const char* text)
{
    std::string out = text != nullptr ? text : "";
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // (spaces round it dropped)
    const auto first = out.find_first_not_of(" \t");
    const auto last = out.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

} // namespace

int parse_max_fps(const char* text)
{
    const std::string value = lowered(text);
    if (value.empty()) {
        return kOriginal;
    }
    if (value == "uncapped" || value == "unlimited") {
        return kUncapped;
    }
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return kOriginal;
    }
    if (number < 0) {
        return kUncapped;
    }
    if (number == 0) {
        return kOriginal;
    }
    return static_cast<int>(std::clamp<long>(number, kLowestCap, kHighestCap));
}

PresentMode parse_present_mode(const char* text)
{
    const std::string value = lowered(text);
    if (value == "mailbox") {
        return PresentMode::kMailbox;
    }
    if (value == "immediate") {
        return PresentMode::kImmediate;
    }
    return PresentMode::kVsync;
}

const char* present_mode_name(PresentMode mode)
{
    switch (mode) {
    case PresentMode::kMailbox:
        return "mailbox";
    case PresentMode::kImmediate:
        return "immediate";
    case PresentMode::kVsync:
        break;
    }
    return "vsync";
}

int frames_this_tick(int max_fps, double* carry)
{
    if (max_fps == kOriginal) {
        *carry = 0.0;
        return 1;
    }
    if (max_fps < 0) {
        *carry = 0.0;
        return kUncappedFrames;
    }
    const double share = (static_cast<double>(max_fps) / kTicksPerSecond) + *carry;
    const double whole = std::floor(share + 1e-9);
    *carry = std::clamp(share - whole, 0.0, 1.0);
    return std::max(1, static_cast<int>(whole));
}

uint64_t frame_due(uint64_t tick_time, uint64_t tick_length, int index, int frames)
{
    if (frames <= 1 || index <= 0) {
        return tick_time;
    }
    // (a 128-bit product isn't needed: a tick is well under 2^32 counter ticks, the index under 2^10)
    return tick_time + (tick_length * static_cast<uint64_t>(index) / static_cast<uint64_t>(frames));
}

bool frame_fits(uint64_t now, uint64_t cost, uint64_t next_tick) { return now < next_tick && next_tick - now >= cost; }

float blend_alpha(uint64_t now, uint64_t tick_time, uint64_t tick_length)
{
    if (tick_length == 0 || now <= tick_time) {
        return 0.0F;
    }
    const double alpha = static_cast<double>(now - tick_time) / static_cast<double>(tick_length);
    return static_cast<float>(std::min(alpha, 1.0));
}

uint32_t shown_time_ms(uint32_t now_ms, float alpha)
{
    constexpr double kTickMs = 1000.0 / kTicksPerSecond;
    const double behind = (1.0 - std::clamp(static_cast<double>(alpha), 0.0, 1.0)) * kTickMs;
    const auto back = static_cast<uint32_t>(std::lround(behind));
    return now_ms >= back ? now_ms - back : 0U;
}

} // namespace frame_pacing
