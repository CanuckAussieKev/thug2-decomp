// Self-tests for the level editor's view options (src/game/front/level_editor_view.*, not original): the collision
// faces' surfaces (face flags as the skater's feeler check 0x005003A0 reads them), clipping a face to the near plane,
// the distance to a sector's box, and the editor-only hidden set / isolate.

#include <cmath>
#include <cstdint>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_view.h"
#include "hook/selftest.h"

namespace {

namespace V = Front::LevelEditor::View;
using Front::LevelEditor::Frame;
using Front::LevelEditor::FrameFromYawPitch;
using Front::LevelEditor::V3;
using S = Front::LevelEditor::View::Surface;

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void TestEditorViewSurfaces()
{
    Check(V::Classify(0x0000, true) == S::kInvisibleWall, "editor view: collision without geometry: invisible wall");
    Check(V::Classify(0x0040, true) == S::kInvisibleWall, "editor view: an invisible wall whatever its flags");
    Check(V::Classify(0x0000, false) == S::kPlain, "editor view: no flags: plain (skatable by slope)");
    Check(V::Classify(0x0001, false) == S::kSkatable, "editor view: 0x01 skatable");
    Check(V::Classify(0x0002, false) == S::kNotSkatable, "editor view: 0x02 not skatable");
    Check(V::Classify(0x0003, false) == S::kSkatable, "editor view: 0x01 wins over 0x02 (0x005003A0 tests 1 first)");
    Check(V::Classify(0x0004, false) == S::kWallride, "editor view: 0x04 wallride");
    Check(V::Classify(0x0484, false) == S::kWallride, "editor view: Foundry's wallride walls (0x484)");
    Check(V::Classify(0x0008, false) == S::kVert, "editor view: 0x08 vert");
    Check(V::Classify(0x0088, false) == S::kVert, "editor view: a quarter pipe (0x88)");
    Check(V::Classify(0x000A, false) == S::kVert, "editor view: 0x08 wins over 0x02");
    Check(V::Classify(0x0010, false) == S::kNoCollide, "editor view: 0x10 not collidable");
    Check(V::Classify(0x0110, false) == S::kNoCollide, "editor view: a railing (0x110)");
    Check(V::Classify(0x0014, false) == S::kNoCollide, "editor view: 0x10 wins over wallride");
    Check(V::Classify(0x0040, false) == S::kTrigger, "editor view: 0x40 trigger");
    Check(V::Classify(0x0050, false) == S::kTrigger, "editor view: water / kill volume (0x50) is a trigger");
    Check(V::Classify(0x00C8, false) == S::kTrigger, "editor view: a trigger quarter pipe (0xC8)");
    Check(V::Classify(0x0080, false) == S::kPlain, "editor view: 0x80 (camera) alone isn't coloured");
    Check(V::Classify(0x2000, false) == S::kPlain, "editor view: 0x2000 (not taggable) alone isn't coloured");
    // Every surface has a see-through colour and a name; no two colours the same.
    std::vector<uint32_t> colours;
    for (int i = 0; i < static_cast<int>(S::kCount); ++i) {
        uint32_t c = V::SurfaceColour(static_cast<S>(i));
        Check((c >> 24) > 0 && (c >> 24) < 0xFF, "editor view: a colour is see-through");
        Check(V::SurfaceName(static_cast<S>(i))[0] != 0, "editor view: a surface has a name");
        for (uint32_t other : colours)
            Check(other != c, "editor view: the surfaces' colours differ");
        colours.push_back(c);
    }
    Check((V::SurfaceColour(S::kInvisibleWall) >> 24) > (V::SurfaceColour(S::kPlain) >> 24),
          "editor view: invisible walls stronger than plain faces");
}
SELFTEST(Early, TestEditorViewSurfaces);

void TestEditorViewClip()
{
    // The camera at the origin looking along +z (yaw 0, pitch 0): depth = z.
    Frame frame = FrameFromYawPitch(0.0f, 0.0f);
    V3 camera = {0.0f, 0.0f, 0.0f};
    V3 out[4];
    V3 front[3] = {{0, 0, 10}, {1, 0, 10}, {0, 1, 12}};
    Check(V::ClipTriangleToNear(camera, frame, 2.0f, front, out) == 3, "editor view: a face in front: kept whole");
    Check(Near(out[0].z, 10) && Near(out[1].x, 1) && Near(out[2].z, 12), "editor view: ... its points in order");
    V3 behind[3] = {{0, 0, -10}, {1, 0, -5}, {0, 1, 1.9f}};
    Check(V::ClipTriangleToNear(camera, frame, 2.0f, behind, out) == 0, "editor view: a face behind: none");
    V3 oneBehind[3] = {{0, 0, 10}, {4, 0, 10}, {0, 0, -6}};
    int n = V::ClipTriangleToNear(camera, frame, 2.0f, oneBehind, out);
    Check(n == 4, "editor view: one point behind: a quad");
    bool allFront = true;
    for (int i = 0; i < n; ++i)
        allFront = allFront && out[i].z >= 2.0f - 1e-4f;
    Check(allFront, "editor view: ... every point at or past the near plane");
    // (the cut on the edge (4,0,10)-(0,0,-6) at z = 2: half way: (2, 0, 2))
    Check(Near(out[1].x, 4) && Near(out[2].x, 2) && Near(out[2].z, 2) && Near(out[3].x, 0) && Near(out[3].z, 2),
          "editor view: ... cut where the edges cross it");
    V3 twoBehind[3] = {{0, 0, 10}, {4, 0, -6}, {0, 0, -6}};
    n = V::ClipTriangleToNear(camera, frame, 2.0f, twoBehind, out);
    Check(n == 3 && Near(out[1].z, 2) && Near(out[1].x, 2) && Near(out[2].z, 2) && Near(out[2].x, 0),
          "editor view: two points behind: a smaller face");
    // A turned camera: looking along +x (yaw 90 degrees).
    Frame side = FrameFromYawPitch(1.5707963f, 0.0f);
    V3 alongX[3] = {{5, 0, 0}, {5, 1, 0}, {-5, 0, 0}};
    n = V::ClipTriangleToNear({0, 0, 0}, side, 1.0f, alongX, out);
    Check(n == 4 && Near(out[2].x, 1) && Near(out[3].x, 1), "editor view: the near plane follows the camera's turn");
}
SELFTEST(Early, TestEditorViewClip);

void TestEditorViewBoxAndHidden()
{
    V3 min = {0, 0, 0}, max = {10, 20, 30};
    Check(Near(V::DistanceToBox({5, 5, 5}, min, max), 0), "editor view: inside a box: 0");
    Check(Near(V::DistanceToBox({13, 5, 5}, min, max), 3), "editor view: beside a box");
    Check(Near(V::DistanceToBox({13, 24, 5}, min, max), 5), "editor view: off its edge (3, 4: 5)");
    Check(Near(V::DistanceToBox({-3, -4, 42}, min, max), 13), "editor view: off its corner (3, 4, 12: 13)");

    V::HiddenSet set;
    Check(set.Add(7) && set.Add(9) && !set.Add(7), "editor view: hidden once each");
    Check(set.Has(7) && set.Has(9) && !set.Has(8) && set.ids.size() == 2, "editor view: the hidden set");
    std::vector<uint32_t> others = V::IsolateOthers({1, 2, 3, 4, 3}, {2, 4});
    Check(others.size() == 2 && others[0] == 1 && others[1] == 3, "editor view: isolate hides all but the selection");
    Check(V::IsolateOthers({1, 2}, {}).size() == 2, "editor view: isolate with nothing kept hides everything given");
    Check(V::IsolateOthers({1, 2}, {1, 2, 3}).empty(), "editor view: isolate everything: nothing hidden");
}
SELFTEST(Early, TestEditorViewBoxAndHidden);

} // namespace
