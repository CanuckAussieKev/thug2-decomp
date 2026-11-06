// NOT ORIGINAL - a Bink file played through FFmpeg (ffmpeg_playback.h).

#include "media_ffmpeg/ffmpeg_playback.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>

#include "base/core/checked_index.h"
#include "engine/file/disk.h"
#include "engine/media/movie.h"
#include "media_ffmpeg/ffmpeg_api.h"
#include "platform/run_log.h"

namespace media::ffmpeg {

namespace {

constexpr uint32_t kFromMemory = 0x04000000;    // BinkOpen: the source is a Bink file in memory
constexpr uint32_t kSurfaceTypeMask = 0x0F;     // BinkCopyToBuffer's flags: the surface type...
constexpr uint32_t kSurface32 = 3;              // ...BINKSURFACE32 (blue, green, red, unused)
constexpr uint32_t kPixelBytes = 4;             // (its pixels)
constexpr int kUnitFixed16 = 1 << 16;           // 1.0 in swscale's 16.16 brightness / contrast / saturation
constexpr int kIoBufferBytes = 0x10000;         // FFmpeg's read buffer
constexpr size_t kHeaderBytes = 44;             // a Bink file's header: through the sound track count
constexpr uint32_t kMaxMemoryFile = 0x40000000; // (a size beyond this isn't a Bink file in memory)
constexpr uint32_t kFileSizeBias = 8;           // the header's size leaves out the signature and itself
constexpr double kStallSeconds = 1.0;           // a next frame later than this restarts the clock (no racing)

// The header's fields (little-endian 32-bit words).
constexpr size_t kSizeAt = 4;
constexpr size_t kFramesAt = 8;
constexpr size_t kWidthAt = 20;
constexpr size_t kHeightAt = 24;
constexpr size_t kRateAt = 28;
constexpr size_t kRateDivAt = 32;

constexpr unsigned kBitsPerByte = 8;
constexpr uint32_t kNoFrame = 0xFFFFFFFF; // last_frame_num_ before the first frame (as binkw32's)
constexpr int kGoToQuick = 1;             // BinkGoto's BINKGOTOQUICK

uint32_t word_at(std::span<const uint8_t> bytes, size_t at)
{
    uint32_t word = 0;
    for (size_t i = 4; i != 0; --i) {
        word = word << kBitsPerByte | core::at(bytes, at + i - 1);
    }
    return word;
}

bool is_bink_header(std::span<const uint8_t> header)
{
    return core::at(header, 0) == 'B' && core::at(header, 1) == 'I' && core::at(header, 2) == 'K';
}

} // namespace

// Where FFmpeg's demuxer reads the file from: a file on disk by path, or a Bink file in memory.
class Input {
public:
    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;
    ~Input()
    {
        if (file_ != fs::no_file()) {
            fs::disk().close(file_);
        }
    }

    static std::unique_ptr<Input> from_path(const char* path)
    {
        auto input = std::make_unique<Input>();
        input->file_ = fs::disk().open(path, {.access = fs::Access::kRead,
                                              .sharing = fs::Sharing::kRead,
                                              .disposition = fs::Disposition::kOpenExisting,
                                              .hint = fs::Hint::kSequential});
        if (input->file_ == fs::no_file() || !fs::disk().size64(input->file_, &input->size_)) {
            return nullptr;
        }
        return input;
    }

    // (the file's size is its header's)
    static std::unique_ptr<Input> from_memory(const void* data)
    {
        const std::span<const uint8_t> start(static_cast<const uint8_t*>(data), kFileSizeBias);
        if (!is_bink_header(start) || word_at(start, kSizeAt) > kMaxMemoryFile) {
            return nullptr;
        }
        auto input = std::make_unique<Input>();
        const size_t size = static_cast<size_t>(word_at(start, kSizeAt)) + kFileSizeBias;
        input->size_ = size;
        input->memory_ = std::span<const uint8_t>(static_cast<const uint8_t*>(data), size);
        return input;
    }

    // Up to `into`'s size from the position: the count (0 at the end, -1 on a failed read).
    int read(std::span<uint8_t> into)
    {
        const uint64_t left = size_ > position_ ? size_ - position_ : 0;
        const auto count = static_cast<uint32_t>(std::min<uint64_t>(into.size(), left));
        if (count == 0) {
            return 0;
        }
        if (file_ == fs::no_file()) {
            std::ranges::copy(memory_.subspan(static_cast<size_t>(position_), count), into.begin());
        } else {
            uint32_t got = 0;
            if (!fs::disk().seek_to(file_, position_) || !fs::disk().read(file_, into.data(), count, &got, nullptr)) {
                return -1;
            }
            if (got != count) {
                return got == 0 ? -1 : static_cast<int>(got);
            }
        }
        position_ += count;
        return static_cast<int>(count);
    }

    // The position `whence` (SEEK_SET, SEEK_CUR, SEEK_END) counts from; -1 for another.
    [[nodiscard]] int64_t origin(int whence) const
    {
        switch (whence) {
        case SEEK_SET:
            return 0;
        case SEEK_CUR:
            return static_cast<int64_t>(position_);
        case SEEK_END:
            return static_cast<int64_t>(size_);
        default:
            return -1;
        }
    }

    // The position moved to `to`: `to`, or -1 for one outside the file.
    int64_t move_to(int64_t to)
    {
        if (to < 0 || std::cmp_greater(to, size_)) {
            return -1;
        }
        position_ = static_cast<uint64_t>(to);
        return to;
    }

    [[nodiscard]] int64_t size() const { return static_cast<int64_t>(size_); }

    // The file's first bytes (false if it is shorter); the position stays at the start.
    bool header(std::span<uint8_t> into)
    {
        position_ = 0;
        const bool whole = std::cmp_equal(read(into), into.size());
        position_ = 0;
        return whole;
    }

private:
    std::span<const uint8_t> memory_;
    fs::DiskFile* file_ = fs::no_file();
    uint64_t size_ = 0;
    uint64_t position_ = 0;
};

namespace {

int read_input(void* opaque, uint8_t* buffer, int size)
{
    const int got = static_cast<Input*>(opaque)->read(std::span<uint8_t>(buffer, static_cast<size_t>(size)));
    if (got < 0) {
        return AVERROR(EIO);
    }
    return got == 0 ? AVERROR_EOF : got;
}

// FFmpeg's seek: `offset` counted from `from` (its whence: SEEK_SET, SEEK_CUR, SEEK_END); the new position (or with
// AVSEEK_SIZE the size), -1 for one outside the file.
int64_t seek_input(void* opaque, int64_t offset, int from)
{
    auto* input = static_cast<Input*>(opaque);
    if ((from & AVSEEK_SIZE) != 0) {
        return input->size();
    }
    const int64_t origin = input->origin(from & ~AVSEEK_FORCE);
    return origin < 0 ? -1 : input->move_to(origin + offset);
}

} // namespace

std::unique_ptr<Playback> Playback::open(const Api& api, const void* source, uint32_t flags)
{
    if (!source) {
        return nullptr;
    }
    std::unique_ptr<Input> input =
        (flags & kFromMemory) != 0U ? Input::from_memory(source) : Input::from_path(static_cast<const char*>(source));
    if (!input) {
        if ((flags & kFromMemory) == 0U) {
            get_log("ffmpeg: can't open %s", static_cast<const char*>(source));
        }
        return nullptr;
    }
    auto playback = std::make_unique<Playback>(api, std::move(input));
    if (!playback->start(source, flags)) {
        return nullptr;
    }
    return playback;
}

Playback::Playback(const Api& api, std::unique_ptr<Input> input) : MovieHeader{}, api_(&api), input_(std::move(input))
{}

Playback::~Playback()
{
    api_->scale_free_context(converter_);
    api_->frame_free(&frame_);
    api_->packet_free(&packet_);
    api_->codec_free_context(&video_);
    api_->format_close_input(&format_); // (our own reader stays ours)
    if (io_) {
        api_->mem_free(io_->buffer);
        api_->io_context_free(&io_);
    }
}

bool Playback::start(const void* source, uint32_t flags)
{
    const char* name = (flags & kFromMemory) != 0U ? "a Bink file in memory" : static_cast<const char*>(source);
    std::array<uint8_t, kHeaderBytes> header{};
    if (!input_->header(header) || !is_bink_header(header)) {
        get_log("ffmpeg: %s isn't a Bink file", name);
        return false;
    }
    width = word_at(header, kWidthAt);
    height = word_at(header, kHeightAt);
    frames = word_at(header, kFramesAt);
    frame_rate_ = word_at(header, kRateAt);
    frame_rate_div_ = word_at(header, kRateDivAt);
    frame_num_ = 1;
    last_frame_num_ = kNoFrame;

    void* buffer = api_->mem_alloc(kIoBufferBytes);
    if (!buffer) {
        return false;
    }
    io_ = api_->io_alloc_context(static_cast<unsigned char*>(buffer), kIoBufferBytes, 0, input_.get(), read_input,
                                 nullptr, seek_input);
    if (!io_) {
        api_->mem_free(buffer);
        return false;
    }
    AVFormatContext* format = api_->format_alloc_context();
    if (!format) {
        return false;
    }
    format->pb = io_;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    // (FFmpeg frees the context when the open fails)
    if (api_->format_open_input(&format, nullptr, api_->find_input_format("bink"), nullptr) < 0) {
        get_log("ffmpeg: FFmpeg's Bink demuxer can't read %s", name);
        return false;
    }
    format_ = format;
    const std::span<AVStream*> streams(format_->streams, format_->nb_streams);
    const auto video = std::ranges::find_if(
        streams, [](const AVStream* stream) { return stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO; });
    if (video == streams.end()) {
        get_log("ffmpeg: %s has no video track", name);
        return false;
    }
    video_stream_ = (*video)->index;
    if (!open_decoder()) {
        get_log("ffmpeg: no Bink video decoder for %s", name);
        return false;
    }
    packet_ = api_->packet_alloc();
    frame_ = api_->frame_alloc();
    return packet_ != nullptr && frame_ != nullptr;
}

bool Playback::open_decoder()
{
    const AVCodecParameters* parameters =
        core::at(std::span<AVStream*>(format_->streams, format_->nb_streams), static_cast<size_t>(video_stream_))
            ->codecpar;
    const AVCodec* codec = api_->find_decoder(parameters->codec_id);
    if (!codec) {
        return false;
    }
    video_ = api_->codec_alloc_context(codec);
    if (!video_ || api_->parameters_to_context(video_, parameters) < 0) {
        return false;
    }
    video_->thread_count = 1; // (a frame at a time, on the caller's thread)
    return api_->codec_open(video_, codec, nullptr) >= 0;
}

void Playback::rewind()
{
    api_->seek_frame(format_, video_stream_, 0, AVSEEK_FLAG_BACKWARD); // (the demuxer's own seek: to the first frame)
    api_->flush_buffers(video_);
    next_packet_ = 0;
    have_frame_ = false;
}

void Playback::decode(AVPacket* packet)
{
    if (api_->send_packet(video_, packet) < 0) {
        return;
    }
    if (api_->receive_frame(video_, frame_) == 0) {
        have_frame_ = true;
        decoded_ = next_packet_;
    }
}

void Playback::read_through(uint32_t index)
{
    if (next_packet_ == index + 1 && (!video_on_ || (have_frame_ && decoded_ == index))) {
        return; // (this frame's already read)
    }
    if (next_packet_ > index) {
        rewind();
    }
    while (next_packet_ <= index) {
        if (api_->read_frame(format_, packet_) < 0) {
            return; // (the end, or a damaged file: the frame decoded last stays)
        }
        if (packet_->stream_index == video_stream_) {
            if (video_on_) {
                decode(packet_);
            }
            ++next_packet_;
        }
        api_->packet_unref(packet_);
    }
}

void Playback::restart_clock(uint32_t frames_off)
{
    clock_running_ = true;
    clock_start_ = Clock::now();
    frames_since_start_ = frames_off;
}

int Playback::wait()
{
    if (paused_) {
        return 1;
    }
    if (!clock_running_ || frame_rate_ == 0) {
        restart_clock(0); // (the first frame is due at once)
        return 0;
    }
    const std::chrono::duration<double> since_start{static_cast<double>(frames_since_start_) * frame_rate_div_ /
                                                    frame_rate_};
    const Clock::time_point due = clock_start_ + std::chrono::duration_cast<Clock::duration>(since_start);
    const Clock::time_point now = Clock::now();
    if (now < due) {
        return 1;
    }
    if (now - due > std::chrono::duration<double>(kStallSeconds)) {
        restart_clock(0);
    }
    return 0;
}

void Playback::do_frame()
{
    if (frame_num_ != 0) {
        read_through(frame_num_ - 1);
    }
}

void Playback::next_frame()
{
    last_frame_num_ = frame_num_;
    frame_num_ = frame_num_ >= frames ? 1 : frame_num_ + 1;
    ++frames_since_start_;
}

void Playback::go_to(uint32_t frame, int flags)
{
    frame = std::clamp<uint32_t>(frame, 1, std::max<uint32_t>(frames, 1));
    if (frame - 1 < next_packet_) {
        rewind();
    }
    if (frame >= 2) {
        read_through(frame - 2);
    }
    // (binkw32's: a quick go-to keeps the frame it was at as the last; the other decodes the frames before it)
    last_frame_num_ = (flags & kGoToQuick) != 0 ? frame_num_ : frame - 1;
    frame_num_ = frame;
    restart_clock(1); // (as binkw32's: the frame gone to is due a frame from now)
}

void Playback::pause(bool paused)
{
    if (paused_ && !paused) {
        restart_clock(1); // (as binkw32's: the next frame is due a frame after the pause ends)
    }
    paused_ = paused;
}

void Playback::copy_to(void* bits, FrameCopy copy)
{
    if ((copy.flags & kSurfaceTypeMask) != kSurface32) {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            get_log("ffmpeg: surface type %u isn't copied (only BINKSURFACE32)", copy.flags & kSurfaceTypeMask);
        }
        return;
    }
    const auto pitch = static_cast<uint32_t>(copy.pitch);
    if (!bits || !have_frame_ || copy.pitch <= 0 || copy.x >= pitch / kPixelBytes || copy.y >= copy.height) {
        return;
    }
    // (clipped to the surface's pitch and rows)
    const int columns = std::min(frame_->width, static_cast<int>((pitch / kPixelBytes) - copy.x));
    const int rows = std::min(frame_->height, static_cast<int>(copy.height - copy.y));
    if (!converter_ || converter_columns_ != columns || converter_rows_ != rows ||
        converter_format_ != frame_->format) {
        api_->scale_free_context(converter_);
        // (no scaling: the same size, YUV 4:2:0 to blue, green, red and an unused byte - BINKSURFACE32; binkw32 writes 0
        // there, swscale 255, which the X8R8G8B8 movie texture ignores. The accurate rounding keeps the colours within a
        // few steps of binkw32's: src/tests/movie_codec_check.cpp)
        converter_ = api_->scale_context(columns, rows, static_cast<AVPixelFormat>(frame_->format), columns, rows,
                                         AV_PIX_FMT_BGR0, SWS_POINT | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
        if (!converter_) {
            return;
        }
        // BT.601 with the video range in, the full range out - Bink 1's colours
        const int* bt601 = api_->scale_coefficients(SWS_CS_ITU601);
        api_->scale_colour_space(converter_, bt601, 0, bt601, 1, 0, kUnitFixed16, kUnitFixed16);
        converter_columns_ = columns;
        converter_rows_ = rows;
        converter_format_ = frame_->format;
    }
    const std::span<uint8_t> surface(static_cast<uint8_t*>(bits), static_cast<size_t>(pitch) * copy.height);
    const std::array<uint8_t*, 4> out{
        surface.subspan((static_cast<size_t>(copy.y) * pitch) + (static_cast<size_t>(copy.x) * kPixelBytes)).data()};
    const std::array<int, 4> out_pitch{copy.pitch};
    const std::span<uint8_t* const> planes(frame_->data);
    const std::span<const int> plane_pitches(frame_->linesize);
    api_->scale(converter_, planes.data(), plane_pitches.data(), 0, rows, out.data(), out_pitch.data());
}

} // namespace media::ffmpeg
