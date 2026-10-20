// Self-tests for the level editor's model viewer (src/game/front/level_editor_preview.*, not original): the framing
// (the subject's bounding sphere fits the picture from the distance chosen, at any orbit) and the orbit's drag / wheel.

#include <cmath>

#include "game/front/level_editor.h"
#include "game/front/level_editor_preview.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace P = Front::LevelEditor::Preview;
using LE::V3;

bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance = 1e-4f)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

// The largest |ndc| of a box's corners seen from an orbit's eye (2: one behind the eye).
float Extent(V3 min, V3 max, const P::Orbit& orbit)
{
    V3 centre;
    float radius;
    P::SphereOf(min, max, 4.0f, &centre, &radius);
    LE::Lens lens = LE::LensFor(P::kAngle, P::kSquareAspect);
    float half = std::atan(std::fmin(lens.tanX, lens.tanY));
    P::Eye eye = P::OrbitEye(centre, P::FitDistance(radius, half, P::kMargin) / orbit.zoom, orbit);
    float extent = 0.0f;
    for (int i = 0; i < 8; ++i) {
        V3 corner = {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
        float nx, ny;
        if (!LE::Project(eye.pos, eye.frame, lens, corner, 2.0f, &nx, &ny))
            return 2.0f;
        extent = std::fmax(extent, std::fmax(std::fabs(nx), std::fabs(ny)));
    }
    return extent;
}

void TestEditorPreviewFraming()
{
    // A square picture: Nx::SetCamera's height factor makes the two half-angles equal at the aspect given.
    LE::Lens lens = LE::LensFor(P::kAngle, P::kSquareAspect);
    Check(Near(lens.tanX, lens.tanY, 1e-5f), "editor preview: the lens is square");
    // The sphere of a box: its centre, half its diagonal; tiny boxes get the minimum.
    V3 centre;
    float radius;
    P::SphereOf({-1, 0, -2}, {3, 6, 2}, 0.0f, &centre, &radius);
    Check(Near(centre, {1, 3, 0}) && Near(radius, std::sqrt(4.0f + 9.0f + 4.0f)), "editor preview: a box's sphere");
    P::SphereOf({0, 0, 0}, {0, 0, 0}, 4.0f, &centre, &radius);
    Check(Near(radius, 4.0f), "editor preview: a point gets the minimum radius");
    // From the fitted distance the sphere's edge is the half-angle (less the margin) off the view's centre.
    float half = 0.35f, d = P::FitDistance(100.0f, half, 1.0f);
    Check(Near(std::asin(100.0f / d), half, 1e-5f), "editor preview: the sphere just fits at margin 1");
    Check(Near(P::FitDistance(100.0f, half, 1.1f), d * 1.1f, 1e-3f), "editor preview: the margin scales the distance");
    // The eye: at the distance, looking at the centre, above it when the pitch is positive.
    P::Orbit orbit;
    orbit.yaw = 0.0f;
    orbit.pitch = 0.5f;
    P::Eye eye = P::OrbitEye({10, 20, 30}, 200.0f, orbit);
    V3 toCentre = V3{10, 20, 30} - eye.pos;
    Check(Near(std::sqrt(LE::Dot(toCentre, toCentre)), 200.0f, 1e-2f) &&
              Near(LE::Normalize(toCentre), eye.frame.back * -1.0f, 1e-5f) && eye.pos.y > 20.0f && eye.pos.z < 30.0f,
          "editor preview: the eye looks at the centre from above, behind at yaw 0");
    // Every corner of boxes of all shapes inside the picture, from every side, at zoom 1; zoomed in, some fall out.
    const V3 boxes[][2] = {{{-50, 0, -50}, {50, 100, 50}},   {{0, 0, 0}, {1000, 20, 30}}, {{-5, -5, -400}, {5, 5, 400}},
                           {{100, 200, 300}, {101, 260, 301}}, {{-3, 0, -3}, {3, 1, 3}}};
    bool inside = true, outsideZoomed = true;
    for (const auto& box : boxes) {
        for (int i = 0; i < 24; ++i) {
            P::Orbit o;
            o.yaw = -3.1f + 0.27f * i;
            o.pitch = P::kMinPitch + (P::kMaxPitch - P::kMinPitch) * (i % 7) / 6.0f;
            inside &= Extent(box[0], box[1], o) <= 1.0f;
            o.zoom = 3.0f;
            outsideZoomed &= Extent(box[0], box[1], o) > 1.0f;
        }
    }
    Check(inside, "editor preview: framed boxes fit the picture from every side");
    Check(outsideZoomed, "editor preview: zoomed in three times they don't");
}
SELFTEST(Early, TestEditorPreviewFraming);

void TestEditorPreviewOrbit()
{
    P::Orbit orbit;
    float yaw = orbit.yaw, pitch = orbit.pitch;
    P::Drag(&orbit, 10.0f, 0.0f, 0.01f);
    Check(Near(orbit.yaw, yaw - 0.1f) && Near(orbit.pitch, pitch), "editor preview: a drag right turns the yaw");
    P::Drag(&orbit, 0.0f, -20.0f, 0.01f);
    Check(Near(orbit.pitch, pitch - 0.2f), "editor preview: a drag up lowers the eye");
    P::Drag(&orbit, 0.0f, 10000.0f, 0.01f);
    Check(Near(orbit.pitch, P::kMaxPitch), "editor preview: the pitch stops at the top");
    P::Drag(&orbit, 0.0f, -10000.0f, 0.01f);
    Check(Near(orbit.pitch, P::kMinPitch), "editor preview: and at the bottom");
    P::Drag(&orbit, 5000.0f, 0.0f, 0.01f);
    Check(std::fabs(orbit.yaw) <= 6.2831853f, "editor preview: the yaw stays within a turn");
    P::Orbit zoom;
    P::Wheel(&zoom, 1.0f);
    Check(Near(zoom.zoom, 1.2f), "editor preview: a wheel step up zooms in 1.2 times");
    P::Wheel(&zoom, -2.0f);
    Check(Near(zoom.zoom, 1.0f / 1.2f), "editor preview: two down zoom out past 1");
    P::Wheel(&zoom, 100.0f);
    Check(Near(zoom.zoom, P::kMaxZoom), "editor preview: the zoom stops at the most");
    P::Wheel(&zoom, -100.0f);
    Check(Near(zoom.zoom, P::kMinZoom), "editor preview: and at the least");
}
SELFTEST(Early, TestEditorPreviewOrbit);

} // namespace
