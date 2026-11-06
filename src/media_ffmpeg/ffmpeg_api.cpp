// NOT ORIGINAL - FFmpeg's DLLs loaded and their functions looked up by name (ffmpeg_api.h).

#include "media_ffmpeg/ffmpeg_api.h"

#include <initializer_list>
#include <utility>

#include "platform/crt.h"
#include "platform/run_log.h"

namespace media::ffmpeg {

namespace {

constexpr crt::Dll kAvutil{.file = "avutil-59.dll"};
constexpr crt::Dll kAvcodec{.file = "avcodec-61.dll"};
constexpr crt::Dll kAvformat{.file = "avformat-61.dll"};
constexpr crt::Dll kSwscale{.file = "swscale-8.dll"};

// FFmpeg's version numbers: major << 16 | minor << 8 | micro.
constexpr unsigned kMajorShift = 16;
constexpr unsigned kMinorShift = 8;
constexpr unsigned kPartMask = 0xFFU;

// `function` looked up in `dll` (false, logged, if it isn't there).
template <typename F>
bool look_up(F& function, crt::Dll dll, const char* name)
{
    function = crt::library_function<F>(dll, name);
    if (!function) {
        get_log("ffmpeg: %s has no %s (or isn't there)", dll.file, name);
        return false;
    }
    return true;
}

bool look_up_all(Api& api)
{
    bool found = look_up(api.avutil_version, kAvutil, "avutil_version");
    found = look_up(api.avcodec_version, kAvcodec, "avcodec_version") && found;
    found = look_up(api.avformat_version, kAvformat, "avformat_version") && found;
    found = look_up(api.swscale_version, kSwscale, "swscale_version") && found;
    if (!found) {
        return false;
    }
    found = look_up(api.log_set_level, kAvutil, "av_log_set_level");
    found = look_up(api.mem_alloc, kAvutil, "av_malloc") && found;
    found = look_up(api.mem_free, kAvutil, "av_free") && found;
    found = look_up(api.frame_alloc, kAvutil, "av_frame_alloc") && found;
    found = look_up(api.frame_free, kAvutil, "av_frame_free") && found;
    found = look_up(api.find_decoder, kAvcodec, "avcodec_find_decoder") && found;
    found = look_up(api.codec_alloc_context, kAvcodec, "avcodec_alloc_context3") && found;
    found = look_up(api.parameters_to_context, kAvcodec, "avcodec_parameters_to_context") && found;
    found = look_up(api.codec_open, kAvcodec, "avcodec_open2") && found;
    found = look_up(api.codec_free_context, kAvcodec, "avcodec_free_context") && found;
    found = look_up(api.send_packet, kAvcodec, "avcodec_send_packet") && found;
    found = look_up(api.receive_frame, kAvcodec, "avcodec_receive_frame") && found;
    found = look_up(api.flush_buffers, kAvcodec, "avcodec_flush_buffers") && found;
    found = look_up(api.packet_alloc, kAvcodec, "av_packet_alloc") && found;
    found = look_up(api.packet_free, kAvcodec, "av_packet_free") && found;
    found = look_up(api.packet_unref, kAvcodec, "av_packet_unref") && found;
    found = look_up(api.io_alloc_context, kAvformat, "avio_alloc_context") && found;
    found = look_up(api.io_context_free, kAvformat, "avio_context_free") && found;
    found = look_up(api.find_input_format, kAvformat, "av_find_input_format") && found;
    found = look_up(api.format_alloc_context, kAvformat, "avformat_alloc_context") && found;
    found = look_up(api.format_open_input, kAvformat, "avformat_open_input") && found;
    found = look_up(api.format_close_input, kAvformat, "avformat_close_input") && found;
    found = look_up(api.read_frame, kAvformat, "av_read_frame") && found;
    found = look_up(api.seek_frame, kAvformat, "av_seek_frame") && found;
    found = look_up(api.scale_context, kSwscale, "sws_getContext") && found;
    found = look_up(api.scale_free_context, kSwscale, "sws_freeContext") && found;
    found = look_up(api.scale_colour_space, kSwscale, "sws_setColorspaceDetails") && found;
    found = look_up(api.scale_coefficients, kSwscale, "sws_getCoefficients") && found;
    found = look_up(api.scale, kSwscale, "sws_scale") && found;
    return found;
}

// Whether the DLLs are the major versions these headers describe (the structures' layouts are fixed within one).
bool versions_match(const Api& api)
{
    const unsigned avutil = api.avutil_version();
    const unsigned avcodec = api.avcodec_version();
    const unsigned avformat = api.avformat_version();
    const unsigned swscale = api.swscale_version();
    for (const auto& [name, version] :
         {std::pair{"avutil", avutil}, {"avcodec", avcodec}, {"avformat", avformat}, {"swscale", swscale}}) {
        get_log("ffmpeg: %s %u.%u.%u", name, version >> kMajorShift, (version >> kMinorShift) & kPartMask,
                version & kPartMask);
    }
    if (avutil >> kMajorShift != LIBAVUTIL_VERSION_MAJOR || avcodec >> kMajorShift != LIBAVCODEC_VERSION_MAJOR ||
        avformat >> kMajorShift != LIBAVFORMAT_VERSION_MAJOR || swscale >> kMajorShift != LIBSWSCALE_VERSION_MAJOR) {
        get_log("ffmpeg: the DLLs aren't FFmpeg %d / %d / %d / %d (avutil / avcodec / avformat / swscale)",
                LIBAVUTIL_VERSION_MAJOR, LIBAVCODEC_VERSION_MAJOR, LIBAVFORMAT_VERSION_MAJOR, LIBSWSCALE_VERSION_MAJOR);
        return false;
    }
    return true;
}

struct Loaded {
    Api api;
    bool usable = false;
};

Loaded load()
{
    Loaded loaded;
    loaded.usable = look_up_all(loaded.api) && versions_match(loaded.api);
    if (loaded.usable) {
        loaded.api.log_set_level(AV_LOG_QUIET); // (our own lines say what failed; FFmpeg's would go to stderr)
    }
    return loaded;
}

} // namespace

const Api* api()
{
    static const Loaded kLoaded = load();
    return kLoaded.usable ? &kLoaded.api : nullptr;
}

} // namespace media::ffmpeg
