// NOT ORIGINAL - thug2_video_check.exe: a video the game wrote (a demo saved as a video - devtools/demo.h -, or
// -encode-video's) read back with the game's own FFmpeg DLLs: its codec, size, frame rate and frame count, and frames
// saved as PNG to compare with the game's own capture of the same frames.
//
//   thug2_video_check <file.mp4> [--frame N <out.png>]...     (frames from 1)
//
// Prints one JSON object: {"file", "codec", "width", "height", "fps", "frames", "decoded": [N...]}. Exit code 0 when the
// file opened and every frame decoded, 1 otherwise.

#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "base/core/parse_number.h"
#include "media_ffmpeg/ffmpeg_api.h"
#include "platform/screenshot_files.h"
#include "tests/no_error_dialogs.h"

namespace {

using media::ffmpeg::Api;
using media::ffmpeg::EncodeApi;

bool write_png(const Api& a, const AVFrame* frame, const std::string& path)
{
    SwsContext* scale = a.scale_context(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                        frame->width, frame->height, AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr,
                                        nullptr);
    if (scale == nullptr) {
        return false;
    }
    // (BT.709 video range in, full-range RGB out - as the writer made it)
    const int* bt709 = a.scale_coefficients(SWS_CS_ITU709);
    a.scale_colour_space(scale, bt709, 0, bt709, 1, 0, 1 << 16, 1 << 16);
    std::vector<uint8_t> rgb(static_cast<size_t>(frame->width) * frame->height * 3);
    uint8_t* planes[1] = {rgb.data()};
    const int strides[1] = {frame->width * 3};
    a.scale(scale, frame->data, frame->linesize, 0, frame->height, planes, strides);
    a.scale_free_context(scale);
    std::vector<uint8_t> png;
    if (!sys::screenshot::encode_png(rgb.data(), frame->width, frame->height, &png)) {
        return false;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
        return false;
    }
    const bool ok = fwrite(png.data(), 1, png.size(), file) == png.size();
    return (fclose(file) == 0) && ok;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> args(argv, static_cast<size_t>(argc));
    if (args.size() < 2) {
        std::fputs("usage: thug2_video_check <file.mp4> [--frame N <out.png>]...\n", stderr);
        return 2;
    }
    std::map<int64_t, std::string> wanted;
    for (size_t i = 2; i < args.size(); i += 3) {
        if (std::string(args[i]) != "--frame" || i + 2 >= args.size()) {
            std::fputs("usage: thug2_video_check <file.mp4> [--frame N <out.png>]...\n", stderr);
            return 2;
        }
        wanted[text::to_int(args[i + 1])] = args[i + 2];
    }
    const Api* a = media::ffmpeg::api();
    const EncodeApi* e = media::ffmpeg::encode_api();
    if (a == nullptr || e == nullptr) {
        std::fputs("FFmpeg's DLLs (with the MP4 demuxer) aren't next to the check\n", stderr);
        return 1;
    }
    const std::string path = args[1];
    AVFormatContext* input = nullptr;
    if (a->format_open_input(&input, path.c_str(), nullptr, nullptr) < 0 || e->find_stream_info(input, nullptr) < 0) {
        std::fprintf(stderr, "%s can't be opened\n", path.c_str());
        return 1;
    }
    int stream = -1;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream = static_cast<int>(i);
            break;
        }
    }
    if (stream < 0) {
        std::fprintf(stderr, "%s has no video\n", path.c_str());
        return 1;
    }
    AVStream* video = input->streams[stream];
    const AVCodec* codec = a->find_decoder(video->codecpar->codec_id);
    AVCodecContext* context = codec != nullptr ? a->codec_alloc_context(codec) : nullptr;
    if (context == nullptr || a->parameters_to_context(context, video->codecpar) < 0 ||
        a->codec_open(context, codec, nullptr) < 0) {
        std::fprintf(stderr, "%s: no decoder for its video\n", path.c_str());
        return 1;
    }
    AVPacket* packet = a->packet_alloc();
    AVFrame* frame = a->frame_alloc();
    int64_t frames = 0;
    bool ok = true;
    std::string decoded;
    const auto take = [&]() {
        while (a->receive_frame(context, frame) >= 0) {
            ++frames;
            if (const auto found = wanted.find(frames); found != wanted.end()) {
                if (write_png(*a, frame, found->second)) {
                    decoded += (decoded.empty() ? "" : ", ") + std::to_string(frames);
                } else {
                    ok = false;
                }
            }
        }
    };
    while (a->read_frame(input, packet) >= 0) {
        if (packet->stream_index == stream && a->send_packet(context, packet) < 0) {
            ok = false;
        }
        a->packet_unref(packet);
        take();
    }
    a->send_packet(context, nullptr);
    take();
    const double fps = video->avg_frame_rate.den != 0
                           ? static_cast<double>(video->avg_frame_rate.num) / video->avg_frame_rate.den
                           : 0.0;
    std::string shown; // (the path as a JSON string's text)
    for (const char character : path) {
        if (character == '\\' || character == '"') {
            shown += '\\';
        }
        shown += character;
    }
    std::printf("{\"file\": \"%s\", \"codec\": \"%s\", \"width\": %d, \"height\": %d, \"fps\": %.3f, \"frames\": %lld, "
                "\"stream_frames\": %lld, \"decoded\": [%s]}\n",
                shown.c_str(), codec->name, context->width, context->height, fps, static_cast<long long>(frames),
                static_cast<long long>(video->nb_frames), decoded.c_str());
    a->frame_free(&frame);
    a->packet_free(&packet);
    a->codec_free_context(&context);
    a->format_close_input(&input);
    return ok && frames > 0 ? 0 : 1;
}
