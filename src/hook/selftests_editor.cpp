// Self-tests for the level editor (src/game/front/level_editor.*, not original): the fly camera's maths, the mouse ray
// against the game's own projection (D3DX's look-at and perspective as Nx::SetCamera 0x004DB200 makes them), the ray
// tests and the drag plane.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "game/front/level_editor.h"
#include "game/mth/matrix.h"
#include "hook/game.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
using LE::V3;

bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance = 1e-4f)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

// --- flying -----------------------------------------------------------------------------------------------------

void TestEditorFly()
{
    // Straight ahead along +z: back -z, right -x (a right-handed frame: right x up = back), up +y.
    LE::Frame frame = LE::FrameFromYawPitch(0.0f, 0.0f);
    Check(Near(frame.back, {0, 0, -1}) && Near(frame.right, {-1, 0, 0}) && Near(frame.up, {0, 1, 0}),
          "editor: frame for yaw 0, pitch 0");
    // A quarter turn: looking along +x.
    frame = LE::FrameFromYawPitch(1.5707963f, 0.0f);
    Check(Near(frame.back, {-1, 0, 0}) && Near(frame.right, {0, 0, 1}), "editor: frame for yaw 90");
    // Looking up 30 degrees: up tilts back.
    frame = LE::FrameFromYawPitch(0.0f, 0.5235988f);
    Check(Near(frame.back, {0, -0.5f, -0.8660254f}) && Near(frame.up, {0, 0.8660254f, -0.5f}),
          "editor: frame for pitch 30");

    bool orthonormal = true, roundTrip = true;
    for (int i = 0; i < 40; ++i) {
        float yaw = -3.0f + 0.157f * i, pitch = -1.5f + 0.075f * i;
        LE::Frame f = LE::FrameFromYawPitch(yaw, pitch);
        orthonormal &= Near(LE::Dot(f.right, f.right), 1) && Near(LE::Dot(f.up, f.up), 1) &&
                       Near(LE::Dot(f.back, f.back), 1) && Near(LE::Dot(f.right, f.up), 0) &&
                       Near(LE::Dot(f.right, f.back), 0) && Near(LE::Dot(f.up, f.back), 0) &&
                       Near(LE::Cross(f.right, f.up), f.back) && f.up.y >= 0.0f && Near(f.right.y, 0);
        float y2, p2;
        LE::YawPitchFromBack(f.back, &y2, &p2);
        LE::Frame g = LE::FrameFromYawPitch(y2, p2);
        roundTrip &= Near(g.back, f.back) && Near(g.right, f.right) && Near(p2, pitch);
    }
    Check(orthonormal, "editor: frames orthonormal, right level, up upwards");
    Check(roundTrip, "editor: yaw / pitch from a frame's back");

    // W / D / E move along the view, its right, the world's up; diagonals keep the speed.
    frame = LE::FrameFromYawPitch(0.3f, -0.4f);
    V3 start = {10, 20, 30};
    V3 forward = frame.back * -1.0f;
    Check(Near(LE::Fly(start, frame, {1, 0, 0}, 5.0f), start + forward * 5.0f), "editor: W flies along the view");
    Check(Near(LE::Fly(start, frame, {-1, 0, 0}, 5.0f), start - forward * 5.0f), "editor: S flies back");
    Check(Near(LE::Fly(start, frame, {0, 1, 0}, 5.0f), start + frame.right * 5.0f), "editor: D flies right");
    Check(Near(LE::Fly(start, frame, {0, -1, 0}, 5.0f), start - frame.right * 5.0f), "editor: A flies left");
    Check(Near(LE::Fly(start, frame, {0, 0, 1}, 5.0f), {10, 25, 30}), "editor: E flies straight up");
    Check(Near(LE::Fly(start, frame, {0, 0, -1}, 5.0f), {10, 15, 30}), "editor: Q flies straight down");
    V3 diagonal = LE::Fly(start, frame, {1, 1, 1}, 5.0f) - start;
    Check(Near(std::sqrt(LE::Dot(diagonal, diagonal)), 5.0f), "editor: diagonals at the same speed");
    Check(Near(LE::Fly(start, frame, {0, 0, 0}, 5.0f), start), "editor: no keys, no move");

    // Mouse look: right turns right, up looks up, pitch clamped.
    float yaw = 0.2f, pitch = 0.1f;
    LE::Frame before = LE::FrameFromYawPitch(yaw, pitch);
    LE::Look(&yaw, &pitch, 50.0f, 0.0f, 0.002f);
    LE::Frame after = LE::FrameFromYawPitch(yaw, pitch);
    Check(LE::Dot(after.back * -1.0f, before.right) > 0.05f, "editor: mouse right turns the view right");
    LE::Look(&yaw, &pitch, 0.0f, -50.0f, 0.002f);
    Check(Near(pitch, 0.2f), "editor: mouse up looks up");
    LE::Look(&yaw, &pitch, 0.0f, -100000.0f, 0.002f);
    Check(Near(pitch, 89.0f * 3.14159265f / 180.0f), "editor: pitch stops at 89 degrees");
    LE::Look(&yaw, &pitch, 0.0f, 100000.0f, 0.002f);
    Check(Near(pitch, -89.0f * 3.14159265f / 180.0f), "editor: and at -89");

    // Turning about up: +z to +x, as yaw.
    Check(Near(LE::TurnAboutUp({0, 7, 1}, 1.5707963f), {1, 7, 0}), "editor: turn about up");
    Check(Near(LE::TurnAboutUp({2, 0, 0}, 1.5707963f), {0, 0, -2}), "editor: turn about up (x)");
}
SELFTEST(Early, TestEditorFly);

// --- the mouse ray against the game's projection ----------------------------------------------------------------

// v * m (row vector, the game's and D3DX's convention), all four columns.
void Transform(const float v[4], const Mth::Matrix& m, float out[4])
{
    const float* r[4] = {&m.row[0].x, &m.row[1].x, &m.row[2].x, &m.row[3].x};
    for (int j = 0; j < 4; ++j)
        out[j] = v[0] * r[0][j] + v[1] * r[1][j] + v[2] * r[2][j] + v[3] * r[3][j];
}

void TestEditorUnproject()
{
    auto lookAt = GameFunction<void*(__stdcall*)(void*, const float*, const float*, const float*)>(0x005C0985);
    auto perspective = GameFunction<void*(__stdcall*)(void*, float, float, float, float)>(0x005C0ACD);
    auto multiply = GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C);
    struct Pose {
        V3 pos;
        float yaw, pitch;
    };
    const Pose poses[] = {{{0, 0, 0}, 0.0f, 0.0f},
                          {{1200.5f, 340.0f, -8000.25f}, 2.1f, -0.35f},
                          {{-500, 80, 2500}, -1.2f, 0.6f},
                          {{30, -900, 45}, 3.0f, -1.3f}};
    const float angles[] = {72.0f, 90.0f, 55.3f, 99.5f};
    const float aspects[] = {4.0f / 3.0f, 16.0f / 9.0f, 0.75f};
    const float points[][2] = {{0, 0}, {0.5f, 0.25f}, {-0.9f, 0.8f}, {0.99f, -0.99f}, {-0.3f, -0.6f}};
    const uint32_t viewport[4] = {80, 0, 1280, 720};
    const float depths[] = {10.0f, 350.0f, 7000.0f};
    Mth::Matrix scratch;
    multiply(&scratch, &scratch, &scratch); // (D3DX picks its implementation on the first call)
    int failures = 0, projectFailures = 0, pixelFailures = 0;
    for (const Pose& pose : poses)
        for (float angle : angles)
            for (float aspect : aspects) {
                LE::Frame frame = LE::FrameFromYawPitch(pose.yaw, pose.pitch);
                // As Nx::SetCamera: the target one step along the view, the camera's up; width at the near plane (2)
                // 4 tan(angle / 2), height width / (aspect * 0.90909094).
                float eye[3] = {pose.pos.x, pose.pos.y, pose.pos.z};
                float at[3] = {pose.pos.x - frame.back.x, pose.pos.y - frame.back.y, pose.pos.z - frame.back.z};
                float up[3] = {frame.up.x, frame.up.y, frame.up.z};
                Mth::Matrix view, projection;
                lookAt(&view, eye, at, up);
                float width = static_cast<float>(std::tan(static_cast<double>(angle) * 0.008726646f) * 4.0);
                float height = static_cast<float>(1.0 / (static_cast<double>(aspect) * 0.90909094f)) * width;
                perspective(&projection, width, height, 2.0f, 96000.0f);
                LE::Lens lens = LE::LensFor(angle, aspect);
                for (const auto& p : points)
                    for (float depth : depths) {
                        // A world point at that place on screen (from our maths), through the game's matrices.
                        V3 dir = LE::RayDirection(frame, lens, p[0], p[1]);
                        V3 world = pose.pos + dir * depth;
                        float v[4] = {world.x, world.y, world.z, 1.0f}, viewed[4], clip[4];
                        Transform(v, view, viewed);
                        Transform(viewed, projection, clip);
                        float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
                        if (!Near(nx, p[0], 2e-3f) || !Near(ny, p[1], 2e-3f) || clip[3] <= 0.0f) {
                            if (++failures <= 3) {
                                char what[200];
                                std::snprintf(what, sizeof(what),
                                              "editor: the game's projection puts the ray's point (%.3f, %.3f) at "
                                              "(%.4f, %.4f) (angle %.1f, aspect %.3f)",
                                              p[0], p[1], nx, ny, angle, aspect);
                                Check(false, what);
                            }
                        }
                        // Our projection agrees.
                        float mx, my;
                        if (!LE::Project(pose.pos, frame, lens, world, 2.0f, &mx, &my) || !Near(mx, nx, 2e-3f) ||
                            !Near(my, ny, 2e-3f))
                            ++projectFailures;
                        // Pixels and back.
                        float px, py, bx, by;
                        LE::NdcToPixel(viewport, nx, ny, &px, &py);
                        if (!LE::PixelToNdc(viewport, px, py, &bx, &by) || !Near(bx, nx, 1e-4f) || !Near(by, ny, 1e-4f))
                            ++pixelFailures;
                    }
            }
    Check(failures == 0, "editor: mouse rays through the game's projection");
    Check(projectFailures == 0, "editor: our projection is the game's");
    Check(pixelFailures == 0, "editor: pixels to device coordinates and back");
    float nx, ny;
    LE::PixelToNdc(viewport, 80.0f, 0.0f, &nx, &ny);
    Check(Near(nx, -1) && Near(ny, 1), "editor: the viewport's top left is (-1, 1)");
    const uint32_t empty[4] = {0, 0, 0, 0};
    Check(!LE::PixelToNdc(empty, 1, 1, &nx, &ny), "editor: no viewport, no ray");
    // Behind the camera: not projected.
    LE::Frame frame = LE::FrameFromYawPitch(0, 0);
    Check(!LE::Project({0, 0, 0}, frame, LE::LensFor(72, 4.0f / 3), {0, 0, -5}, 2.0f, &nx, &ny),
          "editor: points behind aren't projected");
}
SELFTEST(Early, TestEditorUnproject);

// --- ray tests, the drag plane, clipping ------------------------------------------------------------------------

void TestEditorRays()
{
    float t = -1;
    Check(LE::RaySphere({0, 0, 0}, {0, 0, 2}, {0, 0, 10}, 1.0f, &t) && Near(t, 4.5f), "editor: ray hits sphere");
    Check(!LE::RaySphere({0, 0, 0}, {0, 0, 1}, {0, 3, 10}, 1.0f, &t), "editor: ray misses sphere");
    Check(!LE::RaySphere({0, 0, 0}, {0, 0, -1}, {0, 0, 10}, 1.0f, &t), "editor: sphere behind");
    Check(LE::RaySphere({0, 0, 10}, {0, 0, 1}, {0, 0, 10}, 1.0f, &t) && t == 0.0f, "editor: inside a sphere");

    const V3 identity[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Check(LE::RayBox({0, 0, 0}, {0, 0, 1}, {0, 0, 100}, identity, {-5, -5, -5}, {5, 5, 5}, &t) && Near(t, 95),
          "editor: ray hits box");
    Check(!LE::RayBox({0, 0, 0}, {0, 0, 1}, {0, 7, 100}, identity, {-5, -5, -5}, {5, 5, 5}, &t),
          "editor: ray misses box");
    Check(!LE::RayBox({0, 0, 0}, {0, 0, -1}, {0, 0, 100}, identity, {-5, -5, -5}, {5, 5, 5}, &t),
          "editor: box behind");
    Check(LE::RayBox({0, 0, 100}, {1, 0, 0}, {0, 0, 100}, identity, {-5, -5, -5}, {5, 5, 5}, &t) && t == 0.0f,
          "editor: inside a box");
    // A box turned 45 degrees about up and twice as large (rows of length 2), its model space from 0 to 10 on x.
    float c = 1.41421356f;
    const V3 turned[3] = {{c, 0, -c}, {0, 2, 0}, {c, 0, c}};
    // Its local x axis runs along (1, 0, -1): local (5, 0, 0) is world (7.07, 0, -7.07) from its origin.
    V3 origin = {100, 0, 100};
    V3 target = origin + V3{5 * c, 1, -5 * c};
    V3 from = target + V3{0, 50, 0};
    Check(LE::RayBox(from, {0, -1, 0}, origin, turned, {0, 0, -1}, {10, 1, 1}, &t) && Near(t, 50 - 1, 1e-3f),
          "editor: ray hits a turned, scaled box on its top");
    // Just outside its depth (local z 1.5 * 2 = 3 world units off the axis, the box reaching 2).
    V3 off = target + V3{c * 1.5f, 50, c * 1.5f};
    Check(!LE::RayBox(off, {0, -1, 0}, origin, turned, {0, 0, -1}, {10, 1, 1}, &t), "editor: ray beside a turned box");
    // The corners land where the rows put them.
    V3 corners[8];
    LE::BoxCorners(origin, turned, {0, 0, -1}, {10, 1, 1}, corners);
    Check(Near(corners[0], origin + V3{-c, 0, -c}) && Near(corners[7], origin + V3{10 * c + c, 2, -10 * c + c}),
          "editor: box corners");

    // The drag plane: horizontal through height 10, the ray from (0, 100, 0) down at 45 degrees.
    Check(LE::RayPlane({0, 100, 0}, {1, -1, 0}, {5, 10, 5}, {0, 1, 0}, &t) && Near(t, 90), "editor: drag plane hit");
    Check(!LE::RayPlane({0, 100, 0}, {1, 0, 0}, {5, 10, 5}, {0, 1, 0}, &t), "editor: drag plane parallel");
    Check(!LE::RayPlane({0, 100, 0}, {1, 1, 0}, {5, 10, 5}, {0, 1, 0}, &t), "editor: drag plane behind");

    // World-space boxes told from model-space ones (only away from the world's origin).
    Check(LE::BoxLooksWorldSpace({990, 0, 1990}, {1010, 30, 2010}, {1000, 0, 2000}), "editor: a world-space box");
    Check(!LE::BoxLooksWorldSpace({-10, 0, -10}, {10, 30, 10}, {1000, 0, 2000}), "editor: a model-space box");
    Check(!LE::BoxLooksWorldSpace({-10, 0, -10}, {10, 30, 10}, {0, 5, 0}), "editor: at the origin: model space");

    // Lines clipped to the near plane.
    LE::Frame frame = LE::FrameFromYawPitch(0, 0); // looking along +z
    V3 a = {0, 0, -10}, b = {0, 0, 10};
    Check(LE::ClipToNear({0, 0, 0}, frame, 2.0f, &a, &b) && Near(a, {0, 0, 2}) && Near(b, {0, 0, 10}),
          "editor: line clipped at the near plane");
    a = {0, 0, -10};
    b = {0, 0, 1};
    Check(!LE::ClipToNear({0, 0, 0}, frame, 2.0f, &a, &b), "editor: line behind the near plane dropped");

    // The Pause key: only once it has been up (the Enter that chose the menu item is still down).
    bool released = false;
    bool held = LE::PassKey(&released, true);
    bool stillHeld = LE::PassKey(&released, true);
    bool up = LE::PassKey(&released, false);
    bool pressed = LE::PassKey(&released, true);
    Check(!held && !stillHeld && !up && pressed, "editor: the Pause key counts once it has been up");
}
SELFTEST(Early, TestEditorRays);

} // namespace
