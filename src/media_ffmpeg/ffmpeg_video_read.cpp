// NOT ORIGINAL - a video read back with FFmpeg's DLLs (ffmpeg_video_read.h).

#include "media_ffmpeg/ffmpeg_video_read.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "media_ffmpeg/ffmpeg_api.h"
#include "platform/screenshot_files.h"

namespace media::ffmpeg {

namespace {

constexpr int kUnitRange = 1 << 16; // (swscale's brightness 0, contrast and saturation 1: 16.16 fixed point)

// A decoded frame as a PNG file's bytes (empty if it couldn't be made).
std::vector<uint8_t> frame_png(const Api& lib, const AVFrame* frame)
{
    SwsContext* scale = lib.scale_context(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                          frame->width, frame->height, AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr,
                                          nullptr);
    if (scale == nullptr) {
        return {};
    }
    const int* bt709 = lib.scale_coefficients(SWS_CS_ITU709);
    lib.scale_colour_space(scale, bt709, 0, bt709, 1, 0, kUnitRange, kUnitRange);
    std::vector<uint8_t> rgb(static_cast<size_t>(frame->width) * frame->height * 3);
    const std::array<uint8_t*, 1> planes = {rgb.data()};
    const std::array<int, 1> strides = {frame->width * 3};
    lib.scale(scale, std::data(frame->data), std::data(frame->linesize), 0, frame->height, planes.data(),
              strides.data());
    lib.scale_free_context(scale);
    std::vector<uint8_t> png;
    if (!sys::screenshot::encode_png(rgb.data(), frame->width, frame->height, &png)) {
        png.clear();
    }
    return png;
}

} // namespace

VideoReport read_video(const std::string& path, const std::set<int64_t>& wanted)
{
    VideoReport report;
    const Api* lib = api();
    const EncodeApi* enc = encode_api();
    if (lib == nullptr || enc == nullptr) {
        report.error = "FFmpeg's DLLs (with the MP4 demuxer) can't be used";
        return report;
    }
    AVFormatContext* input = nullptr;
    if (lib->format_open_input(&input, path.c_str(), nullptr, nullptr) < 0 ||
        enc->find_stream_info(input, nullptr) < 0) {
        report.error = path + " can't be opened";
        if (input != nullptr) {
            lib->format_close_input(&input);
        }
        return report;
    }
    const std::span<AVStream*> streams(input->streams, input->nb_streams);
    const AVStream* video = nullptr;
    for (const AVStream* stream : streams) {
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video = stream;
            break;
        }
    }
    const AVCodec* codec = video != nullptr ? lib->find_decoder(video->codecpar->codec_id) : nullptr;
    AVCodecContext* context = codec != nullptr ? lib->codec_alloc_context(codec) : nullptr;
    if (context == nullptr || lib->parameters_to_context(context, video->codecpar) < 0 ||
        lib->codec_open(context, codec, nullptr) < 0) {
        report.error = path + ": no video, or no decoder for it";
        lib->codec_free_context(&context);
        lib->format_close_input(&input);
        return report;
    }
    AVPacket* packet = lib->packet_alloc();
    AVFrame* frame = lib->frame_alloc();
    bool decoded_all = true;
    const auto take = [&]() {
        while (lib->receive_frame(context, frame) >= 0) {
            ++report.frames;
            if (wanted.contains(report.frames)) {
                std::vector<uint8_t> png = frame_png(*lib, frame);
                decoded_all = decoded_all && !png.empty();
                report.pngs[report.frames] = std::move(png);
            }
        }
    };
    while (lib->read_frame(input, packet) >= 0) {
        if (packet->stream_index == video->index && lib->send_packet(context, packet) < 0) {
            decoded_all = false;
        }
        lib->packet_unref(packet);
        take();
    }
    lib->send_packet(context, nullptr);
    take();
    report.codec = codec->name;
    report.width = context->width;
    report.height = context->height;
    report.fps = video->avg_frame_rate.den != 0
                     ? static_cast<double>(video->avg_frame_rate.num) / video->avg_frame_rate.den
                     : 0.0;
    report.stream_frames = video->nb_frames;
    report.ok = decoded_all && report.frames > 0;
    if (!report.ok) {
        report.error = report.frames == 0 ? "no frame decoded" : "a frame didn't decode, or its PNG wasn't made";
    }
    lib->frame_free(&frame);
    lib->packet_free(&packet);
    lib->codec_free_context(&context);
    lib->format_close_input(&input);
    return report;
}

} // namespace media::ffmpeg
