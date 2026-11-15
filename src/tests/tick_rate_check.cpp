// NOT ORIGINAL - thug2_tick_rate_check.exe: [Graphics] tick_rate's maths without the game (engine/sys/tick_rate.h,
// docs/design/tick-rate.md).
//
// 1. At 60 every conversion is the original's to the bit: the step's float (0x3C888889, the constants it replaced -
//    stopwatch's 1.0F / 60, the camera lerps' 0x0064BA64, the cutscene's 0x006463B8), the loop's QPF / 60, the clock's
//    rate (60, 50 for PAL), counts of ticks, per-tick factors, and the render-only frames' schedule (frame_pacing.h:
//    the old formulas, written out here, against the new ones over many ticks).
// 2. At 120 / 144 / 240 the conversions are what they should be.
// 3. The exactness yardstick (`--harness`, also run by default): the game's integration kernels - written out as the
//    game's code computes them, with the same float rounding - stepped at 60 and at 120 / 144 / 240 over the same
//    simulated time; the table shows how far each lands from the 60 Hz run (an ollie on flat ground, a roll to a stop,
//    the camera's distance lerp, per-tick decays and counters, a truncated-millisecond timer). The in-game yardstick on
//    a recorded run is tools/game/tick_compare.ts (a demo played at another tick rate). Exit code 0 when 1 and 2 hold;
//    the harness only reports.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/net/net_cadence.h"
#include "engine/sys/frame_pacing.h"
#include "engine/sys/tick_rate.h"
#include "tests/no_error_dialogs.h"

namespace {

int g_failures = 0;
int g_checked = 0;

void expect(bool ok, const std::string& what)
{
    ++g_checked;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

template <typename T> bool same_bits(T a, T b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

// A small deterministic generator for sample values (not the game's).
struct Lcg {
    uint32_t state = 0x12345678U;
    uint32_t next()
    {
        state = (state * 1664525U) + 1013904223U;
        return state;
    }
    float unit() { return static_cast<float>(next() >> 8U) / static_cast<float>(1U << 24U); }
};

// --- 1. the original rate ---

// frame_pacing.cpp's formulas as they were before the tick rate (a 60 constant).
int old_frames_this_tick(int max_fps, double* carry)
{
    if (max_fps == frame_pacing::kOriginal) {
        *carry = 0.0;
        return 1;
    }
    if (max_fps < 0) {
        *carry = 0.0;
        return frame_pacing::kUncappedFrames;
    }
    const double share = (static_cast<double>(max_fps) / 60) + *carry;
    const double whole = std::floor(share + 1e-9);
    *carry = std::clamp(share - whole, 0.0, 1.0);
    return std::max(1, static_cast<int>(whole));
}

uint32_t old_shown_time_ms(uint32_t now_ms, float alpha)
{
    constexpr double kTickMs = 1000.0 / 60;
    const double behind = (1.0 - std::clamp(static_cast<double>(alpha), 0.0, 1.0)) * kTickMs;
    const auto back = static_cast<uint32_t>(std::lround(behind));
    return now_ms >= back ? now_ms - back : 0U;
}

void check_original_rate()
{
    tick_rate::set_rate(60);
    expect(tick_rate::rate() == 60 && tick_rate::original(), "60: the rate");
    const float step = tick_rate::step_seconds();
    expect(std::bit_cast<uint32_t>(step) == 0x3C888889U, "60: the step's bits are 0x3C888889");
    expect(same_bits(step, 1.0F / 60.0F), "60: the step is stopwatch's 1.0F / 60");
    expect(same_bits(step, 0.01666666753590107F), "60: the step is the cutscene's 0x006463B8");
    expect(same_bits(step, std::bit_cast<float>(0x3C888889U)), "60: the step is the camera lerps' 0x0064BA64");
    expect(tick_rate::rate_float() == 60.0F, "60: the rate as a float (the pedestrians' turn frames)");
    expect(same_bits(tick_rate::tick_seconds(), 1.0 / 60.0), "60: the tick in seconds");

    // frame_clock::get_slomo_frame_seconds: slomo * double(the step), as slomo * double(1.0F / 60)
    Lcg lcg;
    bool slomo_same = true;
    for (int i = 0; i < 100000; ++i) {
        const float slomo = i < 4 ? std::array{1.0F, 0.5F, 0.25F, 2.0F}[static_cast<size_t>(i)] : lcg.unit() * 4.0F;
        const double before = slomo * static_cast<double>(1.0F / 60.0F);
        const double after = slomo * static_cast<double>(tick_rate::step_seconds());
        slomo_same = slomo_same && same_bits(before, after);
    }
    expect(slomo_same, "60: get_slomo_frame_seconds bit for bit (100000 slow motions)");

    // the main loop's tick: QueryPerformanceFrequency / 60 (an unsigned division)
    bool counter_same = true;
    for (const uint64_t frequency :
         {uint64_t{10000000}, uint64_t{3579545}, uint64_t{2405000000}, uint64_t{1000000000}, uint64_t{14318180}}) {
        counter_same = counter_same && tick_rate::counter_ticks(frequency) == frequency / 60;
    }
    for (int i = 0; i < 10000; ++i) {
        const uint64_t frequency = (static_cast<uint64_t>(lcg.next()) << 8U) + lcg.next();
        counter_same = counter_same && tick_rate::counter_ticks(frequency) == frequency / 60;
    }
    expect(counter_same, "60: the loop's tick is QPF / 60");

    // the clock's vblanks a second: game_config::frame_rate (60, or 50 with the PAL command line) - get_time unchanged
    expect(tick_rate::clock_rate(60) == 60 && tick_rate::clock_rate(50) == 50, "60: the clock's rate is frame_rate");
    bool time_same = true;
    for (uint32_t vblanks = 0; vblanks < 200000; vblanks += 7) {
        const auto before = static_cast<uint32_t>(
            static_cast<int64_t>((1000.0 / static_cast<float>(60)) * static_cast<double>(vblanks)));
        const auto after = static_cast<uint32_t>(static_cast<int64_t>(
            (1000.0 / static_cast<float>(tick_rate::clock_rate(60))) * static_cast<double>(vblanks)));
        time_same = time_same && before == after;
    }
    expect(time_same, "60: get_time's milliseconds");

    bool counts_same = true;
    for (uint64_t n = 0; n < 5000; ++n) {
        counts_same = counts_same && tick_rate::ticks_for(n) == n;
    }
    expect(counts_same, "60: counts of ticks unchanged (the loop's 20 / 4 / 200)");
    bool factors_same = true;
    for (int i = 0; i < 10000; ++i) {
        const double factor = lcg.unit() * 2.0;
        factors_same = factors_same && same_bits(tick_rate::per_tick_factor(factor), factor);
    }
    expect(factors_same, "60: per-tick factors unchanged");
    bool shares_same = true;
    for (int i = 0; i < 10000; ++i) {
        const double fraction = lcg.unit();
        const auto amount = static_cast<float>(lcg.unit() * 100.0);
        shares_same = shares_same && same_bits(tick_rate::per_tick_fraction(fraction), fraction) &&
                      same_bits(tick_rate::per_tick_amount(amount), amount);
    }
    expect(shares_same, "60: per-tick blend fractions and amounts unchanged (the peds' turn, rigid bodies' damping, fades)");
    bool grid_same = true;
    for (uint64_t n = 0; n < 100000; ++n) {
        grid_same = grid_same && tick_rate::frames_ended_by(n) == n && tick_rate::frame_of_tick(n) == n;
    }
    expect(grid_same, "60: the 60 Hz grid is the ticks (every tick a frame: timers, counters, script waits as before)");

    // the render-only frames' schedule
    bool pacing_same = true;
    for (const int max_fps : {0, -1, 60, 61, 75, 90, 120, 144, 165, 200, 240, 360, 1000}) {
        double carry_old = 0.0;
        double carry_new = 0.0;
        for (int tick = 0; tick < 2000; ++tick) {
            const int a = old_frames_this_tick(max_fps, &carry_old);
            const int b = frame_pacing::frames_this_tick(max_fps, &carry_new);
            pacing_same = pacing_same && a == b && same_bits(carry_old, carry_new);
        }
    }
    expect(pacing_same, "60: frames_this_tick as before (13 caps x 2000 ticks)");
    bool shown_same = true;
    for (uint32_t now = 0; now < 5000; now += 3) {
        for (int a = 0; a <= 64; ++a) {
            const float alpha = static_cast<float>(a) / 64.0F;
            shown_same = shown_same && old_shown_time_ms(now, alpha) == frame_pacing::shown_time_ms(now, alpha);
        }
    }
    expect(shown_same, "60: shown_time_ms as before");

    expect(tick_rate::parse(nullptr) == 60 && tick_rate::parse("") == 60 && tick_rate::parse("60") == 60 &&
               tick_rate::parse("junk") == 60 && tick_rate::parse("0") == 60 && tick_rate::parse("-5") == 60,
           "parse: the original for nothing / junk");
    expect(tick_rate::parse("120") == 120 && tick_rate::parse(" 144Hz ") == 144 && tick_rate::parse("240 hz") == 240 &&
               tick_rate::parse("30") == 60 && tick_rate::parse("500") == 240,
           "parse: rates, clamped to 60 - 240");
}

// --- 2. other rates ---

void check_other_rates()
{
    for (const int rate : {120, 144, 240}) {
        tick_rate::set_rate(rate);
        const std::string at = std::to_string(rate) + ": ";
        expect(!tick_rate::original() && tick_rate::rate() == rate, at + "the rate");
        expect(same_bits(tick_rate::step_seconds(), 1.0F / static_cast<float>(rate)), at + "the step");
        expect(tick_rate::counter_ticks(10000000) == 10000000ULL / static_cast<uint64_t>(rate), at + "the loop's tick");
        expect(tick_rate::clock_rate(60) == rate && tick_rate::clock_rate(50) == rate, at + "the clock counts ticks");
        expect(tick_rate::ticks_for(20) == static_cast<uint64_t>(std::lround(20.0 * rate / 60.0)) &&
                   tick_rate::ticks_for(1) >= 1 && tick_rate::ticks_for(0) == 0,
               at + "counts of ticks scaled");
        const double factor = 0.9;
        expect(std::fabs(std::pow(tick_rate::per_tick_factor(factor), rate / 60.0) - factor) < 1e-12,
               at + "a per-tick factor compounds to the 1/60 s one");
        double carry = 0.0;
        int frames = 0;
        for (int tick = 0; tick < rate; ++tick) {
            frames += frame_pacing::frames_this_tick(240, &carry);
        }
        expect(frames == std::max(240, rate), at + "max_fps=240 over a second of ticks");
        // the 60 Hz grid: 60 frames start in a second of ticks, at most one a tick, the first with the first tick
        bool starts_ok = tick_rate::frame_of_tick(1) == 1;
        uint64_t starts = 0;
        for (uint64_t tick = 1; tick <= static_cast<uint64_t>(rate) * 10; ++tick) {
            const uint64_t started = tick_rate::frame_of_tick(tick) - tick_rate::frame_of_tick(tick - 1);
            starts_ok = starts_ok && started <= 1;
            starts += started;
        }
        expect(starts_ok && starts == 600 && tick_rate::frames_ended_by(static_cast<uint64_t>(rate) * 10) == 600,
               at + "the 60 Hz grid: 60 frames a second, one at most a tick");
        const double share = tick_rate::per_tick_fraction(0.083);
        expect(std::fabs((1.0 - std::pow(1.0 - share, rate / 60.0)) - 0.083) < 1e-12,
               at + "a per-tick blend compounds to the 1/60 s one");
        expect(std::fabs((static_cast<double>(tick_rate::per_tick_amount(0.02F)) * rate / 60.0) - 0.02) < 1e-7,
               at + "a per-tick amount sums to the 1/60 s one");
    }
    tick_rate::set_rate(1000);
    expect(tick_rate::rate() == 240, "set_rate clamps");
    tick_rate::set_rate(60);
}

// --- online: the network's cadence (engine/net/net_cadence.h) ---

// One skater update as it leaves: the app's clock stamp and frame count.
struct Sent {
    uint32_t clock = 0;
    uint32_t frame = 0;
    bool operator==(const Sent&) const = default;
};

// 10 s of a client's skater updates at `rate`: the network runs in the ticks that end a 1/60 s frame
// (frame_clock::grid_frame_ends - tick_rate::ticks_since_frame_end is 0 there), the clock is the 60 Hz grid's
// milliseconds (frame_clock::get_time), the app counts a frame each time it runs, the send decision is the game's
// (net_cadence::skater_update_due; the connection's send interval two frames of 60 a second, 33 ms). `gated` false: the
// network every tick as before phase 5.
std::vector<Sent> skater_updates(int rate, bool gated)
{
    tick_rate::set_rate(rate);
    constexpr uint32_t kTransmitPeriod = 2 * 1000 / 60;
    std::vector<Sent> out;
    uint32_t frame_count = 0;
    uint32_t last_transmit = 0;
    for (uint64_t tick = 1; tick <= static_cast<uint64_t>(rate) * 10; ++tick) {
        if (gated && tick_rate::ticks_since_frame_end(tick) != 0) {
            continue;
        }
        const auto clock = static_cast<uint32_t>(
            static_cast<int64_t>((1000.0 / 60) * static_cast<double>(static_cast<uint32_t>(tick_rate::frame_of_tick(tick)))));
        ++frame_count;
        if (net_cadence::skater_update_due({.on_server = false,
                                            .frame_count = frame_count,
                                            .clock_ms = clock,
                                            .last_transmit_ms = last_transmit,
                                            .transmit_period_ms = kTransmitPeriod})) {
            last_transmit = clock;
            out.push_back({.clock = clock, .frame = frame_count});
        }
    }
    return out;
}

void check_online_cadence()
{
    const std::vector<Sent> at60 = skater_updates(60, true);
    expect(at60 == skater_updates(60, false), "online at 60: the network every tick, as the original");
    for (const int rate : {120, 144, 240}) {
        const std::string at = std::to_string(rate) + ": ";
        expect(skater_updates(rate, true) == at60,
               at + "online: the same skater updates (clock stamps, frame counts) as at 60 - " +
                   std::to_string(at60.size()) + " in 10 s");
        // the remote skaters' playback clock (nonlocal_playback.cpp playback_clock): the network's clock plus the tick
        // time since it ran - never going back, the tick's own time between the network's ticks
        bool monotone = true;
        bool follows = true;
        uint32_t clock = 0;
        uint64_t clock_tick = 0;
        uint32_t previous = 0;
        const auto tick_ms = [rate](uint64_t tick) {
            return static_cast<uint32_t>(static_cast<int64_t>((1000.0 / rate) * static_cast<double>(static_cast<uint32_t>(tick))));
        };
        for (uint64_t tick = 1; tick <= static_cast<uint64_t>(rate) * 10; ++tick) {
            const uint64_t since = tick_rate::ticks_since_frame_end(tick);
            if (since == 0) {
                clock = static_cast<uint32_t>(static_cast<int64_t>(
                    (1000.0 / 60) * static_cast<double>(static_cast<uint32_t>(tick_rate::frame_of_tick(tick)))));
                clock_tick = tick;
            }
            follows = follows && clock_tick == tick - since;
            const uint32_t played = clock + (tick_ms(tick) - tick_ms(clock_tick));
            monotone = monotone && played >= previous;
            previous = played;
        }
        expect(monotone && follows, at + "online: the remote skaters' playback clock moves every tick, never back");
    }
    tick_rate::set_rate(60);
}

// --- 3. the exactness yardstick ---

struct Row {
    std::string kernel;
    std::string metric;
    std::string unit;
    double at60 = 0.0;
    std::array<double, 3> at{}; // 120, 144, 240
};
std::vector<Row> g_rows;
constexpr std::array<int, 3> kRates = {120, 144, 240};

// The skater's move (core_physics.cpp: position += v t + g t^2 / 2, velocity += g t, with its float rounding), an ollie
// off flat ground at y = 0 that lands when a step ends below it (the landing feeler finds the ground on the step's
// segment: the point there). `exact_event`: the landing at the time the arc meets the ground inside the step instead
// (what the next phases do for landings).
struct Jump {
    double apex = 0.0;
    double airtime = 0.0;
    double land_x = 0.0;
    double land_vy = 0.0;
};
Jump ollie(int rate, bool exact_event)
{
    tick_rate::set_rate(rate);
    const auto dt = static_cast<float>(1.0 * tick_rate::step_seconds()); // (slow motion 1)
    constexpr float kGravity = -1100.0F; // (illustrative values in the game's units: inches, seconds)
    float x = 0.0F;
    float y = 0.0F;
    float vx = 400.0F;
    float vy = 330.0F;
    Jump out;
    double time = 0.0;
    for (int tick = 0; tick < 100000; ++tick) {
        const double half = static_cast<double>(dt) * dt * 0.5F;
        const auto half_gravity = static_cast<float>(half * kGravity);
        const float ty = dt * vy;
        const auto sx = static_cast<float>(static_cast<double>(vx) * dt);
        const float new_x = sx + x;
        const float new_y = (ty + half_gravity) + y;
        const float new_vy = vy + (dt * kGravity);
        if (new_y < 0.0F) {
            if (exact_event) {
                // (the arc's own time to the ground inside the step: y + vy s + g s^2 / 2 = 0)
                const double a = 0.5 * kGravity;
                const double s = (-vy - std::sqrt((static_cast<double>(vy) * vy) - (4.0 * a * y))) / (2.0 * a);
                out.airtime = time + s;
                out.land_x = x + (static_cast<double>(vx) * s);
                out.land_vy = vy + (kGravity * s);
            } else {
                const double along = y / (static_cast<double>(y) - new_y); // (where the segment meets the ground)
                out.airtime = time + dt; // (the landing is seen at the step's end)
                out.land_x = x + ((new_x - x) * along);
                out.land_vy = new_vy;
            }
            break;
        }
        x = new_x;
        y = new_y;
        vy = new_vy;
        time += dt;
        out.apex = std::max(out.apex, static_cast<double>(y));
    }
    return out;
}

// A roll to a stop on flat ground: rolling friction (core_physics_ground.cpp: speed less dt / speed * rolling * 60 of
// itself) and the quadratic drag of apply_friction (speed^2 * dt * friction * 60), then the move.
struct Roll {
    double distance = 0.0;
    double time = 0.0;
    double speed_at_1s = 0.0;
};
// `closed_form`: the step integrated exactly for this force law (what the next phases do for the skater's frictions).
Roll roll(int rate, bool closed_form)
{
    tick_rate::set_rate(rate);
    const float dt = tick_rate::step_seconds();
    constexpr float kRolling = 4.0F;
    constexpr float kDrag = 0.00002F;
    float v = 900.0F;
    double x = 0.0;
    Roll out;
    double time = 0.0;
    for (int tick = 0; tick < 1000000 && v > 0.0F; ++tick) {
        if (closed_form) {
            // (dv/dt = -(a + k v^2) solved over the step: v = sqrt(a/k) tan(theta - c t), c = sqrt(a k); the distance its
            // integral, ln(cos(theta - c t) / cos(theta)) / k; the stop at its own time theta / c, not the step's end)
            const double a = kRolling * 60.0;
            const double k = static_cast<double>(kDrag) * 60.0;
            const double c = std::sqrt(a * k);
            const double theta = std::atan(v * std::sqrt(k / a));
            const double moving = std::min(static_cast<double>(dt), theta / c);
            x += std::log(std::cos(theta - (c * moving)) / std::cos(theta)) / k;
            v = moving < dt ? 0.0F : static_cast<float>(std::sqrt(a / k) * std::tan(theta - (c * dt)));
            time += moving;
            if (out.speed_at_1s == 0.0 && time >= 1.0 - 1e-9) {
                out.speed_at_1s = v;
            }
            continue;
        }
        const auto slowing = static_cast<float>(static_cast<double>(v) * v * dt * kDrag * 60.0F);
        v = std::max(0.0F, v - slowing);
        const auto rolling = static_cast<float>(static_cast<double>(dt) * kRolling * 60.0F);
        v = std::max(0.0F, v - rolling);
        x += static_cast<double>(v) * dt;
        time += dt;
        if (out.speed_at_1s == 0.0 && time >= 1.0 - 1e-9) {
            out.speed_at_1s = v;
        }
    }
    out.distance = x;
    out.time = time;
    return out;
}

// The roll as the game steps it at another rate since phase 3b (SkaterPhysicsModule::forces_this_tick): the frictions act
// once a 1/60 s frame, in the tick that starts it, over the whole 1/60 s; every tick moves by its step with the velocity
// that leaves - the 60 Hz scheme's semi-implicit step (velocity first, then the move with it) cut into ticks. At 60: the
// scheme itself.
Roll roll_grid(int rate)
{
    tick_rate::set_rate(rate);
    const float dt = tick_rate::step_seconds();
    const float h = 1.0F / 60.0F;
    constexpr float kRolling = 4.0F;
    constexpr float kDrag = 0.00002F;
    // (a frame starts in a tick: 120 and 240 at its start, 144 inside it - its tick takes the frame's forces then)
    float v = 900.0F;
    double x = 0.0;
    Roll out;
    double time = 0.0;
    for (int tick = 0; tick < 1000000; ++tick) {
        if (tick == 0 || (tick * 60) / rate != ((tick - 1) * 60) / rate) {
            if (v <= 0.0F) {
                break;
            }
            const auto slowing = static_cast<float>(static_cast<double>(v) * v * h * kDrag * 60.0F);
            v = std::max(0.0F, v - slowing);
            const auto rolling = static_cast<float>(static_cast<double>(h) * kRolling * 60.0F);
            v = std::max(0.0F, v - rolling);
        }
        x += static_cast<double>(v) * dt;
        time += dt;
        if (out.speed_at_1s == 0.0 && time >= 1.0 - 1e-9) {
            out.speed_at_1s = v;
        }
    }
    out.distance = x;
    out.time = time;
    return out;
}

// The roll at another rate made to follow the 60 Hz run itself (the target: 60 Hz stays as it is, so "the same at any
// rate" means the same as the 60 Hz scheme, not as the exact solution). The 60 Hz scheme - v -= f h, then x += v h, h =
// 1/60 - is to second order the exact solution of its modified equations dv/dt = f - (h/2) f' f and dx/dt = v + (h/2)
// dv/dt; those integrated finely (RK4 at the tick) land on the 60 Hz run's numbers at any tick rate. At 60: the scheme.
Roll roll_matched(int rate)
{
    if (rate == 60) {
        return roll(60, false);
    }
    tick_rate::set_rate(rate);
    const double dt = tick_rate::step_seconds();
    constexpr double kH = 1.0 / 60.0;
    const double a = 4.0 * 60.0;               // (kRolling, kDrag as roll's)
    const double k = static_cast<double>(0.00002F) * 60.0;
    const auto dv = [&](double v) {
        const double f = -(a + (k * v * v));
        const double df = -2.0 * k * v;
        return f - (0.5 * kH * df * f);
    };
    const auto dx = [&](double v) { return v + (0.5 * kH * dv(v)); };
    double v = 900.0;
    double x = 0.0;
    double time = 0.0;
    Roll out;
    while (v > 0.0 && time < 100.0) {
        const double k1v = dv(v);
        const double k1x = dx(v);
        const double k2v = dv(v + (0.5 * dt * k1v));
        const double k2x = dx(v + (0.5 * dt * k1v));
        const double k3v = dv(v + (0.5 * dt * k2v));
        const double k3x = dx(v + (0.5 * dt * k2v));
        const double k4v = dv(v + (dt * k3v));
        const double k4x = dx(v + (dt * k3v));
        const double next_v = v + (dt / 6.0 * (k1v + (2.0 * k2v) + (2.0 * k3v) + k4v));
        const double step_x = dt / 6.0 * (k1x + (2.0 * k2x) + (2.0 * k3x) + k4x);
        if (next_v <= 0.0) {
            // (the stop: the 60 Hz scheme stops at the end of the tick its speed runs out in - the same time here, the
            // distance to it at the speed's own end)
            const double part = v / (v - next_v);
            x += step_x * part;
            time += dt;
            out.time = std::ceil((time - 1e-9) / kH) * kH;
            v = 0.0;
            break;
        }
        v = next_v;
        x += step_x;
        time += dt;
        if (out.speed_at_1s == 0.0 && time >= 1.0 - 1e-9) {
            out.speed_at_1s = v;
        }
    }
    out.distance = x;
    if (out.time == 0.0) {
        out.time = time;
    }
    return out;
}

// The skater camera's distance lerp (skater_camera_modes.cpp update_lerps: `time` and a step a tick, made by
// change_regime from one_frame() / time) from 100 to 160 over 0.3 s: where it ends and when.
struct Lerp {
    double final_value = 0.0;
    double done_at = 0.0;
};
Lerp camera_lerp(int rate, bool clamped = false)
{
    tick_rate::set_rate(rate);
    const float one_frame = tick_rate::step_seconds();
    float behind = 100.0F;
    const float target = 160.0F;
    float time = 0.3F;
    const double per_frame = static_cast<double>(one_frame) / time;
    const auto step = static_cast<float>((static_cast<double>(target) - behind) * per_frame);
    Lerp out;
    double t = 0.0;
    while (time > 0.0F) {
        // (clamped - phase 3, update_lerps at a rate other than 60: the last step the share of a step left)
        const double share = clamped && rate != 60 && time < one_frame ? static_cast<double>(time) / one_frame : 1.0;
        time = static_cast<float>(static_cast<double>(time) - one_frame);
        behind = static_cast<float>((static_cast<double>(step) * share) + behind);
        t += tick_rate::tick_seconds();
    }
    out.final_value = behind;
    out.done_at = t;
    return out;
}

// A per-tick decay without the step (ped_logic_move.cpp's bias *= decay, rigid bodies' damp - phase 3): what is left
// after 1 s, as it is and with the step's factor (tick_rate::per_tick_factor).
double decay_left(int rate, bool scaled)
{
    tick_rate::set_rate(rate);
    const double factor = scaled ? tick_rate::per_tick_factor(0.95) : 0.95;
    double value = 1.0;
    for (int tick = 0; tick < rate; ++tick) {
        value *= factor;
    }
    return value;
}

// A goal timer that adds the step's milliseconds truncated each tick (goal_run.cpp's trunc(dt * 1000) - phase 3): the
// milliseconds it counts in 60 real seconds, as it is, keeping the fraction (real time - not the 60 Hz run's 57.6 s),
// and on the 60 Hz grid (the 60 Hz run's 16 ms each time a 1/60 s has passed - its own count at any rate).
double timer_ms(int rate, bool keep_fraction, bool grid = false)
{
    tick_rate::set_rate(rate);
    double counted = 0.0;
    double fraction = 0.0;
    for (int tick = 0; tick < rate * 60; ++tick) {
        if (grid) {
            const int64_t before = static_cast<int64_t>(tick) * 60 / rate;
            const int64_t after = static_cast<int64_t>(tick + 1) * 60 / rate;
            counted += static_cast<double>(after - before) * std::trunc(1000.0 * static_cast<double>(1.0F / 60.0F));
            continue;
        }
        const double ms = static_cast<double>(tick_rate::step_seconds()) * 1000.0;
        if (keep_fraction) {
            fraction += ms;
            const double whole = std::floor(fraction);
            counted += whole;
            fraction -= whole;
        } else {
            counted += std::trunc(ms);
        }
    }
    return counted;
}

// A window counted in ticks (the late wallplant's LateWallplantFrames, the throw wait - phase 3): its length in
// seconds as it is (N ticks) and scaled (ticks_for(N)).
double window_seconds(int rate, bool scaled)
{
    tick_rate::set_rate(rate);
    constexpr uint64_t kTicksAt60 = 10;
    const uint64_t ticks = scaled ? tick_rate::ticks_for(kTicksAt60) : kTicksAt60;
    return static_cast<double>(ticks) / rate;
}

// A script's `Wait 0.5 seconds` (script_qb/cscript_wait.cpp update_wait: counted down by the step each Update, done in
// the Update that finds less than a step left, the first Update only zeroing that): the 1/60 s frame it goes on in
// (as seconds), counting down by the tick's step each tick (as it was) and on the 60 Hz grid (each 1/60 s, in the tick
// that starts the frame, by the original's 1/60 s step - phase 3).
double wait_ends(int rate, bool grid)
{
    tick_rate::set_rate(rate);
    float clock = 0.5F;
    bool started = false;
    for (uint64_t tick = 1; tick < 100000; ++tick) {
        if (grid && tick_rate::frame_of_tick(tick) == tick_rate::frame_of_tick(tick - 1)) {
            continue;
        }
        const double frame = grid ? static_cast<double>(1.0F / 60.0F) : static_cast<double>(tick_rate::step_seconds());
        if (frame > clock) {
            if (started) {
                return static_cast<double>(tick_rate::frame_of_tick(tick)) / 60.0;
            }
            clock = 0.0F;
        } else {
            clock = static_cast<float>(clock - frame);
        }
        started = true;
    }
    return 0.0;
}

// A quarterpipe ridden at the tick rate (phase 4's question: what true N Hz stepping costs). A side view: flat ground,
// then a quarter circle of radius 120 in made of 12 flat faces (7.5 degrees each, as a level's mesh), then 12 in of
// vertical wall to the lip; ground gravity -1100 in/s^2, in at 700 in/s, no friction. Two models:
// - `stepped`: the original's ground step, run once a tick at the tick's length (core_ground.cpp do_ground_physics: the
//   velocity turned into the face's plane keeping its speed, gravity along the face, a straight move by v h, then the
//   ground feeler along the face's up puts it on the face it finds) - true N Hz stepping of the original scheme;
// - `continuous`: the same model solved exactly - along a face with its constant acceleration, a face's end crossed at
//   its own time and the velocity turned onto the next face there - the same at every tick length (the 60 Hz run too).
// Reports the lip's take-off speed and the air's height above the lip.
struct Quarterpipe {
    double launch_vy = 0.0;
    double apex = 0.0;
    double lip_time = 0.0;
};
Quarterpipe quarterpipe(int rate, bool continuous)
{
    tick_rate::set_rate(rate);
    const double h = tick_rate::tick_seconds();
    constexpr double kRadius = 120.0;
    constexpr int kFaces = 12;
    constexpr double kWall = 12.0;
    constexpr double kGravity = -1100.0;
    constexpr double kPi = 3.14159265358979323846;
    // the faces: [0] the flat ground from x = -400, [1..12] the arc, [13] the wall; vertices along the surface
    std::vector<std::array<double, 2>> point = {{-400.0, 0.0}};
    for (int k = 0; k <= kFaces; ++k) {
        const double angle = (-kPi / 2) + ((kPi / 2) * k / kFaces);
        point.push_back({kRadius * std::cos(angle), kRadius + (kRadius * std::sin(angle))});
    }
    point.push_back({kRadius, kRadius + kWall});
    const int faces = static_cast<int>(point.size()) - 1;
    const auto tangent = [&](int f) {
        const double dx = point[f + 1][0] - point[f][0];
        const double dy = point[f + 1][1] - point[f][1];
        const double length = std::hypot(dx, dy);
        return std::array<double, 2>{dx / length, dy / length};
    };
    const double top = kRadius + kWall;
    Quarterpipe out;
    double px = -300.0;
    double py = 0.0;
    double vx = 700.0;
    double vy = 0.0;
    int face = 0;
    double time = 0.0;
    const auto take_off = [&](double t_lip) {
        out.launch_vy = vy;
        out.lip_time = t_lip;
        out.apex = (vy * vy) / (-2.0 * kGravity); // (straight up from the lip: the air's height, exact at any rate)
    };
    if (continuous) {
        // along face `face` at distance s from its start, speed u along it
        double s = std::hypot(px - point[0][0], py - point[0][1]);
        double u = vx;
        for (int tick = 0; tick < 100000; ++tick) {
            double left = h;
            while (left > 0.0) {
                const std::array<double, 2> t = tangent(face);
                const double a = kGravity * t[1];
                const double length = std::hypot(point[face + 1][0] - point[face][0], point[face + 1][1] - point[face][1]);
                // time to the face's end: s + u r + a r^2 / 2 = length
                const double c = s - length;
                double r = 0.0;
                if (std::fabs(a) < 1e-12) {
                    r = u > 0.0 ? -c / u : 1e30;
                } else {
                    const double disc = (u * u) - (2.0 * a * c);
                    r = disc < 0.0 ? 1e30 : (-u + std::sqrt(disc)) / a;
                    if (r < 0.0) {
                        r = (-u - std::sqrt(disc)) / a;
                    }
                }
                if (r > left) {
                    s += (u * left) + (0.5 * a * left * left);
                    u += a * left;
                    left = 0.0;
                    continue;
                }
                u += a * r;
                left -= r;
                if (face + 1 == faces) {
                    vy = u; // (the wall is vertical: all of it up)
                    take_off(time + (h - left));
                    return out;
                }
                ++face; // (the velocity turned onto the next face, its speed kept)
                s = 0.0;
            }
            time += h;
        }
        return out;
    }
    for (int tick = 0; tick < 100000; ++tick) {
        const std::array<double, 2> t = tangent(face);
        // the velocity into the face's plane, its speed kept (align_to_plane)
        const double speed = std::hypot(vx, vy);
        const double sign = ((vx * t[0]) + (vy * t[1])) < 0.0 ? -1.0 : 1.0;
        vx = sign * speed * t[0];
        vy = sign * speed * t[1];
        // gravity along the face
        const double along = kGravity * t[1];
        vx += along * t[0] * h;
        vy += along * t[1] * h;
        const double nx = px + (vx * h);
        const double ny = py + (vy * h);
        time += h;
        // the ground feeler along the face's up (n = (-t.y, t.x)): the first face it meets
        const double ux = -t[1];
        const double uy = t[0];
        bool found = false;
        for (int f = 0; f < faces && !found; ++f) {
            const double ax = point[f][0];
            const double ay = point[f][1];
            const double ex = point[f + 1][0] - ax;
            const double ey = point[f + 1][1] - ay;
            // nx + ux q = ax + ex w (q in the feeler's span, w in [0, 1])
            const double det = (ux * -ey) - (uy * -ex);
            if (std::fabs(det) < 1e-12) {
                continue;
            }
            const double bx = ax - nx;
            const double by = ay - ny;
            const double q = ((bx * -ey) - (by * -ex)) / det;
            const double w = ((ux * by) - (uy * bx)) / det;
            if (w >= 0.0 && w <= 1.0 && q >= -200.0 && q <= 36.0) {
                px = nx + (ux * q);
                py = ny + (uy * q);
                face = f;
                found = true;
            }
        }
        if (!found || ny > top) {
            // past the lip: in the air with the step's velocity (straight up the wall)
            take_off(time);
            return out;
        }
    }
    return out;
}

// A manual's balance (manual_balance.cpp Balance::tick_frame - illustrative values): the lean grows by itself (lean +=
// (g lean + lean speed) i a 1/60 s frame, i the instability) and the buttons push the lean speed back (acc a frame);
// the same button presses in time (left 0.30 - 0.55 s, right 1.10 - 1.25 s, left 1.90 - 2.05 s). `stepped`: the
// original's frame update once a tick at the tick's length (its amounts times dt x 60); `exact`: the same linear model
// solved exactly over each tick (the same at every tick length), the buttons read at the tick's start; `timed`: solved
// exactly with each press taking effect at its own time inside the tick (input events carry their time). The lean at 3 s.
double balance_lean(int rate, bool exact, bool timed = false)
{
    tick_rate::set_rate(rate);
    const double h = tick_rate::tick_seconds();
    constexpr double kGrow = 0.01;    // g i: a frame's growth of the lean by itself
    constexpr double kCarry = 0.2;    // i: a frame's lean from the lean speed
    constexpr double kAcc = 0.3;      // a frame's push of the lean speed
    double lean = 40.0;
    double speed = 0.0;
    const auto push = [](double t) {
        if (t >= 0.30 && t < 0.55) {
            return -kAcc;
        }
        if (t >= 1.10 && t < 1.25) {
            return kAcc;
        }
        if (t >= 1.90 && t < 2.05) {
            return -kAcc;
        }
        return 0.0;
    };
    const int ticks = rate * 3;
    for (int tick = 0; tick < ticks; ++tick) {
        const double t = tick * h;
        const double p = push(t + 1e-9) * 60.0; // (per second)
        if (!exact) {
            const double f = h * 60.0;
            lean = ((kGrow * f) + 1.0) * lean + (kCarry * speed * f);
            speed += p * h;
            continue;
        }
        // lean' = 60 g lean + 60 i speed, speed' = p: with k = 60 g, c = 60 i, over a span r exactly
        const auto solve = [&](double r, double push_now) {
            const double k = kGrow * 60.0;
            const double c = kCarry * 60.0;
            const double e = std::exp(k * r);
            const double next_lean =
                (lean * e) + (c * speed * (e - 1.0) / k) + (c * push_now * (((e - 1.0) / k) - r) / k);
            speed += push_now * r;
            lean = next_lean;
        };
        if (!timed) {
            solve(h, p);
            continue;
        }
        // (split at the presses' own times inside the tick)
        double from = t;
        for (const double edge : {0.30, 0.55, 1.10, 1.25, 1.90, 2.05, t + h}) {
            if (edge > from && edge <= t + h) {
                solve(edge - from, push(from + 1e-9) * 60.0);
                from = edge;
            }
        }
    }
    return lean;
}

template <typename F> void add_row(const std::string& kernel, const std::string& metric, const std::string& unit, F value)
{
    Row row{.kernel = kernel, .metric = metric, .unit = unit, .at60 = value(60)};
    for (size_t i = 0; i < kRates.size(); ++i) {
        row.at[i] = value(kRates[i]);
    }
    g_rows.push_back(row);
}

void run_harness()
{
    add_row("ollie (tick-end landing)", "apex", "in", [](int r) { return ollie(r, false).apex; });
    add_row("ollie (tick-end landing)", "airtime", "s", [](int r) { return ollie(r, false).airtime; });
    add_row("ollie (tick-end landing)", "landing x", "in", [](int r) { return ollie(r, false).land_x; });
    add_row("ollie (tick-end landing)", "landing vy", "in/s", [](int r) { return ollie(r, false).land_vy; });
    add_row("ollie (exact landing time)", "airtime", "s", [](int r) { return ollie(r, true).airtime; });
    add_row("ollie (exact landing time)", "landing x", "in", [](int r) { return ollie(r, true).land_x; });
    add_row("ollie (exact landing time)", "landing vy", "in/s", [](int r) { return ollie(r, true).land_vy; });
    add_row("roll to a stop (as is)", "distance", "in", [](int r) { return roll(r, false).distance; });
    add_row("roll to a stop (as is)", "stop time", "s", [](int r) { return roll(r, false).time; });
    add_row("roll to a stop (as is)", "speed at 1 s", "in/s", [](int r) { return roll(r, false).speed_at_1s; });
    add_row("roll to a stop (closed form)", "distance", "in", [](int r) { return roll(r, true).distance; });
    add_row("roll to a stop (closed form)", "stop time", "s", [](int r) { return roll(r, true).time; });
    add_row("roll (60 Hz scheme matched)", "distance", "in", [](int r) { return roll_matched(r).distance; });
    add_row("roll (60 Hz scheme matched)", "stop time", "s", [](int r) { return roll_matched(r).time; });
    add_row("roll (60 Hz scheme matched)", "speed at 1 s", "in/s", [](int r) { return roll_matched(r).speed_at_1s; });
    add_row("roll (forces once a frame)", "distance", "in", [](int r) { return roll_grid(r).distance; });
    add_row("roll (forces once a frame)", "speed at 1 s", "in/s", [](int r) { return roll_grid(r).speed_at_1s; });
    add_row("camera distance lerp", "final value", "in", [](int r) { return camera_lerp(r).final_value; });
    add_row("camera distance lerp", "done at", "s", [](int r) { return camera_lerp(r).done_at; });
    add_row("camera lerp (last step share)", "final value", "in", [](int r) { return camera_lerp(r, true).final_value; });
    add_row("per-tick decay 0.95 (as is)", "left after 1 s", "", [](int r) { return decay_left(r, false); });
    add_row("per-tick decay 0.95 (pow)", "left after 1 s", "", [](int r) { return decay_left(r, true); });
    add_row("goal timer (trunc ms)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, false); });
    add_row("goal timer (fraction kept)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, true); });
    add_row("goal timer (60 Hz grid)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, false, true); });
    add_row("script Wait 0.5 s (tick step)", "goes on in", "s", [](int r) { return wait_ends(r, false); });
    add_row("script Wait 0.5 s (60 Hz grid)", "goes on in", "s", [](int r) { return wait_ends(r, true); });
    add_row("10-tick window (as is)", "length", "s", [](int r) { return window_seconds(r, false); });
    add_row("10-tick window (ticks_for)", "length", "s", [](int r) { return window_seconds(r, true); });
    add_row("online: skater updates (net cadence)", "a second", "", [](int r) { return skater_updates(r, true).size() / 10.0; });
    add_row("online: skater updates (every tick)", "a second", "", [](int r) { return skater_updates(r, false).size() / 10.0; });
    add_row("quarterpipe (stepped a tick)", "take-off vy", "in/s", [](int r) { return quarterpipe(r, false).launch_vy; });
    add_row("quarterpipe (stepped a tick)", "air above lip", "in", [](int r) { return quarterpipe(r, false).apex; });
    add_row("quarterpipe (stepped a tick)", "at the lip", "s", [](int r) { return quarterpipe(r, false).lip_time; });
    add_row("quarterpipe (continuous)", "take-off vy", "in/s", [](int r) { return quarterpipe(r, true).launch_vy; });
    add_row("quarterpipe (continuous)", "air above lip", "in", [](int r) { return quarterpipe(r, true).apex; });
    add_row("quarterpipe (continuous)", "at the lip", "s", [](int r) { return quarterpipe(r, true).lip_time; });
    add_row("manual balance (stepped)", "lean at 3 s", "", [](int r) { return balance_lean(r, false); });
    add_row("manual balance (exact)", "lean at 3 s", "", [](int r) { return balance_lean(r, true); });
    add_row("manual balance (exact, timed)", "lean at 3 s", "", [](int r) { return balance_lean(r, true, true); });
    tick_rate::set_rate(60);

    std::printf("\nexactness yardstick: each kernel at 60 and the difference from it at 120 / 144 / 240\n");
    std::printf("%-30s %-14s %14s %14s %14s %14s\n", "kernel", "metric", "60 Hz", "120 - 60", "144 - 60", "240 - 60");
    for (const Row& row : g_rows) {
        std::printf("%-30s %-14s %14.6f %+14.6f %+14.6f %+14.6f %s\n", row.kernel.c_str(), row.metric.c_str(), row.at60,
                    row.at[0] - row.at60, row.at[1] - row.at60, row.at[2] - row.at60, row.unit.c_str());
    }
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const bool harness_only = argc > 1 && std::strcmp(argv[1], "--harness") == 0;
    if (!harness_only) {
        check_original_rate();
        check_other_rates();
        check_online_cadence();
    }
    run_harness();
    if (!harness_only) {
        std::printf("tick rate check: %d checks, %d failed\n", g_checked, g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
