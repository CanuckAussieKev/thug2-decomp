// NOT ORIGINAL - the light energy audit's census: see rt_light_census.h.
#include "engine/gfx/rt_light_census.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/core/checked_index.h"
#include "base/core/format.h"

namespace render::rt {

namespace {

struct Counters {
    std::array<std::atomic<uint64_t>, kLightPaths> counts{};
    std::atomic<uint64_t> frames{0};
};

Counters& counters()
{
    static Counters made;
    return made;
}

constexpr std::array<const char*, kLightPaths> kNames{"objects lit (field)", "objects lit (own lights)",
                                                       "unlit objects (field)", "unlit objects (listed: the relit level's)",
                                                       "unlit objects (own colours)",
                                                       "unlit objects (the game's instance colour)",
                                                       "models (field)", "models (indoor colour)",
                                                       "particles (level whole)",
                                                       "world meshes without their own light (field)",
                                                       "billboards (field)", "billboards (own colours)"};

} // namespace

void note_light_path(LightPath path, uint32_t count)
{
    const auto at = static_cast<std::size_t>(path);
    if (at < kLightPaths) {
        core::at(counters().counts, at).fetch_add(count, std::memory_order_relaxed);
    }
}

void note_census_frame() { counters().frames.fetch_add(1, std::memory_order_relaxed); }

LightCensus take_light_census()
{
    LightCensus out;
    for (std::size_t i = 0; i < kLightPaths; ++i) {
        core::at(out.counts, i) = core::at(counters().counts, i).exchange(0, std::memory_order_relaxed);
    }
    out.frames = counters().frames.exchange(0, std::memory_order_relaxed);
    return out;
}

const char* light_path_name(LightPath path)
{
    const auto at = static_cast<std::size_t>(path);
    return at < kLightPaths ? core::at(kNames, at) : "?";
}

std::string light_census_text(const LightCensus& census)
{
    std::string line;
    const double frames = census.frames > 0 ? static_cast<double>(census.frames) : 1.0;
    for (std::size_t i = 0; i < kLightPaths; ++i) {
        if (!line.empty()) {
            line += ", ";
        }
        line += text::format("%s %.1f", core::at(kNames, i), static_cast<double>(core::at(census.counts, i)) / frames);
    }
    return text::format("light paths a frame over %llu frames: ", static_cast<unsigned long long>(census.frames)) +
           line;
}

namespace {
std::atomic<WorldCensus>& world_census_slot()
{
    static std::atomic<WorldCensus> census{nullptr};
    return census;
}
} // namespace

void set_world_census(WorldCensus census) { world_census_slot().store(census); }

std::string world_census_text()
{
    const WorldCensus census = world_census_slot().load();
    return census != nullptr ? census() : std::string("rt census: no ray tracing - the world isn't recorded\n");
}

} // namespace render::rt
