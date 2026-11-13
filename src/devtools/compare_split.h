#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ext {
class Extension;
}

// NOT ORIGINAL - a split comparison (ours, added with the demo viewer): the screen-space effects drawn with two sets of
// settings, A on the left of a divider and B on the right, in normal play and in the demo viewer (devtools/demo.h).
//
//   thug2-decomp.ini [Graphics] compare_split=1      on from the start (0, the default: off; the console's `compare on`)
//                    [Graphics] compare_divider=0.5  where the divider is (0.05 - 0.95 of the width)
//                    [Graphics] compare_labels=1     the sides' names at the divider's top (0: none - for pictures)
//                    [Compare] <key>=<value>          side B: [Graphics] keys whose values differ on the right
//                                                     (e.g. rt_ao=1, rt_shadow_softness=2); A is the ini's [Graphics]
//
// How: the first viewport is drawn twice (ext::view_count - the level editor's split views' mechanism), the left part
// with side A's settings, the right with B's, each view's camera the whole viewport's projection cropped to its part
// (ext::ViewPass::crop: the pixels where an unsplit frame has them). Each pass sees its side's rectangle and, the
// effects keeping their histories per rectangle (AO, GI, the fog, the clouds, the sun's rays), its own history; the
// settings are swapped between the two views without starting a history again (rt::settings_swap). What can't differ
// per side - the renderer, ray_tracing, rt_hdr, the upscaler (off while split: both sides at the native resolution),
// rt_tessellation, the relit mode, msaa, the texture sets - is drawn the same on both and named in the viewer's panel
// as "both sides". Drawing the world twice costs about twice the frame. Direct3D 9 has no effects to split: both sides
// are the same there.
//
// The divider: dragged with the mouse (the left button within a few pixels of it), Alt+Left / Alt+Right move it by 1%.
// The console: compare on | off | status | divider <0..1> | set <key> <value> | unset <key> | clear | labels <0|1>.
namespace devtools::compare_split {

ext::Extension& extension();

bool is_on();
// On (side B's settings made from the overrides) or off (A's put back).
void set_on(bool on);
float divider();
void set_divider(float x);
bool labels();
void set_labels(bool on);

// Side B's overrides ([Graphics] key, value) and setting one ("" removes it); the comparison's sides made again.
const std::vector<std::pair<std::string, std::string>>& overrides();
void set_override(const std::string& key, const std::string& value);
// The overrides that can't differ per side (left out of B), as the backend named them; and whether the renderer splits
// at all (false: Direct3D 9).
const std::vector<std::string>& both_sides();
bool renderer_splits();

// The console's `compare ...` (devtools/console.h): what happened.
std::string console_command(const std::string& args);

} // namespace devtools::compare_split
