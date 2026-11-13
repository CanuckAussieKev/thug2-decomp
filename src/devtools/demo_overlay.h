#pragma once

#include <string>

// NOT ORIGINAL - the demos' tag on screen (devtools/demo.h): "REC" while one records, "REPLAY" while one plays,
// "SAVING VIDEO 45%" while one is saved as a video - a small box in the top-right corner, drawn over the frame with
// Dear ImGui (its own context, as the console's). A demo saved as a video takes its picture before the tag is drawn.
namespace devtools::demo_overlay {

// The tag ("" draws nothing), a second, smaller line under it ("" none), and the recording dot before the tag.
void draw(const std::string& tag, const std::string& line, bool dot);
// A split comparison's divider (devtools/compare_split.h): a line down the frame at `x` (0..1 of its width) and, with
// `labels`, each side's name in a small box at its top (`left` against the line's left, `right` against its right).
// Drawn before a demo's video picture is taken: a video saved with the split shows them.
void draw_split(float x, const std::string& left, const std::string& right, bool labels);
// The device is about to be reset (its objects made again on the next draw).
void device_lost();

} // namespace devtools::demo_overlay
