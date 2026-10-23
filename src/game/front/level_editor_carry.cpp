// NOT ORIGINAL - the level editor's carrying (ours). See level_editor_carry.h.

#include "game/front/level_editor_carry.h"

#include <algorithm>
#include <cmath>

#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Carry {

// --- the pure parts -----------------------------------------------------------------------------------------------

Placement Inverse(const Placement& placement)
{
    // y = Q (S x) + o  =>  x = S^-1 Q^-1 (y - o) = Q^-1 (T y) + Q^-1 (T (-o)), T = S^-1 seen through Q (a per-axis
    // scale - level_editor_scale.h: S^-1 Q^-1 = Q^-1 T, odd turns swapping its x and z)
    V3 s = placement.scale;
    V3 reciprocal = {s.x > 0.0f ? 1.0f / s.x : 1.0f, s.y > 0.0f ? 1.0f / s.y : 1.0f, s.z > 0.0f ? 1.0f / s.z : 1.0f};
    int quarters = (4 - (placement.quarters & 3)) & 3;
    V3 t = TurnedScale(reciprocal, placement.quarters);
    Placement inverse;
    inverse.quarters = quarters;
    inverse.scale = t;
    inverse.offset = QuarterTurn(PerAxis(placement.offset * -1.0f, t), quarters);
    return inverse;
}

Placement Between(const Placement& from, const Placement& to) { return Compose(to, Inverse(from)); }

bool IsNone(const Placement& step)
{
    // (a per-axis scale's reciprocal isn't exact - 1 / 1.5 -: Between a placement and itself can be a step of
    // 1.0000001; within a millionth and a ten-thousandth of a unit counts as none)
    auto unit = [](float s) { return std::fabs(s - 1.0f) < 1e-6f; };
    auto zero = [](float v) { return std::fabs(v) < 1e-4f; };
    return (step.quarters & 3) == 0 && unit(step.scale.x) && unit(step.scale.y) && unit(step.scale.z) &&
           zero(step.offset.x) && zero(step.offset.y) && zero(step.offset.z);
}

bool OverFootprint(const OrientedBox& box, V3 p, float margin, float above)
{
    V3 d = p - box.origin;
    float local[3];
    for (int i = 0; i < 3; ++i) {
        float length2 = Dot(box.axes[i], box.axes[i]);
        if (!(length2 > 0.0f))
            return false;
        // (local units are the box's: a margin in world units is margin / |axis| of them)
        local[i] = Dot(d, box.axes[i]) / length2;
    }
    float lengths[3] = {std::sqrt(Dot(box.axes[0], box.axes[0])), std::sqrt(Dot(box.axes[1], box.axes[1])),
                        std::sqrt(Dot(box.axes[2], box.axes[2]))};
    auto within = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
    return within(local[0], box.min.x - margin / lengths[0], box.max.x + margin / lengths[0]) &&
           within(local[2], box.min.z - margin / lengths[2], box.max.z + margin / lengths[2]) &&
           within(local[1], box.min.y - margin / lengths[1], box.max.y + above / lengths[1]);
}

Attachment Attached(uint32_t cluster, const std::vector<uint32_t>& links, uint32_t groundHit,
                    const std::vector<uint32_t>& members, const std::vector<uint32_t>& memberClusters)
{
    auto in = [](const std::vector<uint32_t>& list, uint32_t id) {
        return id != 0 && std::find(list.begin(), list.end(), id) != list.end();
    };
    if (in(members, cluster) || in(memberClusters, cluster))
        return Attachment::kCluster;
    for (uint32_t link : links) {
        if (in(members, link))
            return Attachment::kLink;
    }
    if (in(members, groundHit))
        return Attachment::kStanding;
    return Attachment::kNone;
}

float TurnedHeading(float heading, int quarters)
{
    return NodeEdit::WrapAngle(heading + static_cast<float>(quarters & 3) * 1.57079633f);
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCluster = 0x1A3A966Bu;    // "Cluster" (a node's TrickObject Cluster=)
constexpr uint32_t kClass = 0x12B4E660u;      // Class
constexpr uint32_t kPedestrian = 0xA0DFAC98u; // Pedestrian

bool g_settingRead, g_enabled = true;

struct Carried {
    std::vector<uint32_t> objects; // (ids)
    std::vector<uint32_t> nodes;   // (names)
    Taken taken;                   // (their places when gathered)
    uint32_t reference = 0;        // (the selection's first piece)
    Placement last;                // (its placement when last followed)
    bool armed = false;
    std::vector<uint32_t> noted;   // (objects whose first place was noted)
};
Carried g_carried;

Placement PlacementOf(uint32_t sector)
{
    Placement placement = {0, {0.0f, 0.0f, 0.0f}, 1.0f};
    Scenery::PlacementNow(sector, &placement);
    return placement;
}

// The game's collision ray (as level_editor_scenery.cpp's CastStatic: a CFeeler, constructor 0x00417760, cast
// 0x00417710 - static collision only; the collision object hit at +0x54, its checksum at +0x58): what it meets first.
uint32_t FirstHit(V3 from, V3 to, V3* point)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return 0;
    if (!*reinterpret_cast<void* const*>(feeler + 0x54))
        return 0;
    const float* hit = reinterpret_cast<const float*>(feeler + 0x34);
    *point = {hit[0], hit[1], hit[2]};
    return *reinterpret_cast<const uint32_t*>(feeler + 0x58);
}

Script::CStruct* NodeStruct(uint32_t name, const Nodes::Node** out)
{
    int index = name ? Nodes::Find(name) : -1;
    if (index < 0)
        return nullptr;
    const Nodes::Node& node = Nodes::All()[index];
    if (out)
        *out = &node;
    return Nodes::StructOf(node);
}

uint32_t ClusterOf(uint32_t name)
{
    uint32_t cluster = 0;
    if (Script::CStruct* s = NodeStruct(name, nullptr))
        s->GetChecksum(kCluster, &cluster, 0);
    return cluster;
}

std::vector<uint32_t> LinkNames(const Nodes::Node* node)
{
    std::vector<uint32_t> names;
    if (!node)
        return names;
    const std::vector<Nodes::Node>& all = Nodes::All();
    for (int link : node->links) {
        if (link >= 0 && link < static_cast<int>(all.size()))
            names.push_back(all[link].name);
    }
    return names;
}

struct Gather {
    std::vector<uint32_t> members, clusters;
    std::vector<std::pair<uint32_t, OrientedBox>> boxes; // (each member's but shadows')
    std::vector<uint32_t> objects;
};

// What the ground ray below `p` meets first, when `p` is over one of the members' footprints (0: not over one, or
// nothing below). A piece whose collision is still in a collision-only sector (level_editor_shared_coll.h: taken along
// only at its first move - TR's SkWare_Rail01's faces in SkWare_Main_1) counts when the hit is within its box grown by
// the shared collision's 6 units.
uint32_t GroundUnder(const Gather& g, V3 p)
{
    bool over = std::any_of(g.boxes.begin(), g.boxes.end(),
                            [&](const auto& box) { return OverFootprint(box.second, p, kMargin, kAbove); });
    if (!over)
        return 0;
    V3 point = {};
    uint32_t hit = FirstHit({p.x, p.y + kRayUp, p.z}, {p.x, p.y - kRayDown, p.z}, &point);
    if (!hit || std::find(g.members.begin(), g.members.end(), hit) != g.members.end() || Scenery::SectorGeometry(hit))
        return hit;
    for (const auto& box : g.boxes) {
        if (OverFootprint(box.second, point, 6.0f, 6.0f))
            return box.first;
    }
    return hit;
}

Attachment AttachmentOf(const Gather& g, uint32_t name, V3 pos)
{
    const Nodes::Node* node = nullptr;
    uint32_t cluster = 0;
    if (Script::CStruct* s = NodeStruct(name, &node))
        s->GetChecksum(kCluster, &cluster, 0);
    std::vector<uint32_t> links = LinkNames(node);
    Attachment a = Attached(cluster, links, 0, g.members, g.clusters);
    if (a != Attachment::kNone)
        return a;
    return Attached(0, {}, GroundUnder(g, pos), g.members, g.clusters);
}

void __cdecl GatherObject(Obj::CObject* object, void* data)
{
    auto* g = static_cast<Gather*>(data);
    auto* composite = static_cast<Obj::CCompositeObject*>(object);
    OrientedBox box;
    if (!Objects::ModelBox(composite, &box))
        return;
    uint32_t cls = 0;
    const Nodes::Node* node = nullptr;
    if (Script::CStruct* s = NodeStruct(composite->m_id, &node))
        s->GetChecksum(kClass, &cls, 0);
    if (cls == kPedestrian)
        return; // (peds walk their paths: not carried)
    if (node && node->family == Nodes::Family::Flag)
        return; // (a flag's object: its node is carried, and moves it)
    if (AttachmentOf(*g, composite->m_id, Objects::PosOf(composite)) != Attachment::kNone)
        g->objects.push_back(composite->m_id);
}

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

} // namespace

bool Enabled()
{
    if (!g_settingRead) {
        g_settingRead = true;
        g_enabled = ConfigInt("editor", "carry", 1) != 0;
    }
    return g_enabled;
}

void SetEnabled(bool on)
{
    Enabled();
    g_enabled = on;
    ConfigSetInt("editor", "carry", on ? 1 : 0);
    if (!on)
        Disarm();
}

Taken Arm()
{
    g_carried = Carried{};
    const std::vector<uint32_t>& selection = Scenery::Selection();
    if (!Enabled() || selection.empty())
        return {};
    Gather g;
    g.members = selection;
    for (uint32_t member : selection) {
        if (uint32_t cluster = ClusterOf(member))
            g.clusters.push_back(cluster);
        OrientedBox box;
        if (Scenery::Box(member, &box) && !Scenery::IsShadow(member))
            g.boxes.push_back({member, box});
    }
    if (Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager())
        manager->ForEach(GatherObject, &g);
    g_carried.objects = g.objects;
    for (uint32_t id : g_carried.objects) {
        if (Obj::CCompositeObject* object = LiveObject(id))
            g_carried.taken.objects.push_back(Objects::Save(object));
    }
    for (const Nodes::Node& node : Nodes::All()) {
        if (!node.name || node.hidden || !NodeEdit::Movable(node) || NodeEdit::IsChain(node.name))
            continue;
        NodeEdit::Values values;
        if (!NodeEdit::Read(node.name, &values))
            continue;
        if (AttachmentOf(g, node.name, values.pos) != Attachment::kNone &&
            std::find(g_carried.nodes.begin(), g_carried.nodes.end(), node.name) == g_carried.nodes.end())
            g_carried.nodes.push_back(node.name);
    }
    g_carried.taken.nodes = NodeEdit::Capture(g_carried.nodes, NodeEdit::Read);
    g_carried.reference = selection.front();
    g_carried.last = PlacementOf(g_carried.reference);
    g_carried.armed = true;
    if (DebugEnabled() && (!g_carried.objects.empty() || !g_carried.nodes.empty()))
        Log("level editor: carrying %u object%s and %u node%s with the selection",
            static_cast<unsigned>(g_carried.objects.size()), g_carried.objects.size() == 1 ? "" : "s",
            static_cast<unsigned>(g_carried.nodes.size()), g_carried.nodes.size() == 1 ? "" : "s");
    return g_carried.taken;
}

void Follow(void (*onNode)(uint32_t name))
{
    if (!g_carried.armed || (g_carried.objects.empty() && g_carried.nodes.empty()))
        return;
    Placement now = PlacementOf(g_carried.reference);
    Placement step = Between(g_carried.last, now);
    g_carried.last = now;
    if (IsNone(step))
        return;
    float angle = static_cast<float>(step.quarters & 3) * 1.57079633f;
    for (uint32_t id : g_carried.objects) {
        Obj::CCompositeObject* object = LiveObject(id);
        if (!object)
            continue;
        if (std::find(g_carried.noted.begin(), g_carried.noted.end(), id) == g_carried.noted.end()) {
            Objects::NoteOrigin(object);
            Objects::Touch(id);
            g_carried.noted.push_back(id);
        }
        V3 to = Apply(step, Objects::PosOf(object));
        if (step.quarters & 3)
            Objects::TurnAbout(object, Objects::PosOf(object), angle); // (its heading; turned about itself)
        Objects::Place(object, to);
    }
    for (uint32_t name : g_carried.nodes) {
        NodeEdit::Values values;
        if (!NodeEdit::Read(name, &values))
            continue;
        values.pos = Apply(step, values.pos);
        if (step.quarters & 3)
            values.angles.y = TurnedHeading(values.angles.y, step.quarters);
        NodeEdit::Set(name, values);
        if (onNode)
            onNode(name);
    }
}

void Leave()
{
    if (!g_carried.armed)
        return;
    for (const Objects::State& state : g_carried.taken.objects)
        Objects::Restore(state);
    NodeEdit::Put(g_carried.taken.nodes, [](uint32_t name, const NodeEdit::Values& values) {
        NodeEdit::Values now;
        if (NodeEdit::Read(name, &now) && !NodeEdit::Same(now, values))
            NodeEdit::Set(name, values);
    });
    g_carried = Carried{};
}

void Disarm() { g_carried = Carried{}; }

bool Armed() { return g_carried.armed; }

int Count() { return g_carried.armed ? static_cast<int>(g_carried.objects.size() + g_carried.nodes.size()) : 0; }

std::vector<uint32_t> CarriedObjects() { return g_carried.armed ? g_carried.objects : std::vector<uint32_t>{}; }

std::vector<uint32_t> CarriedNodes() { return g_carried.armed ? g_carried.nodes : std::vector<uint32_t>{}; }

} // namespace Front::LevelEditor::Carry
