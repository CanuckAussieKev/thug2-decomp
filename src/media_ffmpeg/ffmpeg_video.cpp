// NOT ORIGINAL - FFmpeg's video writer (ffmpeg_video.h).
//
// The encoder runs on a thread of its own: Media Foundation's (h264_mf) wants a thread whose COM is multithreaded,
// which the game's main thread isn't promised to be; and the game draws its next frame while the last is encoded.
// add_frame queues a copy of the frame (at most kQueued waiting: the game waits for the encoder beyond that).

#include "media_ffmpeg/ffmpeg_video.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/format.h"
#include "media_ffmpeg/ffmpeg_api.h"
#include "platform/run_log.h"

namespace media::ffmpeg {

namespace {

constexpr std::size_t kQueued = 4;
constexpr int kErrorTextBytes = 128;
constexpr int kMaxQuality = 100;
constexpr int kGopSeconds = 2;
// nvenc's constant quality (0-51, lower is better) over the quality's 1-100
constexpr int kNvencWorst = 40;
constexpr int kNvencBest = 16;
// Media Foundation's bit rate: this many bits a pixel a frame at quality 0 and how many more at 100
constexpr double kMfBitsBase = 0.02;
constexpr double kMfBitsRange = 0.22;
// mpeg4's quantiser (2-31, lower is better)
constexpr int kMpeg4Worst = 31;
constexpr int kMpeg4Best = 2;

struct Frame {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
    bool last = false; // (the end: no picture)
};

std::string error_text(const EncodeApi& e, int code)
{
    std::array<char, kErrorTextBytes> text{};
    e.error_text(code, text.data(), text.size());
    return text.data();
}

// The encoders to try for the choice, best first.
std::vector<std::string> candidates(const std::string& codec)
{
    if (codec.empty() || codec == "auto") {
        return {"h264_nvenc", "h264_mf", "mpeg4"};
    }
    if (codec == "h264") {
        return {"h264_nvenc", "h264_mf"};
    }
    return {codec};
}

// quality 1-100 -> `worst`..`best` (either way round)
int scaled(int quality, int worst, int best)
{
    const int q = std::clamp(quality, 1, kMaxQuality);
    return worst + (((best - worst) * (q - 1)) / (kMaxQuality - 1));
}

class Writer final : public VideoWriter {
public:
    Writer(std::string path, VideoFormat format) : path_(std::move(path)), format_(std::move(format)) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) = delete;
    Writer& operator=(Writer&&) = delete;
    ~Writer() override
    {
        if (thread_.joinable()) {
            (void)finish();
        }
    }

    // The thread started and the file opened on it: false (error()) if no encoder of the choice opened.
    bool start()
    {
        thread_ = std::thread([this] { run(); });
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return opened_ || !error_.empty(); });
        if (!opened_) {
            lock.unlock();
            thread_.join();
            return false;
        }
        return true;
    }

    bool add_frame(std::span<const uint8_t> rgb, int width, int height) override
    {
        if (width <= 0 || height <= 0 || rgb.size() < static_cast<std::size_t>(width) * height * 3) {
            set_error("a frame without its pixels");
            return false;
        }
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return queue_.size() < kQueued || !error_.empty(); });
        if (!error_.empty() || finished_) {
            return false;
        }
        queue_.push_back({.rgb = std::vector<uint8_t>(rgb.begin(), rgb.end()), .width = width, .height = height});
        changed_.notify_all();
        return true;
    }

    bool finish() override
    {
        if (!thread_.joinable()) {
            return error().empty();
        }
        {
            const std::scoped_lock lock(mutex_);
            finished_ = true;
            queue_.push_back({.last = true});
            changed_.notify_all();
        }
        thread_.join();
        return error().empty();
    }

    [[nodiscard]] std::string codec() const override
    {
        const std::scoped_lock lock(mutex_);
        return codec_name_;
    }
    [[nodiscard]] int64_t frames() const override
    {
        const std::scoped_lock lock(mutex_);
        return written_;
    }
    [[nodiscard]] std::string error() const override
    {
        const std::scoped_lock lock(mutex_);
        return error_;
    }

private:
    void set_error(const std::string& text)
    {
        const std::scoped_lock lock(mutex_);
        if (error_.empty()) {
            error_ = text;
        }
        changed_.notify_all();
    }

    // --- the encoder's thread ---

    void run()
    {
        const EncodeApi* e = encode_api();
        if (e == nullptr) {
            set_error("FFmpeg's DLLs can't encode (tools/ffmpeg/build_ffmpeg.ts builds ones that can)");
            return;
        }
        std::string why;
        if (!open_file(*e, &why)) {
            close(*e);
            set_error(why);
            return;
        }
        {
            const std::scoped_lock lock(mutex_);
            opened_ = true;
            changed_.notify_all();
        }
        while (true) {
            Frame frame;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this] { return !queue_.empty(); });
                frame = std::move(queue_.front());
                queue_.pop_front();
                changed_.notify_all();
            }
            if (frame.last) {
                break;
            }
            if (error().empty() && !encode(*e, frame, &why)) {
                set_error(why);
            }
        }
        if (error().empty() && !end_file(*e, &why)) {
            set_error(why);
        }
        close(*e);
    }

    // The first encoder of the choice that opens, its stream, the file and its header.
    bool open_file(const EncodeApi& e, std::string* why)
    {
        const Api& a = *api();
        width_ = std::max(2, format_.width & ~1);
        height_ = std::max(2, format_.height & ~1);
        fps_ = std::clamp(format_.fps, 1, 60);
        int result = e.format_alloc_output(&output_, nullptr, "mp4", path_.c_str());
        if (result < 0 || output_ == nullptr) {
            *why = "no MP4 muxer (" + error_text(e, result) + ")";
            return false;
        }
        std::string tried;
        for (const std::string& name : candidates(format_.codec)) {
            const AVCodec* codec = e.find_encoder_by_name(name.c_str());
            if (codec == nullptr) {
                tried += (tried.empty() ? "" : "; ") + name + ": not in FFmpeg's DLLs";
                continue;
            }
            context_ = a.codec_alloc_context(codec);
            configure(e, name);
            if ((output_->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
                context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            result = a.codec_open(context_, codec, nullptr);
            if (result >= 0) {
                const std::scoped_lock lock(mutex_);
                codec_name_ = name;
                break;
            }
            tried += (tried.empty() ? "" : "; ") + name + ": " + error_text(e, result);
            a.codec_free_context(&context_);
        }
        if (context_ == nullptr) {
            *why = "no encoder opened (" + tried + ")";
            return false;
        }
        if (!tried.empty()) {
            get_log("video: %s (not %s)", codec_name_.c_str(), tried.c_str());
        }
        stream_ = e.format_new_stream(output_, nullptr);
        if (stream_ == nullptr || e.parameters_from_context(stream_->codecpar, context_) < 0) {
            *why = "the video stream couldn't be made";
            return false;
        }
        stream_->time_base = context_->time_base;
        stream_->avg_frame_rate = context_->framerate;
        result = e.io_open(&output_->pb, path_.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            *why = "can't write " + path_ + " (" + error_text(e, result) + ")";
            return false;
        }
        result = e.format_write_header(output_, nullptr);
        if (result < 0) {
            *why = "the MP4 header couldn't be written (" + error_text(e, result) + ")";
            return false;
        }
        picture_ = a.frame_alloc();
        packet_ = a.packet_alloc();
        picture_->format = context_->pix_fmt;
        picture_->width = width_;
        picture_->height = height_;
        if (e.frame_get_buffer(picture_, 0) < 0) {
            *why = "no memory for a frame";
            return false;
        }
        return true;
    }

    // The encoder's settings: size, rate, BT.709 4:2:0, a key frame every 2 s, the quality its own way.
    void configure(const EncodeApi& e, const std::string& name)
    {
        context_->width = width_;
        context_->height = height_;
        context_->time_base = AVRational{1, fps_};
        context_->framerate = AVRational{fps_, 1};
        context_->pix_fmt = AV_PIX_FMT_YUV420P;
        context_->gop_size = fps_ * kGopSeconds;
        context_->color_primaries = AVCOL_PRI_BT709;
        context_->color_trc = AVCOL_TRC_BT709;
        context_->colorspace = AVCOL_SPC_BT709;
        context_->color_range = AVCOL_RANGE_MPEG;
        context_->thread_count = 0; // (as many as there are cores: the software encoder)
        if (name == "h264_nvenc") {
            e.opt_set(context_->priv_data, "preset", "p5", 0);
            e.opt_set(context_->priv_data, "rc", "vbr", 0);
            e.opt_set_int(context_->priv_data, "cq", scaled(format_.quality, kNvencWorst, kNvencBest), 0);
            context_->bit_rate = 0;
            context_->max_b_frames = 0;
        } else if (name == "h264_mf") {
            const double bits =
                static_cast<double>(width_) * height_ * fps_ *
                (kMfBitsBase + (kMfBitsRange * std::clamp(format_.quality, 1, kMaxQuality) / kMaxQuality));
            context_->bit_rate = static_cast<int64_t>(bits);
            e.opt_set(context_->priv_data, "scenario", "archive", 0);
            context_->max_b_frames = 0;
        } else {
            context_->flags |= AV_CODEC_FLAG_QSCALE;
            context_->global_quality = FF_QP2LAMBDA * scaled(format_.quality, kMpeg4Worst, kMpeg4Best);
            context_->max_b_frames = 0;
        }
    }

    bool encode(const EncodeApi& e, const Frame& frame, std::string* why)
    {
        const Api& a = *api();
        if (scaler_ == nullptr || frame.width != source_width_ || frame.height != source_height_) {
            if (scaler_ != nullptr) {
                a.scale_free_context(scaler_);
            }
            source_width_ = frame.width;
            source_height_ = frame.height;
            const bool same = source_width_ == width_ && source_height_ == height_;
            scaler_ = a.scale_context(source_width_, source_height_, AV_PIX_FMT_RGB24, width_, height_,
                                      AV_PIX_FMT_YUV420P, same ? SWS_POINT : SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (scaler_ == nullptr) {
                *why = text::format("no colour conversion from %dx%d", source_width_, source_height_);
                return false;
            }
            // (RGB in full range -> BT.709 YUV in video range)
            const int* bt709 = a.scale_coefficients(SWS_CS_ITU709);
            a.scale_colour_space(scaler_, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        }
        if (e.frame_make_writable(picture_) < 0) {
            *why = "the frame isn't writable";
            return false;
        }
        const std::array<const uint8_t*, 1> planes = {frame.rgb.data()};
        const std::array<int, 1> strides = {frame.width * 3};
        a.scale(scaler_, planes.data(), strides.data(), 0, frame.height, picture_->data, picture_->linesize);
        picture_->pts = next_pts_++;
        const int result = e.send_frame(context_, picture_);
        if (result < 0) {
            *why = "the encoder refused a frame (" + error_text(e, result) + ")";
            return false;
        }
        if (!drain(e, why)) {
            return false;
        }
        const std::scoped_lock lock(mutex_);
        ++written_;
        return true;
    }

    // The packets the encoder has ready, into the file.
    bool drain(const EncodeApi& e, std::string* why)
    {
        const Api& a = *api();
        while (true) {
            const int result = e.receive_packet(context_, packet_);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return true;
            }
            if (result < 0) {
                *why = "the encoder failed (" + error_text(e, result) + ")";
                return false;
            }
            e.packet_rescale_ts(packet_, context_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            const int written = e.interleaved_write_frame(output_, packet_);
            a.packet_unref(packet_);
            if (written < 0) {
                *why = "couldn't write to " + path_ + " (" + error_text(e, written) + ")";
                return false;
            }
        }
    }

    bool end_file(const EncodeApi& e, std::string* why)
    {
        const int result = e.send_frame(context_, nullptr);
        if (result < 0 && result != AVERROR_EOF) {
            *why = "the encoder couldn't finish (" + error_text(e, result) + ")";
            return false;
        }
        if (!drain(e, why)) {
            return false;
        }
        if (e.write_trailer(output_) < 0) {
            *why = "the MP4's index couldn't be written";
            return false;
        }
        return true;
    }

    void close(const EncodeApi& e)
    {
        const Api& a = *api();
        if (scaler_ != nullptr) {
            a.scale_free_context(scaler_);
            scaler_ = nullptr;
        }
        a.frame_free(&picture_);
        a.packet_free(&packet_);
        a.codec_free_context(&context_);
        if (output_ != nullptr) {
            if (output_->pb != nullptr) {
                e.io_closep(&output_->pb);
            }
            e.format_free_context(output_);
            output_ = nullptr;
        }
    }

    std::string path_;
    VideoFormat format_;
    int width_ = 0, height_ = 0, fps_ = 0;
    // (the encoder's thread's)
    AVFormatContext* output_ = nullptr;
    AVCodecContext* context_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* picture_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* scaler_ = nullptr;
    int source_width_ = 0, source_height_ = 0;
    int64_t next_pts_ = 0;
    // (shared, under mutex_)
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Frame> queue_;
    bool opened_ = false;
    bool finished_ = false;
    std::string codec_name_;
    std::string error_;
    int64_t written_ = 0;
    std::thread thread_;
};

} // namespace

std::unique_ptr<VideoWriter> open_video_writer(const std::string& path, const VideoFormat& format, std::string* error)
{
    if (api() == nullptr) {
        *error = "FFmpeg's DLLs can't be used (see the log)";
        return nullptr;
    }
    auto writer = std::make_unique<Writer>(path, format);
    if (!writer->start()) {
        *error = writer->error();
        return nullptr;
    }
    get_log("video: %s - %s, %dx%d, %d a second, quality %d", path.c_str(), writer->codec().c_str(), format.width & ~1,
            format.height & ~1, format.fps, format.quality);
    return writer;
}

} // namespace media::ffmpeg
