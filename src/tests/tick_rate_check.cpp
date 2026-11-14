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
    }
    tick_rate::set_rate(1000);
    expect(tick_rate::rate() == 240, "set_rate clamps");
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
Lerp camera_lerp(int rate)
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
        time = static_cast<float>(static_cast<double>(time) - one_frame);
        behind = static_cast<float>(static_cast<double>(step) + behind);
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
    add_row("camera distance lerp", "final value", "in", [](int r) { return camera_lerp(r).final_value; });
    add_row("camera distance lerp", "done at", "s", [](int r) { return camera_lerp(r).done_at; });
    add_row("per-tick decay 0.95 (as is)", "left after 1 s", "", [](int r) { return decay_left(r, false); });
    add_row("per-tick decay 0.95 (pow)", "left after 1 s", "", [](int r) { return decay_left(r, true); });
    add_row("goal timer (trunc ms)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, false); });
    add_row("goal timer (fraction kept)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, true); });
    add_row("goal timer (60 Hz grid)", "ms in 60 s", "ms", [](int r) { return timer_ms(r, false, true); });
    add_row("10-tick window (as is)", "length", "s", [](int r) { return window_seconds(r, false); });
    add_row("10-tick window (ticks_for)", "length", "s", [](int r) { return window_seconds(r, true); });
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
    }
    run_harness();
    if (!harness_only) {
        std::printf("tick rate check: %d checks, %d failed\n", g_checked, g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
