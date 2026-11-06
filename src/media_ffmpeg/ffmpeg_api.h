#pragma once

// NOT ORIGINAL - FFmpeg's functions the movie codec calls (ffmpeg_movie.cpp, ffmpeg_playback.cpp): libavformat's
// Bink demuxer, libavcodec's Bink video (and, for the movies' sound, Bink audio) decoder, libavutil's frames and
// memory, libswscale's colour conversion. FFmpeg is the LGPL build of FFmpeg 7.1 made by tools/ffmpeg/build_ffmpeg.ts -
// avformat-61.dll, avcodec-61.dll, avutil-59.dll and swscale-8.dll next to the exe -, loaded by name at run time (no import library is linked: the
// DLLs stay replaceable, and an exe without them still starts and plays its movies with Bink). Only src/media_ffmpeg
// includes FFmpeg's headers (tools/audio_headers.test.ts).

#include <cstdint> // IWYU pragma: keep - before FFmpeg's headers, which need its constant macros in C++

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

namespace media::ffmpeg {

// The functions, each looked up by its exported name.
struct Api {
    decltype(&::avformat_version) avformat_version{};
    decltype(&::avcodec_version) avcodec_version{};
    decltype(&::avutil_version) avutil_version{};
    decltype(&::av_log_set_level) log_set_level{};
    decltype(&::av_malloc) mem_alloc{};
    decltype(&::av_free) mem_free{};
    decltype(&::avio_alloc_context) io_alloc_context{};
    decltype(&::avio_context_free) io_context_free{};
    decltype(&::av_find_input_format) find_input_format{};
    decltype(&::avformat_alloc_context) format_alloc_context{};
    decltype(&::avformat_open_input) format_open_input{};
    decltype(&::avformat_close_input) format_close_input{};
    decltype(&::av_read_frame) read_frame{};
    decltype(&::av_seek_frame) seek_frame{};
    decltype(&::avcodec_find_decoder) find_decoder{};
    decltype(&::avcodec_alloc_context3) codec_alloc_context{};
    decltype(&::avcodec_parameters_to_context) parameters_to_context{};
    decltype(&::avcodec_open2) codec_open{};
    decltype(&::avcodec_free_context) codec_free_context{};
    decltype(&::avcodec_send_packet) send_packet{};
    decltype(&::avcodec_receive_frame) receive_frame{};
    decltype(&::avcodec_flush_buffers) flush_buffers{};
    decltype(&::av_packet_alloc) packet_alloc{};
    decltype(&::av_packet_free) packet_free{};
    decltype(&::av_packet_unref) packet_unref{};
    decltype(&::av_frame_alloc) frame_alloc{};
    decltype(&::av_frame_free) frame_free{};
    decltype(&::swscale_version) swscale_version{};
    decltype(&::sws_getContext) scale_context{};
    decltype(&::sws_freeContext) scale_free_context{};
    decltype(&::sws_setColorspaceDetails) scale_colour_space{};
    decltype(&::sws_getCoefficients) scale_coefficients{};
    decltype(&::sws_scale) scale{};
};

// The functions, the DLLs loaded and every name looked up on the first call; null if a DLL is missing, a name isn't
// exported or a library's major version isn't the one these headers are for (each missing piece logged once).
const Api* api();

} // namespace media::ffmpeg
