// Self-tests for the level editor's feature map rows (src/game/front/level_editor_faces.*, _clusters.*, _cameras.*,
// _level_props.* - not original): collision face flags (the flags offered, set / clear, the combined checkbox, the
// selection's clicks, `faces` records), scenery trick-object clusters (as the load registers a node, `cluster`
// records), camera nodes (their angles against the game's own angles-to-matrix 0x0041A730, the virtual_cam offsets,
// the frustum against the projection, `camera` records), the level properties' sky (sky= in the `level` record and the
// level structure's values), and a layout carrying all of them read back.

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_cameras.h"
#include "game/front/level_editor_clusters.h"
#include "game/front/level_editor_faces.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_level_props.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Faces = Front::LevelEditor::Faces;
namespace Clusters = Front::LevelEditor::Clusters;
namespace Cameras = Front::LevelEditor::Cameras;
namespace LevelProps = Front::LevelEditor::LevelProps;
namespace Layout = Front::LevelEditor::Layout;
using LE::V3;

constexpr float kPi = 3.14159265f;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

bool ReadFaces(const std::string& text, Faces::SectorFaces* out)
{
    std::istringstream in(text);
    std::string kind;
    in >> kind;
    return kind == "faces" && Faces::ReadRecord(in, out);
}

void TestEditorFaceFlags()
{
    // The flags offered: exactly the ones the feature map names, each once.
    const uint16_t wanted[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x40, 0x80, 0x2000};
    bool all = Faces::kFlagCount == 8;
    for (int i = 0; i < Faces::kFlagCount; ++i)
        all = all && Faces::Flag(i).bit == wanted[i] && Faces::Flag(i).name && Faces::Flag(i).what;
    Check(all, "editor faces: the flags offered (skatable .. not taggable 0x2000), in order");
    Check(Faces::Applied(0x1001, 0x0040, 0) == 0x1041 && Faces::Applied(0x1041, 0, 0x0001) == 0x1040 &&
              Faces::Applied(0x0010, 0x0010, 0) == 0x0010 && Faces::Applied(0x0080, 0x0004, 0x0080) == 0x0004,
          "editor faces: a flag set / cleared, the others kept");
    Check(Faces::WhileTaken(0x0001) == 0x0011 && Faces::WhileTaken(0x0010) == 0x0010,
          "editor faces: a taken face's original not collidable");
    Check(Faces::Combined({}, 1) == -1 && Faces::Combined({0, 2}, 1) == 0 && Faces::Combined({1, 3}, 1) == 1 &&
              Faces::Combined({1, 2, 3}, 1) == 2 && Faces::Combined({0x2000}, 0x2000) == 1,
          "editor faces: the combined checkbox - none, clear, set, mixed");

    // Clicks: a plain click is the selection; Shift toggles.
    std::vector<Faces::FaceKey> sel;
    Faces::FaceKey a = {0x11, 3}, b = {0x11, 4}, c = {0x22, 3};
    bool ok = Faces::Click(&sel, a, false) && sel.size() == 1;
    ok = ok && Faces::Click(&sel, b, true) && Faces::Click(&sel, c, true) && sel.size() == 3;
    ok = ok && !Faces::Click(&sel, b, true) && sel.size() == 2 && sel[0] == a && sel[1] == c;
    ok = ok && Faces::Click(&sel, c, false) && sel.size() == 1 && sel[0] == c;
    Check(ok, "editor faces: click selects, Shift+click adds / drops");

    // Records.
    std::vector<Faces::SectorFaces> sectors = {{0x0A0B0C0D, {{0, 0x0011}, {7, 0x2001}, {65535, 0xFFFF}}},
                                               {0x12345678, {{42, 0x0080}}}};
    std::string text = Faces::WriteRecords(sectors);
    Check(text == "faces 0A0B0C0D 0:0011,7:2001,65535:FFFF\nfaces 12345678 42:0080\n", "editor faces: records written");
    std::istringstream lines(text);
    std::string line;
    bool round = true;
    for (size_t i = 0; std::getline(lines, line); ++i) {
        Faces::SectorFaces read;
        round = round && i < sectors.size() && ReadFaces(line, &read) && read.checksum == sectors[i].checksum &&
                read.faces == sectors[i].faces;
    }
    Check(round, "editor faces: records read back");
    Faces::SectorFaces read;
    Check(ReadFaces("faces 0A0B0C0D 9:1,2:80", &read) && read.faces.size() == 2 && read.faces[0].first == 2 &&
              read.faces[0].second == 0x80 && read.faces[1].second == 1,
          "editor faces: a record's faces sorted, short hex read");
    const char* bad[] = {"faces 0A0B0C0D 1:0011,1:0012", "faces 0A0B0C0D 1:00110", "faces 0A0B0C0D 1:00G1",
                         "faces 0A0B0C0D", "faces 0A0B0C0 1:0001", "faces 0A0B0C0D 1:0001 junk", "faces 0A0B0C0D :0001",
                         "faces 0A0B0C0D 65536:0001", "faces 0A0B0C0D 1:", "faces 0A0B0C0D 1:0001,"};
    bool refused = true;
    for (const char* t : bad)
        refused = refused && !ReadFaces(t, &read);
    Check(refused, "editor faces: malformed records refused");
    Check(Faces::WriteRecords({{0x1, {}}}).empty(), "editor faces: a sector without faces writes nothing");

    // Grouping the changes: by sector, faces ascending, faces back as the level has them left out.
    std::vector<Faces::Change> changes = {{{0x20, 5}, 0x1, 0x3}, {{0x10, 9}, 0x1, 0x1}, {{0x10, 2}, 0x0, 0x10},
                                          {{0x20, 1}, 0x4, 0x0}, {{0x10, 1}, 0x1, 0x81}};
    std::vector<Faces::SectorFaces> grouped = Faces::Group(changes);
    Check(grouped.size() == 2 && grouped[0].checksum == 0x10 && grouped[0].faces.size() == 2 &&
              grouped[0].faces[0] == std::make_pair<uint16_t, uint16_t>(1, 0x81) &&
              grouped[0].faces[1] == std::make_pair<uint16_t, uint16_t>(2, 0x10) && grouped[1].checksum == 0x20 &&
              grouped[1].faces[0].first == 1 && grouped[1].faces[1].first == 5,
          "editor faces: changes grouped into records, unchanged left out");
}
SELFTEST(Early, TestEditorFaceFlags);

bool ReadCluster(const std::string& text, Clusters::Membership* out)
{
    std::istringstream in(text);
    std::string kind;
    in >> kind;
    return kind == "cluster" && Clusters::ReadRecord(in, out);
}

void TestEditorClusters()
{
    // As CFunc_ParseNodeArray registers a TrickObject node: its Cluster=, else its own name; no TrickObject: none.
    Clusters::Membership m = Clusters::FromNode(0x100, true, false, 0);
    bool ok = m.trick && m.cluster == 0x100;
    m = Clusters::FromNode(0x100, true, true, 0x200);
    ok = ok && m.trick && m.cluster == 0x200;
    m = Clusters::FromNode(0x100, false, true, 0x200);
    ok = ok && !m.trick && m.cluster == 0;
    Check(ok, "editor clusters: a node's membership as the load registers it");
    Check(Clusters::Same({1, false, 5}, {1, false, 7}) && !Clusters::Same({1, true, 5}, {1, true, 7}) &&
              !Clusters::Same({1, true, 5}, {2, true, 5}) && !Clusters::Same({1, true, 5}, {1, false, 0}),
          "editor clusters: memberships compared (a non-trick object's cluster doesn't count)");
    Check(Clusters::WriteRecord({0x0A0B0C0D, true, 0x11223344}) == "cluster 0A0B0C0D 11223344" &&
              Clusters::WriteRecord({0x0A0B0C0D, false, 0x5}) == "cluster 0A0B0C0D -",
          "editor clusters: records written");
    Clusters::Membership read;
    Check(ReadCluster("cluster 0A0B0C0D 11223344", &read) && read.sector == 0x0A0B0C0D && read.trick &&
              read.cluster == 0x11223344 && ReadCluster("cluster 0a0b0c0d -", &read) && !read.trick &&
              read.sector == 0x0A0B0C0D,
          "editor clusters: records read back");
    const char* bad[] = {"cluster 0A0B0C0D", "cluster 0A0B0C0D 00000000", "cluster 00000000 11223344",
                         "cluster 0A0B0C0D 1122334", "cluster 0A0B0C0D 11223344 x", "cluster 0A0B0C0D --"};
    bool refused = true;
    for (const char* t : bad)
        refused = refused && !ReadCluster(t, &read);
    Check(refused, "editor clusters: malformed records refused");
}
SELFTEST(Early, TestEditorClusters);

bool ReadCamera(const std::string& text, Cameras::Camera* out)
{
    std::istringstream in(text);
    std::string kind;
    in >> kind;
    return kind == "camera" && Cameras::ReadRecord(in, out);
}

// The game's angles-to-matrix (0x0041A730, thiscall: the matrix, then the angles) - its at row (row 2).
V3 GameAt(V3 angles)
{
    Mth::Matrix matrix;
    Mth::Vector vector = {angles.x, angles.y, angles.z, 1.0f};
    GameFunction<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0041A730)(&matrix, &vector);
    return {matrix.row[2].x, matrix.row[2].y, matrix.row[2].z};
}

void TestEditorCameras()
{
    // A camera node's angles look where the editor's view looked - as the game turns a node of those angles.
    bool same = true, back = true, game = true;
    for (float yaw : {0.0f, 0.6f, -2.2f, 3.0f}) {
        for (float pitch : {0.0f, 0.4f, -0.9f, 1.3f}) {
            V3 angles = Cameras::AnglesFor(yaw, pitch);
            V3 view = LE::FrameFromYawPitch(yaw, pitch).back * -1.0f;
            same = same && Near(Cameras::Forward(angles), view, 1e-5f);
            game = game && Near(GameAt(angles), view, 1e-4f);
            float y, p;
            Cameras::YawPitchOf(angles, &y, &p);
            back = back && Near(y, yaw, 1e-6f) && Near(p, pitch, 1e-6f);
        }
    }
    Check(same, "editor cameras: a camera looks where the view looked");
    Check(game, "editor cameras: the game's angles-to-matrix (0x0041A730) turns its at row the same way");
    Check(back, "editor cameras: angles back to yaw and pitch");
    // (a node with a z angle too - a level's, typed in the Inspector: the game's RotationZ, last)
    Check(Near(GameAt({-0.4f, 1.1f, 0.8f}), Cameras::Forward({-0.4f, 1.1f, 0.8f}), 1e-4f) &&
              Near(GameAt({0.3f, -2.0f, -1.7f}), Cameras::Forward({0.3f, -2.0f, -1.7f}), 1e-4f),
          "editor cameras: where any node's angles look, as the game turns them");

    // The virtual_cam offsets: the target `distance` ahead, the camera's place relative to it.
    V3 pos = {100, 50, -20}, angles = Cameras::AnglesFor(0.5f, -0.25f);
    V3 target = Cameras::TargetOf(pos, angles, 800.0f), offset = Cameras::PositionOffset(pos, angles, 800.0f);
    V3 d = target - pos;
    Check(Near(target + offset, pos, 1e-3f) && Near(std::sqrt(LE::Dot(d, d)), 800.0f, 1e-2f) &&
              Near(LE::Normalize(d), Cameras::Forward(angles), 1e-5f),
          "editor cameras: targetOffset / positionOffset as a virtual_cam takes them");
    Check(Cameras::ClampDistance(1.0f) == Cameras::kMinDistance && Cameras::ClampDistance(1e9f) == Cameras::kMaxDistance &&
              Cameras::ClampDistance(NAN) == Cameras::kDefaultDistance && Cameras::ClampDistance(700.0f) == 700.0f,
          "editor cameras: the target distance kept in range");
    V3 near = Cameras::TargetOf(pos, angles, 0.0f) - pos;
    Check(Near(std::sqrt(LE::Dot(near, near)), Cameras::kMinDistance, 1e-2f), "editor cameras: a target at least 50 ahead");

    // The frustum's corners are where the view's corners are.
    LE::Lens lens = LE::LensFor(72.0f, 16.0f / 9.0f);
    V3 corners[4];
    Cameras::FrustumCorners(pos, angles, lens.tanX, lens.tanY, 120.0f, corners);
    LE::Frame frame = LE::FrameFromYawPitch(0.5f, -0.25f);
    const float expect[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    bool frustum = true;
    for (int i = 0; i < 4; ++i) {
        float nx = 0, ny = 0;
        frustum = frustum && LE::Project(pos, frame, lens, corners[i], 2.0f, &nx, &ny) && Near(nx, expect[i][0], 1e-3f) &&
                  Near(ny, expect[i][1], 1e-3f);
    }
    Check(frustum, "editor cameras: the frustum's corners are the view's");

    // Records.
    Cameras::Camera c;
    c.name = "ed_camera01";
    c.pos = {1.5f, -2.25f, 1000.125f};
    c.angles = Cameras::AnglesFor(1.2345678f, -0.3f);
    c.distance = 1234.5f;
    std::string text = Cameras::WriteRecord(c);
    Cameras::Camera read;
    Check(ReadCamera(text, &read) && Cameras::Same(read, c), "editor cameras: a record read back exactly");
    Check(ReadCamera("camera ed_camera02 pos=1,2,3", &read) && read.distance == Cameras::kDefaultDistance &&
              read.angles.x == 0.0f && ReadCamera("camera ed_camera02 pos=1,2,3 dist=5 later=1", &read) &&
              read.distance == Cameras::kMinDistance,
          "editor cameras: defaults, a distance clamped, a later key skipped");
    const char* bad[] = {"camera ed_camera02", "camera ed_camera02 angles=0,0,0", "camera ed_camera02 pos=1,2",
                         "camera ed_camera02 pos=1,2,3 pos=1,2,3", "camera bad-name pos=1,2,3",
                         "camera ed_camera02 pos=1,2,3 dist=x", "camera ed_camera02 pos=1,2,3 nokey"};
    bool refused = true;
    for (const char* t : bad)
        refused = refused && !ReadCamera(t, &read);
    Check(refused, "editor cameras: malformed records refused");
}
SELFTEST(Early, TestEditorCameras);

bool ReadLevel(const std::string& text, LevelProps::Props* out)
{
    std::istringstream in(text);
    std::string kind;
    in >> kind;
    return kind == "level" && LevelProps::ReadRecord(in, out);
}

void TestEditorSky()
{
    Check(LevelProps::ValidSky("TR_Sky") && LevelProps::ValidSky("thps3_ap_sky") && LevelProps::ValidSky("sk5ed2_Sky") &&
              !LevelProps::ValidSky("") && !LevelProps::ValidSky("TR") && !LevelProps::ValidSky("TR Sky") &&
              !LevelProps::ValidSky("TR_Sky\\x") && !LevelProps::ValidSky(std::string(70, 'a') + "_sky"),
          "editor sky: a sky scene's name (letters, digits, _, holding _sky)");
    LevelProps::Props p;
    p.sky = "BO_Sky";
    p.tod = 3;
    std::string text = LevelProps::WriteRecord(p);
    LevelProps::Props read;
    Check(text == "level tod=night sky=BO_Sky\n", "editor sky: sky= written in the level record");
    Check(ReadLevel(text, &read) && read.sky == "BO_Sky" && read.tod == 3 && LevelProps::Same(read, p),
          "editor sky: sky= read back from the level record");
    LevelProps::Props upper = p;
    upper.sky = "bo_sky";
    Check(LevelProps::Same(upper, p) && !LevelProps::Empty(p) && LevelProps::Empty(LevelProps::Props{}),
          "editor sky: skies compared without case");
    Check(!ReadLevel("level sky=TR", &read) && !ReadLevel("level sky=", &read) && !ReadLevel("level sky=TR%20Sky", &read),
          "editor sky: a sky= that isn't a sky refused");
    auto values = LevelProps::StructureValues(p);
    bool sky = false;
    for (const auto& v : values)
        sky = sky || (std::string(v.first) == "sky" && v.second.kind == LE::Fields::Kind::String && v.second.text == "BO_Sky");
    Check(sky && LevelProps::StructureValues(LevelProps::Props{}).empty(),
          "editor sky: the level structure's sky= (a string) set for the load");
}
SELFTEST(Early, TestEditorSky);

void TestEditorMap3Layout()
{
    Layout::File file;
    file.scene = 0xCAFEF00D;
    file.faces = {{0x0A0B0C0D, {{3, 0x0015}, {4, 0x0001}}}};
    file.clusters = {{0x11111111, true, 0x22222222}, {0x33333333, false, 0}};
    Cameras::Camera c;
    c.name = "ed_camera01";
    c.pos = {1, 2, 3};
    c.angles = {-0.25f, 1.5f, 0};
    c.distance = 640.0f;
    file.cameras = {c};
    file.level.sky = "AU_Sky";
    std::string text = Layout::Write(file);
    Layout::File read;
    int line = -1;
    bool ok = Layout::Read(text, &read, &line) && read.scene == file.scene && read.faces.size() == 1 &&
              read.faces[0].checksum == 0x0A0B0C0D && read.faces[0].faces == file.faces[0].faces &&
              read.clusters.size() == 2 && Clusters::Same(read.clusters[0], file.clusters[0]) &&
              Clusters::Same(read.clusters[1], file.clusters[1]) && read.cameras.size() == 1 &&
              Cameras::Same(read.cameras[0], c) && read.level.sky == "AU_Sky";
    Check(ok, "editor map3: faces / cluster / camera records and the sky in a layout, read back");
    // A malformed one refuses the file (as every record kind).
    Check(!Layout::Read("thug2-decomp layout 2\nscene CAFEF00D\nfaces 0A0B0C0D 1:XYZ\n", &read, &line) && line == 3 &&
              !Layout::Read("thug2-decomp layout 2\nscene CAFEF00D\ncamera ed_camera01\n", &read, &line) &&
              !Layout::Read("thug2-decomp layout 2\nscene CAFEF00D\ncluster 11111111\n", &read, &line),
          "editor map3: a malformed record refuses the layout");
}
SELFTEST(Early, TestEditorMap3Layout);

} // namespace
