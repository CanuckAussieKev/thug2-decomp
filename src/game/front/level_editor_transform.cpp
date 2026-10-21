// NOT ORIGINAL - the level editor's transform tools (ours; see level_editor_transform.h).

#include "game/front/level_editor_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game/front/level_editor_prefs.h"

namespace Front::LevelEditor::Transform {

namespace {

constexpr float kPi = 3.14159265f;

Settings g_settings;
Repeat g_repeat;

} // namespace

Settings& Current() { return g_settings; }

bool FlyAlways() { return Prefs::Get().wasdFlies; }
void SetFlyAlways(bool always)
{
    Prefs::Values v = Prefs::Get();
    v.wasdFlies = always;
    Prefs::Set(v);
}
float Grid() { return static_cast<float>(Prefs::Get().snapMove); }
void SetGrid(float grid)
{
    Prefs::Values v = Prefs::Get();
    v.snapMove = static_cast<int>(grid);
    Prefs::Set(v);
}
float AngleStep() { return static_cast<float>(Prefs::Get().snapTurn); }
void SetAngleStep(float degrees)
{
    Prefs::Values v = Prefs::Get();
    v.snapTurn = static_cast<int>(degrees);
    Prefs::Set(v);
}

const std::vector<float>& GridSizes()
{
    static const std::vector<float> sizes = {1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f};
    return sizes;
}
const std::vector<float>& AngleSteps()
{
    static const std::vector<float> steps = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    return steps;
}

const char* ModeName(Mode mode)
{
    switch (mode) {
    case Mode::Move: return "Move";
    case Mode::Rotate: return "Rotate";
    case Mode::Scale: return "Scale";
    }
    return "?";
}
const char* PivotName(Pivot pivot)
{
    switch (pivot) {
    case Pivot::Base: return "Base";
    case Pivot::Centre: return "Centre";
    case Pivot::Own: return "Own";
    }
    return "?";
}
const char* TargetName(SnapTarget target)
{
    switch (target) {
    case SnapTarget::Grid: return "Grid";
    case SnapTarget::WorldGrid: return "World grid";
    case SnapTarget::Surface: return "Surface";
    case SnapTarget::Vertex: return "Vertex";
    }
    return "?";
}
Mode NextMode(Mode mode)
{
    return mode == Mode::Move ? Mode::Rotate : mode == Mode::Rotate ? Mode::Scale : Mode::Move;
}

// --- the pure parts ---

void HeadingAxes(float yaw, V3 out[3])
{
    float c = std::cos(yaw), s = std::sin(yaw);
    out[0] = {c, 0.0f, -s};
    out[1] = {0.0f, 1.0f, 0.0f};
    out[2] = {s, 0.0f, c};
}

float HeadingOf(V3 at) { return std::atan2(at.x, at.z); }

void UnitAxes(const V3 rows[3], V3 out[3])
{
    static const V3 kWorld[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int i = 0; i < 3; ++i)
        out[i] = Dot(rows[i], rows[i]) > 1e-12f ? Normalize(rows[i]) : kWorld[i];
}

float AngleAbout(V3 pivot, V3 point) { return std::atan2(point.x - pivot.x, point.z - pivot.z); }

float Wrap(float radians)
{
    float r = std::fmod(radians, 2.0f * kPi);
    if (r <= -kPi)
        r += 2.0f * kPi;
    else if (r > kPi)
        r -= 2.0f * kPi;
    return r;
}

float SnapAngle(float radians, float stepDegrees)
{
    if (stepDegrees <= 0.0f)
        return radians;
    float step = stepDegrees * kPi / 180.0f;
    return std::round(radians / step) * step;
}

int Quarters(float radians) { return static_cast<int>(std::lround(radians / (kPi * 0.5f))); }

float SnapRelative(float distance, float grid) { return grid > 0.0f ? std::round(distance / grid) * grid : distance; }

float FactorAlong(float s0, float s)
{
    if (std::fabs(s0) < 1e-3f)
        return 1.0f;
    return std::max(0.05f, std::min(20.0f, s / s0));
}

float FactorFromPixels(float d0, float d)
{
    if (d0 < 4.0f)
        return 1.0f; // (grabbed on the pivot itself: the distance says nothing)
    return std::max(0.05f, std::min(20.0f, d / d0));
}

float SnapFactor(float factor) { return std::max(0.1f, std::round(factor * 10.0f) / 10.0f); }

std::vector<V3> Ring(V3 pivot, float radius, int count)
{
    std::vector<V3> points;
    for (int i = 0; i < count; ++i) {
        float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
        points.push_back({pivot.x + std::sin(a) * radius, pivot.y, pivot.z + std::cos(a) * radius});
    }
    return points;
}

std::vector<Segment> GizmoSegments(const Gizmo& gizmo)
{
    std::vector<Segment> out;
    V3 p = gizmo.pivot;
    float length = gizmo.length;
    switch (gizmo.mode) {
    case Mode::Move:
        for (int axis = 0; axis < 3; ++axis)
            out.push_back({p, p + gizmo.axes[axis] * length, axis, true});
        break;
    case Mode::Rotate: {
        if (!gizmo.rotates)
            break;
        // (the heading ring, and a spoke towards the local z: which way it faces)
        std::vector<V3> ring = Ring(p, length * 0.8f, 48);
        for (size_t i = 0; i < ring.size(); ++i)
            out.push_back({ring[i], ring[(i + 1) % ring.size()], 1, false});
        V3 forward = {gizmo.axes[2].x, 0.0f, gizmo.axes[2].z};
        if (Dot(forward, forward) > 1e-6f)
            out.push_back({p, p + Normalize(forward) * (length * 0.8f), 1, false});
        break;
    }
    case Mode::Scale: {
        float cube = length * 0.06f;
        auto addCube = [&](V3 c, int handle) {
            V3 corners[8];
            for (int i = 0; i < 8; ++i)
                corners[i] = c + V3{(i & 1) ? cube : -cube, (i & 2) ? cube : -cube, (i & 4) ? cube : -cube};
            for (int i = 0; i < 8; ++i) {
                for (int bit = 1; bit < 8; bit <<= 1) {
                    if (!(i & bit))
                        out.push_back({corners[i], corners[i | bit], handle, false});
                }
            }
        };
        for (int axis = 0; axis < 3; ++axis) {
            V3 end = p + gizmo.axes[axis] * length;
            out.push_back({p, end, axis, false});
            addCube(end, axis);
        }
        addCube(p, kCentre);
        break;
    }
    }
    return out;
}

int HandleAt(const Gizmo& gizmo, const Projector& project, float x, float y, float grab)
{
    int best = -1;
    float bestDistance = grab;
    // (Scale's centre cube wins near the pivot: it sits where the three axes start)
    float px, py;
    if (gizmo.mode == Mode::Scale && project(gizmo.pivot, &px, &py) &&
        (x - px) * (x - px) + (y - py) * (y - py) <= grab * grab * 2.25f)
        return kCentre;
    for (const Segment& s : GizmoSegments(gizmo)) {
        float ax, ay, bx, by;
        if (!project(s.a, &ax, &ay) || !project(s.b, &bx, &by))
            continue;
        float d = DistanceToSegment(x, y, ax, ay, bx, by);
        if (d < bestDistance) {
            bestDistance = d;
            best = s.handle;
        }
    }
    return best;
}

uint32_t HandleColour(int handle, bool lit)
{
    if (lit)
        return 0xFFFFFF40;
    switch (handle) {
    case 0: return 0xFFFF4040;
    case 1: return 0xFF40E040;
    case 2: return 0xFF4080FF;
    default: return 0xFFE8E8E8;
    }
}

// --- typed values ---

bool TypeKey(Typed* typed, char c)
{
    if (c >= 'x' && c <= 'z') {
        int axis = c - 'x';
        typed->axis = typed->axis == axis ? -1 : axis;
        typed->axisChanged = true;
        return true;
    }
    if (c >= '0' && c <= '9') {
        typed->text += c;
        return true;
    }
    if (c == '.') {
        if (typed->text.find('.') != std::string::npos)
            return false;
        typed->text += c;
        return true;
    }
    if (c == '-') {
        if (!typed->text.empty() && typed->text[0] == '-')
            typed->text.erase(0, 1);
        else
            typed->text.insert(typed->text.begin(), '-');
        return true;
    }
    if (c == '\b') {
        if (!typed->text.empty())
            typed->text.pop_back();
        else if (typed->axis >= 0) {
            typed->axis = -1;
            typed->axisChanged = true;
        } else
            return false;
        return true;
    }
    return false;
}

bool TypedValue(const Typed& typed, float* value)
{
    const std::string& t = typed.text;
    if (t.empty() || t == "-" || t == "." || t == "-.")
        return false;
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
        return false;
    *value = static_cast<float>(v);
    return true;
}

std::string Describe(Mode mode, const Typed& typed, int defaultAxis)
{
    static const char* kAxis[3] = {"X", "Y", "Z"};
    int axis = typed.axis >= 0 ? typed.axis : defaultAxis;
    std::string text;
    switch (mode) {
    case Mode::Move:
        text = std::string("Move along ") + kAxis[axis >= 0 && axis < 3 ? axis : 0];
        break;
    case Mode::Rotate:
        text = "Turn (degrees of heading)";
        break;
    case Mode::Scale:
        text = axis >= 0 && axis < 3 ? std::string("Scale along ") + kAxis[axis] : std::string("Scale (all axes)");
        break;
    }
    text += ": ";
    text += typed.text.empty() ? std::string("_") : typed.text + "_";
    return text;
}

// --- the repeat of the last duplicate ---

void NoteDuplicate(Repeat* memory, Kind kind, const std::vector<uint32_t>& made, V3 sourcePos, float sourceHeading)
{
    memory->kind = kind;
    memory->made = made;
    memory->sourcePos = sourcePos;
    memory->sourceHeading = sourceHeading;
}

bool RepeatFor(const Repeat& memory, Kind kind, const std::vector<uint32_t>& selected, V3 pos, float heading,
               RepeatStep* step)
{
    if (memory.kind == Kind::None || kind != memory.kind || selected.empty())
        return false;
    std::vector<uint32_t> a = memory.made, b = selected;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b)
        return false;
    step->offset = pos - memory.sourcePos;
    float turn = Wrap(heading - memory.sourceHeading);
    if (kind == Kind::Scenery) {
        step->quarters = Quarters(turn) & 3;
        step->turn = 0.0f;
    } else {
        step->quarters = 0;
        step->turn = std::fabs(turn) < 1e-5f ? 0.0f : turn;
    }
    return true;
}

V3 RepeatTarget(V3 from, const RepeatStep& step)
{
    V3 offset = step.quarters ? QuarterTurn(step.offset, step.quarters)
                              : step.turn != 0.0f ? TurnAboutUp(step.offset, step.turn) : step.offset;
    return from + offset;
}

Repeat& LastDuplicate() { return g_repeat; }

} // namespace Front::LevelEditor::Transform
