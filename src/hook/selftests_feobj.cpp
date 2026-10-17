// Self-tests for the menu_backdrop feature's front-end objects (game/front/frontend_objects.*: NOT ORIGINAL - ours):
// the placement's worked values for both views, the apply / restore state machine on stand-in objects, the hooks
// in CCompositeObject::Update, and the reading of the game's state (on stand-in game state).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/front/frontend_objects.h"
#include "game/front/menu_backdrop.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "game/core/dbg.h"
#include "game/gfx/camera.h"
#include "game/gfx/scene.h"
#include "game/script/struct.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
namespace FO = Front::FrontEndObjects;
namespace MB = Front::MenuBackdrop;
using Obj::CCompositeObject;

std::vector<Saved> g_feobj_saved;
void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_feobj_saved);
    g_feobj_saved.clear();
    FO::Reset();
}

struct SettingScope {
    int saved;
    explicit SettingScope(int value) : saved(MB::Setting()) { MB::Setting() = value; }
    ~SettingScope() { MB::Setting() = saved; }
};

bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
bool VecNear(const Mth::Vector& v, double x, double y, double z, double tolerance)
{
    return Near(v.x, x, tolerance) && Near(v.y, y, tolerance) && Near(v.z, z, tolerance);
}

// The stand-in menu camera: at (180, 40, 300), looking along -(0.30, 0.20, 0.93), up about (-0.05, 0.99, 0.02).
const float kCamPos[3] = {180.0f, 40.0f, 300.0f};
const float kCamBack[3] = {0.30f, 0.20f, 0.93f};
const float kCamUp[3] = {-0.05f, 0.99f, 0.02f};
const float kNoOffset[3] = {0.0f, 0.0f, 0.0f};

// --- the placement -------------------------------------------------------------------------------------------------

void TestFrontEndPlacement()
{
    bool ok = true;
    // The camera's frame (made orthonormal) - worked out independently (python, double).
    double frame[3][3];
    FO::CameraFrame(kCamBack, kCamUp, frame);
    const double camFrame[9] = {0.946842, 0.054226, -0.317095, -0.114141, 0.978189,
                                -0.173544, 0.300768, 0.200512, 0.932381};
    for (int i = 0; i < 9; ++i)
        ok = ok && Near(frame[i / 3][i % 3], camFrame[i], 1e-6);
    Check(ok, "front-end objects: a camera's frame");

    struct Worked {
        double rot[9];
        double trans[3];
        double skater[3], wheel[3], skaterOffset[3];
    };
    const Worked worked[2] = {
        {{-0.537644, -0.058288, -0.841155, -0.028091, 0.998292, -0.051222, 0.842704, -0.003911, -0.538364},
         {-1963.1665, -370.8876, 6119.1508},
         {-1954.9877, -439.7046, 6017.8390},
         {-1963.4713, -383.3855, 6120.2317},
         {-1960.6580, -444.1745, 6008.9442}},
        {{0.461354, -0.058554, 0.885282, 0.023809, 0.998278, 0.053620, -0.886897, -0.003660, 0.461953},
         {-1813.6369, -370.8924, 5716.2086},
         {-1830.7417, -439.7155, 5816.3962},
         {-1813.2423, -383.3903, 5715.1573},
         {-1825.8794, -444.1883, 5825.7558}},
    };
    const Mth::Vector skater = {84.8329f, -63.7398f, 61.704f, 1.0f};  // trg_playerrestart
    const Mth::Vector wheel = {-0.016818f, -12.5234f, -0.78992f, 0.5f}; // wol_pie_1 (w: passed through)
    const float offset[3] = {10.0f, -5.0f, 2.5f};
    for (int v = 0; v < 2; ++v) {
        const MB::View& view = *MB::ViewFor(v + 1);
        const Worked& w = worked[v];
        FO::Placement p = FO::MakePlacement(kCamPos, kCamBack, kCamUp, view, kNoOffset);
        for (int i = 0; i < 9; ++i)
            ok = ok && Near(p.rot[i / 3][i % 3], w.rot[i], 1e-6);
        for (int j = 0; j < 3; ++j)
            ok = ok && Near(p.trans[j], w.trans[j], 1e-3);
        double det = p.rot[0][0] * (p.rot[1][1] * p.rot[2][2] - p.rot[1][2] * p.rot[2][1]) -
                     p.rot[0][1] * (p.rot[1][0] * p.rot[2][2] - p.rot[1][2] * p.rot[2][0]) +
                     p.rot[0][2] * (p.rot[1][0] * p.rot[2][1] - p.rot[1][1] * p.rot[2][0]);
        ok = ok && Near(det, 1.0, 1e-9);
        Mth::Vector s = FO::MapPoint(p, skater);
        Mth::Vector h = FO::MapPoint(p, wheel);
        ok = ok && VecNear(s, w.skater[0], w.skater[1], w.skater[2], 2e-3) && s.w == 1.0f;
        ok = ok && VecNear(h, w.wheel[0], w.wheel[1], w.wheel[2], 2e-3) && h.w == 0.5f;
        // The camera lands on the view; 100 units in front of it, 100 in front of the view.
        Mth::Vector camera = FO::MapPoint(p, {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f});
        ok = ok && VecNear(camera, view.pos[0], view.pos[1], view.pos[2], 1e-3);
        Mth::Vector ahead = {static_cast<float>(kCamPos[0] - 100.0 * frame[2][0]),
                             static_cast<float>(kCamPos[1] - 100.0 * frame[2][1]),
                             static_cast<float>(kCamPos[2] - 100.0 * frame[2][2]), 1.0f};
        double viewFrame[3][3];
        FO::CameraFrame(view.back, view.up, viewFrame);
        Mth::Vector mapped = FO::MapPoint(p, ahead);
        ok = ok && VecNear(mapped, view.pos[0] - 100.0 * viewFrame[2][0], view.pos[1] - 100.0 * viewFrame[2][1],
                           view.pos[2] - 100.0 * viewFrame[2][2], 2e-3);
        // The camera's axes become the view's.
        for (int r = 0; r < 3; ++r) {
            Mth::Vector axis = {static_cast<float>(frame[r][0]), static_cast<float>(frame[r][1]),
                                static_cast<float>(frame[r][2]), 0.0f};
            Mth::Vector a = FO::MapDirection(p, axis);
            ok = ok && VecNear(a, viewFrame[r][0], viewFrame[r][1], viewFrame[r][2], 1e-6);
        }
        // Directions: rows of the rotation; w passed through.
        Mth::Vector x = FO::MapDirection(p, {1.0f, 0.0f, 0.0f, 0.25f});
        Mth::Vector z = FO::MapDirection(p, {0.0f, 0.0f, 1.0f, 0.0f});
        ok = ok && VecNear(x, w.rot[0], w.rot[1], w.rot[2], 1e-6) && x.w == 0.25f;
        ok = ok && VecNear(z, w.rot[6], w.rot[7], w.rot[8], 1e-6);
        // And back.
        Mth::Vector back = FO::UnmapPoint(p, s);
        ok = ok && VecNear(back, skater.x, skater.y, skater.z, 2e-3) && back.w == 1.0f;
        Mth::Vector dir = FO::UnmapDirection(p, FO::MapDirection(p, {0.6f, -0.8f, 0.0f, 3.0f}));
        ok = ok && VecNear(dir, 0.6, -0.8, 0.0, 1e-6) && dir.w == 3.0f;
        // Unmapping is the inverse: of a backdrop point that isn't a mapped one too.
        Mth::Vector q = {-1900.0f, -400.0f, 6000.0f, 1.0f};
        Mth::Vector qq = FO::MapPoint(p, FO::UnmapPoint(p, q));
        ok = ok && VecNear(qq, q.x, q.y, q.z, 2e-3);
        // An offset: along the view's right, up and back.
        FO::Placement po = FO::MakePlacement(kCamPos, kCamBack, kCamUp, view, offset);
        Mth::Vector so = FO::MapPoint(po, skater);
        ok = ok && VecNear(so, w.skaterOffset[0], w.skaterOffset[1], w.skaterOffset[2], 2e-3);
        if (!ok) {
            Log("  front-end placement: view %d: skater (%.4f %.4f %.4f) wheel (%.4f %.4f %.4f)", v + 1, s.x, s.y, s.z,
                h.x, h.y, h.z);
            break;
        }
    }
    Check(ok, "front-end objects: the placement for views 1 and 2 (worked values), its inverse, offsets");
}

// --- stand-in objects ----------------------------------------------------------------------------------------------

constexpr size_t kObjectSize = 0x108;
constexpr uintptr_t kCompositeVtable = 0x00647650, kSkaterVtable = 0x0064EC44, kObjectVtable = 0x006477E8;
constexpr uint32_t kTeleported = Obj::COMPOSITE_FLAG_TELEPORTED;

struct alignas(16) ObjectMemory {
    uint8_t bytes[0x110]; // (kObjectSize, padded to 16)
    CCompositeObject* Get() { return reinterpret_cast<CCompositeObject*>(bytes); }
};

// A component whose every slot is the game's empty function (0x005480A0: RET) - Teleport for SetTeleported.
void* g_emptyVtable[16];
struct alignas(16) ComponentMemory {
    uint8_t bytes[0x40];
    Obj::CBaseComponent* Get() { return reinterpret_cast<Obj::CBaseComponent*>(bytes); }
};

void FillObject(ObjectMemory& memory, uintptr_t vtable, uint32_t id, int seed)
{
    std::memset(memory.bytes, 0, sizeof(memory.bytes));
    CCompositeObject* o = memory.Get();
    o->m_vtable = reinterpret_cast<void**>(vtable);
    o->m_id = id;
    float a = 0.3f * seed, c = std::cos(a), s = std::sin(a);
    o->m_pos = {10.0f + 3.5f * seed, -60.0f + seed, 40.0f - 2.0f * seed, 1.0f};
    o->m_oldPos = {o->m_pos.x + 0.5f, o->m_pos.y, o->m_pos.z - 0.25f, 1.0f};
    o->m_vel = {0.1f * seed, 0.0f, -0.2f, 0.0f};
    o->m_matrix.row[0] = {c, 0.0f, -s, 0.0f};
    o->m_matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    o->m_matrix.row[2] = {s, 0.0f, c, 0.0f};
    o->m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    float d = a + 0.1f, cd = std::cos(d), sd = std::sin(d);
    o->m_displayMatrix.row[0] = {cd, 0.0f, -sd, 0.0f};
    o->m_displayMatrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    o->m_displayMatrix.row[2] = {sd, 0.0f, cd, 0.0f};
    o->m_displayMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}

// The stand-in lookup (ids -> objects).
constexpr int kMaxFound = 8;
uint32_t g_found_ids[kMaxFound];
Obj::CObject* g_found_objects[kMaxFound];
int g_foundCount;
void SetFound(uint32_t id, Obj::CObject* object)
{
    for (int i = 0; i < g_foundCount; ++i) {
        if (g_found_ids[i] == id) {
            g_found_objects[i] = object;
            return;
        }
    }
    g_found_ids[g_foundCount] = id;
    g_found_objects[g_foundCount++] = object;
}
Obj::CObject* FoundLookup(uint32_t id)
{
    for (int i = 0; i < g_foundCount; ++i) {
        if (g_found_ids[i] == id)
            return g_found_objects[i];
    }
    return nullptr;
}

bool Same(const ObjectMemory& a, const std::vector<uint8_t>& b) { return std::memcmp(a.bytes, b.data(), b.size()) == 0; }
std::vector<uint8_t> Snapshot(const ObjectMemory& memory)
{
    return std::vector<uint8_t>(memory.bytes, memory.bytes + kObjectSize);
}

// Whether the object is moved by `p` from the pose in `before` (every moved field; w and row 3 as they were).
bool MovedBy(CCompositeObject* o, const std::vector<uint8_t>& before, const FO::Placement& p)
{
    const auto* was = reinterpret_cast<const CCompositeObject*>(before.data());
    auto same = [](const Mth::Vector& a, const Mth::Vector& b) { return std::memcmp(&a, &b, 16) == 0; };
    bool ok = same(o->m_pos, FO::MapPoint(p, was->m_pos)) && same(o->m_oldPos, FO::MapPoint(p, was->m_oldPos)) &&
              same(o->m_vel, FO::MapDirection(p, was->m_vel));
    for (int r = 0; r < 3; ++r) {
        ok = ok && same(o->m_matrix.row[r], FO::MapDirection(p, was->m_matrix.row[r]));
        ok = ok && same(o->m_displayMatrix.row[r], FO::MapDirection(p, was->m_displayMatrix.row[r]));
    }
    ok = ok && same(o->m_matrix.row[3], was->m_matrix.row[3]) &&
         same(o->m_displayMatrix.row[3], was->m_displayMatrix.row[3]);
    return ok && (o->m_compositeFlags & kTeleported);
}

// The bytes outside the pose and the flags (+0x104) as they were.
bool RestUnchanged(const ObjectMemory& memory, const std::vector<uint8_t>& before)
{
    return std::memcmp(memory.bytes, before.data(), 0x4C) == 0 &&
           std::memcmp(memory.bytes + 0x100, before.data() + 0x100, 4) == 0;
}

// The pose bytes (and the rest but the flags) as they were.
bool PoseRestored(const ObjectMemory& memory, const std::vector<uint8_t>& before)
{
    return std::memcmp(memory.bytes, before.data(), 0x104) == 0 &&
           (reinterpret_cast<const CCompositeObject*>(memory.bytes)->m_compositeFlags & kTeleported);
}

void ClearTeleported(ObjectMemory& memory) { memory.Get()->m_compositeFlags &= ~kTeleported; }

// --- the state machine -------------------------------------------------------------------------------------------

void TestFrontEndObjectStates()
{
    FO::Reset();
    for (void*& slot : g_emptyVtable)
        slot = reinterpret_cast<void*>(0x005480A0);
    const uint32_t kSkater = Crc::GenerateCRCFromString("Skater");
    const uint32_t kRing = Crc::GenerateCRCFromString("wol_inner_ring");
    const uint32_t kPie = Crc::GenerateCRCFromString("wol_pie_3");
    const uint32_t kTag = Crc::GenerateCRCFromString("wol_tag");
    const uint32_t kBulb = Crc::GenerateCRCFromString("wol_lightbulb_28");
    const uint32_t kOther = Crc::GenerateCRCFromString("SkWare_Light_Sign23");
    const uint32_t cam02 = Crc::GenerateCRCFromString("mainmenu_camera02");
    const uint32_t cam03 = Crc::GenerateCRCFromString("mainmenu_camera03");
    bool ok = true;
    auto fail = [&](const char* where) {
        if (ok)
            Log("  front-end objects: failed at: %s", where);
        ok = false;
    };

    // The table: 49 objects, the skater the only skater.
    {
        uint32_t ids[64];
        FO::Kind kinds[64];
        int n = FO::TableIds(ids, kinds, 64);
        int skaters = 0;
        bool ring = false, bulb = false, pie16 = false, tag = false;
        for (int i = 0; i < n; ++i) {
            skaters += kinds[i] == FO::Kind::Skater;
            ring |= ids[i] == kRing;
            bulb |= ids[i] == kBulb;
            tag |= ids[i] == kTag;
            pie16 |= ids[i] == Crc::GenerateCRCFromString("wol_pie_16");
        }
        if (!(n == 49 && skaters == 1 && ids[0] == kSkater && kinds[0] == FO::Kind::Skater && ring && bulb && pie16 &&
              tag && ids[n - 1] == kBulb))
            fail("the table");
    }

    // The objects: the skater (physics paused by its physics control component), two wheel parts, an object of the
    // table's that isn't a composite object, one not in the table; the menu camera's object.
    static ObjectMemory skater, ring, pie, tag, other, ring2, camera, outer;
    static ComponentMemory control;
    FillObject(skater, kSkaterVtable, kSkater, 1);
    FillObject(ring, kCompositeVtable, kRing, 2);
    FillObject(pie, kCompositeVtable, kPie, 3);
    FillObject(tag, kObjectVtable, kTag, 4);
    FillObject(other, kCompositeVtable, kOther, 5);
    FillObject(outer, kCompositeVtable, Crc::GenerateCRCFromString("wol_outer_ring"), 7);
    outer.Get()->m_objectFlags = Obj::OBJECT_FLAG_DEAD; // (dead, still found)
    std::memset(control.bytes, 0, sizeof(control.bytes));
    control.Get()->m_vtable = g_emptyVtable;
    control.Get()->m_type = 0xD9151D4D;
    control.Get()->m_object = skater.Get();
    control.bytes[0x18] = 1;
    skater.Get()->mp_components = control.Get();
    FillObject(camera, kCompositeVtable, 0x967C138C, 0);
    camera.Get()->m_pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
    camera.Get()->m_matrix.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
    camera.Get()->m_matrix.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
    g_foundCount = 0;
    SetFound(kSkater, skater.Get());
    SetFound(kRing, ring.Get());
    SetFound(kPie, pie.Get());
    SetFound(kTag, tag.Get());
    SetFound(kOther, other.Get());
    SetFound(Crc::GenerateCRCFromString("wol_outer_ring"), outer.Get());
    std::vector<uint8_t> sSkater = Snapshot(skater), sRing = Snapshot(ring), sPie = Snapshot(pie),
                         sTag = Snapshot(tag), sOther = Snapshot(other), sOuter = Snapshot(outer);
    auto unmoved = [&]() {
        return Same(skater, sSkater) && Same(ring, sRing) && Same(pie, sPie) && Same(tag, sTag) &&
               Same(other, sOther) && Same(outer, sOuter) && FO::MovedCount() == 0 && !FO::Applied();
    };
    auto clearFlags = [&]() {
        for (ObjectMemory* m : {&skater, &ring, &pie, &tag, &other})
            ClearTeleported(*m);
    };
    const MB::View* view1 = MB::ViewFor(1);
    const MB::View* view2 = MB::ViewFor(2);
    FO::Placement p1 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *view1, kNoOffset);
    FO::Placement p2 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *view2, kNoOffset);
    FO::Inputs in = {view1, true, true, cam02, camera.Get()};

    // Off, not the front end, no backdrop, no menu camera: nothing, however often.
    for (int i = 0; i < 4; ++i) {
        FO::Inputs off = in;
        if (i == 0)
            off.view = nullptr;
        if (i == 1)
            off.frontEnd = false;
        if (i == 2)
            off.backdropLoaded = false;
        if (i == 3)
            off.menuCamera = 0;
        for (int k = 0; k < 3; ++k)
            FO::Step(off, true, FoundLookup);
        if (!unmoved() || FO::CameraPoseCount() != 0)
            fail("nothing moved while off");
    }

    // The camera's pose: not from a render step; the same for two updates.
    FO::Step(in, false, FoundLookup);
    FO::Step(in, false, FoundLookup);
    if (!unmoved())
        fail("no pose from render steps");
    FO::Step(in, true, FoundLookup);
    if (!unmoved())
        fail("first sample");
    camera.Get()->m_pos.y += 1.0f; // still moving
    FO::Step(in, true, FoundLookup);
    if (!unmoved())
        fail("a moving camera");
    camera.Get()->m_pos.y -= 1.0f;
    camera.Get()->m_matrix.row[1].x += 0.01f; // (only its up differs from the next)
    FO::Step(in, true, FoundLookup);
    if (!unmoved())
        fail("a moving camera (2)");
    camera.Get()->m_matrix.row[1].x = kCamUp[0];
    FO::Step(in, true, FoundLookup);
    if (!unmoved())
        fail("a turning camera");
    FO::Step(in, true, FoundLookup);
    // Moved: the skater and the two wheel parts; not the object that isn't composite, nor the one not in the table.
    if (!(FO::Applied() && FO::MovedCount() == 3 && FO::CameraPoseCount() == 1 && MovedBy(skater.Get(), sSkater, p1) &&
          MovedBy(ring.Get(), sRing, p1) && MovedBy(pie.Get(), sPie, p1) && Same(tag, sTag) && Same(other, sOther) &&
          Same(outer, sOuter) && RestUnchanged(skater, sSkater) && RestUnchanged(ring, sRing) && RestUnchanged(pie, sPie)))
        fail("moved");
    // Nothing more while nothing changes (no teleport either).
    clearFlags();
    std::vector<uint8_t> mRing = Snapshot(ring), mSkater = Snapshot(skater);
    FO::Step(in, true, FoundLookup);
    FO::Step(in, false, FoundLookup);
    FO::BeforeComponents(ring.Get());
    FO::AfterComponents(skater.Get());
    if (!(Same(ring, mRing) && Same(skater, mSkater) && FO::MovedCount() == 3))
        fail("steady");

    // Back exactly, on each trigger, and moved again.
    struct Trigger {
        const char* name;
        int kind;
    };
    const Trigger triggers[] = {{"another camera", 0}, {"not the front end", 1}, {"no backdrop", 2},
                                {"setting off", 3},    {"LoadScene", 4},         {"Cleanup", 5},
                                {"UnloadAllLevelGeometry", 6}, {"UnloadScene", 7}};
    for (const Trigger& t : triggers) {
        clearFlags();
        FO::Inputs changed = in;
        switch (t.kind) {
        case 0: changed.menuCamera = 0; break;
        case 1: changed.frontEnd = false; break;
        case 2: changed.backdropLoaded = false; break;
        case 3: changed.view = nullptr; break;
        case 4: FO::OnLevelChange(); break;
        case 5: FO::BeforeCFunction(reinterpret_cast<const void*>(0x0059FE50)); break;
        case 6: FO::BeforeCFunction(reinterpret_cast<const void*>(0x00445120)); break;
        default: FO::BeforeCFunction(reinterpret_cast<const void*>(0x004457B0)); break;
        }
        if (t.kind <= 3)
            FO::Step(changed, true, FoundLookup);
        if (!(PoseRestored(skater, sSkater) && PoseRestored(ring, sRing) && PoseRestored(pie, sPie) &&
              Same(tag, sTag) && Same(other, sOther) && FO::MovedCount() == 0 && !FO::Applied()))
            fail(t.name);
        // Held off after a level change until the backdrop goes or comes again; the poses kept only while the
        // front end and its backdrop stay.
        bool level = t.kind >= 4;
        if (FO::HoldingOff() != level || FO::CameraPoseCount() != (t.kind == 0 || level ? 1 : 0))
            fail(t.name);
        clearFlags();
        FO::Step(in, false, FoundLookup);
        if (level) {
            if (FO::Applied())
                fail("held off");
            FO::Step(in, true, FoundLookup);
            if (FO::Applied())
                fail("held off (2)");
            if (t.kind == 5) {
                FO::OnBackdropLoaded(); // the front end loaded again: poses captured anew
                if (FO::HoldingOff() || FO::CameraPoseCount() != 0)
                    fail("backdrop loaded");
            } else {
                FO::Inputs gone = in;
                gone.backdropLoaded = false;
                FO::Step(gone, true, FoundLookup);
                if (FO::HoldingOff())
                    fail("hold off cleared");
            }
            FO::Step(in, true, FoundLookup);
            FO::Step(in, true, FoundLookup);
        } else if (t.kind != 0) {
            // (the poses were forgotten with the front end: two samples)
            if (FO::Applied())
                fail("pose forgotten");
            FO::Step(in, true, FoundLookup);
            FO::Step(in, true, FoundLookup);
        }
        // (another camera: the pose kept, moved again by a render step at once)
        if (!(FO::Applied() && FO::MovedCount() == 3 && MovedBy(skater.Get(), sSkater, p1) &&
              MovedBy(ring.Get(), sRing, p1) && MovedBy(pie.Get(), sPie, p1)))
            fail(t.name);
    }
    // Other C functions: nothing.
    clearFlags();
    mRing = Snapshot(ring);
    FO::BeforeCFunction(reinterpret_cast<const void*>(0x005654D0)); // RequestLevel
    FO::BeforeCFunction(reinterpret_cast<const void*>(0x00445130)); // LoadScene's function itself
    if (!(Same(ring, mRing) && FO::Applied() && !FO::HoldingOff()))
        fail("other C functions");

    // Another view: restored, then placed for it (the camera pose kept).
    {
        FO::Inputs v2 = in;
        v2.view = view2;
        FO::Step(v2, false, FoundLookup);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p2) && MovedBy(skater.Get(), sSkater, p2)))
            fail("view 2");
        FO::Step(in, false, FoundLookup);
        if (!MovedBy(ring.Get(), sRing, p1))
            fail("view 1 again");
    }

    // Another menu camera: restored; its own pose captured (two updates), and placed for it.
    {
        const float pos3[3] = {-40.0f, 20.0f, 150.0f};
        const float back3[3] = {-0.5f, 0.1f, 0.86f};
        const float up3[3] = {0.0f, 1.0f, 0.0f};
        camera.Get()->m_pos = {pos3[0], pos3[1], pos3[2], 1.0f};
        camera.Get()->m_matrix.row[2] = {back3[0], back3[1], back3[2], 0.0f};
        camera.Get()->m_matrix.row[1] = {up3[0], up3[1], up3[2], 0.0f};
        FO::Inputs c3 = in;
        c3.menuCamera = cam03;
        FO::Step(c3, true, FoundLookup);
        if (!(PoseRestored(ring, sRing) && PoseRestored(skater, sSkater) && !FO::Applied()))
            fail("camera 03: restored");
        FO::Step(c3, true, FoundLookup);
        FO::Placement p3 = FO::MakePlacement(pos3, back3, up3, *view1, kNoOffset);
        if (!(FO::Applied() && FO::CameraPoseCount() == 2 && MovedBy(ring.Get(), sRing, p3) &&
              MovedBy(pie.Get(), sPie, p3)))
            fail("camera 03: placed");
        // Back to camera 02: its pose known, placed at once.
        FO::Step(in, false, FoundLookup);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p1) && MovedBy(pie.Get(), sPie, p1)))
            fail("camera 02 again");
        camera.Get()->m_pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
        camera.Get()->m_matrix.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
        camera.Get()->m_matrix.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
    }

    // A menu camera that keeps moving: taken as it is at its 31st sample.
    {
        FO::Inputs moving = in;
        moving.menuCamera = Crc::GenerateCRCFromString("SS_MenuCam");
        for (int k = 0; k < 30; ++k) {
            camera.Get()->m_pos.x = kCamPos[0] + k;
            FO::Step(moving, true, FoundLookup);
            if (FO::Applied())
                fail("a moving camera: too soon");
        }
        // (a render step in between doesn't count)
        FO::Step(moving, false, FoundLookup);
        camera.Get()->m_pos.x = kCamPos[0] - 7.0f;
        FO::Step(moving, true, FoundLookup);
        const float movedPos[3] = {kCamPos[0] - 7.0f, kCamPos[1], kCamPos[2]};
        FO::Placement pm = FO::MakePlacement(movedPos, kCamBack, kCamUp, *view1, kNoOffset);
        if (!(FO::Applied() && FO::CameraPoseCount() == 3 && MovedBy(ring.Get(), sRing, pm)))
            fail("a moving camera: taken");
        camera.Get()->m_pos.x = kCamPos[0];
        // Another camera's samples don't count for it: only samples in a row.
        FO::Inputs level = in, first = in;
        level.menuCamera = Crc::GenerateCRCFromString("level_select_cam");
        first.menuCamera = Crc::GenerateCRCFromString("mainmenu_camera");
        for (int k = 0; k < 20; ++k) {
            camera.Get()->m_pos.x = kCamPos[0] + k;
            FO::Step(level, true, FoundLookup);
            camera.Get()->m_pos.x = kCamPos[0] + k + 0.5f;
            FO::Step(first, true, FoundLookup);
        }
        for (int k = 0; k < 15; ++k) {
            camera.Get()->m_pos.x = kCamPos[0] + 200.0f + k;
            FO::Step(level, true, FoundLookup);
        }
        if (FO::Applied() || FO::CameraPoseCount() != 3)
            fail("a moving camera: samples in a row");
        camera.Get()->m_pos.x = kCamPos[0];
        FO::Step(in, false, FoundLookup);
    }

    // The game's writes.
    // - near the object's real place (Obj_MoveToNode): moved there; restored there exactly.
    clearFlags();
    ring.Get()->m_pos = {5.0f, -12.0f, 3.0f, 1.0f};
    FO::AfterComponents(ring.Get());
    std::vector<uint8_t> eRing = sRing;
    reinterpret_cast<CCompositeObject*>(eRing.data())->m_pos = {5.0f, -12.0f, 3.0f, 1.0f};
    if (!MovedBy(ring.Get(), eRing, p1))
        fail("a write near its place");
    // - near where it is now (Obj_Rotate: about its own axis): kept; its real orientation turned the same way.
    clearFlags();
    {
        CCompositeObject* o = pie.Get();
        const float a = 0.3926991f, c = std::cos(a), s = std::sin(a); // 22.5 degrees about its own z
        Mth::Vector r0 = o->m_matrix.row[0], r1 = o->m_matrix.row[1];
        for (int k = 0; k < 3; ++k) {
            (&o->m_matrix.row[0].x)[k] = c * (&r0.x)[k] + s * (&r1.x)[k];
            (&o->m_matrix.row[1].x)[k] = -s * (&r0.x)[k] + c * (&r1.x)[k];
        }
        o->m_displayMatrix = o->m_matrix; // (the motion component copies it)
        std::vector<uint8_t> turned = Snapshot(pie);
        FO::AfterComponents(o);
        if (!Same(pie, turned) || (o->m_compositeFlags & kTeleported))
            fail("a turn in place kept");
        // What it should come back as: the original turned the same way about its own z.
        auto* was = reinterpret_cast<CCompositeObject*>(sPie.data());
        Mth::Vector o0 = was->m_matrix.row[0], o1 = was->m_matrix.row[1];
        double expected[2][3];
        for (int k = 0; k < 3; ++k) {
            expected[0][k] = c * (&o0.x)[k] + s * (&o1.x)[k];
            expected[1][k] = -s * (&o0.x)[k] + c * (&o1.x)[k];
        }
        FO::BeforeComponents(o); // (nothing new)
        // Restore through a camera change and check.
        FO::Inputs none = in;
        none.menuCamera = 0;
        FO::Step(none, true, FoundLookup);
        bool turnedBack = VecNear(o->m_matrix.row[0], expected[0][0], expected[0][1], expected[0][2], 2e-6) &&
                          VecNear(o->m_matrix.row[1], expected[1][0], expected[1][1], expected[1][2], 2e-6) &&
                          VecNear(o->m_matrix.row[2], was->m_matrix.row[2].x, was->m_matrix.row[2].y,
                                  was->m_matrix.row[2].z, 2e-6) &&
                          VecNear(o->m_displayMatrix.row[0], expected[0][0], expected[0][1], expected[0][2], 2e-6) &&
                          std::memcmp(&o->m_pos, &was->m_pos, 16) == 0;
        if (!turnedBack)
            fail("a turn in place, restored");
        // The ring: back at the place written near it, exactly.
        if (std::memcmp(ring.bytes, eRing.data(), 0x104) != 0)
            fail("a write near its place, restored");
        // (start again from these poses)
        sPie = Snapshot(pie);
        sRing = Snapshot(ring);
        clearFlags();
        FO::Step(in, true, FoundLookup);
        if (!(MovedBy(pie.Get(), sPie, p1) && MovedBy(ring.Get(), sRing, p1)))
            fail("moved again after the writes");
    }
    // - only w changed (the update sets the position's w to 1): taken as it is, x y z kept exact.
    clearFlags();
    ring.Get()->m_pos.w = 0.25f;
    skater.Get()->m_matrix.row[3].w = 2.0f;
    FO::BeforeComponents(ring.Get());
    FO::BeforeComponents(skater.Get());
    if (ring.Get()->m_compositeFlags & kTeleported)
        fail("w only: no teleport");
    {
        FO::Inputs none = in;
        none.menuCamera = 0;
        FO::Step(none, true, FoundLookup);
        auto* o = reinterpret_cast<CCompositeObject*>(sRing.data());
        if (!(ring.Get()->m_pos.w == 0.25f && std::memcmp(&ring.Get()->m_pos, &o->m_pos, 12) == 0 &&
              skater.Get()->m_matrix.row[3].w == 2.0f))
            fail("w only, restored");
        ring.Get()->m_pos.w = 1.0f;
        skater.Get()->m_matrix.row[3].w = 1.0f;
        sSkater = Snapshot(skater);
        FO::Step(in, true, FoundLookup);
    }
    // - a little move where it is now: kept (no teleport); its real place moved the same way back when restored -
    //   also when no hook saw it before the restore.
    clearFlags();
    {
        CCompositeObject* r = ring.Get();
        CCompositeObject* q = pie.Get();
        CCompositeObject* k = skater.Get();
        r->m_pos.x += 0.5f;
        q->m_pos.x += 0.5f;
        std::vector<uint8_t> nudged = Snapshot(ring);
        FO::AfterComponents(r);
        if (!Same(ring, nudged) || (r->m_compositeFlags & kTeleported))
            fail("a little move kept");
        // - a velocity written near the real one: turned as a direction.
        r->m_vel = {0.3f, 0.0f, -0.2f, 0.0f};
        FO::AfterComponents(r);
        Mth::Vector vel = FO::MapDirection(p1, {0.3f, 0.0f, -0.2f, 0.0f});
        if (std::memcmp(&r->m_vel, &vel, 16) != 0)
            fail("a velocity written");
        // - one row of the orientation (row 2) changed a little in place: taken in.
        auto* ks = reinterpret_cast<CCompositeObject*>(sSkater.data());
        k->m_matrix.row[2].x += 0.01f;
        FO::AfterComponents(k);
        if (k->m_compositeFlags & kTeleported)
            fail("one row changed in place");
        // - w only, then the position written back to its real value: moved again, the new w kept.
        k->m_matrix.row[0].w = 3.0f;
        FO::BeforeComponents(k);
        k->m_pos = ks->m_pos;
        FO::AfterComponents(k);
        Mth::Vector pos = FO::MapPoint(p1, ks->m_pos);
        if (std::memcmp(&k->m_pos, &pos, 16) != 0 || k->m_matrix.row[0].w != 3.0f || !(k->m_compositeFlags & kTeleported))
            fail("written back after a w change");
        FO::Inputs none = in;
        none.menuCamera = 0;
        FO::Step(none, true, FoundLookup);
        auto* rs = reinterpret_cast<CCompositeObject*>(sRing.data());
        auto* qs = reinterpret_cast<CCompositeObject*>(sPie.data());
        bool back = VecNear(r->m_pos, rs->m_pos.x + 0.5 * p1.rot[0][0], rs->m_pos.y + 0.5 * p1.rot[1][0],
                            rs->m_pos.z + 0.5 * p1.rot[2][0], 2e-3) &&
                    VecNear(q->m_pos, qs->m_pos.x + 0.5 * p1.rot[0][0], qs->m_pos.y + 0.5 * p1.rot[1][0],
                            qs->m_pos.z + 0.5 * p1.rot[2][0], 2e-3) &&
                    r->m_vel.x == 0.3f && r->m_vel.y == 0.0f && r->m_vel.z == -0.2f && k->m_matrix.row[0].w == 3.0f &&
                    VecNear(k->m_matrix.row[2], ks->m_matrix.row[2].x + 0.01 * p1.rot[0][0],
                            ks->m_matrix.row[2].y + 0.01 * p1.rot[1][0], ks->m_matrix.row[2].z + 0.01 * p1.rot[2][0],
                            2e-6);
        if (!back)
            fail("a little move, restored");
        k->m_matrix.row[0].w = 0.0f;
        sRing = Snapshot(ring);
        sPie = Snapshot(pie);
        sSkater = Snapshot(skater);
        FO::Step(in, true, FoundLookup);
        if (!(MovedBy(ring.Get(), sRing, p1) && MovedBy(skater.Get(), sSkater, p1)))
            fail("moved again after the little moves");
    }

    // The skater's physics unpaused (its script): put back before its components run; not moved again while it
    // skates; moved again once paused.
    clearFlags();
    control.bytes[0x18] = 0;
    FO::BeforeComponents(skater.Get());
    if (!(PoseRestored(skater, sSkater) && FO::MovedCount() == 2 && !FO::IsMoved(skater.Get())))
        fail("unpaused: restored");
    FO::Step(in, true, FoundLookup);
    if (FO::IsMoved(skater.Get()) || std::memcmp(skater.bytes, sSkater.data(), 0x104) != 0)
        fail("unpaused: not moved");
    control.bytes[0x18] = 1;
    FO::Step(in, false, FoundLookup);
    if (!(FO::IsMoved(skater.Get()) && MovedBy(skater.Get(), sSkater, p1) && FO::MovedCount() == 3))
        fail("paused again: moved");
    // A skater without the physics control component: never moved.
    {
        FO::Inputs none = in;
        none.menuCamera = 0;
        FO::Step(none, true, FoundLookup);
        skater.Get()->mp_components = nullptr;
        sSkater = Snapshot(skater);
        FO::Step(in, true, FoundLookup);
        if (FO::IsMoved(skater.Get()) || !Same(skater, sSkater) || FO::MovedCount() != 2)
            fail("no physics control");
        FO::Step(none, true, FoundLookup);
        skater.Get()->mp_components = control.Get();
        sSkater = Snapshot(skater);
        FO::Step(in, true, FoundLookup);
    }

    // Destroyed while moved: forgotten, never touched again.
    clearFlags();
    std::memset(pie.bytes, 0xCD, sizeof(pie.bytes)); // (freed)
    SetFound(kPie, nullptr);
    FO::Step(in, false, FoundLookup);
    if (!(FO::MovedCount() == 2 && !FO::IsMoved(pie.Get())))
        fail("destroyed: forgotten");
    // Another object under the ring's id: the old one forgotten untouched, the new one moved.
    FillObject(ring2, kCompositeVtable, kRing, 6);
    std::vector<uint8_t> sRing2 = Snapshot(ring2);
    std::memset(ring.bytes, 0xAB, sizeof(ring.bytes));
    std::vector<uint8_t> freedRing = Snapshot(ring);
    SetFound(kRing, ring2.Get());
    FO::Step(in, false, FoundLookup);
    if (!(Same(ring, freedRing) && FO::IsMoved(ring2.Get()) && !FO::IsMoved(ring.Get()) &&
          MovedBy(ring2.Get(), sRing2, p1)))
        fail("replaced");
    // Dead (MarkAsDead, still registered in this stand-in): forgotten untouched; also by the update hooks.
    ClearTeleported(ring2);
    ring2.Get()->m_objectFlags |= Obj::OBJECT_FLAG_DEAD;
    std::vector<uint8_t> deadRing2 = Snapshot(ring2);
    FO::AfterComponents(ring2.Get());
    if (!(Same(ring2, deadRing2) && !FO::IsMoved(ring2.Get())))
        fail("dead");
    // Gone when restoring: untouched.
    std::vector<uint8_t> movedSkater = Snapshot(skater);
    SetFound(kSkater, nullptr);
    FO::RestoreAll();
    if (!(Same(skater, movedSkater) && FO::MovedCount() == 0 && !FO::Applied()))
        fail("gone when restoring");
    std::vector<uint8_t> cdPie(kObjectSize, 0xCD);
    if (!Same(pie, cdPie))
        fail("forgotten objects untouched (destroyed)");
    if (!Same(ring, freedRing))
        fail("forgotten objects untouched (replaced)");
    if (!Same(ring2, deadRing2))
        fail("forgotten objects untouched (dead)");

    // The update hooks with nothing moved: nothing.
    FO::Reset();
    FillObject(ring, kCompositeVtable, kRing, 2);
    sRing = Snapshot(ring);
    FO::BeforeComponents(ring.Get());
    FO::AfterComponents(ring.Get());
    if (!Same(ring, sRing))
        fail("hooks with nothing moved");

    Check(ok, "front-end objects: apply, restore on each trigger, the game's writes, skater physics, objects gone");
    FO::Reset();
}

// --- the hooks in CCompositeObject::Update -------------------------------------------------------------------------

// A component whose Update records the object's position and writes `g_hook_write` (if set) - through a vtable
// whose slot 1 is this function (thiscall: this in ECX).
Mth::Vector g_hook_seen;
bool g_hook_doWrite;
Mth::Vector g_hook_write;
void __fastcall HookComponentUpdate(Obj::CBaseComponent* component, void* /*edx*/)
{
    CCompositeObject* o = component->m_object;
    g_hook_seen = o->m_pos;
    if (g_hook_doWrite)
        o->m_pos = g_hook_write;
}
void* g_hookVtable[16];

void TestFrontEndObjectHooks()
{
    FO::Reset();
    for (void*& slot : g_hookVtable)
        slot = reinterpret_cast<void*>(0x005480A0);
    g_hookVtable[1] = reinterpret_cast<void*>(&HookComponentUpdate);
    for (void*& slot : g_emptyVtable)
        slot = reinterpret_cast<void*>(0x005480A0);
    const uint32_t kSkater = Crc::GenerateCRCFromString("Skater");
    const uint32_t kRing = Crc::GenerateCRCFromString("wol_inner_ring");
    static ObjectMemory skater, ring, camera;
    static ComponentMemory control, recorder;
    FillObject(skater, kSkaterVtable, kSkater, 1);
    // (suspended: no script nor own update - the skater's own update is real skater code)
    skater.Get()->m_compositeFlags = Obj::COMPOSITE_FLAG_SUSPENDED;
    FillObject(ring, kCompositeVtable, kRing, 2);
    std::memset(control.bytes, 0, sizeof(control.bytes));
    std::memset(recorder.bytes, 0, sizeof(recorder.bytes));
    control.Get()->m_vtable = g_emptyVtable;
    control.Get()->m_type = 0xD9151D4D;
    control.Get()->m_object = skater.Get();
    control.Get()->m_flags = Obj::COMPONENT_FLAG_SUSPENDED; // (not updated)
    control.bytes[0x18] = 1;
    recorder.Get()->m_vtable = g_hookVtable;
    recorder.Get()->m_type = 0x12345678;
    recorder.Get()->m_object = skater.Get();
    control.Get()->mp_next = recorder.Get();
    skater.Get()->mp_components = control.Get();
    static ComponentMemory recorder2;
    std::memcpy(recorder2.bytes, recorder.bytes, sizeof(recorder.bytes));
    recorder2.Get()->m_object = ring.Get();
    ring.Get()->mp_components = recorder2.Get();
    FillObject(camera, kCompositeVtable, 0x967C138C, 0);
    camera.Get()->m_pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
    camera.Get()->m_matrix.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
    camera.Get()->m_matrix.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
    g_foundCount = 0;
    SetFound(kSkater, skater.Get());
    SetFound(kRing, ring.Get());
    std::vector<uint8_t> sSkater = Snapshot(skater), sRing = Snapshot(ring);
    FO::Placement p1 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *MB::ViewFor(1), kNoOffset);
    FO::Inputs in = {MB::ViewFor(1), true, true, Crc::GenerateCRCFromString("mainmenu_camera02"), camera.Get()};
    bool ok = true;

    // Not moved: Update as always (the components see the object where it is).
    g_hook_doWrite = false;
    ring.Get()->Update();
    ok = ok && std::memcmp(&g_hook_seen, &sRing.data()[0x4C], 16) == 0;

    FO::Step(in, true, FoundLookup);
    FO::Step(in, true, FoundLookup);
    ok = ok && FO::MovedCount() == 2;
    Mth::Vector movedRing = ring.Get()->m_pos;

    // A component writes near the object's real place: moved there before the update ends (teleported, cleared
    // by the update's end as always).
    g_hook_doWrite = true;
    g_hook_write = {7.0f, -11.0f, 2.0f, 1.0f};
    ring.Get()->Update();
    Mth::Vector expected = FO::MapPoint(p1, g_hook_write);
    ok = ok && std::memcmp(&g_hook_seen, &movedRing, 16) == 0;
    ok = ok && std::memcmp(&ring.Get()->m_pos, &expected, 16) == 0 &&
         !(ring.Get()->m_compositeFlags & kTeleported);

    // The skater's physics unpaused by its script (here: before its update): put back before its components run.
    g_hook_doWrite = false;
    control.bytes[0x18] = 0;
    skater.Get()->Update();
    ok = ok && std::memcmp(&g_hook_seen, &sSkater.data()[0x4C], 16) == 0 && !FO::IsMoved(skater.Get());
    ok = ok && std::memcmp(skater.bytes + 0x4C, sSkater.data() + 0x4C, 0x100 - 0x4C) == 0;
    (void)p1;
    Check(ok, "front-end objects: CCompositeObject::Update's hooks (before and after the components)");
    FO::Reset();
}

// --- the game's state (stand-ins) --------------------------------------------------------------------------------

uint32_t g_gs_anim;
Gfx::Camera* g_gs_viewport;
Obj::CBaseComponent* g_gs_cameraComponent;
uint8_t g_gs_skate[0x400], g_gs_career[0x800];
uint8_t g_gs_scene[2][0x90];
uint32_t g_gs_nodes[3][4];
uint8_t g_gs_manager[0x448];

void SetBackdrop(bool loaded)
{
    uint32_t names[2] = {MB::kFrontEndSceneName, MB::kBackdropSceneName};
    int n = loaded ? 2 : 1;
    for (int i = 0; i < n; ++i) {
        std::memset(g_gs_scene[i], 0, sizeof(g_gs_scene[i]));
        std::memcpy(g_gs_scene[i] + 0x84, &names[i], 4);
        g_gs_nodes[i][1] = reinterpret_cast<uint32_t>(g_gs_scene[i]);
        g_gs_nodes[i][3] = reinterpret_cast<uint32_t>(g_gs_nodes[i + 1]);
    }
    g_gs_nodes[n][1] = 0xFFFFFFFF;
    GameVariable<uint32_t>(0x006FA7EC) = reinterpret_cast<uint32_t>(g_gs_nodes[0]);
}

void TestFrontEndObjectsGameState()
{
    FO::Reset();
    g_feobj_saved = {{0x007CE478, 4, {}}, {0x006FA7EC, 4, {}}, {0x006F0FB8, 4, {}}, {0x00701348, 4, {}}};
    SaveAll(g_feobj_saved);
    for (void*& slot : g_emptyVtable)
        slot = reinterpret_cast<void*>(0x005480A0);
    const uint32_t kRing = Crc::GenerateCRCFromString("wol_inner_ring");
    static ObjectMemory ring, camera;
    static ComponentMemory cameraComponent;
    alignas(16) static uint8_t viewportMemory[0xC4];
    FillObject(ring, kCompositeVtable, kRing, 2);
    FillObject(camera, kCompositeVtable, 0x967C138C, 0);
    camera.Get()->m_pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
    camera.Get()->m_matrix.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
    camera.Get()->m_matrix.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
    std::memset(cameraComponent.bytes, 0, sizeof(cameraComponent.bytes));
    cameraComponent.Get()->m_type = 0xC4E311FA;
    cameraComponent.Get()->m_object = camera.Get();
    *reinterpret_cast<void**>(cameraComponent.bytes + 0x18) = viewportMemory;
    g_gs_viewport = reinterpret_cast<Gfx::Camera*>(viewportMemory);
    g_gs_cameraComponent = cameraComponent.Get();
    g_foundCount = 0;
    SetFound(kRing, ring.Get());
    std::vector<uint8_t> sRing = Snapshot(ring);

    auto* s = new StandIns;
    s->At({0x0057B030}, 0, [](Regs&) { return g_gs_anim; });
    s->At({0x004A09C0}, 0, [](Regs& r) { return r.args[0] == 0 ? reinterpret_cast<uint32_t>(g_gs_viewport) : 0u; });
    s->At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0,
          [](Regs& r) { return reinterpret_cast<uint32_t>(FoundLookup(r.args[0])); });
    s->At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        return r.args[0] == 0xC4E311FA ? reinterpret_cast<uint32_t>(g_gs_cameraComponent) : 0u;
    });
    // (the held view's field of view: the default angle, the adjusted one stood in)
    s->At({0x00485AA0, AddressOf(&Gfx::Camera::UpdateAdjustedHFOV)}, 0, [](Regs&) { return 0u; });
    GameVariable<float>(0x00701348) = 72.0f;
    std::memset(g_gs_skate, 0, sizeof(g_gs_skate));
    std::memset(g_gs_career, 0, sizeof(g_gs_career));
    *reinterpret_cast<uint8_t**>(g_gs_skate + 0x20) = g_gs_career;
    *reinterpret_cast<uint32_t*>(g_gs_skate + 0x368) = 0x0BADF00D;
    GameVariable<uint8_t*>(0x007CE478) = g_gs_skate;
    GameVariable<uint8_t*>(0x006F0FB8) = g_gs_manager;
    const uint32_t cam02 = Crc::GenerateCRCFromString("mainmenu_camera02");
    FO::Placement p1 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *MB::ViewFor(1), kNoOffset);
    FO::Placement p2 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *MB::ViewFor(2), kNoOffset);
    bool ok = true;
    auto fail = [&](const char* where) {
        if (ok)
            Log("  front-end objects (game state): failed at: %s", where);
        ok = false;
    };

    {
        SettingScope scope(1);
        // Every condition but one: nothing, however often.
        for (int c = 0; c < 4; ++c) {
            g_gs_career[0x630] = c == 0 ? 15 : 0;
            SetBackdrop(c != 1);
            g_gs_anim = c == 2 ? Crc::GenerateCRCFromString("skater_select_cam") : cam02;
            g_gs_viewport = c == 3 ? nullptr : reinterpret_cast<Gfx::Camera*>(viewportMemory);
            for (int k = 0; k < 3; ++k)
                FO::Update(true);
            if (!Same(ring, sRing) || FO::Applied())
                fail("a condition missing");
        }
        g_gs_viewport = reinterpret_cast<Gfx::Camera*>(viewportMemory);
        // All: the camera's object's pose, sampled once a frame by whichever step comes first (the frames counted
        // by the renders), the same twice in a row: moved.
        FO::Update(true);  // frame 0: a sample
        FO::Update(true);  // (the same frame: none)
        FO::Update(false); // (its render: none)
        if (!Same(ring, sRing) || FO::Applied())
            fail("before the second sample");
        FO::Update(true); // frame 1: the second
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p1)))
            fail("moved");
        // Another camera (Create-A-Skater): back.
        ClearTeleported(ring);
        g_gs_anim = Crc::GenerateCRCFromString("skater_select_cam");
        FO::Update(false);
        if (!(PoseRestored(ring, sRing) && !FO::Applied()))
            fail("another camera");
        g_gs_anim = cam02;
        FO::Update(false);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p1)))
            fail("the menu camera again");
        // A level (15): back, the poses forgotten.
        ClearTeleported(ring);
        g_gs_career[0x630] = 15;
        FO::Update(true);
        if (!(PoseRestored(ring, sRing) && !FO::Applied() && FO::CameraPoseCount() == 0))
            fail("a level");
        g_gs_career[0x630] = 0;
    }
    {
        SettingScope scope(2);
        FO::Update(true);
        FO::Update(false); // (the next frame)
        FO::Update(true);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p2)))
            fail("view 2");
    }
    {
        // The setting off: back, and nothing again.
        ClearTeleported(ring);
        SettingScope scope(0);
        FO::Update(true);
        if (!(PoseRestored(ring, sRing) && !FO::Applied()))
            fail("off");
        ClearTeleported(ring);
        for (int k = 0; k < 3; ++k)
            FO::Update(true);
        if (!Same(ring, sRing) || FO::Applied())
            fail("off: nothing");
    }
    // Renders only (no object update walk): sampled by the renders.
    {
        SettingScope scope(1);
        ClearTeleported(ring);
        g_gs_career[0x630] = 15;
        FO::Update(false); // (a level: the poses forgotten)
        g_gs_career[0x630] = 0;
        FO::Update(false);
        if (FO::Applied())
            fail("renders only: one sample");
        FO::Update(false);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p1)))
            fail("renders only");
    }
    // No camera component owning the viewport camera: the pose the game gave the viewport camera's setters (noted
    // before the held view replaces it).
    {
        SettingScope scope(1);
        ClearTeleported(ring);
        g_gs_career[0x630] = 15;
        FO::Update(true);
        g_gs_career[0x630] = 0;
        if (!PoseRestored(ring, sRing))
            fail("setters: restored");
        g_gs_cameraComponent = nullptr;
        MB::ForgetGameCameraPose();
        FO::Update(false);
        FO::Update(false);
        if (FO::Applied())
            fail("setters: nothing noted");
        Gfx::Camera* viewport = g_gs_viewport;
        Mth::Vector pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
        Mth::Matrix m = {};
        m.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
        m.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
        m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        viewport->SetPos(pos); // (a position alone isn't a pose)
        FO::Update(false);
        FO::Update(false);
        if (FO::Applied())
            fail("setters: a position only");
        viewport->SetMatrix(m);
        // (held: the camera shows the view; the game's pose noted)
        if (!(viewport->m_pos.x == MB::ViewFor(1)->pos[0]))
            fail("setters: held");
        FO::Update(false);
        viewport->SetPos(pos);
        viewport->SetMatrix(m);
        FO::Update(true);
        if (!(FO::Applied() && MovedBy(ring.Get(), sRing, p1)))
            fail("setters");
        // Another camera than the viewport's, or not held: not noted.
        Mth::Vector noted;
        Mth::Matrix notedMatrix;
        alignas(16) static uint8_t otherMemory[0xC4];
        reinterpret_cast<Gfx::Camera*>(otherMemory)->SetPos({1.0f, 2.0f, 3.0f, 1.0f});
        if (!(MB::GameCameraPose(&noted, &notedMatrix) && noted.x == kCamPos[0]))
            fail("setters: another camera");
        g_gs_anim = Crc::GenerateCRCFromString("skater_select_cam");
        viewport->SetPos({4.0f, 5.0f, 6.0f, 1.0f});
        if (!(MB::GameCameraPose(&noted, &notedMatrix) && noted.x == kCamPos[0]))
            fail("setters: not held");
        g_gs_anim = cam02;
        FO::OnBackdropLoaded();
        if (MB::GameCameraPose(&noted, &notedMatrix))
            fail("setters: forgotten");
        g_gs_cameraComponent = cameraComponent.Get();
    }
    // No skate module (CurrentLevel 0): the scene list decides; no camera animation: nothing.
    {
        SettingScope scope(1);
        GameVariable<uint8_t*>(0x007CE478) = nullptr;
        FO::Update(true); // (restores what the blocks above moved)
        if (!PoseRestored(ring, sRing))
            fail("no skate module: restored");
        ClearTeleported(ring);
        for (int k = 0; k < 3; ++k)
            FO::Update(true);
        if (!Same(ring, sRing) || FO::Applied())
            fail("no skate module");
    }
    Check(ok, "front-end objects: the game's state read (setting, level, backdrop, menu camera, camera object)");
    Cleanup();
}

// --- the front end's load as the game does it -------------------------------------------------------------------

// LoadScene's callees stood in: structures (strings and flags kept per address), the texture dictionary and the
// scene file (each loaded scene added to the stand-in scene list, by its name's checksum).
struct LsStruct {
    std::map<uint32_t, std::string> strings;
    std::set<uint32_t> flags;
};
std::map<uint32_t, LsStruct> g_lq_structs;
std::vector<uint32_t> g_lq_loaded;
uint8_t g_lq_scenes[8][0x90];
uint32_t g_lq_nodes[9][4];
constexpr uint32_t kSceneParam = 0x26861025;

void SetLoadedScenes()
{
    size_t n = g_lq_loaded.size();
    for (size_t i = 0; i < n; ++i) {
        std::memset(g_lq_scenes[i], 0, sizeof(g_lq_scenes[i]));
        std::memcpy(g_lq_scenes[i] + 0x84, &g_lq_loaded[i], 4);
        g_lq_nodes[i][1] = reinterpret_cast<uint32_t>(g_lq_scenes[i]);
        g_lq_nodes[i][3] = reinterpret_cast<uint32_t>(g_lq_nodes[i + 1]);
    }
    g_lq_nodes[n][1] = 0xFFFFFFFF;
    GameVariable<uint32_t>(0x006FA7EC) = reinterpret_cast<uint32_t>(g_lq_nodes[0]);
}

void InstallLoadStandIns(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4,
         [](Regs& r) { return g_lq_structs[r.ecx].flags.count(r.args[0]) ? 1u : 0u; });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             auto& strings = g_lq_structs[r.ecx].strings;
             auto it = strings.find(r.args[0]);
             if (it == strings.end())
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = it->second.c_str();
             return 1u;
         });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        g_lq_structs[r.ecx] = {};
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x1717;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0;
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        g_lq_structs[r.ecx] = {};
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             if (r.args[0] == 0)
                 g_lq_structs[r.ecx].flags.insert(r.args[1]);
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             g_lq_structs[r.ecx].strings[r.args[0]] = reinterpret_cast<const char*>(r.args[1]);
             return 0u;
         });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs&) { return 0u; });
    s.At({0x0049F0C0}, 0, [](Regs&) { return 0x7E000010u; });
    s.At({0x0048CCB0}, 0, [](Regs& r) {
        g_lq_loaded.push_back(Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0])));
        SetLoadedScenes();
        return 0x7D000000u;
    });
}

// The front end loading as in the game: load_level's LoadScene "mainmenu" (OnLevelChange at its start), which
// loads LA_Sky and LA through LoadScene again from MenuBackdrop::OnSceneLoaded (each starting with OnLevelChange),
// then OnBackdropLoaded; then frames go by with the main menu's camera: the objects must be moved - also when only
// renders step (no object walk), as with the game's frame order not known for sure.
void TestFrontEndObjectsLoadSequence()
{
    FO::Reset();
    g_feobj_saved = {{0x007CE478, 4, {}}, {0x006FA7EC, 4, {}}, {0x006F0FB8, 4, {}}, {0x00701348, 4, {}}};
    SaveAll(g_feobj_saved);
    for (void*& slot : g_emptyVtable)
        slot = reinterpret_cast<void*>(0x005480A0);
    const uint32_t kRing = Crc::GenerateCRCFromString("wol_inner_ring");
    static ObjectMemory ring, camera;
    static ComponentMemory cameraComponent;
    alignas(16) static uint8_t viewportMemory[0xC4];
    FillObject(ring, kCompositeVtable, kRing, 2);
    FillObject(camera, kCompositeVtable, 0x967C138C, 0);
    camera.Get()->m_pos = {kCamPos[0], kCamPos[1], kCamPos[2], 1.0f};
    camera.Get()->m_matrix.row[2] = {kCamBack[0], kCamBack[1], kCamBack[2], 0.0f};
    camera.Get()->m_matrix.row[1] = {kCamUp[0], kCamUp[1], kCamUp[2], 0.0f};
    std::memset(cameraComponent.bytes, 0, sizeof(cameraComponent.bytes));
    cameraComponent.Get()->m_type = 0xC4E311FA;
    cameraComponent.Get()->m_object = camera.Get();
    *reinterpret_cast<void**>(cameraComponent.bytes + 0x18) = viewportMemory;
    g_gs_viewport = reinterpret_cast<Gfx::Camera*>(viewportMemory);
    g_gs_cameraComponent = cameraComponent.Get();
    g_foundCount = 0;
    SetFound(kRing, ring.Get());
    std::vector<uint8_t> sRing = Snapshot(ring);

    auto* s = new StandIns;
    InstallLoadStandIns(*s);
    s->At({0x0057B030}, 0, [](Regs&) { return g_gs_anim; });
    s->At({0x004A09C0}, 0, [](Regs& r) { return r.args[0] == 0 ? reinterpret_cast<uint32_t>(g_gs_viewport) : 0u; });
    s->At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0,
          [](Regs& r) { return reinterpret_cast<uint32_t>(FoundLookup(r.args[0])); });
    s->At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        return r.args[0] == 0xC4E311FA ? reinterpret_cast<uint32_t>(g_gs_cameraComponent) : 0u;
    });
    std::memset(g_gs_skate, 0, sizeof(g_gs_skate));
    std::memset(g_gs_career, 0, sizeof(g_gs_career));
    *reinterpret_cast<uint8_t**>(g_gs_skate + 0x20) = g_gs_career;
    *reinterpret_cast<uint32_t*>(g_gs_skate + 0x368) = 0x0BADF00D;
    GameVariable<uint8_t*>(0x007CE478) = g_gs_skate;
    GameVariable<uint8_t*>(0x006F0FB8) = g_gs_manager;
    FO::Placement p1 = FO::MakePlacement(kCamPos, kCamBack, kCamUp, *MB::ViewFor(1), kNoOffset);
    bool ok = true;
    int savedDebug = FO::DebugBackdrop();
    for (int walks = 1; walks >= 0 && ok; --walks) {
        SettingScope scope(1);
        FO::DebugBackdrop() = walks ? 0 : 1; // (the renders-only run logged)
        FO::Reset();
        std::memcpy(ring.bytes, sRing.data(), sRing.size());
        g_gs_anim = 0; // (no camera animation while loading)
        g_lq_loaded.clear();
        SetLoadedScenes();
        // Frames before the load (the loading screen).
        if (walks)
            FO::Update(true);
        FO::Update(false);
        alignas(4) static uint8_t params[8];
        g_lq_structs.clear();
        g_lq_structs[reinterpret_cast<uint32_t>(params)].strings[kSceneParam] = "mainmenu";
        Nx::CFunc_LoadScene(reinterpret_cast<Script::CStruct*>(params), nullptr);
        const uint32_t expected[] = {MB::kFrontEndSceneName, MB::kBackdropSkyName, MB::kBackdropSceneName};
        ok = ok && g_lq_loaded.size() == 3 && std::equal(g_lq_loaded.begin(), g_lq_loaded.end(), expected);
        ok = ok && !FO::HoldingOff();
        if (!ok)
            Log("  front-end objects (load): %u scenes loaded, held off %d", static_cast<unsigned>(g_lq_loaded.size()),
                FO::HoldingOff());
        // The main menu's camera starts; frames go by (walk if any, then render).
        g_gs_anim = Crc::GenerateCRCFromString("mainmenu_camera02");
        for (int frame = 0; frame < 3; ++frame) {
            if (walks)
                FO::Update(true);
            FO::Update(false);
        }
        ok = ok && FO::Applied() && MovedBy(ring.Get(), sRing, p1);
        if (!ok)
            Log("  front-end objects (load, %s): applied %d, %d moved", walks ? "walks and renders" : "renders only",
                FO::Applied(), FO::MovedCount());
    }
    FO::DebugBackdrop() = savedDebug;
    Check(ok, "front-end objects: the front end's load as in the game (nested LoadScene), then frames: moved");
    Cleanup();
}

} // namespace

SELFTEST(Early, TestFrontEndObjectsLoadSequence, Cleanup);
SELFTEST(Early, TestFrontEndPlacement);
SELFTEST(Early, TestFrontEndObjectStates, Cleanup);
SELFTEST(Early, TestFrontEndObjectHooks, Cleanup);
SELFTEST(Early, TestFrontEndObjectsGameState, Cleanup);
