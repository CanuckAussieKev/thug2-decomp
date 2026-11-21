#pragma once

// NOT ORIGINAL - the exe as a clip reader (ours: tools/game/clip_frames.ts takes frames out of a clip the clip key
// recorded - devtools/clip.h - with the FFmpeg decoders the exe already carries, no ffmpeg.exe needed):
//
//   thug2-decomp.exe -clip-frames <clip.mp4> -frames-out <folder> [-frames a,b,c] [-frames-crop x,y,w,h]
//                    [-frames-scale N]
//
// decodes the clip whole and writes the video frames asked for (numbers from 0, the sidecar's `vf` column; none: only the
// clip's facts) as <folder>\vf_000123.png, each cut to the crop rectangle and scaled to N percent (default 100) when
// given - no window, no game, no log of its own. On its standard error: one line "clip: <codec> <width>x<height>
// <fps> <frames>" (frames decoded) then a "frame <n> <file>" line each, or "clip: error - <why>". Exit code 0 when read.
namespace app {

// With -clip-frames on the command line: the clip read and the exit code (true, `exit_code` set); else false.
bool run_clip_frames(int* exit_code);

} // namespace app
