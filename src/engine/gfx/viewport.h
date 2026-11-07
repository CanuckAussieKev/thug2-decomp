#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "base/core/api.h"
#include "base/core/layout_32bit.h"
#include "engine/memory/mem.h"
#include "base/maths/vec4.h"
#include "engine/objects/smtptr.h"
#include "base/maths/vec2.h"
#include "base/maths/rect.h"

namespace gfx {
class ViewCam;
}
namespace script {
class Context;
class Struct;
} // namespace script

// The engine's viewports (Nx): ten of them (0x00701350, made at start-up), each a rectangle of the screen in
// fractions and the camera it shows. A screen mode (one_camera, split_vertical, split_horizontal, split_quarters,
// horse1, horse2) maps up to four "views" onto them (the mode's map: -1 for a view not shown); the active
// viewports are the mapped ones in map order. Also the occlusion queries the frame issues (four requests at most).
namespace render {

constexpr std::size_t kViewRegionFlagBitsOffset = 0x14;
constexpr std::size_t kViewRegionViewerOffset = 0x18;
constexpr std::size_t kViewRegionBytes = 0x24;

// The engine's viewport (0x24 bytes, vtable 0x00649DA0; the PC's PcViewport derives from it).
class ViewRegion : public memory::HeapObject {
public:
    // 0x004A0DB0: the rectangle given (x, y, width, height - none: the whole screen) and the camera.
    ViewRegion(const std::array<float, 4>* rect, gfx::ViewCam* camera);
    // 0x004A0E60 (0x004A0E80 the scalar deleting destructor, vtable slot 0): the camera let go.
    virtual ~ViewRegion();
    ViewRegion(const ViewRegion&) = delete;
    ViewRegion& operator=(const ViewRegion&) = delete;
    // 0x004A0CC0 (vtable slot 1): "STUB: PlatTransformToScreenCoord"; -1.
    // (`at`: the screen x and y, written only where the projection succeeds)
    virtual float project_to_screen(const maths::Vec4* point, maths::Vec2* at, int* depth);
    // 0x004A0CF0 (slot 2; the same code is in many vtables): no query, 0.
    virtual uint8_t query_point(const maths::Vec4* point, int unused, uint8_t query);
    // An occlusion query's depth and its slot.
    struct RectQuery {
        uint32_t z{};
        uint8_t query{};
    };
    // 0x004A0CE0 (slot 3; also shared): no query, 0.
    virtual uint8_t query_rect(maths::Rect box, RectQuery rect_query);
    // 0x004A0D00 (slot 4): "STUB: plat_get_visibility_test"; 0.
    virtual int fetch_shown(int unused);

    // 0x004A0D20: the screen's aspect times the rectangle's width / height.
    [[nodiscard]] float fetch_width_over_height() const;
    // 0x004A0D50: if `camera` is this viewport's, it's marked to be deleted (flag 1) and true returned.
    bool flag_camera_for_removal(gfx::ViewCam* camera);
    // 0x004A0D70: a camera marked for deletion is deleted (its virtual [0]) and let go.
    void remove_flagged_camera();

    // The fields other code reads and sets.
    [[nodiscard]] float pos_x() const { return pos_x_; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float span() const { return span_; }
    [[nodiscard]] float level_y() const { return level_y_; }
    objects::SharedRef<gfx::ViewCam>& viewer() { return viewer_; }
    [[nodiscard]] const objects::SharedRef<gfx::ViewCam>& viewer() const { return viewer_; }

private:
    float pos_x_{0.0F}, y_{0.0F}, span_{1.0F}, level_y_{1.0F}; // +0x04 fractions of the screen
    uint32_t flag_bits_{0};                                    // +0x14 1: the camera is to be deleted
    objects::SharedRef<gfx::ViewCam> viewer_; // +0x18
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(ViewRegion, flag_bits_) == kViewRegionFlagBitsOffset && offsetof(ViewRegion, viewer_) == kViewRegionViewerOffset &&
                          sizeof(ViewRegion) == kViewRegionBytes),
                      "layout must match the original");
    }
};

// An occlusion query asked for (0x10 bytes; four kept at 0x00701390): the query's slot (filled in by
// request_occlusion_query), what's called once it's drawn (data, slot) and whether that's been done.
struct OcclusionRequest {
    uint32_t slot;
    void (*callback)(uint32_t data, uint32_t slot);
    uint32_t data;
    uint8_t issued; // +0x0C
    std::array<uint8_t, 3> pad;
};
constexpr std::size_t kOcclusionRequestBytes = 0x10;

static_assert(core::layout_32bit(sizeof(OcclusionRequest) == kOcclusionRequestBytes), "layout must match the original");

// 0x004A0760: every viewport's camera marked for deletion deleted.
void purge_doomed_cameras();
// 0x004A0840: the ten viewports made (the PC's create_viewport) from the rectangles at 0x00649C28.
void create_viewports();
// 0x004A0950: the camera of view `view` of the screen mode (null: the view isn't shown).
gfx::ViewCam* obtain_active_lens(int view);
// 0x004A0970: view `view`'s camera set (nothing if the view isn't shown); its old one deleted if it was marked.
void choose_camera(int view, gfx::ViewCam* camera);
// 0x004A09C0: the camera of the n-th active viewport (0x004A06D0 - no check for none).
gfx::ViewCam* fetch_live_camera(int view_number);
// 0x004A09E0: of the active viewports' cameras, the one nearest `position` (null: none) and its distance (if
// `distance` isn't null).
THUG2_API gfx::ViewCam* fetch_nearest_camera(const maths::Vec4* position, float* distance);
// 0x004A0AD0: the screen mode (0-5) set, and each active viewport's camera given the horizontal angle
// Skater_Cam_Horiz_FOV (halved for split_vertical).
void set_screen_mode(int mode);
// 0x004A0B70: the screens' angle (0: the cameras' default, get_screen_angle) kept (0x00701344), the angle factor
// (0x0067E544) = tan(angle / 2) / tan(default / 2), and the active viewports' cameras updated.
void set_screen_field_of_view(float degrees);
// 0x004A0EB0: an occlusion query requested for this frame (three at most: 0 when full): a free query slot taken
// (1023 if none - not marked), written into `request` and the request copied in. Returns the slot.
uint32_t request_occlusion_query(OcclusionRequest* request);
// 0x004A0F30: each request with a slot and a callback not yet issued: called (data, slot) and marked issued.
void issue_occlusion_queries();
// 0x004A0F80: the first request dropped (the others moved up).
void pop_occlusion_request();

// 0x004A07D0: Set2DIn3DSpaceParams [0x33AA4915=<f>] [max_scale=<f>] [min_scale=<f>]: the size factor of screen
// elements placed in 3D (the numerator, its upper and lower bounds). Always true.
bool cfunc_set_2d_in_3d_space_params(script::Struct* params, script::Context context);
// 0x004A08A0: ScriptGetScreenMode: screen_mode=<the mode's name> added to the script's parameters. Always true.
bool cfunc_script_get_screen_mode(script::Struct* params, script::Context context);
// 0x004A0C20: SetScreenMode <mode name>: an unknown name changes nothing. Always true.
bool cfunc_set_screen_mode(script::Struct* params, script::Context context);

// The split-screen modes' viewport maps: four viewport numbers per mode (-1: none).
inline constexpr std::array<int, 24> kModeMaps{{0, -1, -1, -1, 2, 3, -1, -1, 4, 5, -1, -1, 6, 7, 8, 9, 0, -1, -1, -1, -1, 1, -1, -1}};

} // namespace render

// The number of viewports the screen mode shows (set_screen_mode sets it; the frame, the particles, the suspension and
// the skate module read it). Named in modules:: as the skate module, which used to hold it, named it.
namespace modules {

int viewport_count();
void set_viewport_count(int value);

} // namespace modules
