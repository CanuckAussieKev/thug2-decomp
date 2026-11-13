// NOT ORIGINAL - the installed video writer (video_writer.h).

#include "engine/media/video_writer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "platform/run_log.h"

namespace media {

namespace {

constexpr int kGameFramesPerSecond = 60;
constexpr const char* kSection = "demo";
constexpr int kMaxQuality = 100;
constexpr int kFullScale = 100;
constexpr int kMinScale = 10;
constexpr int kMaxScale = 400;

VideoWriterFactory& installed()
{
    static VideoWriterFactory factory = nullptr;
    return factory;
}

} // namespace

std::unique_ptr<VideoWriter> open_video(const std::string& path, const VideoFormat& format, std::string* error)
{
    if (installed() == nullptr) {
        *error = "no video writer";
        return nullptr;
    }
    return installed()(path, format, error);
}

void install_video_writer(VideoWriterFactory factory) { installed() = factory; }

VideoFormat configured_format()
{
    VideoFormat format;
    format.fps = std::clamp(config_int(kSection, "video_fps", format.fps), 1, kGameFramesPerSecond);
    format.quality = std::clamp(config_int(kSection, "video_quality", format.quality), 1, kMaxQuality);
    format.codec = config_text(kSection, "video_codec", "auto");
    if (format.codec.empty()) {
        format.codec = "auto";
    }
    return format;
}

int configured_scale_percent()
{
    return std::clamp(config_int(kSection, "video_scale", kFullScale), kMinScale, kMaxScale);
}

bool starts_video_frame(uint32_t game_frame, const VideoFormat& format)
{
    const auto rate = static_cast<uint64_t>(std::clamp(format.fps, 1, kGameFramesPerSecond));
    // (video frame k's time is k / fps s, game frame g's g / 60 s: k is shown from the first g at or past it, so the
    // video frames begun by g are floor(g * fps / 60) + 1 - g starts one when that goes up)
    const auto due = [rate](uint64_t frame) { return (frame * rate) / kGameFramesPerSecond; };
    return game_frame == 0 || due(game_frame) != due(game_frame - 1ULL);
}

} // namespace media
