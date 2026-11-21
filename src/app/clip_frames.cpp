// NOT ORIGINAL - the exe as a clip reader (clip_frames.h).

#include "app/clip_frames.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "fs_win32/win32_disk.h"
#include "game/front_end/start_level.h"
#include "media_ffmpeg/ffmpeg_video_read.h"
#include "platform/std_streams.h"

namespace app {

namespace {

constexpr size_t kCropNumbers = 4;
constexpr int kMaxScalePercent = 1600;

void say(const std::string& line) { platform::write_standard_error(line + "\n"); }

std::vector<int> numbers_of(const std::string& text)
{
    std::vector<int> numbers;
    size_t from = 0;
    while (!text.empty() && from <= text.size()) {
        size_t comma = text.find(',', from);
        if (comma == std::string::npos) {
            comma = text.size();
        }
        numbers.push_back(text::to_int(text.substr(from, comma - from).c_str()));
        from = comma + 1;
    }
    return numbers;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes)
{
    const fs::OwnedFile file(fs::disk().open(path.c_str(), {.access = fs::Access::kWrite,
                                                            .sharing = fs::Sharing::kNone,
                                                            .disposition = fs::Disposition::kCreateAlways,
                                                            .hint = fs::Hint::kNormal}));
    if (!file) {
        return false;
    }
    uint32_t written = 0;
    return fs::disk().write(file.get(), bytes.data(), static_cast<uint32_t>(bytes.size()), &written) &&
           written == bytes.size();
}

} // namespace

bool run_clip_frames(int* exit_code)
{
    const std::string line = GetCommandLineA();
    using frontend::begin_level::option_value;
    const std::string path = option_value(line, "clip-frames");
    if (path.empty()) {
        return false;
    }
    *exit_code = 1;
    fs::install_disk(fs::win32::disk()); // (this mode runs before the game's start-up installs it)
    const std::string folder = option_value(line, "frames-out");
    std::set<int64_t> wanted; // (the decoder counts from 1)
    for (const int index : numbers_of(option_value(line, "frames"))) {
        wanted.insert(static_cast<int64_t>(index) + 1);
    }
    if (!wanted.empty() && folder.empty()) {
        say("clip: error - -frames-out <folder> needed");
        return true;
    }
    media::ffmpeg::FrameOptions options;
    if (const std::vector<int> crop = numbers_of(option_value(line, "frames-crop")); crop.size() == kCropNumbers) {
        options.crop_x = crop.at(0);
        options.crop_y = crop.at(1);
        options.crop_width = crop.at(2);
        options.crop_height = crop.at(3);
    }
    if (const std::string scale = option_value(line, "frames-scale"); !scale.empty()) {
        options.scale_percent = text::to_int(scale.c_str());
        if (options.scale_percent < 1 || options.scale_percent > kMaxScalePercent) {
            say("clip: error - -frames-scale is a percentage, 1-1600");
            return true;
        }
    }
    const media::ffmpeg::VideoReport report = media::ffmpeg::read_video(path, wanted, options);
    if (!report.ok) {
        say("clip: error - " + report.error);
        return true;
    }
    say(text::format("clip: %s %dx%d %.3f %lld", report.codec.c_str(), report.width, report.height, report.fps,
                     static_cast<long long>(report.frames)));
    if (!wanted.empty()) {
        fs::disk().make_folders((folder + "\\").c_str());
    }
    for (const auto& [number, png] : report.pngs) {
        const std::string file = text::format("vf_%06lld.png", static_cast<long long>(number - 1));
        if (!write_file(folder + "\\" + file, png)) {
            say("clip: error - couldn't write " + folder + "\\" + file);
            return true;
        }
        say(text::format("frame %lld %s", static_cast<long long>(number - 1), file.c_str()));
    }
    *exit_code = 0;
    return true;
}

} // namespace app
