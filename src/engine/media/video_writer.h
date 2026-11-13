#pragma once

// NOT ORIGINAL - media::VideoWriter: a video file written a frame at a time (ours: the demos' SAVE AS VIDEO and
// -demo-video - devtools/demo.h - and the exe's -encode-video for tools/game/demo_compare.ts; the game never wrote
// video). The engine side only says what it wants - a file, a size, a frame rate, a quality and a codec choice - and
// hands it RGB frames; app/ installs the writer at start-up (install_video_writer), as it does the movie codec:
// FFmpeg's encoders and MP4 muxer (media_ffmpeg/ffmpeg_video.h). Not installed, open_video fails ("no video writer").
//
// The frames go in at a fixed rate whatever the time they take to make: frame n is shown at n / fps seconds.

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace media {

// What the video is: its size (even numbers: H.264's 4:2:0 needs them - odd ones are rounded down), its frame rate,
// a quality from 1 (smallest) to 100 (best) and the codec: "auto" (the best H.264 encoder that opens - NVIDIA's,
// then Windows' Media Foundation - else MPEG-4 part 2), "h264" (H.264 only), "mpeg4" or an FFmpeg encoder's name.
struct VideoFormat {
    int width = 0;
    int height = 0;
    int fps = 60;
    int quality = 80;
    std::string codec = "auto";
};

class VideoWriter {
public:
    VideoWriter() = default;
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;
    VideoWriter(VideoWriter&&) = delete;
    VideoWriter& operator=(VideoWriter&&) = delete;
    virtual ~VideoWriter() = default;

    // The next frame: tightly packed RGB (3 bytes a pixel, top row first) of any size - scaled to the video's. False
    // (error() says why) if it couldn't be encoded or written; the writer is then of no more use.
    virtual bool add_frame(std::span<const uint8_t> rgb, int width, int height) = 0;
    // The last frames encoded, the file finished and closed. False (error()) if that failed.
    virtual bool finish() = 0;
    // The encoder in use ("h264_nvenc", "h264_mf", "mpeg4"...) and the frames written.
    [[nodiscard]] virtual std::string codec() const = 0;
    [[nodiscard]] virtual int64_t frames() const = 0;
    [[nodiscard]] virtual std::string error() const = 0;
};

// Makes a writer for `path` (the file made, or replaced): null (and why in `error`) if no encoder of the choice opens.
using VideoWriterFactory = std::unique_ptr<VideoWriter> (*)(const std::string& path, const VideoFormat& format,
                                                            std::string* error);

// The settings in thug2-decomp.ini's [demo]: video_fps (default 60, 1-60), video_quality (80, 1-100), video_codec
// (auto) - the size left 0 (the caller's) - and video_scale (100: the picture's size; a percentage, 10-400).
VideoFormat configured_format();
int configured_scale_percent();

std::unique_ptr<VideoWriter> open_video(const std::string& path, const VideoFormat& format, std::string* error);
void install_video_writer(VideoWriterFactory factory);

// Whether game frame `game_frame` (from 0; 60 a second) is the first at or past a video frame's time at `fps` a
// second - the frames a video takes: 60 every frame; 30 every other one (0, 2, 4...); 24 two or three apart. `fps`
// clamped to 1-60.
bool starts_video_frame(uint32_t game_frame, int fps);

} // namespace media
