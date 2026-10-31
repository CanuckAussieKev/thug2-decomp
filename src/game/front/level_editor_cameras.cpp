// NOT ORIGINAL - the level editor's camera nodes (ours). See level_editor_cameras.h.

#include "game/front/level_editor_cameras.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>

#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/mth/vector.h"
#include "game/obj/movie_cfuncs.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Cameras {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265f;

// Numbers separated by commas, the whole text (as the layout's other records read them).
bool ReadNumbers(const std::string& text, float* out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(out[i]))
            return false;
        at = end;
        if (i + 1 < count) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    return *at == '\0';
}

bool ReadTriple(const std::string& text, V3* out)
{
    float v[3];
    if (!ReadNumbers(text, v, 3))
        return false;
    *out = {v[0], v[1], v[2]};
    return true;
}

} // namespace

float ClampDistance(float d)
{
    if (!(d == d))
        return kDefaultDistance;
    return std::clamp(d, kMinDistance, kMaxDistance);
}

// The game's angles-to-matrix (0x0041A730): the identity times RotationX(x), RotationY(y), RotationZ(z) (D3DX's, row
// vectors) - its at row (sin y cos x, -sin x, cos y cos x) for z 0. The editor's view looks along (sin yaw cos pitch,
// sin pitch, cos yaw cos pitch): x = -pitch, y = yaw.
V3 AnglesFor(float yaw, float pitch) { return {-pitch, yaw, 0.0f}; }

void YawPitchOf(V3 angles, float* yaw, float* pitch)
{
    *yaw = angles.y;
    *pitch = -angles.x;
}

V3 Forward(V3 angles)
{
    // (RotationZ last: it turns that row about the world's z axis - the editor's cameras keep z 0)
    float cx = std::cos(angles.x), sx = std::sin(angles.x);
    V3 v = {std::sin(angles.y) * cx, -sx, std::cos(angles.y) * cx};
    float cz = std::cos(angles.z), sz = std::sin(angles.z);
    return {v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z};
}

V3 TargetOf(V3 pos, V3 angles, float distance) { return pos + Forward(angles) * ClampDistance(distance); }

V3 PositionOffset(V3 pos, V3 angles, float distance) { return pos - TargetOf(pos, angles, distance); }

int ShotFrames(float seconds)
{
    if (!std::isfinite(seconds))
        seconds = kShotDefaultSeconds;
    seconds = std::max(kShotMinSeconds, std::min(kShotMaxSeconds, seconds));
    return static_cast<int>(std::lround(seconds * 60.0f));
}

void FrustumCorners(V3 pos, V3 angles, float tanX, float tanY, float depth, V3 out[4])
{
    float yaw, pitch;
    YawPitchOf(angles, &yaw, &pitch);
    Frame f = FrameFromYawPitch(yaw, pitch);
    V3 centre = pos - f.back * depth;
    V3 right = f.right * (tanX * depth), up = f.up * (tanY * depth);
    out[0] = centre + right + up;
    out[1] = centre - right + up;
    out[2] = centre - right - up;
    out[3] = centre + right - up;
}

bool Same(const Camera& a, const Camera& b)
{
    auto same = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.name == b.name && same(a.pos, b.pos) && same(a.angles, b.angles) && a.distance == b.distance;
}

std::string WriteRecord(const Camera& c)
{
    char text[320];
    std::snprintf(text, sizeof(text), "camera %s pos=%.9g,%.9g,%.9g angles=%.9g,%.9g,%.9g dist=%.9g", c.name.c_str(),
                  c.pos.x, c.pos.y, c.pos.z, c.angles.x, c.angles.y, c.angles.z, c.distance);
    return text;
}

bool ReadRecord(std::istringstream& in, Camera* out)
{
    Camera c;
    if (!(in >> c.name) || !NodeCreate::ValidName(c.name))
        return false;
    bool havePos = false, haveAngles = false, haveDistance = false;
    std::string field;
    while (in >> field) {
        size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        std::string key = field.substr(0, equals), value = field.substr(equals + 1);
        if (key == "pos") {
            if (havePos || !ReadTriple(value, &c.pos))
                return false;
            havePos = true;
        } else if (key == "angles") {
            if (haveAngles || !ReadTriple(value, &c.angles))
                return false;
            haveAngles = true;
        } else if (key == "dist") {
            if (haveDistance || !ReadNumbers(value, &c.distance, 1))
                return false;
            c.distance = ClampDistance(c.distance);
            haveDistance = true;
        } // (another key: a later version's - skipped)
    }
    if (!havePos)
        return false;
    *out = c;
    return true;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

std::map<std::string, float> g_distances; // (each camera's target distance, by name)
std::vector<std::function<void()>> g_queue;
// A field being dragged (pitch / distance): the cameras before its first change (one undo step).
bool g_dragging;

bool SameText(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

float DistanceOf(const std::string& name)
{
    auto it = g_distances.find(name);
    return it == g_distances.end() ? kDefaultDistance : it->second;
}

NodeCreate::Spec SpecOf(const Camera& c)
{
    NodeCreate::Spec n;
    n.name = c.name;
    n.cls = "GenericNode";
    n.type = "ed_camera";
    n.pos = c.pos;
    n.angles = c.angles;
    auto vector = [&](const char* key, V3 v) {
        NodeCreate::Field f = {NodeCreate::Field::kVector, key};
        f.value[0] = v.x;
        f.value[1] = v.y;
        f.value[2] = v.z;
        n.extra.push_back(f);
    };
    vector("targetOffset", TargetOf(c.pos, c.angles, c.distance));
    vector("positionOffset", PositionOffset(c.pos, c.angles, c.distance));
    n.owned = true;
    return n;
}

// The whole state (the cameras and their distances) - an undo step puts it back.
struct Step : ModuleStep {
    std::vector<Camera> cameras;
    void Restore() override
    {
        std::vector<Camera> now = All();
        // (those not in the step: removed; those in it: made, or put where the step has them)
        for (const Camera& c : now) {
            bool keep = std::any_of(cameras.begin(), cameras.end(), [&](const Camera& k) { return SameText(k.name, c.name); });
            if (!keep) {
                NodeCreate::Spec removed;
                int at = -1;
                NodeCreate::Remove(NodeCreate::Checksum(c.name), &removed, &at);
            }
        }
        std::string message;
        for (const Camera& c : cameras) {
            g_distances[c.name] = c.distance;
            uint32_t name = NodeCreate::Checksum(c.name);
            if (!NodeCreate::IsAdded(name)) {
                NodeCreate::Create(SpecOf(c), -1, &message);
            } else {
                NodeEdit::Values values;
                values.pos = c.pos;
                values.angles = c.angles;
                NodeEdit::Set(name, values);
            }
        }
    }
    std::shared_ptr<ModuleStep> Now() const override
    {
        auto now = std::make_shared<Step>();
        now->cameras = All();
        return now;
    }
};

void PushUndo()
{
    auto step = std::make_shared<Step>();
    step->cameras = All();
    PushModuleUndo(step);
}

const Camera* Find(const std::vector<Camera>& all, uint32_t name)
{
    for (const Camera& c : all)
        if (NodeCreate::Checksum(c.name) == name)
            return &c;
    return nullptr;
}

// The node's offsets written when they're not those of its place (it was moved / turned since).
void KeepOffsets(const Camera& c)
{
    Script::CStruct* node = SkateScript::NodeManagerInstance() ? SkateScript::GetNode(NodeCreate::Checksum(c.name), false)
                                                               : nullptr;
    if (!node)
        return;
    struct Key {
        uint32_t key;
        V3 value;
    } keys[2] = {{Fields::NameChecksum("targetOffset"), TargetOf(c.pos, c.angles, c.distance)},
                 {Fields::NameChecksum("positionOffset"), PositionOffset(c.pos, c.angles, c.distance)}};
    for (const Key& k : keys) {
        Fields::Own own = Fields::OwnIn(node, k.key, false);
        bool same = own.present && own.value.kind == Fields::Kind::Vector && own.value.numbers[0] == k.value.x &&
                    own.value.numbers[1] == k.value.y && own.value.numbers[2] == k.value.z;
        if (same)
            continue;
        Fields::Own put = own;
        put.present = true;
        put.value = Fields::Value{};
        put.value.kind = Fields::Kind::Vector;
        put.value.numbers[0] = k.value.x;
        put.value.numbers[1] = k.value.y;
        put.value.numbers[2] = k.value.z;
        Fields::PutIn(node, k.key, false, put);
    }
}

bool LookThrough(const Camera& c)
{
    float yaw, pitch;
    YawPitchOf(c.angles, &yaw, &pitch);
    LookFrom(c.pos, yaw, pitch);
    Hud::Message("Viewing through " + c.name);
    return true;
}

} // namespace

std::vector<Camera> All()
{
    std::vector<Camera> out;
    for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
        if (!SameText(spec.type, "ed_camera") || !SameText(spec.cls, "GenericNode"))
            continue;
        Camera c;
        c.name = spec.name;
        c.pos = spec.pos;
        c.angles = spec.angles;
        NodeEdit::Values values;
        if (NodeEdit::Read(NodeCreate::Checksum(spec.name), &values)) {
            c.pos = values.pos;
            c.angles = values.angles;
        }
        c.distance = DistanceOf(spec.name);
        out.push_back(c);
    }
    return out;
}

namespace {

uint32_t Crc(const char* text) { return Fields::NameChecksum(text); }

// When the last shot ends (steady clock seconds; 0: none played).
double g_shotUntil;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using CFunction = bool(__cdecl*)(Script::CStruct*, Script::CScript*);

// The offsets of the camera `name` (false: no such node).
bool ShotOffsets(uint32_t name, V3* target, V3* position)
{
    std::vector<Camera> all = All();
    if (const Camera* c = Find(all, name)) {
        *target = TargetOf(c->pos, c->angles, c->distance);
        *position = PositionOffset(c->pos, c->angles, c->distance);
        return true;
    }
    Script::CStruct* node = SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
    if (!node)
        return false;
    Script::VectorValue t, p;
    if (node->GetVector(Crc("targetOffset"), &t, 0) && node->GetVector(Crc("positionOffset"), &p, 0)) {
        *target = {t.x, t.y, t.z};
        *position = {p.x, p.y, p.z};
        return true;
    }
    Mth::Vector at;
    if (!SkateScript::GetPosition(node, &at))
        return false;
    Script::VectorValue a = {};
    node->GetVector(Crc("Angles"), &a, 0);
    V3 pos = {at.x, at.y, at.z}, angles = {a.x, a.y, a.z};
    *target = TargetOf(pos, angles, kDefaultDistance);
    *position = PositionOffset(pos, angles, kDefaultDistance);
    return true;
}

} // namespace

bool __cdecl CFunc_PlayCamera(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    if (!params || !params->GetChecksum(Crc("camera"), &name, 0) || !name) {
        Log("level editor: LevelEditor_PlayCamera needs camera=<node>");
        return false;
    }
    if (Active())
        return false; // (the editor's own camera; Look through previews a camera there)
    V3 target, position;
    if (!ShotOffsets(name, &target, &position)) {
        Log("level editor: LevelEditor_PlayCamera - no node %s", Nodes::Label(name).c_str());
        return false;
    }
    float seconds = kShotDefaultSeconds;
    int whole = 0;
    if (!params->GetFloat(Crc("time"), &seconds, 0) && params->GetInteger(Crc("time"), &whole, 0))
        seconds = static_cast<float>(whole);
    const uint32_t shot = Crc("ed_camera_shot");
    double now = Now();
    if (now < g_shotUntil) {
        // (the last shot still playing: killed first, as the goals kill theirs)
        Script::CStruct* kill = Script::NewStruct()->Init();
        kill->AddChecksum(Crc("name"), shot);
        CFuncs::CFunc_KillSkaterCamAnim(kill, script);
        kill->Clear();
        Script::DeleteStruct(kill);
    }
    // The game's own virtual_cam, as view_goals_menu.qb's play_single_view_goals_cam plays a goal's camera.
    Script::CStruct* anim = Script::NewStruct()->Init();
    anim->AddChecksum(Crc("name"), shot);
    anim->AddChecksum(0u, Crc("virtual_cam"));
    anim->AddChecksum(Crc("targetid"), Crc("world"));
    anim->AddVector(Crc("targetOffset"), target.x, target.y, target.z);
    anim->AddVector(Crc("positionOffset"), position.x, position.y, position.z);
    int frames = ShotFrames(seconds);
    anim->AddInteger(Crc("frames"), frames);
    anim->AddInteger(Crc("skippable"), 1);
    CFuncs::CFunc_PlaySkaterCamAnim(anim, script);
    anim->Clear();
    Script::DeleteStruct(anim);
    g_shotUntil = now + frames / 60.0;
    if (DebugEnabled())
        Log("level editor: camera %s played for %d frames", Nodes::Label(name).c_str(), frames);
    return true;
}

bool IsCamera(uint32_t name)
{
    std::vector<Camera> all = All();
    return Find(all, name) != nullptr;
}

std::string CreateFromView()
{
    ViewNow view;
    if (!CurrentView(&view)) {
        Hud::Message("No view available");
        return "";
    }
    Camera c;
    c.name = NodeCreate::FreshName(kStem, [](const std::string& n) { return NodeCreate::NameTaken(n); });
    c.pos = view.pos;
    c.angles = AnglesFor(view.yaw, view.pitch);
    c.distance = kDefaultDistance;
    PushUndo();
    std::string message;
    g_distances[c.name] = c.distance;
    if (!NodeCreate::Create(SpecOf(c), -1, &message)) {
        Hud::Message(message.empty() ? "Couldn't create the camera" : message, 4.0);
        return "";
    }
    Hud::Message("Added camera " + c.name);
    Log("level editor: camera %s made at (%.1f, %.1f, %.1f), heading %.1f, pitch %.1f", c.name.c_str(), c.pos.x, c.pos.y,
        c.pos.z, view.yaw * 180.0f / kPi, view.pitch * 180.0f / kPi);
    return c.name;
}

bool Preview(uint32_t name)
{
    std::vector<Camera> all = All();
    const Camera* c = Find(all, name);
    return c && LookThrough(*c);
}

bool AimFromView(uint32_t name)
{
    std::vector<Camera> all = All();
    const Camera* c = Find(all, name);
    ViewNow view;
    if (!c || !CurrentView(&view))
        return false;
    PushUndo();
    NodeEdit::Values values;
    values.pos = view.pos;
    values.angles = AnglesFor(view.yaw, view.pitch);
    NodeEdit::NoteOrigin(name);
    NodeEdit::Set(name, values);
    Hud::Message("Moved camera " + c->name + " to the view");
    return true;
}

bool SetPitch(uint32_t name, float degrees, bool done)
{
    std::vector<Camera> all = All();
    const Camera* c = Find(all, name);
    if (!c)
        return false;
    if (!g_dragging)
        PushUndo();
    g_dragging = !done;
    NodeEdit::Values values;
    values.pos = c->pos;
    values.angles = c->angles;
    values.angles.x = -std::clamp(degrees, -89.0f, 89.0f) * kPi / 180.0f;
    NodeEdit::Set(name, values);
    return true;
}

bool SetDistance(uint32_t name, float distance, bool done)
{
    std::vector<Camera> all = All();
    const Camera* c = Find(all, name);
    if (!c)
        return false;
    if (!g_dragging)
        PushUndo();
    g_dragging = !done;
    g_distances[c->name] = ClampDistance(distance);
    return true;
}

bool Remove(uint32_t name)
{
    std::vector<Camera> all = All();
    const Camera* c = Find(all, name);
    if (!c)
        return false;
    PushUndo();
    NodeCreate::Spec removed;
    int at = -1;
    std::string label = c->name;
    if (!NodeCreate::Remove(name, &removed, &at))
        return false;
    Hud::Message("Deleted camera " + label);
    return true;
}

std::vector<Camera> Capture() { return All(); }

int Apply(const std::vector<Camera>& cameras)
{
    int made = 0;
    std::string message;
    for (const Camera& c : cameras) {
        g_distances[c.name] = ClampDistance(c.distance);
        uint32_t name = NodeCreate::Checksum(c.name);
        if (NodeCreate::IsAdded(name))
            continue;
        if (NodeCreate::NameTaken(c.name)) {
            Log("level editor: camera %s - the level has a node of that name already: not made", c.name.c_str());
            continue;
        }
        if (NodeCreate::Create(SpecOf(c), -1, &message))
            ++made;
    }
    return made;
}

void ResetAll()
{
    for (const Camera& c : All()) {
        NodeCreate::Spec removed;
        int at = -1;
        NodeCreate::Remove(NodeCreate::Checksum(c.name), &removed, &at);
    }
}

void ForgetLevel()
{
    g_distances.clear();
    g_queue.clear();
    g_dragging = false;
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    for (const Camera& c : All())
        KeepOffsets(c);
}

void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                uint32_t selected, std::vector<Ui::Line>* lines)
{
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!ClipToNear(cameraPos, frame, nearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by, nx, ny;
        if (!Project(cameraPos, frame, lens, a, nearZ, &nx, &ny))
            return;
        NdcToPixel(viewport, nx, ny, &ax, &ay);
        if (!Project(cameraPos, frame, lens, b, nearZ, &nx, &ny))
            return;
        NdcToPixel(viewport, nx, ny, &bx, &by);
        lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    for (const Camera& c : All()) {
        V3 d = c.pos - cameraPos;
        if (Dot(d, d) > 12000.0f * 12000.0f)
            continue;
        bool lit = NodeCreate::Checksum(c.name) == selected;
        uint32_t colour = lit ? 0xFFFFF060 : 0xFFFF9030;
        float thickness = lit ? 2.0f : 1.25f;
        V3 corners[4];
        FrustumCorners(c.pos, c.angles, lens.tanX, lens.tanY, 120.0f, corners);
        for (int i = 0; i < 4; ++i) {
            line(c.pos, corners[i], colour, thickness);
            line(corners[i], corners[(i + 1) % 4], colour, thickness);
        }
        // (its up: a small triangle over the top edge; what it looks at: a line to the target and a cross there)
        V3 top = (corners[0] + corners[1]) * 0.5f;
        float yaw, pitch;
        YawPitchOf(c.angles, &yaw, &pitch);
        V3 up = FrameFromYawPitch(yaw, pitch).up * 30.0f;
        line(corners[0] * 0.75f + corners[1] * 0.25f, top + up, colour, thickness);
        line(corners[1] * 0.75f + corners[0] * 0.25f, top + up, colour, thickness);
        V3 target = TargetOf(c.pos, c.angles, c.distance);
        line(c.pos, target, lit ? 0xC0FFF060 : 0x60FF9030, 1.0f);
        float s = lit ? 25.0f : 15.0f;
        line(target - V3{s, 0, 0}, target + V3{s, 0, 0}, colour, 1.0f);
        line(target - V3{0, s, 0}, target + V3{0, s, 0}, colour, 1.0f);
        line(target - V3{0, 0, s}, target + V3{0, 0, s}, colour, 1.0f);
    }
}

void InspectorSection(uint32_t node, ImFont* mono, ImFont* smallFont)
{
    std::vector<Camera> all = All();
    const Camera* c = node ? Find(all, node) : nullptr;
    if (!c)
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Camera###camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    float yaw, pitch;
    YawPitchOf(c->angles, &yaw, &pitch);
    float pitchDegrees = pitch * 180.0f / kPi;
    ImGui::PushFont(mono);
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::DragFloat("Pitch##camera", &pitchDegrees, 0.25f, -89.0f, 89.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp)) {
        float value = pitchDegrees;
        g_queue.push_back([node, value] { SetPitch(node, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        g_queue.push_back([] { g_dragging = false; });
    float distance = c->distance;
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::DragFloat("Target##camera", &distance, 5.0f, kMinDistance, kMaxDistance, "%.0f units",
                         ImGuiSliderFlags_AlwaysClamp)) {
        float value = distance;
        g_queue.push_back([node, value] { SetDistance(node, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        g_queue.push_back([] { g_dragging = false; });
    V3 target = TargetOf(c->pos, c->angles, c->distance), offset = PositionOffset(c->pos, c->angles, c->distance);
    ImGui::PushFont(smallFont);
    ImGui::Text("targetOffset   (%.1f, %.1f, %.1f)", target.x, target.y, target.z);
    ImGui::Text("positionOffset (%.1f, %.1f, %.1f)", offset.x, offset.y, offset.z);
    ImGui::PopFont();
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Computed from position and heading");
    if (ImGui::Button("Look through##camera"))
        g_queue.push_back([node] { Preview(node); });
    ImGui::SameLine();
    if (ImGui::Button("Put at the view##camera"))
        g_queue.push_back([node] { AimFromView(node); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move the camera to the current view");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Delete##camera"))
        g_queue.push_back([node] { Remove(node); });
    ImGui::PopStyleColor();
    
}

void ListSection(ImFont* mono, ImFont* smallFont)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Cameras###cameras"))
        return;
    ImGui::PushFont(smallFont);
    ImGui::TextDisabled("Camera nodes, saved with the layout");
    ImGui::PopFont();
    if (ImGui::Button("New Camera from View"))
        g_queue.push_back([] { CreateFromView(); });
    std::vector<Camera> all = All();
    if (all.empty())
        ImGui::TextDisabled("None yet.");
    ImGui::PushFont(mono);
    for (const Camera& c : all) {
        uint32_t name = NodeCreate::Checksum(c.name);
        ImGui::PushID(static_cast<int>(name));
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("View Through"))
            g_queue.push_back([name] { Preview(name); });
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace Front::LevelEditor::Cameras
