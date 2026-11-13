// The game clock.

#include "engine/gfx/engine.h"
#include "engine/sys/stopwatch.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <array>

#include "engine/sys/game_config.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace frame_clock {

constexpr float kDefaultVblanksPerSecond = 60.0F;
constexpr int kStallVblanks = 10;       // a history entry of more vblanks than this is a stall
constexpr float kHistoryWeight = 0.25F; // the frame length is the mean of the last four
constexpr double kHistoryWeightDouble = 0.25;

namespace {

// NOT ORIGINAL grouping - stopwatch.cpp's state (the original's globals). Made on first use, never destroyed
// (stopwatch_state()).
struct StopwatchState {
    float slomo_{1.0F};
    float next_slomo_{1.0F};
    float vblanks_per_second_{kDefaultVblanksPerSecond};
    float frame_length_{};
    double raw_step_seconds_{};
    uint64_t last_frame_vblank_{};
    uint64_t previous_frame_vblank_{};
    int history_index_{};
    uint64_t start_time_stamp_{};
    uint64_t unknown1ea8_{};
    uint64_t start_clock_{};
    uint64_t unknown1eb8_{};
    uint64_t vblanks_{};
    uint64_t elapsed_refreshes_{};
    uint64_t render_frame_{};
    uint64_t unknown1ee0_{};
    bool flag_1e_e8_{};
    bool flag_1e_e9_{};
    bool rate_set_{};
    std::array<int, 4> history_{};
};

StopwatchState& stopwatch_state()
{
    static core::NoDestroy<StopwatchState> state;
    return state.get();
}

// The timing block (0x007D1E68), saved whole by Reset.
float& get_frame_length() { return stopwatch_state().frame_length_; }
double& raw_frame_duration() { return stopwatch_state().raw_step_seconds_; }

std::array<int, 4>& get_history() { return stopwatch_state().history_; }
uint64_t& last_frame_vblank() { return stopwatch_state().last_frame_vblank_; }
uint64_t& previous_frame_vblank() { return stopwatch_state().previous_frame_vblank_; }
int& history_index() { return stopwatch_state().history_index_; }
// The timing block's copy Reset keeps (0x38 bytes, only ever written).
struct TimingBlock {
    float frame_length_;
    uint32_t pad;
    double raw_step_;
    std::array<int, 4> history;
    uint64_t last_frame_vblank_;
    uint64_t previous_frame_vblank_;
    int history_index_;
};
constexpr std::size_t kTimingBlockBytes = 0x38;

static_assert(sizeof(TimingBlock) == kTimingBlockBytes, "the timing block's size");
TimingBlock& saved_timing_block()
{
    static core::NoDestroy<TimingBlock> saved_timing_block;
    return saved_timing_block.get();
}

uint64_t& start_time_stamp() { return stopwatch_state().start_time_stamp_; }
uint64_t& unknown_1e_a8() { return stopwatch_state().unknown1ea8_; }
uint64_t& start_clock() { return stopwatch_state().start_clock_; }
uint64_t& unknown_1e_b8() { return stopwatch_state().unknown1eb8_; }
uint64_t& get_vblanks() { return stopwatch_state().vblanks_; } // since Reset
uint64_t& elapsed_refreshes() { return stopwatch_state().elapsed_refreshes_; }
uint64_t& render_frame() { return stopwatch_state().render_frame_; }
uint64_t& unknown_1e_e0() { return stopwatch_state().unknown1ee0_; }
bool& flag_1e_e8() { return stopwatch_state().flag_1e_e8_; }
bool& flag_1e_e9() { return stopwatch_state().flag_1e_e9_; }
bool& rate_set() { return stopwatch_state().rate_set_; }

int& refresh_rate() { return game_config::frame_rate(); } // 60
float& get_slomo() { return stopwatch_state().slomo_; }
float& next_slomo() { return stopwatch_state().next_slomo_; }
float& vblanks_per_second() { return stopwatch_state().vblanks_per_second_; }

// A history entry's weight: 0-4 vblanks; anything outside 0-10 (a stall) counts as 1.
int get_clamped(int vblanks)
{
    if (vblanks > kStallVblanks || vblanks < 0) {
        return 1;
    }
    return vblanks > 4 ? 4 : vblanks;
}

} // namespace

// 0x005BE6F0
uint64_t read_cpu_time_stamp() { return __rdtsc(); }

// 0x005BE9B0
void get_reset()
{
    if (!rate_set()) {
        vblanks_per_second() = static_cast<float>(refresh_rate());
        rate_set() = true;
    }
    raw_frame_duration() = 1.0;
    for (int& history : get_history()) {
        history = 1;
    }
    get_frame_length() = 1.0F;
    last_frame_vblank() = 0;
    previous_frame_vblank() = 0;
    history_index() = 0;
    saved_timing_block().frame_length_ = get_frame_length();
    saved_timing_block().raw_step_ = raw_frame_duration();
    std::memcpy(saved_timing_block().history.data(), get_history().data(), sizeof(saved_timing_block().history));
    saved_timing_block().last_frame_vblank_ = last_frame_vblank();
    saved_timing_block().previous_frame_vblank_ = previous_frame_vblank();
    saved_timing_block().history_index_ = history_index();
    get_vblanks() = 0;
    unknown_1e_e0() = 0;
}

// 0x005BEA50
void prepare_all()
{
    get_reset();
    unknown_1e_b8() = 0;
    start_clock() = static_cast<uint32_t>(std::clock());
    start_time_stamp() = read_cpu_time_stamp();
    unknown_1e_a8() = 0;
}

// 0x005BE700
void note_refresh()
{
    ++get_vblanks();
    ++elapsed_refreshes();
}

// 0x005BE740
void once_every_draw()
{
    ++render_frame();
    previous_frame_vblank() = last_frame_vblank();
    last_frame_vblank() = get_vblanks();
    core::at(get_history(), history_index()) =
        static_cast<int>(static_cast<uint32_t>(get_vblanks()) - static_cast<uint32_t>(previous_frame_vblank()));
    if (++history_index() == 4) {
        history_index() = 0;
    }
}

// 0x005BE7F0
void update_frame_length()
{
    const std::array<int, 4>& history = get_history();
    const int clamped = get_clamped(core::at(history, 0)) + get_clamped(core::at(history, 1)) +
                        get_clamped(core::at(history, 2)) + get_clamped(core::at(history, 3));
    const int raw = static_cast<int>(static_cast<uint32_t>(core::at(history, 0)) + core::at(history, 1) +
                                     core::at(history, 2) + core::at(history, 3)); // (wraps, as the original)
    get_slomo() = next_slomo();
    get_frame_length() = static_cast<float>(clamped) * kHistoryWeight;
    if (clamped * kHistoryWeightDouble < 1.0) {
        get_frame_length() = 1.0F;
    }
    raw_frame_duration() = raw * kHistoryWeightDouble;
}

// 0x005BE920
void wait_for_vblank()
{
    const uint64_t start = elapsed_refreshes();
    while (elapsed_refreshes() == start) {
        // the idle function (0x0048C1C0 reads it from 0x006FA7D8)
        if (render::engine::get_idle_function()) {
            render::engine::get_idle_function()();
        }
    }
}

uint64_t vblanks_since_reset() { return get_vblanks(); }

void set_vblanks_since_reset(uint64_t vblanks)
{
    const uint64_t delta = vblanks - get_vblanks(); // (wraps for a move back: the sums below wrap back with it)
    get_vblanks() = vblanks;
    last_frame_vblank() += delta;
    previous_frame_vblank() += delta;
}

// 0x005BE8E0
uint64_t fetch_retrace_count() { return elapsed_refreshes(); }

// 0x005BE8D0
uint64_t get_draw_frame() { return render_frame(); }

// 0x005BE970 (x87: 1000 / rate * the low 32 bits, truncated through 64 bits)
uint32_t get_time()
{
    const double milliseconds =
        (1000.0 / vblanks_per_second()) * static_cast<double>(static_cast<uint32_t>(get_vblanks()));
    return static_cast<uint32_t>(static_cast<int64_t>(milliseconds));
}

// 0x005BE910
double get_frame_seconds() { return raw_frame_duration() / vblanks_per_second(); }

// 0x005BEAA0 (the original leaves the unrounded product on the x87 stack)
double get_slomo_frame_seconds() { return get_slomo() * static_cast<double>(1.0F / kDefaultVblanksPerSecond); }

// 0x005BE8F0
float get_time_scale() { return get_slomo(); }

// 0x005BE900
void set_slomo(float slomo) { next_slomo() = slomo; }

// 0x005BE7C0
void change_bits()
{
    if (!flag_1e_e8()) {
        flag_1e_e8() = true;
    }
    if (!flag_1e_e9()) {
        flag_1e_e9() = true;
    }
}

} // namespace frame_clock
