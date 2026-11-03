// The engine's viewports, screen modes and occlusion requests: see viewport.h.

#include "engine/gfx/viewport.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/pc_viewport.h"
#include "engine/gfx/tod_manager.h"
#include "engine/gfx/viewport_list.h"
#include "engine/gfx/screen.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/view_setup.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec2.h"
#include "engine/maths/rect.h"
namespace render {

namespace {
// NOT ORIGINAL grouping - viewport.cpp's state (the original's globals). Made on first use, never destroyed
// (viewport_state()).
struct ViewportState {
    uint32_t occlusion_request_count_{};
    float screen_angle_{};
};

ViewportState& viewport_state()
{
    static core::NoDestroy<ViewportState> state;
    return state.get();
}

} // namespace

namespace {
constexpr float kMinusOne{-1.0f};
constexpr float kHalfDegreeRadians{0.008726646f};
constexpr std::array<float, 40> kSplitLayouts{{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f,
                          0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f,
                          0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}};
// The viewports' tables: how many viewports each screen mode has, and each mode's map of views to viewports.
constexpr std::array<int, 6> kModeViewportCounts{{1, 2, 2, 4, 1, 1}};

} // namespace

namespace {

constexpr int kViewports = 10;
constexpr int kOcclusionSlots = 0x400;
constexpr int kOcclusionRequests = 4;

constexpr uint32_t kScreenMode = 0x688B4DDB; // screen_mode
// The modes' names, by number.
constexpr std::array<uint32_t, 6> kModeNames = {{
    0x6F70EA65, // one_camera
    0x9B62E5A6, // split_vertical
    0xB07B580E, // split_horizontal
    0xB6624A73, // split_quarters
    0x6F2D1231, // horse1
    0xF624438B, // horse2
}};
constexpr uint32_t kScaleNumerator = 0x33AA4915;
constexpr uint32_t kMaxScale = 0xA0A0DB70; // max_scale
constexpr uint32_t kMinScale = 0x774B6931; // min_scale

ViewRegion*& get_viewport(int index) { return core::at(view_setup().viewports(), index); }
// The screen mode's map of views onto viewports (4 per mode at 0x00649CE0; -1: not shown).

int mode_viewport_count(int mode) { return core::at(kModeViewportCounts, mode); }
std::span<const int> mode_map_of(int mode) { return std::span<const int>(kModeMaps).subspan(4 * static_cast<size_t>(mode)); }
// The map's entry for view `i` (a mode's map is read ten entries on, past the table's end for the last modes: the
// original read the words after the table there; here those views are not shown).
int map_entry(std::span<const int> map, int i) { return static_cast<size_t>(i) < map.size() ? core::at(map, i) : -1; }

// The n-th mapped entry's viewport - the map's ten words are scanned (past the mode's four: the next modes'),
// null when there's none.
ViewRegion* current_view_slot(int n)
{
    const std::span<const int> map = view_setup().mode_map();
    int found = 0;
    for (int i = 0; i < kViewports; ++i) {
        if (map_entry(map, i) < 0)
            continue;
        if (found == n)
            return get_viewport(map_entry(map, i));
        ++found;
    }
    return nullptr;
}

uint32_t& occlusion_request_count() { return viewport_state().occlusion_request_count_; }

} // namespace

// 0x004A0DB0
ViewRegion::ViewRegion(const std::array<float, 4>* rect, gfx::ViewCam* camera)
{
    if (rect) {
        pos_x_ = core::at(*rect, 0);
        y_ = core::at(*rect, 1);
        span_ = core::at(*rect, 2);
        level_y_ = core::at(*rect, 3);
    } else {
        pos_x_ = 0.0f;
        y_ = 0.0f;
        span_ = 1.0f;
        level_y_ = 1.0f;
    }
    viewer_.assign_value(camera);
}

// 0x004A0E60
ViewRegion::~ViewRegion() { viewer_.get_release(); }

// 0x004A0CC0
float ViewRegion::project_to_screen(const maths::Vec4*, maths::Vec2*, int*)
{
    debug::printf("STUB: PlatTransformToScreenCoord\n");
    return kMinusOne; // -1
}

// 0x004A0CF0
uint8_t ViewRegion::query_point(const maths::Vec4*, int, uint8_t) { return 0; }

// 0x004A0CE0
uint8_t ViewRegion::query_rect(maths::Rect /*box*/, RectQuery /*rect_query*/) { return 0; }

// 0x004A0D00
int ViewRegion::fetch_shown(int)
{
    debug::printf("STUB: plat_get_visibility_test\n");
    return 0;
}

// 0x004A0D20
float ViewRegion::fetch_width_over_height() const
{
    const float width = span_;
    const float height = level_y_;
    const double aspect = fetch_view_ratio();
    return static_cast<float>(aspect * (static_cast<double>(width) / height));
}

// 0x004A0D50
bool ViewRegion::flag_camera_for_removal(gfx::ViewCam* camera)
{
    if (viewer_.get_pull() != camera)
        return false;
    flag_bits_ |= 1;
    return true;
}

// 0x004A0D70
void ViewRegion::remove_flagged_camera()
{
    if (!(flag_bits_ & 1))
        return;
    delete viewer_.get_pull(); // (its destructor clears the pointer)
    viewer_.get_release();
    viewer_.target_ = nullptr;
    flag_bits_ &= ~1u;
}

// 0x004A0760
void purge_doomed_cameras()
{
    for (int i = 0; i < kViewports; ++i) {
        if (get_viewport(i))
            get_viewport(i)->remove_flagged_camera();
    }
}

// 0x004A0840
void create_viewports()
{
    for (int i = 0; i < kViewports; ++i) {
        const std::span<const float> table = std::span<const float>(kSplitLayouts).subspan(4 * static_cast<size_t>(i), 4);
        const std::array<float, 4> rect = {{core::at(table, 0), core::at(table, 1), core::at(table, 2), core::at(table, 3)}};
        auto viewport = create_viewport(&rect, nullptr); // (the table's)
        get_viewport(i) = viewport;
    }
}

// 0x004A0950
gfx::ViewCam* obtain_active_lens(int view)
{
    const int index = map_entry(view_setup().mode_map(), view);
    if (index == -1)
        return nullptr;
    return get_viewport(index)->viewer().get_pull();
}

// 0x004A0970
void choose_camera(int view, gfx::ViewCam* camera)
{
    const int index = map_entry(view_setup().mode_map(), view);
    if (index == -1)
        return;
    ViewRegion* viewport = get_viewport(index);
    viewport->remove_flagged_camera();
    viewport->viewer().assign_value(camera);
}

// 0x004A09C0
gfx::ViewCam* fetch_live_camera(int n)
{
    auto* viewport = fetch_current_view(n);
    return viewport->viewer().get_pull();
}

// 0x004A09E0
gfx::ViewCam* fetch_nearest_camera(const maths::Vec4* position, float* distance)
{
    const int count = mode_viewport_count(view_setup().screen_mode());
    gfx::ViewCam* closest = nullptr;
    float best = -1.0f; // (squared)
    for (int i = 0; i < count; ++i) {
        // (none: read through null, as the original - a mode's count never exceeds its map)
        gfx::ViewCam* camera = current_view_slot(i)->viewer().get_pull();
        if (!camera)
            continue;
        const maths::Vec4* at = camera->fetch_location();
        const double dx = static_cast<double>(position->x) - at->x;
        const double dy = static_cast<double>(position->y) - at->y;
        const double dz = static_cast<double>(position->z) - at->z;
        const double squared = dx * dx + dy * dy + dz * dz;
        if (best < 0.0f || squared < best) {
            best = static_cast<float>(squared);
            closest = camera;
        }
    }
    if (!closest)
        return nullptr;
    if (distance)
        *distance = static_cast<float>(std::sqrt(static_cast<double>(best)));
    return closest;
}

// 0x004A0AD0
void set_screen_mode(int mode)
{
    auto& table = view_setup();
    table.set_mode_map(mode_map_of(mode));
    table.set_screen_mode(mode);
    modules::set_viewport_count(mode_viewport_count(mode));
    for (int i = 0; i < mode_viewport_count(mode); ++i) {
        gfx::ViewCam* camera = current_view_slot(i)->viewer().get_pull(); // (no check, as the original)
        if (!camera)
            continue;
        float angle = 0.0f;
        if (mode == 1)
            angle = static_cast<float>(static_cast<double>(script::fetch_float("Skater_Cam_Horiz_FOV")) *
                                       gfx::kHalf); // (0.5: split_vertical)
        else
            angle = script::fetch_float("Skater_Cam_Horiz_FOV");
        camera->change_h_fov(angle);
    }
}

// 0x004A0B70
void set_screen_field_of_view(float degrees)
{
    if (degrees == 0.0f)
        degrees = view_setup().base_fov_angle();
    viewport_state().screen_angle_ = degrees;
    // tan(degrees / 2) / tan(default / 2)
    view_setup().set_angle_factor(static_cast<float>(std::tan(static_cast<double>(degrees) * kHalfDegreeRadians) /
                                        std::tan(static_cast<double>(view_setup().base_fov_angle()) * kHalfDegreeRadians)));
    for (int i = 0; i < modules::viewport_count(); ++i) {
        const ViewRegion* viewport = current_view_slot(i);
        if (viewport && viewport->viewer().get_pull())
            viewport->viewer().get_pull()->refresh_effective_h_fov();
    }
}

// 0x004A0EB0
uint32_t request_occlusion_query(OcclusionRequest* request)
{
    if (occlusion_request_count() >= 3)
        return 0;
    uint32_t slot = kOcclusionSlots - 1; // (none free: the last, not marked)
    for (uint32_t i = 0; i < kOcclusionSlots; ++i) {
        if (!core::at(view_setup().occlusion_slots(), i)) {
            core::at(view_setup().occlusion_slots(), i) = 1;
            slot = i;
            break;
        }
    }
    request->slot = slot;
    core::at(view_setup().occlusion_requests(), occlusion_request_count()) = *request;
    ++occlusion_request_count();
    return slot;
}

// 0x004A0F30
void issue_occlusion_queries()
{
    for (int i = 0; i < kOcclusionRequests; ++i) {
        OcclusionRequest& request = core::at(view_setup().occlusion_requests(), i);
        if (request.slot && request.callback && !request.issued) {
            request.callback(request.data, static_cast<uint8_t>(request.slot));
            request.issued = 1;
        }
    }
}

// 0x004A0F80
void pop_occlusion_request()
{
    auto& requests = view_setup().occlusion_requests();
    for (int i = 0; i < kOcclusionRequests - 1; ++i)
        core::at(requests, i) = core::at(requests, i + 1);
    core::at(requests, kOcclusionRequests - 1).slot = 0;
    --occlusion_request_count();
}

// 0x004A07D0
bool cfunc_set_2d_in_3d_space_params(script::Struct* params, script::Script*)
{
    float value = 0.0f;
    if (params->fetch_float(kScaleNumerator, &value, 0))
        view_setup().set_scale_2d_in_3d_numerator(value);
    if (params->fetch_float(kMaxScale, &value, 0))
        view_setup().set_scale_2d_in_3d_max(value);
    if (params->fetch_float(kMinScale, &value, 0))
        view_setup().set_scale_2d_in_3d_min(value);
    return true;
}

// 0x004A08A0
bool cfunc_script_get_screen_mode(script::Struct*, script::Script* script)
{
    script::Struct* results = script->args().get();
    auto mode = static_cast<uint32_t>(view_setup().screen_mode());
    results->put_name_key(checksums::Name{kScreenMode}, mode <= 5 ? core::at(kModeNames, mode) : 0u);
    return true;
}

// 0x004A0C20
bool cfunc_set_screen_mode(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (the original's is left uninitialised: it asserts when there's none)
    params->read_hash_value(0u, &name, 1);
    for (int mode = 0; mode < 6; ++mode) {
        if (name == core::at(kModeNames, mode)) {
            set_screen_mode(mode);
            break;
        }
    }
    return true;
}

} // namespace render
