#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// NOT ORIGINAL - a video read back with FFmpeg's DLLs (ffmpeg_video_read.cpp): what the video check program
// (src/tests/video_check.cpp) asks of a demo saved as a video - its codec, size, frame rate and frame count, and frames
// saved as PNG to compare with the game's own capture of them.
namespace media::ffmpeg {

struct VideoReport {
    bool ok = false;     // opened, and every frame decoded (and saved where asked)
    std::string error;   // why not ("" when ok)
    std::string codec;   // the decoder's name ("h264", "mpeg4")
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t frames = 0;        // decoded
    int64_t stream_frames = 0; // as the file's index says
    std::map<int64_t, std::vector<uint8_t>> pngs; // the frames asked for, as PNG files' bytes
};

// The file at `path` decoded whole; the frames in `wanted` (from 1) made PNG files (BT.709 video range back to full-range
// RGB, as the writer made it).
VideoReport read_video(const std::string& path, const std::set<int64_t>& wanted);

} // namespace media::ffmpeg
