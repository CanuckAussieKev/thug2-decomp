#pragma once

// NOT ORIGINAL - the exe as a video encoder (ours: tools/game/demo_compare.ts makes its side-by-side video with the
// game's own encoder, no ffmpeg.exe needed):
//
//   thug2-decomp.exe -encode-video <file.mp4> -encode-size <width>x<height> [-encode-fps N] [-encode-quality Q]
//                    [-encode-codec auto|h264|mpeg4|<encoder>]
//
// reads raw RGB frames (width * height * 3 bytes each, top row first) from its standard input until it ends, writes
// them as <file.mp4> through media::VideoWriter (FFmpeg's encoders: media_ffmpeg/ffmpeg_video.h; defaults: the
// [demo] video_fps / video_quality / video_codec settings) and exits - no window, no game, no log of its own: one line
// on its standard error, "video: <file> <codec> <frames> frames" or "video: error ...". Exit code 0 when written.
namespace app {

// With -encode-video on the command line: the frames encoded and the exit code (true, `exit_code` set); else false.
bool run_encode_video(int* exit_code);

} // namespace app
