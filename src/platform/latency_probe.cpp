// NOT ORIGINAL - input-to-display latency measured inside the game: see latency_probe.h.
#include "platform/latency_probe.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "platform/run_log.h"
#include "platform/sdl_events.h"

namespace latency_probe {

namespace {

constexpr int kShortestGapMs = 37;  // the probes' spacing: random in this range, so they land anywhere in a frame
constexpr int kLongestGapMs = 113;
constexpr std::size_t kReportEvery = 300;
constexpr double kNsPerMs = 1.0e6;
constexpr double kPercentile = 0.95;
constexpr double kHeldMs = 100.0; // a probe that waited longer for its step: the loop was held (a load) - left out

struct Probe {
    uint64_t stamp = 0;    // SDL's time on the event (ns)
    uint64_t step = 0;     // the simulation step that read it (0: none yet)
    uint64_t first = 0;    // the first present showing any of the step (ns; 0: not yet)
};

struct Samples {
    std::vector<double> to_step;
    std::vector<double> to_first;
    std::vector<double> to_full;
    std::size_t held = 0; // left out (kHeldMs)
};

struct ProbeState {
    bool read = false;
    bool on = false;
    uint32_t event_type = 0;
    std::atomic<bool> stopping{false};
    std::thread pusher;
    std::vector<Probe> waiting;            // handled, not yet fully shown
    std::vector<uint64_t> step_started_at; // per waiting probe: when its step started (ns)
    Samples samples;
};

ProbeState& probe_state()
{
    static core::NoDestroy<ProbeState> state;
    return state.get();
}

void push_events()
{
    const ProbeState& own = probe_state();
    std::mt19937 random(std::random_device{}());
    std::uniform_int_distribution<int> gap(kShortestGapMs, kLongestGapMs);
    while (!own.stopping.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(gap(random)));
        SDL_Event event{};
        event.type = own.event_type;
        event.user.timestamp = SDL_GetTicksNS();
        SDL_PushEvent(&event);
    }
}

void handle(SDL_Event* event) { note_event(event); }

double average(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double percentile(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto at = static_cast<std::size_t>(kPercentile * static_cast<double>(values.size() - 1));
    return core::at(values, at);
}

void report()
{
    Samples& samples = probe_state().samples;
    get_log("latency: %zu probes - event to step %.2f ms (95%% %.2f), to first shown %.2f ms (95%% %.2f), to fully "
            "shown %.2f ms (95%% %.2f); %zu left out (the loop held: a load)",
            samples.to_full.size(), average(samples.to_step), percentile(samples.to_step), average(samples.to_first),
            percentile(samples.to_first), average(samples.to_full), percentile(samples.to_full), samples.held);
    samples = {};
}

double ms_between(uint64_t from, uint64_t to) { return to > from ? static_cast<double>(to - from) / kNsPerMs : 0.0; }

} // namespace

void start()
{
    ProbeState& own = probe_state();
    if (own.read) {
        return;
    }
    own.read = true;
    own.on = config_int("debug", "latency_probe", 0) != 0;
    if (!own.on) {
        return;
    }
    own.event_type = SDL_RegisterEvents(1);
    if (own.event_type == 0) {
        own.on = false;
        get_log("latency: no SDL user event left - the probe is off");
        return;
    }
    os_layer::register_event_handler(handle);
    own.pusher = std::thread(push_events);
    get_log("latency: the probe runs ([debug] latency_probe) - a report every %zu probes", kReportEvery);
}

bool on() { return probe_state().on; }

void note_event(const void* sdl_event)
{
    ProbeState& own = probe_state();
    const auto* event = static_cast<const SDL_Event*>(sdl_event);
    if (!own.on || event->type != own.event_type) {
        return;
    }
    own.waiting.push_back({.stamp = event->user.timestamp});
    own.step_started_at.push_back(0);
}

void note_step(uint64_t step)
{
    ProbeState& own = probe_state();
    if (!own.on) {
        return;
    }
    const uint64_t now = SDL_GetTicksNS();
    for (std::size_t i = 0; i < own.waiting.size(); ++i) {
        Probe& probe = core::at(own.waiting, i);
        if (probe.step == 0) {
            probe.step = step;
            core::at(own.step_started_at, i) = now;
        }
    }
}

void note_present(uint64_t step, float alpha, bool blended)
{
    ProbeState& own = probe_state();
    if (!own.on || own.waiting.empty()) {
        return;
    }
    const uint64_t now = SDL_GetTicksNS();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < own.waiting.size(); ++i) {
        Probe probe = core::at(own.waiting, i);
        const uint64_t started = core::at(own.step_started_at, i);
        bool done = false;
        if (probe.step != 0 && step >= probe.step) {
            // (a frame of the probe's step shows some of it once blended towards it; one of a later step shows all of it)
            const bool some = step > probe.step || !blended || alpha > 0.0F;
            const bool all = step > probe.step || !blended || alpha >= 1.0F;
            if (some && probe.first == 0) {
                probe.first = now;
            }
            if (all) {
                if (ms_between(probe.stamp, started) > kHeldMs) {
                    ++own.samples.held;
                } else {
                    own.samples.to_step.push_back(ms_between(probe.stamp, started));
                    own.samples.to_first.push_back(ms_between(probe.stamp, probe.first));
                    own.samples.to_full.push_back(ms_between(probe.stamp, now));
                }
                done = true;
            }
        }
        if (!done) {
            core::at(own.waiting, kept) = probe;
            core::at(own.step_started_at, kept) = started;
            ++kept;
        }
    }
    own.waiting.resize(kept);
    own.step_started_at.resize(kept);
    if (own.samples.to_full.size() >= kReportEvery) {
        report();
    }
}

void stop()
{
    ProbeState& own = probe_state();
    own.stopping.store(true);
    if (own.pusher.joinable()) {
        own.pusher.join();
    }
}

} // namespace latency_probe
