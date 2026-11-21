// NOT ORIGINAL - the clip key (devtools/clip.h).

#include "devtools/clip.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include <SDL3/SDL.h>

#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "devtools/screenshot.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/renderer.h"
#include "engine/media/video_writer.h"
#include "engine/pad_input/input_event.h"
#include "engine/sys/mlp.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/screenshot_files.h"
#include "platform/sdl_keys.h"

namespace sys::clip {

namespace {

using screenshot::Prop;
using screenshot::Section;
using screenshot::TickCycle;

constexpr int kDefaultScale = 50;
constexpr int kDefaultMaxSeconds = 300;
constexpr size_t kQueueMax = 8; // frames waiting for the encoder (~15 MB each at 1440p)
constexpr size_t kPoolMax = 6;
constexpr int kLogLines = 20;
constexpr size_t kTextSize = 192;
constexpr double kMsPerSecond = 1000.0;
constexpr double kBaselineKeep = 0.98; // the frame time before a clip: a running average
constexpr int kMinFps = 1;
constexpr int kMaxFps = 240;
constexpr int kPercent = 100;
constexpr int kKeyTextSize = 64;
constexpr int kTmYearBase = 1900;

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point from) { return std::chrono::duration<double>(Clock::now() - from).count(); }

struct Item {
    TickCycle frame;
    double time = 0; // seconds into the clip
};

// What the main thread and the encoder's thread share, one per clip.
struct Job {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Item> queue;
    std::vector<TickCycle> pool; // buffers the encoder is done with
    bool finishing = false;
    double end_time = 0;
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::string error;
    // the clip, set by the main thread before the start / at the finish
    std::string video_path;
    std::string sidecar_path;
    std::string name;
    media::VideoFormat format;
    int scale = kDefaultScale;
    int source_width = 0; // the picture's size when known at the start: the encoder opens while the first frames come
    int source_height = 0;
    std::vector<Section> sections;
    std::vector<std::string> log_tail;
    std::vector<Prop> summary; // (the main thread's part of the clip section)
    std::string frame_lines;
    // the result, the encoder's thread
    int64_t written = 0;
    int width = 0;
    int height = 0;
    std::string codec;
    bool ok = false;
    std::thread thread;
};

struct Pending {
    uint64_t id = 0;
    double time = 0;
};

// The recording in progress (main thread).
struct Recording {
    std::shared_ptr<Job> job;
    Clock::time_point began;
    Clock::time_point last_present;
    uint64_t next_id = 1;
    std::deque<Pending> pending;
    int64_t displayed = 0;
    int64_t captured = 0;
    int64_t dropped = 0;
    double last_capture = -1.0;
    double work_ms = 0;
    int fps = media::kDefaultVideoFps;
    int max_seconds = kDefaultMaxSeconds;
    uint32_t level = 0;
    uint32_t game_frame_start = 0;
    double baseline_ms = 0;
    std::vector<Section> start_sections;
    std::string started_text;
    std::string frame_lines;
    std::string capture_error; // a picture that couldn't be read: the clip ends at the frame's end
};

struct ClipState {
    int key = 0;
    std::atomic<int> request{-1}; // -1 none, else a Request
    std::unique_ptr<Recording> recording;
    std::vector<std::shared_ptr<Job>> finishing;
    std::string last_result;
    double frame_ms = 0; // running average of the frame time (the baseline of a clip's cost)
    Clock::time_point last_frame;
    bool have_last_frame = false;
    bool at_exit_set = false;
};

ClipState& state()
{
    static core::NoDestroy<ClipState> instance;
    return instance.get();
}

int setting_int(const char* key, int fallback, int low, int high)
{
    return std::clamp(build_info::get_int("features", key, fallback), low, high);
}

std::string stamp_text(const std::tm& local)
{
    std::array<char, kTextSize> text{};
    text::format_to(text, "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + kTmYearBase, local.tm_mon + 1, local.tm_mday,
                    local.tm_hour, local.tm_min, local.tm_sec);
    return text.data();
}

bool write_new_file(const std::string& path, const void* data, size_t size)
{
    const fs::OwnedFile file(fs::disk().open(path.c_str(), {.access = fs::Access::kWrite,
                                                            .sharing = fs::Sharing::kNone,
                                                            .disposition = fs::Disposition::kCreateNew,
                                                            .hint = fs::Hint::kNormal}));
    if (!file) {
        return false;
    }
    uint32_t written = 0;
    return fs::disk().write(file.get(), data, static_cast<uint32_t>(size), &written) && written == size;
}

std::string with_ending(const std::string& png_name, const char* ending)
{
    return png_name.substr(0, png_name.size() - 4) + ending;
}

// --- the encoder's thread ---

// `source` (RGB, sw x sh) as dw x dh: each new pixel the average of the source pixels it covers (the same size: copied).
void area_scale(const std::vector<uint8_t>& source, int sw, int sh, int dw, int dh, std::vector<uint8_t>* out)
{
    if (sw == dw && sh == dh) {
        *out = source;
        return;
    }
    out->assign(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 3U, 0);
    for (int y = 0; y < dh; ++y) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh));
        for (int x = 0; x < dw; ++x) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw));
            std::array<int, 3> sum = {0, 0, 0};
            for (int sy = y0; sy < y1 && sy < sh; ++sy) {
                const size_t row = static_cast<size_t>(sy) * static_cast<size_t>(sw);
                for (int sx = x0; sx < x1 && sx < sw; ++sx) {
                    const size_t at = (row + static_cast<size_t>(sx)) * 3U;
                    sum[0] += source[at];
                    sum[1] += source[at + 1];
                    sum[2] += source[at + 2];
                }
            }
            const int count = std::max(1, (std::min(y1, sh) - y0) * (std::min(x1, sw) - x0));
            const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 3U;
            for (size_t channel = 0; channel < 3; ++channel) {
                (*out)[to + channel] = static_cast<uint8_t>(sum.at(channel) / count);
            }
        }
    }
}

// The frames go in at a fixed rate: slot k is shown at k / fps seconds and holds the latest frame at or before that
// time (a slower game repeats its last frame, a faster one has frames skipped).
class Encoder {
public:
    explicit Encoder(Job& job) : job_(job), fps_(std::max(job.format.fps, 1)) {}

    void run()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        if (job_.source_width > 0 && job_.source_height > 0) {
            open(job_.source_width, job_.source_height); // (the hardware encoder takes a moment to start: the frames wait in the queue)
        }
        for (;;) {
            Item item;
            {
                std::unique_lock lock(job_.mutex);
                job_.wake.wait(lock, [&] { return !job_.queue.empty() || job_.finishing; });
                if (job_.queue.empty()) {
                    break;
                }
                item = std::move(job_.queue.front());
                job_.queue.pop_front();
            }
            if (!broken_) {
                take(std::move(item));
            } else {
                give_back(std::move(item.frame));
            }
        }
        if (!broken_ && have_current_) {
            emit_until(job_.end_time);
            if (job_.written == 0) {
                emit_one();
            }
        }
        finish();
    }

private:
    void give_back(TickCycle&& frame)
    {
        const std::scoped_lock lock(job_.mutex);
        if (job_.pool.size() < kPoolMax) {
            job_.pool.push_back(std::move(frame));
        }
    }

    void take(Item item)
    {
        if (!have_current_) {
            current_ = std::move(item.frame);
            have_current_ = true;
            emit_until(item.time);
        } else {
            emit_until(item.time);
            give_back(std::move(current_));
            current_ = std::move(item.frame);
        }
        rgb_valid_ = false;
    }

    void emit_until(double time)
    {
        while (!broken_ && static_cast<double>(job_.written) / fps_ < time) {
            emit_one();
        }
    }

    bool open(int width, int height)
    {
        media::VideoFormat format = job_.format;
        format.width = std::max(2, width * job_.scale / kPercent) & ~1;
        format.height = std::max(2, height * job_.scale / kPercent) & ~1;
        std::string error;
        writer_ = media::open_video(job_.video_path, format, &error);
        if (!writer_) {
            fail(error);
            return false;
        }
        job_.width = format.width;
        job_.height = format.height;
        job_.codec = writer_->codec();
        return true;
    }

    void emit_one()
    {
        if (!writer_ && !open(current_.width, current_.height)) {
            return;
        }
        if (!rgb_valid_) {
            if (current_.bytes.empty() || !screenshot::to_rgb(current_, &full_)) {
                fail("a picture couldn't be read");
                return;
            }
            // (scaled here, by area: the writer's own scaling of a smaller size shows broken chroma)
            area_scale(full_, current_.width, current_.height, job_.width, job_.height, &rgb_);
            rgb_valid_ = true;
        }
        if (!writer_->add_frame(rgb_, job_.width, job_.height)) {
            fail(writer_->error());
            return;
        }
        ++job_.written;
    }

    void fail(const std::string& why)
    {
        broken_ = true;
        const std::scoped_lock lock(job_.mutex);
        job_.error = why;
        job_.failed = true;
    }

    void finish()
    {
        bool finished = false;
        if (writer_) {
            finished = writer_->finish();
            if (!finished && !broken_) {
                fail(writer_->error());
            }
            job_.written = writer_->frames();
        }
        const bool success = finished && !broken_;
        // the sidecar: the clip's section first, then the states, the log, the frame lines
        Section clip{.title = "clip", .fields = {}};
        clip.fields.push_back({.key = "file", .value = job_.name + ".mp4"});
        clip.fields.push_back({.key = "result", .value = success ? "ok" : "failed: " + job_.error});
        clip.fields.push_back({.key = "video", .value = std::to_string(job_.width) + " x " + std::to_string(job_.height) +
                                                           " at " + std::to_string(fps_) + " a second, " + job_.codec});
        clip.fields.push_back({.key = "frames written", .value = std::to_string(job_.written)});
        for (const Prop& field : job_.summary) {
            clip.fields.push_back(field);
        }
        job_.sections.insert(job_.sections.begin(), clip);
        std::string text = screenshot::format_sidecar("THUG2 decomp clip " + job_.name + ".mp4", job_.sections, job_.log_tail);
        text += "\n[frames]\n" + std::string(kFrameHeader) + job_.frame_lines;
        if (!write_new_file(job_.sidecar_path, text.data(), text.size())) {
            get_log("clip: couldn't write %s", job_.sidecar_path.c_str());
        }
        job_.ok = success;
        rgb_.clear();
        rgb_.shrink_to_fit();
        job_.done = true;
    }

public:
    static constexpr const char* kFrameHeader =
        "frame     vf    time_s     game  dt_ms   pos_x pos_y pos_z   dir_x dir_y dir_z\n";

private:
    Job& job_;
    int fps_;
    TickCycle current_;
    bool have_current_ = false;
    std::vector<uint8_t> full_; // the picture as RGB
    std::vector<uint8_t> rgb_;  // and at the video's size
    bool rgb_valid_ = false;
    bool broken_ = false;
    std::unique_ptr<media::VideoWriter> writer_;
};

void encoder_thread(const std::shared_ptr<Job>& job)
{
    Encoder encoder(*job);
    encoder.run();
}

// --- the recording ---

std::vector<Section> sections_now(const std::string& prefix, bool with_build)
{
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    std::vector<Section> all = screenshot::state_sections(0, 0, stamp_text(local));
    std::vector<Section> kept;
    for (Section& section : all) {
        if (section.title == "screenshot" || (section.title == "build" && !with_build)) {
            continue;
        }
        section.title = (section.title == "build") ? section.title : prefix + section.title;
        kept.push_back(std::move(section));
    }
    return kept;
}

std::string camera_text(const std::vector<Section>& sections)
{
    for (const Section& section : sections) {
        if (section.title.ends_with("camera") && !section.fields.empty()) {
            return section.fields.front().value;
        }
    }
    return "";
}

void collect_finished()
{
    ClipState& clip = state();
    std::erase_if(clip.finishing, [&clip](const std::shared_ptr<Job>& job) {
        if (!job->done) {
            return false;
        }
        job->thread.join();
        const std::string name = job->name + ".mp4";
        if (job->ok) {
            clip.last_result = "ok: " + job->video_path;
            get_log("clip: saved %s (%lld frames, %dx%d, %s) and %s", job->video_path.c_str(),
                    static_cast<long long>(job->written), job->width, job->height, job->codec.c_str(),
                    job->sidecar_path.c_str());
            screenshot::show_notice("Clip saved: " + name);
        } else {
            clip.last_result = "error: " + job->error;
            get_log("clip: failed - %s", job->error.c_str());
            screenshot::show_notice("Clip failed (see the log)");
        }
        return true;
    });
}

void finish_job(Recording& rec, const std::string& reason)
{
    ClipState& clip = state();
    Job& job = *rec.job;
    const double length = seconds_since(rec.began);
    // what is still in the ring goes on
    for (;;) {
        TickCycle frame;
        uint64_t id = 0;
        if (!render::device().capture_ring_drain(&frame, &id)) {
            break;
        }
        const auto found = std::ranges::find(rec.pending, id, &Pending::id);
        if (found == rec.pending.end()) {
            continue;
        }
        const double time = found->time;
        rec.pending.erase(found);
        if (frame.bytes.empty()) {
            ++rec.dropped;
            continue;
        }
        {
            const std::scoped_lock lock(job.mutex);
            job.queue.push_back({.frame = std::move(frame), .time = time});
        }
    }
    render::device().capture_ring_release();
    rec.dropped += static_cast<int64_t>(rec.pending.size()); // (never read back)
    std::vector<Section> end = sections_now("end ", false);
    job.sections = std::move(rec.start_sections);
    job.sections.insert(job.sections.end(), end.begin(), end.end());
    job.log_tail = get_log_tail(kLogLines);
    job.frame_lines = std::move(rec.frame_lines);
    const double work = rec.displayed > 0 ? rec.work_ms / static_cast<double>(rec.displayed) : 0.0;
    const double average = rec.displayed > 0 ? length * kMsPerSecond / static_cast<double>(rec.displayed) : 0.0;
    std::array<char, kTextSize> text{};
    job.summary.push_back({.key = "started", .value = rec.started_text});
    text::format_to(text, "%.2f s (ended: %s)", length, reason.c_str());
    job.summary.push_back({.key = "duration", .value = text.data()});
    job.summary.push_back({.key = "displayed frames", .value = std::to_string(rec.displayed)});
    job.summary.push_back({.key = "pictures taken", .value = std::to_string(rec.captured)});
    job.summary.push_back({.key = "frames dropped", .value = std::to_string(rec.dropped) + " (the encoder's queue was full)"});
    text::format_to(text, "%.2f ms a frame in the game's thread; frames averaged %.2f ms (%.2f ms before)", work, average,
                    rec.baseline_ms);
    job.summary.push_back({.key = "cost", .value = text.data()});
    job.summary.push_back({.key = "frame lines", .value = "see [frames]: frame = displayed frame, vf = the video frame it first "
                                                         "shows in, time_s = its time, game = the game's frame, dt_ms = time "
                                                         "since the last displayed frame"});
    get_log("clip: stopped after %.1f s (%s): %lld displayed, %lld taken, %lld dropped; %.2f ms a frame in the game's thread, "
            "frames averaged %.2f ms (%.2f ms before)",
            length, reason.c_str(), static_cast<long long>(rec.displayed), static_cast<long long>(rec.captured),
            static_cast<long long>(rec.dropped), work, average, rec.baseline_ms);
    {
        const std::scoped_lock lock(job.mutex);
        job.finishing = true;
        job.end_time = length;
    }
    job.wake.notify_all();
    clip.finishing.push_back(rec.job);
}

void stop_recording(const std::string& reason)
{
    ClipState& clip = state();
    if (!clip.recording) {
        return;
    }
    const std::unique_ptr<Recording> rec = std::move(clip.recording);
    finish_job(*rec, reason);
}

std::string start_recording()
{
    ClipState& clip = state();
    if (clip.recording) {
        return "already recording";
    }
    const std::string folder = screenshot::make_output_folder("clip_dir", "Videos");
    if (folder.empty()) {
        return "no folder (see the log)";
    }
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    const screenshot::Stamp stamp{.year = local.tm_year + kTmYearBase,
                                  .month = local.tm_mon + 1,
                                  .day = local.tm_mday,
                                  .hour = local.tm_hour,
                                  .minute = local.tm_min,
                                  .second = local.tm_sec};
    const std::string png = screenshot::pick_name(screenshot::file_stem(stamp), [&](const std::string& name) {
        const std::string wanted = name.ends_with(".png") ? with_ending(name, ".mp4") : name;
        return fs::disk().kind((folder + wanted).c_str()) != fs::PathKind::kNone;
    });
    if (png.empty()) {
        return "no free name in " + folder;
    }
    auto rec = std::make_unique<Recording>();
    auto job = std::make_shared<Job>();
    job->name = png.substr(0, png.size() - 4);
    job->video_path = folder + job->name + ".mp4";
    job->sidecar_path = folder + job->name + ".txt";
    job->format = media::configured_format();
    job->format.fps = setting_int("clip_fps", job->format.fps, kMinFps, kMaxFps);
    job->format.quality = setting_int("clip_quality", job->format.quality, 1, kPercent);
    job->scale = setting_int("clip_scale", kDefaultScale, 10, 400);
    rec->fps = job->format.fps;
    rec->max_seconds = setting_int("clip_max_seconds", kDefaultMaxSeconds, 1, 24 * 3600);
    job->source_width = render::renderer().back_buffer_width();
    job->source_height = render::renderer().back_buffer_height();
    rec->level = screenshot::level_key();
    rec->game_frame_start = frame_loop::frames_run();
    rec->baseline_ms = clip.frame_ms;
    rec->start_sections = sections_now("start ", true);
    rec->started_text = stamp_text(local);
    rec->began = Clock::now();
    rec->last_present = rec->began;
    rec->job = job;
    job->thread = std::thread(encoder_thread, job);
    clip.recording = std::move(rec);
    get_log("clip: recording to %s (%d a second, %d%% size, at most %d s)", job->video_path.c_str(), job->format.fps,
            job->scale, clip.recording->max_seconds);
    return "";
}

void handle_event(const input::Event& event)
{
    if (event.type == input::EventType::kKeyDown && !event.repeat && event.key == state().key) {
        state().request = static_cast<int>(Request::kToggle);
    }
}

// The frame line for the frame being presented, then its picture.
void take_frame(Recording& rec, double dt_ms)
{
    ClipState& clip = state();
    const double time = seconds_since(rec.began);
    ++rec.displayed;
    std::array<float, 3> position{};
    std::array<float, 3> forward{};
    const bool camera = screenshot::last_view(position, forward);
    std::array<char, kTextSize> line{};
    const int video_frame = static_cast<int>(std::ceil(time * rec.fps));
    if (camera) {
        text::format_to(line, "%5lld %6d %9.3f %8u %6.2f  %9.2f %9.2f %9.2f  %7.4f %7.4f %7.4f\n",
                        static_cast<long long>(rec.displayed), video_frame, time, frame_loop::frames_run(), dt_ms,
                        static_cast<double>(position[0]), static_cast<double>(position[1]),
                        static_cast<double>(position[2]), static_cast<double>(forward[0]), static_cast<double>(forward[1]),
                        static_cast<double>(forward[2]));
    } else {
        text::format_to(line, "%5lld %6d %9.3f %8u %6.2f          - - -        - - -\n",
                        static_cast<long long>(rec.displayed), video_frame, time, frame_loop::frames_run(), dt_ms);
    }
    rec.frame_lines += line.data();
    // (a picture every half slot's time at least: a game faster than the video's rate skips the extra frames)
    if (rec.last_capture >= 0.0 && time - rec.last_capture < 0.5 / rec.fps) {
        return;
    }
    rec.last_capture = time;
    Job& job = *rec.job;
    TickCycle frame;
    bool room = false;
    {
        const std::scoped_lock lock(job.mutex);
        room = job.queue.size() < kQueueMax;
        if (room && !job.pool.empty()) {
            frame = std::move(job.pool.back());
            job.pool.pop_back();
        }
    }
    const uint64_t id = rec.next_id++;
    rec.pending.push_back({.id = id, .time = time});
    ++rec.captured;
    uint64_t ready_id = 0;
    bool have = false;
    std::string error;
    const int shown = static_cast<int>(time);
    const bool lit = std::fmod(time, 1.0) < 0.6;
    if (!render::device().capture_ring_submit(id, shown, lit, room ? &frame : nullptr, &ready_id, &have, &error)) {
        rec.capture_error = error;
        return;
    }
    if (!have) {
        return;
    }
    const auto found = std::ranges::find(rec.pending, ready_id, &Pending::id);
    if (found == rec.pending.end()) {
        return;
    }
    const double ready_time = found->time;
    rec.pending.erase(found);
    if (!room || frame.bytes.empty()) {
        ++rec.dropped;
        return;
    }
    {
        const std::scoped_lock lock(job.mutex);
        job.queue.push_back({.frame = std::move(frame), .time = ready_time});
    }
    job.wake.notify_one();
}

} // namespace

bool is_switched_on()
{
    static build_info::LiveInt enabled{"features", "clips", 1};
    return enabled.on();
}

bool recording() { return state().recording != nullptr; }

std::string last_result() { return state().last_result; }

void request(Request what) { state().request = static_cast<int>(what); }

void prepare_all()
{
    if (!is_switched_on()) {
        get_log("clip key: off ([features] clips=0)");
        return;
    }
    std::array<char, kKeyTextSize> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "RecordClip", "", setting.data(), sizeof(setting));
    auto by_name = [](const std::string& name) { return platform::scancode_from_name(name); };
    auto by_number = [](int code) { return platform::scancode_from_sdl2(code); };
    state().key = screenshot::parse_key(setting.data(), SDL_SCANCODE_F4, by_name, by_number);
    get_log("clip key: %s (scan code %d) - saves to %s", SDL_GetScancodeName(static_cast<SDL_Scancode>(state().key)),
            state().key, screenshot::make_output_folder("clip_dir", "Videos").c_str());
    input::add_event_handler(handle_event);
    if (!state().at_exit_set) {
        state().at_exit_set = true;
        std::atexit(shutdown);
    }
}

void before_present(bool rendering)
{
    ClipState& clip = state();
    const auto now = Clock::now();
    const double dt_ms = clip.have_last_frame ? std::chrono::duration<double, std::milli>(now - clip.last_frame).count() : 0.0;
    clip.last_frame = now;
    clip.have_last_frame = true;
    if (!clip.recording && dt_ms > 0.0 && dt_ms < kMsPerSecond) {
        clip.frame_ms = clip.frame_ms == 0.0 ? dt_ms : clip.frame_ms * kBaselineKeep + dt_ms * (1.0 - kBaselineKeep);
    }
    if (!clip.finishing.empty()) {
        collect_finished();
    }
    if ((clip.key == 0 && clip.request < 0) || !is_switched_on()) {
        if (clip.recording) {
            stop_recording("clips switched off");
        }
        return;
    }
    const int asked = clip.request.exchange(-1);
    if (asked >= 0) {
        const bool start = asked == static_cast<int>(Request::kStart) || (asked == static_cast<int>(Request::kToggle) && !clip.recording);
        if (!start && clip.recording) {
            stop_recording("stopped");
            return;
        }
        if (start && !clip.recording && rendering && !render::engine_parts().loading_screen_up()) {
            const std::string failed = start_recording();
            if (!failed.empty()) {
                get_log("clip: not started - %s", failed.c_str());
                screenshot::show_notice("Clip failed: " + failed);
            }
        }
    }
    if (!clip.recording || !rendering) {
        return;
    }
    Recording& rec = *clip.recording;
    if (render::engine_parts().loading_screen_up() || screenshot::level_key() != rec.level) {
        stop_recording("level change");
        return;
    }
    if (clip.recording->job->failed) {
        stop_recording("the encoder failed: " + clip.recording->job->error);
        return;
    }
    const auto began = Clock::now();
    take_frame(rec, std::chrono::duration<double, std::milli>(began - rec.last_present).count());
    rec.last_present = began;
    if (!rec.capture_error.empty()) {
        get_log("clip: the picture couldn't be read - %s", rec.capture_error.c_str());
        stop_recording("the picture couldn't be read: " + rec.capture_error);
        return;
    }
    rec.work_ms += std::chrono::duration<double, std::milli>(Clock::now() - began).count();
    if (seconds_since(rec.began) >= rec.max_seconds) {
        stop_recording("length cap");
    }
}

void device_lost()
{
    if (state().recording) {
        stop_recording("device lost");
    }
}

void shutdown()
{
    ClipState& clip = state();
    stop_recording("game exit");
    for (const std::shared_ptr<Job>& job : clip.finishing) {
        if (job->thread.joinable()) {
            job->thread.join();
        }
    }
    clip.finishing.clear();
}

} // namespace sys::clip
