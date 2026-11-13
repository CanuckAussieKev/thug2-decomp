#pragma once

// NOT ORIGINAL - FFmpeg's functions the movie codec calls (ffmpeg_movie.cpp, ffmpeg_playback.cpp): libavformat's
// Bink demuxer, libavcodec's Bink video (and, for the movies' sound, Bink audio) decoder, libavutil's frames and
// memory, libswscale's colour conversion. FFmpeg is the LGPL build of FFmpeg 7.1 made by tools/ffmpeg/build_ffmpeg.ts -
// avformat-61.dll, avcodec-61.dll, avutil-59.dll and swscale-8.dll next to the exe -, loaded by name at run time (no import library is linked: the
// DLLs stay replaceable, and an exe without them still starts, with no movies and no music or sound streams). Only src/media_ffmpeg
// includes FFmpeg's headers (tools/audio_headers.test.ts).

#include <cstdint> // IWYU pragma: keep - before FFmpeg's headers, which need its constant macros in C++

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
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

// NOT ORIGINAL - what writing a video and reading one back take besides (ffmpeg_video.cpp, the video check): the
// encoders, the MP4 muxer, the file protocol, the options. Looked up apart from the movie codec's: an FFmpeg build
// without them (the decode-only DLLs) still plays the movies.
struct EncodeApi {
    decltype(&::avcodec_find_encoder_by_name) find_encoder_by_name{};
    decltype(&::avcodec_send_frame) send_frame{};
    decltype(&::avcodec_receive_packet) receive_packet{};
    decltype(&::avcodec_parameters_from_context) parameters_from_context{};
    decltype(&::av_packet_rescale_ts) packet_rescale_ts{};
    decltype(&::avformat_alloc_output_context2) format_alloc_output{};
    decltype(&::avformat_free_context) format_free_context{};
    decltype(&::avformat_new_stream) format_new_stream{};
    decltype(&::avformat_write_header) format_write_header{};
    decltype(&::av_interleaved_write_frame) interleaved_write_frame{};
    decltype(&::av_write_trailer) write_trailer{};
    decltype(&::avformat_find_stream_info) find_stream_info{};
    decltype(&::avio_open) io_open{};
    decltype(&::avio_closep) io_closep{};
    decltype(&::av_frame_get_buffer) frame_get_buffer{};
    decltype(&::av_frame_make_writable) frame_make_writable{};
    decltype(&::av_opt_set) opt_set{};
    decltype(&::av_opt_set_int) opt_set_int{};
    decltype(&::av_strerror) error_text{};
};

// The encoding functions (api()'s DLLs; looked up on the first call); null if api() is, or a name isn't exported.
const EncodeApi* encode_api();

} // namespace media::ffmpeg
