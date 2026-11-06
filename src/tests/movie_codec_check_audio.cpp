// NOT ORIGINAL - thug2_movie_codec_check's sound comparison (movie_codec_check_audio.h).

#include "tests/movie_codec_check_audio.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ratio>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/audio/audio_device.h"
#include "engine/file/disk.h"
#include "engine/media/movie.h"
#include "media_bink/bink_movie.h"
#include "media_ffmpeg/ffmpeg_movie.h"
#include "tests/movie_codec_check_sound.h"

namespace tests::audio_check {

namespace {

using Clock = std::chrono::steady_clock;
using sound::BufferRecord;

constexpr uint32_t kFromMemory = 0x04000000; // BinkOpen's: the source is the file in memory
constexpr uint32_t kIoSize = 0x01000000;     // BinkOpen's: the I/O buffer size set (the streams' path open)
constexpr auto kStreamService = std::chrono::milliseconds(5); // the stream thread's pace
constexpr double kMusicSeconds = 8.0;
constexpr double kMovieVolumeScale = 16384.0; // the movie player: the louder of music and effects (0-1) x 16384
constexpr int kFullStreamVolume = 32768;      // a stream at 100 (0-100 x 327.68)
constexpr int kOnsetLevel = 64;               // the first sample this loud starts the sound
constexpr size_t kAlignWindow = 8192;         // frames compared to find the alignment
constexpr int kAlignSearch = 2048;            // frames either way
constexpr size_t kCompareWindow = 4096;       // frames compared at a time
// A window matches when nearly all its samples are within the rounding (binkw32 limits the blocks' overlap at full
// scale and silences a sound's last few hundredths quieter than FFmpeg: a handful of samples may differ more).
constexpr int kNearSample = 2;
constexpr double kNearShare = 0.98;
constexpr ptrdiff_t kResyncFrames = 24000;    // a lost alignment looked for this far either way
constexpr ptrdiff_t kProbeFrames = 256;       // (with this many frames, then the whole window checked)
constexpr size_t kUnmatchedShare = 50;
constexpr double kDecibels = 20.0;
constexpr double kMilliseconds = 1000.0;
constexpr size_t kHeaderBytes = 44;
constexpr size_t kSizeAt = 4;
constexpr uint32_t kSizeBias = 8;
constexpr uint32_t kDefaultWadFiles = 12;
constexpr unsigned kBitsPerByte = 8;
constexpr double kQuietSnr = 60.0; // (a difference this far under the signal is rounding)
constexpr int kMaxLevel = 65536;   // the sweeps' last level and pan
constexpr size_t kShownDifferences = 8;
// The levels whose values are printed (and swept besides the sample): full, half, a quarter, a tenth...
constexpr std::array kShownLevels{1, 64, 1000, 3276, 3277, 8191, 8192, 16383, 16384, 32767, 32768, 32769, 65536};
constexpr size_t kKeptBytes = 0x100; // (a stand-in for the stream's from_where_: larger than the header)
constexpr double kPowerDecibels = 10.0;
constexpr int kDenseLevels = 200; // every level below this, then every kLevelStep-th
constexpr int kLevelStep = 37;
constexpr auto kLevelSettles = std::chrono::milliseconds(15);

uint32_t word_at(std::span<const uint8_t> bytes, size_t at)
{
    uint32_t word = 0;
    for (size_t i = 4; i != 0; --i) {
        word = word << kBitsPerByte | core::at(bytes, at + i - 1);
    }
    return word;
}

// What one codec did with one file.
struct Run {
    bool opened = false;
    uint32_t frames = 0;        // the file's
    uint32_t frames_played = 0; // do_frame calls
    double milliseconds = 0.0;  // from the open to the close
    double open_ms = 0.0;       // the open
    double first_frame_ms = -1.0; // from the open's start to the first frame decoded
    std::vector<std::pair<double, uint32_t>> frame_times; // each frame decoded: when (ms from the open), which
    std::vector<BufferRecord> buffers;
};

enum class Kind : uint8_t { kMovie, kStream };

struct Source {
    const void* data = nullptr;
    uint32_t flags = 0;
    Kind kind = Kind::kMovie;
    double seconds = 0.0;
    bool once = true; // (a stream stops at its last frame)
    bool events = false;
    uint32_t go_to = 0; // (the events' first go-to: 0 half way)
};

void note_frame(Run& run, Clock::time_point start, uint32_t frame)
{
    const double now = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (run.first_frame_ms < 0.0) {
        run.first_frame_ms = now;
    }
    run.frame_times.emplace_back(now, frame);
}

// Where --events is: the first go-to's frame (0: half way) and the event next.
struct Events {
    uint32_t go_to = 0;
    int next = 0;
};

// The go-to, pause and resume of --events, at their times (seconds from the open): a quick go-to half way at 2 s (the
// music's seek), a pause at 4 s and the resume at 4.5 s (the movie player's while the game's window is away), a go-to
// back to the start at 6 s.
void apply_events(media::Movie& codec, media::MovieHeader* movie, double seconds, Events& events)
{
    const uint32_t frame = events.go_to;
    int& next = events.next;
    constexpr std::array kTimes{2.0, 4.0, 4.5, 6.0};
    while (std::cmp_less(next, kTimes.size()) && seconds >= core::at(kTimes, static_cast<size_t>(next))) {
        switch (next++) {
        case 0:
            codec.go_to(movie, {.frame = frame != 0 ? frame : movie->frames / 2, .flags = 1});
            break;
        case 1:
            codec.pause(movie, 1);
            break;
        case 2:
            codec.pause(movie, 0);
            break;
        default:
            codec.go_to(movie, {.frame = 1, .flags = 1});
            break;
        }
    }
}

Run play(media::Movie& codec, const Source& source)
{
    Run run;
    sound::begin_records();
    const Clock::time_point start = Clock::now();
    media::MovieHeader* movie = codec.open(source.data, source.flags);
    if (!movie) {
        return run;
    }
    run.opened = true;
    run.open_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    run.frames = movie->frames;
    const auto limit = std::chrono::duration<double>(source.seconds);
    if (source.kind == Kind::kMovie) {
        codec.set_sound_on_off(movie, 1);
        codec.set_volume(movie, {.track = 0, .volume = static_cast<int>(kMovieVolumeScale)});
        while (Clock::now() - start < limit) {
            if (codec.wait(movie) != 0) {
                std::this_thread::yield();
                continue;
            }
            codec.do_frame(movie);
            note_frame(run, start, movie->frame_num_);
            ++run.frames_played;
            codec.next_frame(movie);
            if (movie->frame_num_ >= movie->frames - 1) {
                break;
            }
        }
    } else {
        codec.set_video_on_off(movie, 0);
        codec.set_volume(movie, {.track = 0, .volume = kFullStreamVolume});
        bool ended = false;
        Events events{.go_to = source.go_to};
        while (!ended && Clock::now() - start < limit) {
            if (source.events) {
                apply_events(codec, movie, std::chrono::duration<double>(Clock::now() - start).count(), events);
            }
            while (codec.wait(movie) == 0) {
                codec.do_frame(movie);
                note_frame(run, start, movie->frame_num_);
                ++run.frames_played;
                codec.next_frame(movie);
                if (source.once && movie->frame_num_ == movie->frames) {
                    ended = true;
                }
            }
            std::this_thread::sleep_for(kStreamService);
        }
    }
    codec.close(movie);
    run.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    run.buffers = sound::records();
    return run;
}

// The buffer that played the sound (the first secondary buffer).
const BufferRecord* sound_buffer(const Run& run)
{
    const auto found =
        std::ranges::find_if(run.buffers, [](const BufferRecord& buffer) { return !buffer.primary; });
    return found != run.buffers.end() ? &*found : nullptr;
}

// The first frame (from the start of the heard samples) with a sample at kOnsetLevel or louder; the frame count if none.
size_t onset(const BufferRecord& buffer)
{
    const size_t channels = std::max<size_t>(buffer.channels, 1);
    for (size_t at = 0; at < buffer.heard.size(); ++at) {
        if (std::abs(static_cast<int>(core::at(buffer.heard, at))) >= kOnsetLevel) {
            return at / channels;
        }
    }
    return buffer.heard.size() / channels;
}

struct Comparison {
    int offset = 0;          // FFmpeg's frames later than Bink's at the start
    std::vector<int> moves;  // each time the alignment moved on the way (a go-to, a loop's or a pause's timing)
    size_t windows = 0;      // windows of kCompareWindow frames compared (the matched ones)
    size_t quiet = 0;        // silent in both
    size_t unmatched = 0;    // with no alignment within kResyncFrames that matches
    std::vector<size_t> unmatched_at; // (where: Bink's frame)
    size_t frames = 0;       // compared
    int largest = 0;
    double mean = 0.0;
    double snr = 0.0;
    double bink_rms = 0.0;
    double ffmpeg_rms = 0.0;
    size_t differing = 0; // samples more than 1 apart
};

// The heard samples of the two outputs, by sample frame.
struct Heard {
    std::span<const int16_t> bink;
    std::span<const int16_t> ffmpeg;
    size_t channels = 1;

    [[nodiscard]] ptrdiff_t bink_frames() const { return static_cast<ptrdiff_t>(bink.size() / channels); }
    [[nodiscard]] ptrdiff_t ffmpeg_frames() const { return static_cast<ptrdiff_t>(ffmpeg.size() / channels); }

    // The mean square difference over `frames` frames from Bink's `from` and FFmpeg's `from + shift` (negative if
    // either runs out).
    [[nodiscard]] double mean_square(ptrdiff_t from, ptrdiff_t shift, ptrdiff_t frames) const
    {
        if (from < 0 || from + shift < 0 || from + frames > bink_frames() || from + shift + frames > ffmpeg_frames() ||
            frames <= 0) {
            return -1.0;
        }
        double sum = 0.0;
        for (ptrdiff_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < channels; ++channel) {
                const auto at_bink = (static_cast<size_t>(from + frame) * channels) + channel;
                const auto at_ffmpeg = (static_cast<size_t>(from + shift + frame) * channels) + channel;
                const double delta = static_cast<double>(core::at(bink, at_bink)) - core::at(ffmpeg, at_ffmpeg);
                sum += delta * delta;
            }
        }
        return sum / static_cast<double>(frames * static_cast<ptrdiff_t>(channels));
    }

    // Whether the window of `frames` from Bink's `from` matches FFmpeg's at `shift`.
    [[nodiscard]] bool matches(ptrdiff_t from, ptrdiff_t shift, ptrdiff_t frames) const
    {
        if (from < 0 || from + shift < 0 || from + frames > bink_frames() || from + shift + frames > ffmpeg_frames() ||
            frames <= 0) {
            return false;
        }
        size_t near = 0;
        for (ptrdiff_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < channels; ++channel) {
                const auto at_bink = (static_cast<size_t>(from + frame) * channels) + channel;
                const auto at_ffmpeg = (static_cast<size_t>(from + shift + frame) * channels) + channel;
                near += std::abs(static_cast<int>(core::at(bink, at_bink)) - core::at(ffmpeg, at_ffmpeg)) <= kNearSample
                            ? 1U
                            : 0U;
            }
        }
        return static_cast<double>(near) >= kNearShare * static_cast<double>(frames * static_cast<ptrdiff_t>(channels));
    }

    // Shifts within `reach` of `around`.
    struct Search {
        ptrdiff_t around = 0;
        ptrdiff_t reach = 0;
    };

    // The shift within the search that matches best over `frames` frames from `from` (and its mean square).
    [[nodiscard]] std::pair<ptrdiff_t, double> best_shift(ptrdiff_t from, Search search, ptrdiff_t frames) const
    {
        std::pair<ptrdiff_t, double> best{search.around, -1.0};
        for (ptrdiff_t shift = search.around - search.reach; shift <= search.around + search.reach; ++shift) {
            const double error = mean_square(from, shift, frames);
            if (error >= 0.0 && (best.second < 0.0 || error < best.second)) {
                best = {shift, error};
            }
        }
        return best;
    }

    [[nodiscard]] bool quiet(std::span<const int16_t> samples, ptrdiff_t from, ptrdiff_t frames) const
    {
        const auto first = static_cast<size_t>(std::max<ptrdiff_t>(from, 0)) * channels;
        const size_t count = std::min(static_cast<size_t>(frames) * channels, samples.size() - std::min(first, samples.size()));
        return std::ranges::all_of(samples.subspan(std::min(first, samples.size()), count),
                                   [](int16_t sample) { return std::abs(static_cast<int>(sample)) < kOnsetLevel; });
    }
};

// The heard samples compared window by window: aligned at the start (the onsets, then the best match), the alignment
// looked for again where a window stops matching (the outputs' timing around a go-to, a loop's first frame or a pause
// differs by the stream thread's few milliseconds).
Comparison compare(const BufferRecord& bink, const BufferRecord& ffmpeg)
{
    Comparison comparison;
    if (bink.channels != ffmpeg.channels) {
        return comparison;
    }
    const Heard heard{.bink = bink.heard, .ffmpeg = ffmpeg.heard, .channels = std::max<size_t>(bink.channels, 1)};
    auto bink_onset = static_cast<ptrdiff_t>(onset(bink));
    auto ffmpeg_onset = static_cast<ptrdiff_t>(onset(ffmpeg));
    if (bink_onset >= heard.bink_frames() && ffmpeg_onset >= heard.ffmpeg_frames()) {
        bink_onset = 0; // (a quiet sound in both: compared from the start as it is)
        ffmpeg_onset = 0;
    } else if (bink_onset >= heard.bink_frames() || ffmpeg_onset >= heard.ffmpeg_frames()) {
        return comparison;
    }
    const auto window = static_cast<ptrdiff_t>(kCompareWindow);
    const ptrdiff_t start_frames =
        std::min({static_cast<ptrdiff_t>(kAlignWindow), heard.bink_frames() - bink_onset,
                  heard.ffmpeg_frames() - ffmpeg_onset - kAlignSearch});
    ptrdiff_t shift = heard.best_shift(bink_onset, {.around = ffmpeg_onset - bink_onset, .reach = kAlignSearch},
                                       std::max<ptrdiff_t>(start_frames, window / 2))
                          .first;
    comparison.offset = static_cast<int>(shift);
    double sum = 0.0;
    double squares = 0.0;
    double bink_squares = 0.0;
    double ffmpeg_squares = 0.0;
    for (ptrdiff_t from = bink_onset; from + window <= heard.bink_frames(); from += window) {
        if (heard.quiet(heard.bink, from, window) && heard.quiet(heard.ffmpeg, from + shift, window)) {
            ++comparison.quiet; // (compared, but no alignment looked for in silence)
            if (!heard.matches(from, shift, window)) {
                continue;
            }
        } else if (!heard.matches(from, shift, window)) {
            // (probed with the window's last frames: past the change that lost the alignment)
            const auto [moved, probe_error] =
                heard.best_shift(from + window - kProbeFrames, {.around = shift, .reach = kResyncFrames}, kProbeFrames);
            if (probe_error < 0.0 || !heard.matches(from, moved, window)) {
                ++comparison.unmatched;
                comparison.unmatched_at.push_back(static_cast<size_t>(from));
                continue;
            }
            comparison.moves.push_back(static_cast<int>(moved - shift));
            shift = moved;
        }
        ++comparison.windows;
        for (ptrdiff_t frame = 0; frame < window; ++frame) {
            for (size_t channel = 0; channel < heard.channels; ++channel) {
                const int bink_sample = core::at(heard.bink, (static_cast<size_t>(from + frame) * heard.channels) + channel);
                const int ffmpeg_sample =
                    core::at(heard.ffmpeg, (static_cast<size_t>(from + shift + frame) * heard.channels) + channel);
                const int delta = std::abs(bink_sample - ffmpeg_sample);
                comparison.largest = std::max(comparison.largest, delta);
                comparison.differing += delta > 1 ? 1U : 0U;
                sum += delta;
                squares += static_cast<double>(delta) * delta;
                bink_squares += static_cast<double>(bink_sample) * bink_sample;
                ffmpeg_squares += static_cast<double>(ffmpeg_sample) * ffmpeg_sample;
            }
        }
    }
    comparison.frames = comparison.windows * kCompareWindow;
    const auto samples = static_cast<double>(comparison.frames * heard.channels);
    if (samples > 0.0) {
        comparison.mean = sum / samples;
        comparison.bink_rms = std::sqrt(bink_squares / samples);
        comparison.ffmpeg_rms = std::sqrt(ffmpeg_squares / samples);
        comparison.snr = squares > 0.0 ? kPowerDecibels * std::log10(bink_squares / squares) : INFINITY;
    }
    return comparison;
}

void print_buffer(const char* codec, const Run& run, const BufferRecord* buffer)
{
    std::printf("  %-6s open %.1f ms, first frame decoded at %.1f ms; %u of %u frames in %.0f ms; ", codec, run.open_ms,
                run.first_frame_ms, run.frames_played, run.frames, run.milliseconds);
    if (!buffer) {
        std::printf("no sound buffer\n");
    } else {
        const size_t channels = std::max<size_t>(buffer->channels, 1);
        std::printf("buffer %u bytes, %u Hz x %u x %u bits, flags %08X; first play %.1f ms; sound from %.1f ms; heard "
                    "%.0f ms; %d plays, %d stops; written %llu bytes\n",
                    buffer->bytes, buffer->rate, buffer->channels, buffer->bits, buffer->flags,
                    buffer->first_play_ms,
                    buffer->first_play_ms + (static_cast<double>(onset(*buffer)) * kMilliseconds /
                                             std::max<uint32_t>(buffer->rate, 1)),
                    static_cast<double>(buffer->heard.size()) / static_cast<double>(channels) * kMilliseconds /
                        std::max<uint32_t>(buffer->rate, 1),
                    buffer->plays, buffer->stops, static_cast<unsigned long long>(buffer->written_bytes));
        std::string volumes;
        for (const int volume : buffer->volumes) {
            volumes += std::to_string(volume) + " ";
        }
        std::string pans;
        for (const int pan : buffer->pans) {
            pans += std::to_string(pan) + " ";
        }
        std::printf("         volumes [%s] pans [%s] frequencies %zu%s%s\n", volumes.c_str(), pans.c_str(),
                    buffer->frequencies.size(), buffer->other_calls.empty() ? "" : "; also ",
                    buffer->other_calls.c_str());
        std::printf("         %s\n", buffer->timeline.c_str());
        std::printf("         writes reached %.0f ms past the play position at most, %.0f ms on average (%u writes)\n",
                    buffer->lead_max_ms, buffer->leads != 0 ? buffer->lead_sum_ms / buffer->leads : 0.0,
                    buffer->leads);
    }
    for (const BufferRecord& other : run.buffers) {
        if (other.primary) {
            std::printf("         primary buffer (flags %08X)%s%s\n", other.flags, other.other_calls.empty() ? "" : ": ",
                        other.other_calls.c_str());
        }
    }
}

bool compare_runs(const Run& bink, const Run& ffmpeg)
{
    const BufferRecord* bink_buffer = sound_buffer(bink);
    const BufferRecord* ffmpeg_buffer = sound_buffer(ffmpeg);
    print_buffer("bink", bink, bink_buffer);
    print_buffer("ffmpeg", ffmpeg, ffmpeg_buffer);
    if (bink_buffer == nullptr || ffmpeg_buffer == nullptr) {
        return bink_buffer == nullptr && ffmpeg_buffer == nullptr;
    }
    const Comparison comparison = compare(*bink_buffer, *ffmpeg_buffer);
    const double rate = std::max<uint32_t>(bink_buffer->rate, 1);
    std::string moves;
    for (const int move : comparison.moves) {
        moves += (moves.empty() ? "" : ", ") + std::to_string(move);
    }
    std::printf("  heard: %zu frames compared (%zu windows; %zu silent, %zu unmatched), ffmpeg %+d frames (%+.1f ms) "
                "from bink, %zu moves on the way%s%s%s; largest %d, mean %.3f, %zu samples > 1 apart; SNR %.1f dB; RMS "
                "bink %.1f, ffmpeg %.1f (%+.2f dB)\n",
                comparison.frames, comparison.windows, comparison.quiet, comparison.unmatched, comparison.offset,
                comparison.offset * kMilliseconds / rate, comparison.moves.size(), moves.empty() ? "" : " (",
                moves.c_str(), moves.empty() ? "" : " frames)", comparison.largest, comparison.mean,
                comparison.differing, comparison.snr, comparison.bink_rms, comparison.ffmpeg_rms,
                comparison.bink_rms > 0.0 ? kDecibels * std::log10(comparison.ffmpeg_rms / comparison.bink_rms) : 0.0);
    if (!comparison.unmatched_at.empty()) {
        std::string at;
        for (const size_t frame : comparison.unmatched_at) {
            at += " " + std::to_string(static_cast<int>(static_cast<double>(frame) * kMilliseconds / rate));
        }
        std::printf("         unmatched windows at (ms of Bink's sound):%s\n", at.c_str());
    }
    // (the windows where the outputs' timing parted - a go-to, a loop's first frame, a pause - may not match: two for
    // each move and one more, and one in fifty besides)
    return comparison.windows != 0 &&
           comparison.unmatched <= (2 * comparison.moves.size()) + 1 + (comparison.windows / kUnmatchedShare) &&
           comparison.snr >= kQuietSnr;
}

// `bytes` into the file at `path` (made again).
void write_file(const std::string& path, std::span<const std::byte> bytes)
{
    fs::DiskFile* file = fs::disk().open(path.c_str(), {.access = fs::Access::kWrite,
                                                         .sharing = fs::Sharing::kNone,
                                                         .disposition = fs::Disposition::kCreateAlways});
    if (file == fs::no_file()) {
        return;
    }
    uint32_t wrote = 0;
    fs::disk().write(file, bytes.data(), static_cast<uint32_t>(bytes.size()), &wrote);
    fs::disk().close(file);
}

// A buffer's heard samples into `path` (16-bit, interleaved, as heard).
void dump_heard(const BufferRecord* buffer, const std::string& path)
{
    if (buffer) {
        write_file(path, std::as_bytes(std::span(buffer->heard)));
    }
}

bool check_source(const char* name, const Source& source, const char* dump)
{
    std::printf("%s\n", name);
    const Run bink = play(media::bink::movie(), source);
    const Run ffmpeg = play(media::ffmpeg::movie(), source);
    if (dump) {
        dump_heard(sound_buffer(bink), std::string(dump) + ".bink.raw");
        dump_heard(sound_buffer(ffmpeg), std::string(dump) + ".ffmpeg.raw");
        std::string text;
        const size_t lines = std::max(bink.frame_times.size(), ffmpeg.frame_times.size());
        for (size_t line = 0; line < lines; ++line) {
            for (const Run* run : {&bink, &ffmpeg}) {
                if (line < run->frame_times.size()) {
                    const auto& [ms, frame] = core::at(run->frame_times, line);
                    text += std::to_string(ms) + " " + std::to_string(frame) + (run == &bink ? " " : "");
                } else {
                    text += run == &bink ? "- - " : "- -";
                }
            }
            text += "\n";
        }
        write_file(std::string(dump) + ".frames.txt", std::as_bytes(std::span(text)));
    }
    if (!bink.opened || !ffmpeg.opened) {
        std::printf("  doesn't open: bink %s, ffmpeg %s\n", bink.opened ? "yes" : "no", ffmpeg.opened ? "yes" : "no");
        return false;
    }
    return compare_runs(bink, ffmpeg);
}

std::vector<uint8_t> read_file(const char* file)
{
    std::vector<uint8_t> data;
    fs::DiskFile* opened = fs::disk().open(file, {.access = fs::Access::kRead, .sharing = fs::Sharing::kRead});
    if (opened != fs::no_file()) {
        data.resize(fs::disk().size(opened));
        uint32_t got = 0;
        fs::disk().read(opened, data.data(), static_cast<uint32_t>(data.size()), &got, nullptr);
        data.resize(got);
        fs::disk().close(opened);
    }
    return data;
}

} // namespace

void set_up()
{
    sound::start_listening();
    audio::install_device(sound::recording_device());
    if (!media::bink::movie().use_sound_output(sound::bink_output())) {
        std::printf("binkw32 doesn't take the stand-in DirectSound object\n");
    }
    media::ffmpeg::movie().use_sound_output(sound::recording_output());
}

bool check_file(const char* file, const Options& options)
{
    if (!options.stream) {
        return check_source(file, {.data = file,
                                   .flags = 0,
                                   .kind = Kind::kMovie,
                                   .seconds = options.seconds > 0.0 ? options.seconds : INFINITY,
                                   .once = true},
                            options.dump);
    }
    const double seconds = options.seconds > 0.0 ? options.seconds : kMusicSeconds;
    if (!options.from_memory) {
        return check_source(file,
                            {.data = file,
                             .flags = kIoSize,
                             .kind = Kind::kStream,
                             .seconds = seconds,
                             .once = false,
                             .events = options.events,
                             .go_to = options.go_to},
                            options.dump);
    }
    const std::vector<uint8_t> data = read_file(file);
    return check_source(file, {.data = data.data(),
                               .flags = kFromMemory,
                               .kind = Kind::kStream,
                               .seconds = seconds,
                               .once = false,
                               .events = options.events,
                             .go_to = options.go_to},
                        options.dump);
}

bool check_wad(const char* file, const Options& options)
{
    const std::vector<uint8_t> wad = read_file(file);
    const std::span<const uint8_t> bytes(wad);
    std::vector<size_t> starts;
    for (size_t at = 0; at + kHeaderBytes <= bytes.size(); ++at) {
        if (core::at(bytes, at) == 'B' && core::at(bytes, at + 1) == 'I' && core::at(bytes, at + 2) == 'K' &&
            core::at(bytes, at + 3) == 'i') {
            const uint32_t size = word_at(bytes, at + kSizeAt) + kSizeBias;
            if (at + size <= bytes.size()) {
                starts.push_back(at);
                at += size - 1;
            }
        }
    }
    std::printf("%s: %zu Bink files\n", file, starts.size());
    bool all = true;
    const size_t every = options.wad_every != 0 ? options.wad_every : 1;
    const size_t count = options.wad_every != 0 ? starts.size() : std::min<size_t>(kDefaultWadFiles, starts.size());
    for (size_t index = 0; index < count; index += every) {
        const size_t at = core::at(starts, index);
        const uint32_t size = word_at(bytes, at + kSizeAt) + kSizeBias;
        // (a copy of its own, as the stream files' cache holds each)
        const std::vector<uint8_t> entry(bytes.begin() + static_cast<ptrdiff_t>(at),
                                         bytes.begin() + static_cast<ptrdiff_t>(at + size));
        const std::string name = std::string(file) + " #" + std::to_string(index) + " at " + std::to_string(at);
        all = check_source(name.c_str(), {.data = entry.data(),
                                          .flags = kFromMemory,
                                          .kind = Kind::kStream,
                                          .seconds = options.seconds > 0.0 ? options.seconds : INFINITY,
                                          .once = true},
                                         nullptr) &&
              all;
    }
    return all;
}

namespace {

// Levels set on `file`'s track - a sample of them, each given time to reach the output (binkw32 sets a track's volume
// from its own sound thread) -, then every pan: what the codec's output was given for each.
struct Levels {
    bool opened = false;
    std::vector<int> volumes;
    std::vector<int> pans;
};

std::vector<int> sampled_levels()
{
    std::vector<int> levels;
    levels.reserve(static_cast<size_t>(kDenseLevels + (kMaxLevel / kLevelStep) + kShownLevels.size()));
    for (int level = 0; level < kDenseLevels; ++level) {
        levels.push_back(level);
    }
    for (int level = kDenseLevels; level <= kMaxLevel; level += kLevelStep) {
        levels.push_back(level);
    }
    for (const int level : kShownLevels) {
        levels.push_back(level);
    }
    return levels;
}

Levels sweep_levels(media::Movie& codec, const char* file)
{
    Levels levels;
    sound::begin_records();
    media::MovieHeader* movie = codec.open(file, kIoSize);
    if (!movie) {
        return levels;
    }
    levels.opened = true;
    codec.set_video_on_off(movie, 0);
    codec.do_frame(movie); // (the sound started)
    for (const int volume : sampled_levels()) {
        codec.set_volume(movie, {.track = 0, .volume = volume});
        std::this_thread::sleep_for(kLevelSettles);
        levels.volumes.push_back(sound::last_settings().volume);
    }
    codec.set_volume(movie, {.track = 0, .volume = kFullStreamVolume});
    for (int pan = 0; pan <= kMaxLevel; ++pan) {
        codec.set_pan(movie, {.track = 0, .pan = pan});
        levels.pans.push_back(sound::last_settings().pan);
    }
    codec.close(movie);
    return levels;
}

void print_sweep(const char* what, std::span<const int> levels, const std::vector<int>& bink,
                 const std::vector<int>& ffmpeg)
{
    if (bink.size() != ffmpeg.size() || bink.empty()) {
        std::printf("  %s: not all set (bink %zu, ffmpeg %zu)\n", what, bink.size(), ffmpeg.size());
        return;
    }
    size_t differ = 0;
    std::string first;
    for (size_t level = 0; level < bink.size(); ++level) {
        if (core::at(bink, level) != core::at(ffmpeg, level)) {
            if (differ++ < kShownDifferences) {
                first += " " + std::to_string(core::at(levels, level)) + ": " + std::to_string(core::at(bink, level)) + " / " +
                         std::to_string(core::at(ffmpeg, level));
            }
        }
    }
    std::string samples;
    for (size_t at = 0; at < levels.size(); ++at) {
        const int level = core::at(levels, at);
        if (std::ranges::find(kShownLevels, level) != kShownLevels.end()) {
            samples += " " + std::to_string(level) + ":" + std::to_string(core::at(bink, at));
        }
    }
    std::printf("  %s, %zu of 0 ... %d: %zu differ%s%s\n    (bink's:%s)\n", what, bink.size(), kMaxLevel, differ,
                differ != 0 ? ", the first:" : "", first.c_str(), samples.c_str());
}

} // namespace

void check_levels(const char* file, const Options& options)
{
    std::printf("%s: the levels and pans each codec's output is given\n", file);
    const Levels bink = sweep_levels(media::bink::movie(), file);
    const Levels ffmpeg = sweep_levels(media::ffmpeg::movie(), file);
    if (!bink.opened || !ffmpeg.opened) {
        std::printf("  doesn't open: bink %s, ffmpeg %s\n", bink.opened ? "yes" : "no", ffmpeg.opened ? "yes" : "no");
        return;
    }
    const std::vector<int> levels = sampled_levels();
    std::vector<int> pans(static_cast<size_t>(kMaxLevel) + 1);
    for (size_t pan = 0; pan < pans.size(); ++pan) {
        core::at(pans, pan) = static_cast<int>(pan);
    }
    print_sweep("volume -> attenuation", levels, bink.volumes, ffmpeg.volumes);
    if (options.dump != nullptr && bink.volumes.size() == levels.size() && ffmpeg.volumes.size() == levels.size()) {
        std::string text;
        for (size_t at = 0; at < levels.size(); ++at) {
            text += std::to_string(core::at(levels, at)) + " " + std::to_string(core::at(bink.volumes, at)) + " " +
                    std::to_string(core::at(ffmpeg.volumes, at)) + "\n";
        }
        write_file(std::string(options.dump) + ".levels.txt", std::as_bytes(std::span(text)));
    }
    print_sweep("pan -> pan", pans, bink.pans, ffmpeg.pans);
    // A paused pcm.wad stream reopened (BinkStream::tick_frame): the stream keeps only the file's first four bytes,
    // and opens those as a Bink file in memory.
    std::array<char, kKeptBytes> kept{};
    std::memcpy(kept.data(), "BIKi", 4);
    for (media::Movie* codec : {&media::bink::movie(), &media::ffmpeg::movie()}) {
        media::MovieHeader* reopened = codec->open(kept.data(), kFromMemory);
        std::printf("  %-6s a paused memory stream's reopen: %s\n", codec == &media::bink::movie() ? "bink" : "ffmpeg",
                    reopened ? "opens" : "doesn't open");
        if (reopened) {
            std::printf("         %ux%u, %u frames\n", reopened->width, reopened->height, reopened->frames);
            codec->close(reopened);
        }
    }
}

} // namespace tests::audio_check
