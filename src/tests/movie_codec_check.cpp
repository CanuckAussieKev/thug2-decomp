// NOT ORIGINAL - thug2_movie_codec_check.exe: the FFmpeg movie codec (src/media_ffmpeg) compared with Bink's
// (src/media_bink, the player's binkw32.dll) on the game's own Bink files. For each file: the headers both give, how
// the frame counters move (after the open, a step, a pause, a go-to, across the last frame, opened from memory), the
// pace wait gives, and the frames decoded and copied as the game copies them (BINKSURFACE32: the first frames, one half
// way, the last two and the first again) - per frame the largest difference of a colour byte, the mean difference and
// the PSNR, and whether the unused fourth byte agrees.
//
//   thug2_movie_codec_check [--frames N] [--goto F] <file.bik>...
//
// Run it from the game folder (binkw32.dll is found there; FFmpeg's DLLs are next to the check). Both movies are kept
// silent (Bink's sound off and at volume 0; the FFmpeg codec has none). Exit code 1 if a file won't open with both.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ratio>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/file/disk.h"
#include "engine/media/movie.h"
#include "fs_win32/win32_disk.h"
#include "media_bink/bink_movie.h"
#include "media_ffmpeg/ffmpeg_movie.h"
#include "tests/no_error_dialogs.h"

namespace {

constexpr uint32_t kSurface32CopyAll = 0x80000003; // BINKSURFACE32 | BINKCOPYALL: what the game asks for
constexpr uint32_t kPixelBytes = 4;
constexpr double kPeak = 255.0;
constexpr double kDecibels = 10.0;
constexpr uint32_t kDefaultFrames = 30;
constexpr uint32_t kPacedFrames = 60;
constexpr uint8_t kUnwritten = 0x55; // (the buffers' bytes before a copy)
constexpr size_t kBinkHeaderBytes = 44;
constexpr int kDecimal = 10;
constexpr uint32_t kFromMemory = 0x04000000; // BinkOpen's: the source is the file in memory

struct Options {
    uint32_t frames = kDefaultFrames;
    uint32_t go_to = 0; // 0: half way
    std::vector<const char*> files;
};

// The two codecs' movies of one file.
struct Pair {
    media::Movie* bink;
    media::MovieHeader* bink_movie;
    media::Movie* ffmpeg;
    media::MovieHeader* ffmpeg_movie;
};

struct FrameDifference {
    uint8_t bink_fourth = 0; // (the first pixel's)
    uint8_t ffmpeg_fourth = 0;
    int largest = 0;
    double mean = 0.0;
    double psnr = 0.0;
    size_t fourth_byte_differs = 0;
};

FrameDifference compare(std::span<const uint8_t> bink, std::span<const uint8_t> ffmpeg)
{
    FrameDifference difference;
    difference.bink_fourth = core::at(bink, kPixelBytes - 1);
    difference.ffmpeg_fourth = core::at(ffmpeg, kPixelBytes - 1);
    double squares = 0.0;
    double sum = 0.0;
    size_t count = 0;
    for (size_t at = 0; at < bink.size(); ++at) {
        const int delta = std::abs(static_cast<int>(core::at(bink, at)) - static_cast<int>(core::at(ffmpeg, at)));
        if (at % kPixelBytes == kPixelBytes - 1) {
            difference.fourth_byte_differs += delta != 0 ? 1U : 0U;
            continue;
        }
        difference.largest = std::max(difference.largest, delta);
        sum += delta;
        squares += static_cast<double>(delta) * delta;
        ++count;
    }
    difference.mean = count != 0 ? sum / static_cast<double>(count) : 0.0;
    const double mean_square = count != 0 ? squares / static_cast<double>(count) : 0.0;
    difference.psnr = mean_square > 0.0 ? kDecibels * std::log10(kPeak * kPeak / mean_square) : INFINITY;
    return difference;
}

void print_counters(const char* when, const Pair& pair)
{
    std::printf("  %-26s bink frame %u (last %u)   ffmpeg frame %u (last %u)%s\n", when, pair.bink_movie->frame_num_,
                pair.bink_movie->last_frame_num_, pair.ffmpeg_movie->frame_num_, pair.ffmpeg_movie->last_frame_num_,
                pair.bink_movie->frame_num_ == pair.ffmpeg_movie->frame_num_ &&
                        pair.bink_movie->last_frame_num_ == pair.ffmpeg_movie->last_frame_num_
                    ? ""
                    : "   DIFFER");
}

// The time the frames took (do_frame and copy_to_buffer), over the frames compared.
struct Timing {
    std::chrono::steady_clock::duration bink{};
    std::chrono::steady_clock::duration ffmpeg_decode{};
    std::chrono::steady_clock::duration ffmpeg_copy{};
    uint32_t frames = 0;
};

Timing& timings()
{
    static Timing timing;
    return timing;
}

double milliseconds_a_frame(std::chrono::steady_clock::duration total, uint32_t frames)
{
    return frames != 0 ? std::chrono::duration<double, std::milli>(total).count() / frames : 0.0;
}

// Each codec's current frame decoded and copied; the difference printed.
FrameDifference decode_and_compare(const Pair& pair, std::vector<uint8_t>& bink_bits, std::vector<uint8_t>& ffmpeg_bits)
{
    const media::MovieHeader& header = *pair.bink_movie;
    const media::FrameCopy copy{.pitch = static_cast<int>(header.width * kPixelBytes),
                                .height = header.height,
                                .x = 0,
                                .y = 0,
                                .flags = kSurface32CopyAll};
    std::ranges::fill(bink_bits, kUnwritten);
    std::ranges::fill(ffmpeg_bits, kUnwritten);
    using Clock = std::chrono::steady_clock;
    const Clock::time_point bink_start = Clock::now();
    pair.bink->do_frame(pair.bink_movie);
    pair.bink->copy_to_buffer(pair.bink_movie, bink_bits.data(), copy);
    const Clock::time_point ffmpeg_start = Clock::now();
    pair.ffmpeg->do_frame(pair.ffmpeg_movie);
    const Clock::time_point ffmpeg_decoded = Clock::now();
    pair.ffmpeg->copy_to_buffer(pair.ffmpeg_movie, ffmpeg_bits.data(), copy);
    const Clock::time_point end = Clock::now();
    Timing& timing = timings();
    timing.bink += ffmpeg_start - bink_start;
    timing.ffmpeg_decode += ffmpeg_decoded - ffmpeg_start;
    timing.ffmpeg_copy += end - ffmpeg_decoded;
    ++timing.frames;
    return compare(bink_bits, ffmpeg_bits);
}

void print_difference(uint32_t frame, const FrameDifference& difference)
{
    std::printf("  frame %5u: largest %3d  mean %6.3f  PSNR %6.2f dB  fourth byte differs %zu (%02X / %02X)\n", frame,
                difference.largest, difference.mean, difference.psnr, difference.fourth_byte_differs,
                difference.bink_fourth, difference.ffmpeg_fourth);
}

// The file opened from memory as the sound streams open pcm.wad's entries (Bink's from-memory flag, the video off):
// the headers, and the counters after stepping a few frames.
void check_from_memory(const char* file)
{
    std::vector<uint8_t> data;
    // (shared: both codecs still have the file open)
    fs::DiskFile* opened = fs::disk().open(file, {.access = fs::Access::kRead, .sharing = fs::Sharing::kRead});
    if (opened != fs::no_file()) {
        data.resize(fs::disk().size(opened));
        uint32_t got = 0;
        fs::disk().read(opened, data.data(), static_cast<uint32_t>(data.size()), &got, nullptr);
        data.resize(got);
        fs::disk().close(opened);
    }
    if (data.size() < kBinkHeaderBytes) {
        std::printf("  from memory: can't read %s\n", file);
        return;
    }
    media::Movie& bink = media::bink::movie();
    media::Movie& ffmpeg = media::ffmpeg::movie();
    media::MovieHeader* bink_movie = bink.open(data.data(), kFromMemory);
    media::MovieHeader* ffmpeg_movie = ffmpeg.open(data.data(), kFromMemory);
    if (!bink_movie || !ffmpeg_movie) {
        std::printf("  from memory: doesn't open: bink %s, ffmpeg %s\n", bink_movie ? "yes" : "no",
                    ffmpeg_movie ? "yes" : "no");
    } else {
        bink.set_sound_on_off(bink_movie, 0);
        bink.set_volume(bink_movie, {.track = 0, .volume = 0});
        const Pair pair{.bink = &bink, .bink_movie = bink_movie, .ffmpeg = &ffmpeg, .ffmpeg_movie = ffmpeg_movie};
        for (const auto& [codec, movie] : {std::pair{&bink, bink_movie}, std::pair{&ffmpeg, ffmpeg_movie}}) {
            codec->set_video_on_off(movie, 0);
            for (int step = 0; step < 3; ++step) {
                codec->do_frame(movie);
                codec->next_frame(movie);
            }
        }
        std::printf("  from memory: %ux%u, %u frames / %ux%u, %u frames\n", bink_movie->width, bink_movie->height,
                    bink_movie->frames, ffmpeg_movie->width, ffmpeg_movie->height, ffmpeg_movie->frames);
        print_counters("from memory, 3 frames on:", pair);
    }
    bink.close(bink_movie);
    ffmpeg.close(ffmpeg_movie);
}

bool check_file(const char* file, const Options& options)
{
    media::Movie& bink = media::bink::movie();
    media::Movie& ffmpeg = media::ffmpeg::movie();
    media::MovieHeader* bink_movie = bink.open(file, 0);
    media::MovieHeader* ffmpeg_movie = ffmpeg.open(file, 0);
    std::printf("%s\n", file);
    if (!bink_movie || !ffmpeg_movie) {
        std::printf("  doesn't open: bink %s, ffmpeg %s\n", bink_movie ? "yes" : "no", ffmpeg_movie ? "yes" : "no");
        bink.close(bink_movie);
        ffmpeg.close(ffmpeg_movie);
        return false;
    }
    bink.set_sound_on_off(bink_movie, 0);
    bink.set_volume(bink_movie, {.track = 0, .volume = 0});
    const Pair pair{.bink = &bink, .bink_movie = bink_movie, .ffmpeg = &ffmpeg, .ffmpeg_movie = ffmpeg_movie};
    for (const auto* movie : {bink_movie, ffmpeg_movie}) {
        std::printf("  %-6s %ux%u, %u frames at %u/%u\n", movie == bink_movie ? "bink" : "ffmpeg", movie->width,
                    movie->height, movie->frames, movie->frame_rate_, movie->frame_rate_div_);
    }
    print_counters("after the open:", pair);
    std::printf("  first wait: bink %d, ffmpeg %d\n", bink.wait(bink_movie), ffmpeg.wait(ffmpeg_movie));

    std::vector<uint8_t> bink_bits(static_cast<size_t>(bink_movie->width) * bink_movie->height * kPixelBytes);
    std::vector<uint8_t> ffmpeg_bits(bink_bits.size());
    FrameDifference worst;
    worst.psnr = INFINITY;
    const uint32_t frames = std::min(options.frames, bink_movie->frames);
    for (uint32_t frame = 1; frame <= frames; ++frame) {
        const FrameDifference difference = decode_and_compare(pair, bink_bits, ffmpeg_bits);
        print_difference(bink_movie->frame_num_, difference);
        worst.largest = std::max(worst.largest, difference.largest);
        worst.mean = std::max(worst.mean, difference.mean);
        worst.psnr = std::min(worst.psnr, difference.psnr);
        worst.fourth_byte_differs = std::max(worst.fourth_byte_differs, difference.fourth_byte_differs);
        bink.next_frame(bink_movie);
        ffmpeg.next_frame(ffmpeg_movie);
    }
    print_counters("after the frames:", pair);

    bink.pause(bink_movie, 1);
    ffmpeg.pause(ffmpeg_movie, 1);
    std::printf("  wait while paused: bink %d, ffmpeg %d\n", bink.wait(bink_movie), ffmpeg.wait(ffmpeg_movie));
    bink.pause(bink_movie, 0);
    ffmpeg.pause(ffmpeg_movie, 0);
    std::printf("  wait after the pause: bink %d, ffmpeg %d\n", bink.wait(bink_movie), ffmpeg.wait(ffmpeg_movie));

    const uint32_t target = options.go_to != 0 ? options.go_to : bink_movie->frames / 2;
    bink.go_to(bink_movie, {.frame = target, .flags = 0});
    ffmpeg.go_to(ffmpeg_movie, {.frame = target, .flags = 0});
    print_counters("after go_to(frame):", pair);
    print_difference(bink_movie->frame_num_, decode_and_compare(pair, bink_bits, ffmpeg_bits));

    // Across the end: the last two frames and on.
    const uint32_t near_end = bink_movie->frames > 2 ? bink_movie->frames - 1 : 1;
    bink.go_to(bink_movie, {.frame = near_end, .flags = 0});
    ffmpeg.go_to(ffmpeg_movie, {.frame = near_end, .flags = 0});
    print_counters("after go_to(frames - 1):", pair);
    for (int step = 0; step < 3; ++step) {
        const FrameDifference difference = decode_and_compare(pair, bink_bits, ffmpeg_bits);
        print_difference(bink_movie->frame_num_, difference);
        bink.next_frame(bink_movie);
        ffmpeg.next_frame(ffmpeg_movie);
        print_counters("after next_frame:", pair);
    }
    print_difference(bink_movie->frame_num_, decode_and_compare(pair, bink_bits, ffmpeg_bits));
    // The pace: frames played as the game plays them (wait until due, decode, step), each codec alone.
    for (const auto& [codec, movie] : {std::pair{&bink, bink_movie}, std::pair{&ffmpeg, ffmpeg_movie}}) {
        codec->go_to(movie, {.frame = 1, .flags = 0});
        const auto start = std::chrono::steady_clock::now();
        for (uint32_t frame = 0; frame < kPacedFrames && frame + 1 < movie->frames; ++frame) {
            while (codec->wait(movie) != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            codec->do_frame(movie);
            codec->next_frame(movie);
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
        std::printf("  %u frames paced by wait: %-6s %.0f ms\n", kPacedFrames, movie == bink_movie ? "bink" : "ffmpeg",
                    elapsed.count());
    }
    // A quick go-to (flags 1: what the sound streams ask for): the counters only - Bink decodes none of the frames
    // before it, so its picture is built on the frame it was at.
    bink.go_to(bink_movie, {.frame = target, .flags = 1});
    ffmpeg.go_to(ffmpeg_movie, {.frame = target, .flags = 1});
    print_counters("after go_to(frame, quick):", pair);

    check_from_memory(file);
    const Timing& timing = timings();
    std::printf(
        "  a frame decoded and copied: bink %.2f ms, ffmpeg %.2f ms (decode %.2f, copy %.2f) - over %u frames\n",
        milliseconds_a_frame(timing.bink, timing.frames),
        milliseconds_a_frame(timing.ffmpeg_decode + timing.ffmpeg_copy, timing.frames),
        milliseconds_a_frame(timing.ffmpeg_decode, timing.frames),
        milliseconds_a_frame(timing.ffmpeg_copy, timing.frames), timing.frames);
    timings() = {};
    std::printf("  worst of the first %u frames: largest %d, mean %.3f, PSNR %.2f dB, fourth byte differs %zu\n",
                frames, worst.largest, worst.mean, worst.psnr, worst.fourth_byte_differs);
    bink.close(bink_movie);
    ffmpeg.close(ffmpeg_movie);
    return true;
}

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument(core::at(arguments, i));
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--frames" && has_value) {
            options.frames = static_cast<uint32_t>(std::strtoul(core::at(arguments, ++i), nullptr, kDecimal));
        } else if (argument == "--goto" && has_value) {
            options.go_to = static_cast<uint32_t>(std::strtoul(core::at(arguments, ++i), nullptr, kDecimal));
        } else {
            options.files.push_back(core::at(arguments, i));
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span<char*>(argv, static_cast<size_t>(argc)));
    if (options.files.empty()) {
        std::fprintf(stderr, "usage: thug2_movie_codec_check [--frames N] [--goto F] <file.bik>...\n");
        return 2;
    }
    fs::install_disk(fs::win32::disk());
    if (!media::ffmpeg::usable()) {
        std::fprintf(
            stderr, "FFmpeg's DLLs can't be used (next to the check: avutil-59, avcodec-61, avformat-61, swscale-8)\n");
        return 1;
    }
    bool all = true;
    for (const char* file : options.files) {
        all = check_file(file, options) && all;
    }
    return all ? 0 : 1;
}
