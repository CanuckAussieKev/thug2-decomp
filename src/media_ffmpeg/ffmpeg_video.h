#pragma once

#include <memory>
#include <string>

#include "engine/media/video_writer.h"

// NOT ORIGINAL - FFmpeg's media::VideoWriter (ffmpeg_video.cpp): an MP4 file, H.264 by NVIDIA's hardware encoder
// (h264_nvenc) or Windows' Media Foundation encoder (h264_mf), or MPEG-4 part 2 by FFmpeg's own (mpeg4) - the LGPL DLLs
// next to the exe (tools/ffmpeg/build_ffmpeg.ts), no x264. The RGB frames are scaled and turned into BT.709 4:2:0 by
// swscale. Video only: the game's sound isn't recorded (a demo is rendered slower than real time, its sound played as
// it goes). app/main.cpp installs it.
namespace media::ffmpeg {

std::unique_ptr<VideoWriter> open_video_writer(const std::string& path, const VideoFormat& format, std::string* error);

} // namespace media::ffmpeg
