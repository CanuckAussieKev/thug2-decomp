// NOT ORIGINAL - the simulation's tick rate (see tick_rate.h). Pure: the ini is read in tick_rate_settings.cpp.

#include "engine/sys/tick_rate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace tick_rate {

namespace {

struct TickState {
    int rate = kOriginal;
    float step = 1.0F / static_cast<float>(kOriginal);
    double seconds = 1.0 / kOriginal;
};

TickState& tick_state()
{
    static TickState state;
    return state;
}

} // namespace

int parse(const char* text)
{
    std::string value = text != nullptr ? text : "";
    const auto first = value.find_first_not_of(" \t");
    const auto last = value.find_last_not_of(" \t\r\n");
    value = first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
    if (value.empty()) {
        return kOriginal;
    }
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    // ("120hz" / "120 Hz" read as 120)
    while (end != nullptr && (*end == ' ' || std::tolower(static_cast<unsigned char>(*end)) == 'h' ||
                              std::tolower(static_cast<unsigned char>(*end)) == 'z')) {
        ++end;
    }
    if (end == value.c_str() || end == nullptr || *end != '\0' || number <= 0) {
        return kOriginal;
    }
    return static_cast<int>(std::clamp<long>(number, kLowest, kHighest));
}

void set_rate(int rate)
{
    TickState& state = tick_state();
    state.rate = std::clamp(rate, kLowest, kHighest);
    // (computed, not a table: at 60 the division gives the original's constant to the bit - tick_rate_check)
    state.step = 1.0F / static_cast<float>(state.rate);
    state.seconds = 1.0 / state.rate;
}

int rate() { return tick_state().rate; }

bool original() { return tick_state().rate == kOriginal; }

float step_seconds() { return tick_state().step; }

double tick_seconds() { return tick_state().seconds; }

float rate_float() { return static_cast<float>(tick_state().rate); }

uint64_t counter_ticks(uint64_t frequency) { return frequency / static_cast<uint64_t>(tick_state().rate); }

int clock_rate(int configured) { return original() ? configured : tick_state().rate; }

uint64_t ticks_for(uint64_t ticks_at_60)
{
    if (original() || ticks_at_60 == 0) {
        return ticks_at_60;
    }
    const uint64_t scaled =
        ((ticks_at_60 * static_cast<uint64_t>(tick_state().rate)) + (kOriginal / 2)) / static_cast<uint64_t>(kOriginal);
    return std::max<uint64_t>(1, scaled);
}

double per_tick_factor(double factor_per_60th)
{
    if (original()) {
        return factor_per_60th;
    }
    return std::pow(factor_per_60th, static_cast<double>(kOriginal) / tick_state().rate);
}

} // namespace tick_rate
