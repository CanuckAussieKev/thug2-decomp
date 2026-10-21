// NOT ORIGINAL - the level editor's camera tools (ours). See level_editor_camera.h.

#include "game/front/level_editor_camera.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <fstream>
#include <sstream>

#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_prefs.h"
#include "hook/log.h"

namespace Front::LevelEditor::Camera {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTopPitch = 89.0f * kPi / 180.0f; // (Look's limit: the frame needs a heading)
} // namespace

// --- the pieces ---

float Ease(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float NearestAngle(float from, float to)
{
    double d = std::fmod(static_cast<double>(to) - from, 2.0 * kPi);
    if (d > kPi)
        d -= 2.0 * kPi;
    else if (d < -kPi)
        d += 2.0 * kPi;
    return static_cast<float>(from + d);
}

Pose Blend(const Pose& from, const Pose& to, float t)
{
    float e = Ease(t);
    float yaw = NearestAngle(from.yaw, to.yaw);
    return {from.pos + (to.pos - from.pos) * e, from.yaw + (yaw - from.yaw) * e, from.pitch + (to.pitch - from.pitch) * e};
}

float FrameDistance(float radius, float tanHalf, float minimum)
{
    if (tanHalf <= 0.0f)
        return std::max(minimum, radius * 3.0f);
    // (the sphere seen whole: its radius over the sine of the half-angle, a little more)
    double half = std::atan(static_cast<double>(tanHalf));
    return std::max(minimum, static_cast<float>(radius / std::sin(half) * 1.15));
}

float SpeedForRadius(float radius) { return std::clamp(radius * 4.0f, 100.0f, 20000.0f); }

V3 OrbitAbout(V3 pos, V3 pivot, const Frame& before, const Frame& after)
{
    V3 v = pos - pivot;
    float r = Dot(v, before.right), u = Dot(v, before.up), b = Dot(v, before.back);
    return pivot + after.right * r + after.up * u + after.back * b;
}

float FlyRamp(float heldSeconds, int accelMs)
{
    if (accelMs <= 0)
        return 1.0f;
    float t = heldSeconds * 1000.0f / static_cast<float>(accelMs);
    // (never quite still: the first frame moves a little)
    return std::max(0.05f, Ease(t));
}

float EyeDistance(float equivalent) { return std::clamp(equivalent * 15.0f, 15000.0f, 60000.0f); }

float OrthoAngle(float baseDegrees, float equivalent)
{
    double half = static_cast<double>(baseDegrees) * kPi / 360.0;
    double narrowed = std::atan(std::tan(half) * equivalent / EyeDistance(equivalent));
    return static_cast<float>(narrowed * 360.0 / kPi);
}

float OrthoNear(float equivalent) { return EyeDistance(equivalent) * 0.4f; }

void AxisYawPitch(View view, float* yaw, float* pitch)
{
    *yaw = 0.0f;
    *pitch = 0.0f;
    switch (view) {
    case View::Top: *pitch = -kTopPitch; break;     // (looking down; up the screen +z)
    case View::Bottom: *pitch = kTopPitch; break;
    case View::Front: *yaw = 0.0f; break;           // (looking along +z)
    case View::Back: *yaw = kPi; break;
    case View::Right: *yaw = -kPi * 0.5f; break;    // (from +x, looking along -x)
    case View::Left: *yaw = kPi * 0.5f; break;
    default: break;
    }
}

const char* ViewName(View view)
{
    switch (view) {
    case View::Top: return "Top";
    case View::Bottom: return "Bottom";
    case View::Front: return "Front";
    case View::Back: return "Back";
    case View::Right: return "Right";
    case View::Left: return "Left";
    case View::Free: return "User";
    default: return "Perspective";
    }
}

std::string FormatBookmarks(const Bookmark marks[kBookmarks])
{
    std::string text = "thug2-decomp camera bookmarks 1\n";
    for (int i = 0; i < kBookmarks; ++i) {
        const Bookmark& m = marks[i];
        if (!m.set)
            continue;
        char line[256];
        std::snprintf(line, sizeof(line), "%d %.9g %.9g %.9g %.9g %.9g %d %.9g\n", i + 1, m.pose.pos.x, m.pose.pos.y,
                      m.pose.pos.z, m.pose.yaw, m.pose.pitch, static_cast<int>(m.view), m.equivalent);
        text += line;
    }
    return text;
}

bool ParseBookmarks(const std::string& text, Bookmark marks[kBookmarks])
{
    for (int i = 0; i < kBookmarks; ++i)
        marks[i] = Bookmark{};
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("thug2-decomp camera bookmarks ", 0) != 0)
        return false;
    while (std::getline(in, line)) {
        int n = 0, view = 0;
        float x, y, z, yaw, pitch, equivalent;
        if (sscanf_s(line.c_str(), "%d %f %f %f %f %f %d %f", &n, &x, &y, &z, &yaw, &pitch, &view, &equivalent) != 8)
            continue;
        if (n < 1 || n > kBookmarks || view < 0 || view > static_cast<int>(View::Free))
            continue;
        Bookmark& m = marks[n - 1];
        m.set = true;
        m.pose = {{x, y, z}, yaw, pitch};
        m.view = static_cast<View>(view);
        m.equivalent = equivalent;
    }
    return true;
}

std::string BookmarksFileName(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.cameras.txt", scene);
    return name;
}

// --- the editor's state ---

namespace {

// A flight: from, to, how far along (seconds), and what it turns into at its end (an ortho view).
struct Flight {
    bool on = false;
    Pose from, to;
    float t = 0.0f, duration = 0.33f;
    View thenView = View::None;
    float thenEquivalent = 0.0f;
};
Flight g_flight;

// The ortho view: which, its heading (kept: the selection's focus would turn it), the point it looks at, and its zoom.
struct OrthoState {
    View view = View::None;
    float yaw = 0.0f, pitch = 0.0f;
    V3 pivot = {};
    float equivalent = 1500.0f;
    V3 lastEye = {};
};
OrthoState g_ortho;

// Alt+left drag.
struct OrbitState {
    bool pending = false, orbiting = false;
    float travel = 0.0f;
    float dx = 0.0f, dy = 0.0f;
    int wheel = 0;
    bool havePivot = false;
    V3 pivot = {};
};
OrbitState g_orbit;

uint32_t g_scene;
Bookmark g_marks[kBookmarks];
float g_flyHeld;

std::string BookmarksPath() { return GameFolderPath("thug2-decomp-layouts") + "\\" + BookmarksFileName(g_scene); }

void LoadBookmarks()
{
    for (Bookmark& m : g_marks)
        m = Bookmark{};
    std::ifstream file(BookmarksPath(), std::ios::binary);
    if (!file)
        return;
    std::stringstream text;
    text << file.rdbuf();
    if (!ParseBookmarks(text.str(), g_marks))
        Log("level editor: %s isn't a bookmarks file - not read", BookmarksPath().c_str());
}

bool SaveBookmarks()
{
    CreateDirectoryA(GameFolderPath("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(BookmarksPath(), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << FormatBookmarks(g_marks);
    return static_cast<bool>(file);
}

V3 Forward(float yaw, float pitch) { return FrameFromYawPitch(yaw, pitch).back * -1.0f; }

void FlyTo(const Rig& rig, const Pose& to, View thenView = View::None, float thenEquivalent = 0.0f)
{
    g_flight.on = true;
    g_flight.from = {*rig.pos, *rig.yaw, *rig.pitch};
    g_flight.to = to;
    g_flight.t = 0.0f;
    g_flight.thenView = thenView;
    g_flight.thenEquivalent = thenEquivalent;
}

void EnterOrtho(const Rig& rig, View view, float yaw, float pitch, V3 pivot, float equivalent)
{
    g_ortho.view = view;
    g_ortho.yaw = yaw;
    g_ortho.pitch = pitch;
    g_ortho.pivot = rig.haveSelection ? rig.target : pivot;
    g_ortho.equivalent = std::clamp(equivalent, 50.0f, 20000.0f);
    *rig.yaw = yaw;
    *rig.pitch = pitch;
    V3 forward = Forward(yaw, pitch);
    *rig.pos = g_ortho.pivot - forward * EyeDistance(g_ortho.equivalent);
    if (rig.haveSelection)
        *rig.orbitDistance = EyeDistance(g_ortho.equivalent);
    g_ortho.lastEye = *rig.pos;
    Hud::Message(std::string(ViewName(view)) + " (orthographic) - numpad 5: perspective; the wheel zooms", 2.5);
}

void ExitOrtho(const Rig& rig)
{
    if (g_ortho.view == View::None)
        return;
    V3 forward = Forward(*rig.yaw, *rig.pitch);
    V3 pivot = rig.haveSelection ? rig.target : g_ortho.pivot;
    *rig.pos = pivot - forward * g_ortho.equivalent;
    if (rig.haveSelection)
        *rig.orbitDistance = std::clamp(g_ortho.equivalent, 20.0f, 20000.0f);
    g_ortho.view = View::None;
    Hud::Message("Perspective", 1.5);
}

// The point the view looks at: the selection, else the surface ahead (within 20000), else 1000 ahead.
V3 LookPoint(const Rig& rig)
{
    if (rig.haveSelection)
        return rig.target;
    V3 forward = Forward(*rig.yaw, *rig.pitch);
    V3 hit;
    if (rig.cast && rig.cast(*rig.pos, *rig.pos + forward * 20000.0f, &hit))
        return hit;
    return *rig.pos + forward * 1000.0f;
}

} // namespace

void Start(uint32_t scene)
{
    g_flight = Flight{};
    g_ortho = OrthoState{};
    g_orbit = OrbitState{};
    g_flyHeld = 0.0f;
    g_scene = scene;
    LoadBookmarks();
}

void Stop()
{
    g_flight = Flight{};
    g_ortho.view = View::None;
    g_orbit = OrbitState{};
}

void Cancel()
{
    g_flight = Flight{};
    g_ortho.view = View::None;
    g_orbit = OrbitState{};
}

bool Ortho() { return g_ortho.view != View::None; }
View CurrentView() { return g_ortho.view; }

float Apparent(float distance)
{
    if (!Ortho())
        return distance;
    return distance * g_ortho.equivalent / EyeDistance(g_ortho.equivalent);
}

float ViewAngle(float baseDegrees) { return Ortho() ? OrthoAngle(baseDegrees, g_ortho.equivalent) : baseDegrees; }

float NearPlane() { return Ortho() ? OrthoNear(g_ortho.equivalent) : 2.0f; }

std::string Status()
{
    std::string s;
    if (Ortho()) {
        char text[64];
        std::snprintf(text, sizeof(text), "%s ortho (%.0f)", ViewName(g_ortho.view), g_ortho.equivalent);
        s = text;
    }
    if (g_orbit.orbiting)
        s += s.empty() ? "orbiting" : "  orbiting";
    return s;
}

bool Orbiting() { return g_orbit.orbiting; }

void FilterMouse(const Rig& rig, MouseIn m, bool alt, bool haveLastRay, V3 lastRay)
{
    // The wheel with the right button held: the flying speed (Unreal's).
    if (m.right && *m.wheel && !g_orbit.orbiting) {
        *rig.speed = std::clamp(*rig.speed * std::pow(1.25f, static_cast<float>(*m.wheel)), 50.0f, 20000.0f);
        *m.wheel = 0;
        char text[64];
        std::snprintf(text, sizeof(text), "Fly speed %.0f", *rig.speed);
        Hud::Message(text, 1.2);
    }
    // Looking about ends an ortho view (Blender's auto perspective).
    if (m.right && (*m.relX != 0.0f || *m.relY != 0.0f) && Ortho())
        ExitOrtho(rig);
    // Alt+left: held back until it moves (an orbit) or is let go (an Alt+click).
    if (!g_orbit.pending && *m.leftPresses && alt && !rig.dragging && !m.right) {
        g_orbit = OrbitState{};
        g_orbit.pending = true;
        *m.leftPresses = 0;
        // (the pivot: the selection, else the surface under the cursor where the press was)
        if (!rig.haveSelection && haveLastRay && rig.cast) {
            V3 dir = Normalize(lastRay);
            g_orbit.havePivot = rig.cast(*rig.pos, *rig.pos + dir * 30000.0f, &g_orbit.pivot);
        }
        if (!rig.haveSelection && !g_orbit.havePivot) {
            g_orbit.pivot = LookPoint(rig);
            g_orbit.havePivot = true;
        }
        return;
    }
    if (!g_orbit.pending)
        return;
    if (!g_orbit.orbiting) {
        g_orbit.travel += std::fabs(*m.relX) + std::fabs(*m.relY);
        if (g_orbit.travel >= 4.0f) {
            g_orbit.orbiting = true;
            g_flight.on = false;
            if (Ortho())
                ExitOrtho(rig);
        }
    }
    if (g_orbit.orbiting) {
        g_orbit.dx += *m.relX;
        g_orbit.dy += *m.relY;
        g_orbit.wheel += *m.wheel;
        *m.relX = *m.relY = 0.0f;
        *m.wheel = 0;
    }
    if (!m.left) {
        // let go: unmoved, the press given back as an Alt+click (with its release)
        if (!g_orbit.orbiting) {
            *m.leftPresses = 1;
            *m.leftReleases = 1; // (the button is up: a press and its release, as a quick click)
        } else
            *m.leftReleases = 0;
        g_orbit.pending = false;
        g_orbit.orbiting = false;
    }
}

void ApplyOrbit(const Rig& rig)
{
    if (!g_orbit.orbiting && g_orbit.dx == 0.0f && g_orbit.dy == 0.0f && g_orbit.wheel == 0)
        return;
    const Prefs::Values& prefs = Prefs::Get();
    float sensitivity = 0.0035f * static_cast<float>(prefs.lookPct) / 100.0f;
    float dy = prefs.invertY ? -g_orbit.dy : g_orbit.dy;
    Frame before = FrameFromYawPitch(*rig.yaw, *rig.pitch);
    Look(rig.yaw, rig.pitch, g_orbit.dx, dy, sensitivity);
    g_orbit.dx = g_orbit.dy = 0.0f;
    int wheel = g_orbit.wheel;
    g_orbit.wheel = 0;
    if (rig.haveSelection) {
        // (the editor puts the camera round it at its orbit distance: the wheel nearer / further)
        if (wheel)
            *rig.orbitDistance = std::clamp(*rig.orbitDistance * std::pow(0.85f, static_cast<float>(wheel)), 20.0f, 20000.0f);
        return;
    }
    if (!g_orbit.havePivot)
        return;
    Frame after = FrameFromYawPitch(*rig.yaw, *rig.pitch);
    V3 pos = OrbitAbout(*rig.pos, g_orbit.pivot, before, after);
    if (wheel) {
        V3 offset = pos - g_orbit.pivot;
        float length = std::sqrt(Dot(offset, offset));
        float wanted = std::clamp(length * std::pow(0.85f, static_cast<float>(wheel)), 20.0f, 30000.0f);
        if (length > 0.001f)
            pos = g_orbit.pivot + offset * (wanted / length);
    }
    *rig.pos = pos;
}

void Keys(const Rig& rig, const uint8_t* keys, bool ctrl, bool browserOpen)
{
    static bool fWas, digitWas[kBookmarks], kpWas[4];
    auto pressed = [&](SDL_Scancode key, bool* was) {
        bool down = keys[key] != 0;
        bool p = down && !*was;
        *was = down;
        return p;
    };
    // F: the selection framed (the browser has its own F).
    if (pressed(SDL_SCANCODE_F, &fWas) && !ctrl && !browserOpen) {
        if (!rig.haveSelection) {
            Hud::Message("F frames the selection - nothing is selected", 2.0);
        } else {
            float tanHalf = std::min(rig.lens.tanX, rig.lens.tanY);
            if (Ortho()) {
                // (an ortho view: its zoom to fit, the lens as perspective has it)
                float base = tanHalf * EyeDistance(g_ortho.equivalent) / std::max(1.0f, g_ortho.equivalent);
                g_ortho.equivalent = std::clamp(FrameDistance(rig.radius, base, 100.0f), 50.0f, 20000.0f);
            } else {
                float distance = std::min(20000.0f, FrameDistance(rig.radius, tanHalf, 100.0f));
                Frame frame = FrameFromYawPitch(*rig.yaw, *rig.pitch);
                FlyTo(rig, {rig.target + frame.back * distance, *rig.yaw, *rig.pitch});
            }
            if (Prefs::Get().speedBySelection)
                *rig.speed = SpeedForRadius(rig.radius);
        }
    }
    // 1..9 recall a bookmark, Ctrl+1..9 store it. (Alt+1..4 are the view modes - level_editor_overlays.h.)
    bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    for (int i = 0; i < kBookmarks; ++i) {
        if (!pressed(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i), &digitWas[i]) || alt)
            continue;
        char text[96];
        if (ctrl) {
            Bookmark& m = g_marks[i];
            m.set = true;
            m.pose = {*rig.pos, *rig.yaw, *rig.pitch};
            m.view = g_ortho.view;
            m.equivalent = g_ortho.equivalent;
            if (Ortho())
                m.pose.pos = rig.haveSelection ? rig.target : g_ortho.pivot; // (an ortho bookmark keeps its pivot)
            bool saved = SaveBookmarks();
            std::snprintf(text, sizeof(text), "Camera bookmark %d stored%s", i + 1, saved ? "" : " (not saved: the file)");
            Hud::Message(text, 2.0);
        } else if (g_marks[i].set) {
            const Bookmark& m = g_marks[i];
            if (rig.deselect)
                rig.deselect();
            Rig free = rig;
            free.haveSelection = false;
            ExitOrtho(free);
            if (m.view != View::None) {
                // (fly to the perspective place it stands for, then the ortho view)
                V3 forward = Forward(m.pose.yaw, m.pose.pitch);
                FlyTo(free, {m.pose.pos - forward * m.equivalent, m.pose.yaw, m.pose.pitch}, m.view, m.equivalent);
            } else {
                FlyTo(free, m.pose);
            }
            std::snprintf(text, sizeof(text), "Camera bookmark %d", i + 1);
            Hud::Message(text, 1.5);
        } else {
            std::snprintf(text, sizeof(text), "No camera bookmark %d yet - Ctrl+%d stores one", i + 1, i + 1);
            Hud::Message(text, 2.0);
        }
    }
    // The numpad: 7 top, 1 front, 3 right (Ctrl: the opposite), 5 perspective / ortho.
    static const SDL_Scancode kPad[4] = {SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_5};
    for (int i = 0; i < 4; ++i) {
        if (!pressed(kPad[i], &kpWas[i]))
            continue;
        g_flight.on = false;
        if (i == 3) {
            if (Ortho()) {
                ExitOrtho(rig);
            } else {
                V3 pivot = LookPoint(rig);
                V3 d = pivot - *rig.pos;
                EnterOrtho(rig, View::Free, *rig.yaw, *rig.pitch, pivot, std::sqrt(Dot(d, d)));
            }
            continue;
        }
        static const View kViews[3][2] = {{View::Top, View::Bottom}, {View::Front, View::Back}, {View::Right, View::Left}};
        View view = kViews[i][ctrl ? 1 : 0];
        V3 pivot = Ortho() ? (rig.haveSelection ? rig.target : g_ortho.pivot) : LookPoint(rig);
        V3 d = pivot - *rig.pos;
        float equivalent = Ortho() ? g_ortho.equivalent : std::clamp(std::sqrt(Dot(d, d)), 200.0f, 20000.0f);
        float yaw, pitch;
        AxisYawPitch(view, &yaw, &pitch);
        EnterOrtho(rig, view, yaw, pitch, pivot, equivalent);
    }
}

void AfterUpdate(const Rig& rig, int wheel, bool interrupted)
{
    if (g_flight.on && interrupted)
        g_flight.on = false;
    if (g_flight.on) {
        // (the frame's time from SDL's clock: the editor caps a frame at a tenth of a second)
        static uint64_t last;
        uint64_t now = SDL_GetPerformanceCounter();
        double seconds = g_flight.t == 0.0f || !last ? 1.0 / 60.0 : static_cast<double>(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        g_flight.t += static_cast<float>(std::min(seconds, 0.1)) / g_flight.duration;
        Pose to = g_flight.to;
        if (rig.haveSelection) {
            // (the selection may move meanwhile: the place is kept relative to it)
            Frame frame = FrameFromYawPitch(to.yaw, to.pitch);
            V3 d = g_flight.to.pos - rig.target;
            to.pos = rig.target + frame.back * std::sqrt(Dot(d, d));
        }
        Pose pose = Blend(g_flight.from, to, std::min(1.0f, g_flight.t));
        *rig.pos = pose.pos;
        *rig.yaw = pose.yaw;
        *rig.pitch = pose.pitch;
        if (rig.haveSelection) {
            V3 d = *rig.pos - rig.target;
            *rig.orbitDistance = std::clamp(std::sqrt(Dot(d, d)), 20.0f, 20000.0f);
        }
        if (g_flight.t >= 1.0f) {
            g_flight.on = false;
            if (g_flight.thenView != View::None) {
                V3 forward = Forward(to.yaw, to.pitch);
                EnterOrtho(rig, g_flight.thenView, to.yaw, to.pitch, to.pos + forward * g_flight.thenEquivalent,
                           g_flight.thenEquivalent);
            } else if (rig.haveSelection && rig.focus) {
                rig.focus();
            }
        }
        return;
    }
    if (!Ortho())
        return;
    // The ortho view: its heading kept, the camera far back along it.
    *rig.yaw = g_ortho.yaw;
    *rig.pitch = g_ortho.pitch;
    V3 forward = Forward(g_ortho.yaw, g_ortho.pitch);
    if (rig.haveSelection) {
        if (wheel && !rig.dragging)
            g_ortho.equivalent = std::clamp(g_ortho.equivalent * std::pow(0.85f, static_cast<float>(wheel)), 50.0f, 20000.0f);
        g_ortho.pivot = rig.target;
    } else {
        // (the editor flew the camera: along the view a zoom, across it a pan)
        V3 moved = *rig.pos - g_ortho.lastEye;
        float along = Dot(moved, forward);
        g_ortho.equivalent = std::clamp(g_ortho.equivalent - along, 50.0f, 20000.0f);
        g_ortho.pivot = g_ortho.pivot + (moved - forward * along);
    }
    float eye = EyeDistance(g_ortho.equivalent);
    *rig.pos = g_ortho.pivot - forward * eye;
    if (rig.haveSelection)
        *rig.orbitDistance = eye;
    g_ortho.lastEye = *rig.pos;
}

float FlyFactor(bool held, double seconds)
{
    g_flyHeld = held ? g_flyHeld + static_cast<float>(seconds) : 0.0f;
    return FlyRamp(g_flyHeld, Prefs::Get().flyAccelMs);
}

} // namespace Front::LevelEditor::Camera
