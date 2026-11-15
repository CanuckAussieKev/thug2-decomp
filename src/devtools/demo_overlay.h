#pragma once

#include <string>
#include <vector>

// NOT ORIGINAL - the demos' tags on screen (devtools/demo.h): "REC" while one records, the REPLAY badge while one plays,
// "SAVING VIDEO 45%" while one is saved as a video - a small box in the top-right corner, drawn over the frame with
// Dear ImGui (its own context, as the console's). A demo saved as a video takes its picture after the REPLAY badge and
// before the other tags.
namespace devtools::demo_overlay {

// The tag ("" draws nothing), a second, smaller line under it ("" none), and the recording dot before the tag;
// `top`: the box's top in pixels from the frame's (0: the corner's margin).
void draw(const std::string& tag, const std::string& line, bool dot, float top = 0.0F);

// The REPLAY badge (a playback's: devtools/demo.h): REPLAY with a play mark, then its lines (the skater, the date, the
// time), then a warning in red ("": none). Top-right, larger than the tags. Returns the box's bottom in pixels (the
// next tag goes below it).
struct Badge {
    std::vector<std::string> lines;
    std::string warning;
    float right_inset = 0.0F; // (pixels kept clear at the right: the demo viewer's panel)
};
float draw_badge(const Badge& badge);

// A split comparison's divider (devtools/compare_split.h): a line down the frame at `x` (0..1 of its width) and, with
// `labels`, each side's name in a small box at its top (`left` against the line's left, `right` against its right).
// Drawn before a demo's video picture is taken: a video saved with the split shows them.
void draw_split(float x, const std::string& left, const std::string& right, bool labels);
// The device is about to be reset (its objects made again on the next draw).
void device_lost();

} // namespace devtools::demo_overlay
