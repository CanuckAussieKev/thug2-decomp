// NOT ORIGINAL - thug2_video_check.exe: a video the game wrote (a demo saved as a video - devtools/demo.h -, or
// -encode-video's) read back with the game's own FFmpeg DLLs (media_ffmpeg/ffmpeg_video_read.h): its codec, size, frame
// rate and frame count, and frames saved as PNG to compare with the game's own capture of the same frames.
//
//   thug2_video_check <file.mp4> [--frame N <out.png>]...     (frames from 1)
//
// Prints one JSON object: {"file", "codec", "width", "height", "fps", "frames", "stream_frames", "decoded": [N...]}.
// Exit code 0 when the file opened, every frame decoded and every PNG was written; 1 otherwise (2: wrong arguments).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <ios>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/parse_number.h"
#include "media_ffmpeg/ffmpeg_video_read.h"
#include "tests/no_error_dialogs.h"

namespace {

constexpr const char* kUsage = "usage: thug2_video_check <file.mp4> [--frame N <out.png>]...\n";

// A path as a JSON string's text.
std::string json_text(const std::string& text)
{
    std::string out;
    for (const char character : text) {
        if (character == '\\' || character == '"') {
            out += '\\';
        }
        out += character;
    }
    return out;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    const std::string text(bytes.begin(), bytes.end());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return !file.fail();
}

// The check (main's, inside its exception handler).
int run(std::span<char*> args)
{
    if (args.size() < 2) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    std::map<int64_t, std::string> wanted;
    for (size_t i = 2; i < args.size(); i += 3) {
        if (std::string(core::at(args, i)) != "--frame" || i + 2 >= args.size()) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        wanted[text::to_int(core::at(args, i + 1))] = core::at(args, i + 2);
    }
    std::set<int64_t> frames;
    for (const auto& [frame, png] : wanted) {
        frames.insert(frame);
    }
    const std::string path = core::at(args, 1);
    const media::ffmpeg::VideoReport report = media::ffmpeg::read_video(path, frames);
    if (report.frames == 0) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), report.error.c_str());
        return 1;
    }
    bool written = true;
    std::string decoded;
    for (const auto& [frame, png] : report.pngs) {
        written = written && !png.empty() && write_file(wanted.at(frame), png);
        decoded += (decoded.empty() ? "" : ", ") + std::to_string(frame);
    }
    std::printf("{\"file\": \"%s\", \"codec\": \"%s\", \"width\": %d, \"height\": %d, \"fps\": %.3f, \"frames\": %lld, "
                "\"stream_frames\": %lld, \"decoded\": [%s]}\n",
                json_text(path).c_str(), report.codec.c_str(), report.width, report.height, report.fps,
                static_cast<long long>(report.frames), static_cast<long long>(report.stream_frames), decoded.c_str());
    return report.ok && written ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    try {
        return run(std::span<char*>(argv, static_cast<size_t>(argc)));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "thug2_video_check: %s\n", error.what());
    } catch (...) {
        std::fputs("thug2_video_check: an unknown exception\n", stderr);
    }
    return 1;
}
