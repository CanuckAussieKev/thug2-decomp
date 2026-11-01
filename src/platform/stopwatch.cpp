// The game clock.

#include "engine/gfx/engine.h"
#include "platform/stopwatch.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <array>

#include "engine/sys/game_config.h"
#include "engine/core/checked_index.h"

namespace frame_clock {

namespace {

float g_slomo{1.0f};
float g_next_slomo{1.0f};
float g_vblanks_per_second{60.0f};
float g_frame_length;
double g_raw_step_seconds;
uint64_t g_last_frame_vblank;
uint64_t g_previous_frame_vblank;
int g_history_index;
uint64_t g_start_time_stamp;
uint64_t g_unknown1ea8;
uint64_t g_start_clock;
uint64_t g_unknown1eb8;
uint64_t g_vblanks;
uint64_t g_elapsed_refreshes;
uint64_t g_render_frame;
uint64_t g_unknown1ee0;
bool g_flag_1e_e8;
bool g_flag_1e_e9;
bool g_rate_set;

// The timing block (0x007D1E68), saved whole by Reset.
float& get_frame_length() { return g_frame_length; }
double& raw_frame_duration() { return g_raw_step_seconds; }

std::array<int, 4> g_history;

std::array<int, 4>& get_history() { return g_history; }
uint64_t& last_frame_vblank() { return g_last_frame_vblank; }
uint64_t& previous_frame_vblank() { return g_previous_frame_vblank; }
int& history_index() { return g_history_index; }
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
static_assert(sizeof(TimingBlock) == 0x38, "the timing block's size");
TimingBlock g_saved_timing_block;

uint64_t& start_time_stamp() { return g_start_time_stamp; }
uint64_t& unknown_1e_a8() { return g_unknown1ea8; }
uint64_t& start_clock() { return g_start_clock; }
uint64_t& unknown_1e_b8() { return g_unknown1eb8; }
uint64_t& get_vblanks() { return g_vblanks; } // since Reset
uint64_t& elapsed_refreshes() { return g_elapsed_refreshes; }
uint64_t& render_frame() { return g_render_frame; }
uint64_t& unknown_1e_e0() { return g_unknown1ee0; }
bool& flag_1e_e8() { return g_flag_1e_e8; }
bool& flag_1e_e9() { return g_flag_1e_e9; }
bool& rate_set() { return g_rate_set; }

int& refresh_rate() { return game_config::g_frame_rate; } // 60
float& get_slomo() { return g_slomo; }
float& next_slomo() { return g_next_slomo; }
float& vblanks_per_second() { return g_vblanks_per_second; }

// A history entry's weight: 0-4 vblanks; anything outside 0-10 (a stall) counts as 1.
int get_clamped(int vblanks)
{
    if (vblanks > 10 || vblanks < 0)
        return 1;
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
    for (int& h : get_history())
        h = 1;
    get_frame_length() = 1.0f;
    last_frame_vblank() = 0;
    previous_frame_vblank() = 0;
    history_index() = 0;
    g_saved_timing_block.frame_length_ = get_frame_length();
    g_saved_timing_block.raw_step_ = raw_frame_duration();
    std::memcpy(g_saved_timing_block.history.data(), get_history().data(), sizeof(g_saved_timing_block.history));
    g_saved_timing_block.last_frame_vblank_ = last_frame_vblank();
    g_saved_timing_block.previous_frame_vblank_ = previous_frame_vblank();
    g_saved_timing_block.history_index_ = history_index();
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
    if (++history_index() == 4)
        history_index() = 0;
}

// 0x005BE7F0
void update_frame_length()
{
    const std::array<int, 4>& h = get_history();
    const int clamped = get_clamped(core::at(h, 0)) + get_clamped(core::at(h, 1)) + get_clamped(core::at(h, 2)) + get_clamped(core::at(h, 3));
    const int raw = static_cast<int>(static_cast<uint32_t>(core::at(h, 0)) + core::at(h, 1) + core::at(h, 2) + core::at(h, 3)); // (wraps, as the original)
    get_slomo() = next_slomo();
    get_frame_length() = static_cast<float>(clamped) * 0.25f;
    if (clamped * 0.25 < 1.0)
        get_frame_length() = 1.0f;
    raw_frame_duration() = raw * 0.25;
}

// 0x005BE920
void wait_for_vblank()
{
    const uint64_t start = elapsed_refreshes();
    while (elapsed_refreshes() == start) {
        // the idle function (0x0048C1C0 reads it from 0x006FA7D8)
        if (render::engine::get_idle_function())
            render::engine::get_idle_function()();
    }
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
double get_slomo_frame_seconds() { return get_slomo() * static_cast<double>(1.0f / 60.0f); }

// 0x005BE8F0
float get_time_scale() { return get_slomo(); }

// 0x005BE900
void set_slomo(float slomo) { next_slomo() = slomo; }

// 0x005BE7C0
void change_bits()
{
    if (!flag_1e_e8())
        flag_1e_e8() = true;
    if (!flag_1e_e9())
        flag_1e_e9() = true;
}

} // namespace frame_clock
