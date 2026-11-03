#pragma once

#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"

namespace script {
class Struct;
class Script;
} // namespace script

// A camera (gfx::ViewCam, vtable 0x006487E0, an GameObjects::Entity, 0xC4 bytes): where a viewport looks from. Camera
// components copy their object's position and orientation into theirs every frame (ViewpointModule 0x0041B810 /
// 0x0041B8A0, through change_location / change_mat); the renderer and the 3D screen elements read it back through fetch_location /
// fetch_mat / fetch_effective_h_fov.
namespace gfx {

class ViewCam final : public objects::Entity {
public:
    // 0x00485BA0: at the origin (w 1, the old position too), the identity orientation, the cameras' default angle
    // (render::get_screen_angle), clip distances 12 / 12000 (the far one 30 x the distance-clipping setting when
    // that's on).
    ViewCam();
    // 0x004859F0 (0x00485A20 the deleting one): Entity's.
    ~ViewCam() override = default;
    // 0x00485B40: the member function 0x44EA2B6D (its name unknown) sets the horizontal angle from radians (the
    // unnamed float parameter); anything else goes to Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00485A00 / 0x00485A10 (4 bytes each: source only).
    maths::Vec4* fetch_location() { return &at_point_; }
    maths::Mat4* fetch_mat() { return &basis_frame_; }
    // 0x00485A50: the position kept as the previous frame's.
    void remember_prev_pos();
    // 0x00485A70
    void change_location(const maths::Vec4& pos);
    // 0x00485A90 (returns the camera's matrix: the copy's result).
    maths::Mat4& change_mat(const maths::Mat4& matrix);
    // 0x00485AA0: corrected_horiz_fov_ (h_fov_ widened by render::fetch_fov_ratio, over unknownc0_) and unknownb4_.
    void refresh_effective_h_fov();
    // 0x00485B10: the horizontal field of view in degrees as drawn (updated first).
    float fetch_effective_h_fov();
    // 0x00485B20: the horizontal field of view in degrees (at 4:3).
    void change_h_fov(float degrees);

    // The fields other code reads and sets.
    maths::Vec4& old_pos() { return old_pos_; }
    [[nodiscard]] const maths::Vec4& old_pos() const { return old_pos_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    void set_at_point(const maths::Vec4& value) { at_point_ = value; }
    maths::Mat4& basis_frame() { return basis_frame_; }
    [[nodiscard]] const maths::Mat4& basis_frame() const { return basis_frame_; }
    void set_basis_frame(const maths::Mat4& value) { basis_frame_ = value; }
    [[nodiscard]] float h_fov() const { return h_fov_; }
    void set_h_fov(float value) { h_fov_ = value; }
    [[nodiscard]] float corrected_horiz_fov() const { return corrected_horiz_fov_; }
    void set_corrected_horiz_fov(float value) { corrected_horiz_fov_ = value; }
    [[nodiscard]] float unknownb4() const { return unknownb4_; }
    void set_unknownb4(float value) { unknownb4_ = value; }
    [[nodiscard]] float near_dist() const { return near_dist_; }
    void set_near_dist(float value) { near_dist_ = value; }
    [[nodiscard]] float far_clip() const { return far_clip_; }
    void set_far_clip(float value) { far_clip_ = value; }
    [[nodiscard]] float unknownc0() const { return unknownc0_; }
    void set_unknownc0(float value) { unknownc0_ = value; }

private:
    maths::Vec4 old_pos_{};     // +0x4C
    maths::Vec4 at_point_{};    // +0x5C
    maths::Mat4 basis_frame_{}; // +0x6C: row 2 points backward (the view looks along -row 2), row 1 up
    float h_fov_;               // +0xAC: degrees, at 4:3
    float corrected_horiz_fov_{}; // +0xB0: degrees, as drawn
    float unknownb4_{};           // +0xB4: tan(half corrected_horiz_fov_) / tan(half the default angle)
    // the planes a camera starts with
    static constexpr float kNearDistance = 12.0f;
    static constexpr float kFarClip = 12000.0f;
    float near_dist_{kNearDistance}; // +0xB8: 12 (by its value)
    float far_clip_{kFarClip};       // +0xBC: 12000 (by its value)
    float unknownc0_{1.0f};     // +0xC0: 1 (divides the widened angle's tangent)
};

int draw_distance_setting();
void set_draw_distance_setting(int value);
} // namespace gfx
