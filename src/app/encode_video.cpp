// NOT ORIGINAL - the exe as a video encoder (encode_video.h).

#include "app/encode_video.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/media/video_writer.h"
#include "game/front_end/start_level.h"
#include "media_ffmpeg/ffmpeg_video.h"

namespace app {

namespace {

constexpr int kMaxSide = 16384;

void say(const std::string& line)
{
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error == nullptr || error == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string text = line + "\n";
    DWORD written = 0;
    WriteFile(error, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// The whole of `bytes` from standard input; false at its end (or a read error) before it is filled.
bool read_exactly(HANDLE input, std::vector<uint8_t>* bytes)
{
    size_t got = 0;
    while (got < bytes->size()) {
        DWORD read = 0;
        const auto want = static_cast<DWORD>(std::min<size_t>(bytes->size() - got, 1U << 24U));
        if (ReadFile(input, std::span(*bytes).subspan(got).data(), want, &read, nullptr) == 0 || read == 0) {
            return false;
        }
        got += read;
    }
    return true;
}

} // namespace

bool run_encode_video(int* exit_code)
{
    const std::string line = GetCommandLineA();
    using frontend::begin_level::option_value;
    const std::string path = option_value(line, "encode-video");
    if (path.empty()) {
        return false;
    }
    *exit_code = 1;
    const std::string size = option_value(line, "encode-size");
    const size_t by = size.find('x');
    const int width = by == std::string::npos ? 0 : text::to_int(size.substr(0, by).c_str());
    const int height = by == std::string::npos ? 0 : text::to_int(size.substr(by + 1).c_str());
    if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) {
        say("video: error - -encode-size <width>x<height> needed");
        return true;
    }
    media::VideoFormat format = media::configured_format();
    format.width = width;
    format.height = height;
    if (const std::string fps = option_value(line, "encode-fps"); !fps.empty()) {
        format.fps = text::to_int(fps.c_str());
    }
    if (const std::string quality = option_value(line, "encode-quality"); !quality.empty()) {
        format.quality = text::to_int(quality.c_str());
    }
    if (const std::string codec = option_value(line, "encode-codec"); !codec.empty()) {
        format.codec = codec;
    }
    std::string error;
    const std::unique_ptr<media::VideoWriter> writer = media::ffmpeg::open_video_writer(path, format, &error);
    if (!writer) {
        say("video: error - " + error);
        return true;
    }
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::vector<uint8_t> frame(static_cast<size_t>(width) * height * 3);
    while (input != nullptr && input != INVALID_HANDLE_VALUE && read_exactly(input, &frame)) {
        if (!writer->add_frame(frame, width, height)) {
            break;
        }
    }
    const bool finished = writer->finish();
    if (!finished || writer->frames() == 0) {
        say("video: error - " + (finished ? std::string("no frames") : writer->error()));
        return true;
    }
    say(text::format("video: %s %s %lld frames", path.c_str(), writer->codec().c_str(),
                     static_cast<long long>(writer->frames())));
    *exit_code = 0;
    return true;
}

} // namespace app
