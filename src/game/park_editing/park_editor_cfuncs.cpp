// The park editor's script commands (CFunctions, park_editor.h) at 0x0058D730-0x0058F896 - the park's memory, the
// gap being made, the clipboard, the theme and the name, the camera and the cursor, the park's size, saving and
// loading - and the two helpers of resizing (get_resize_area, resize_to).
//
// The parameter names the original passes as strings (fetch_text("name") ...) are written here as string literals:
// THUG2.exe has filler bytes over some of the exe's own strings (park_cfuncs.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/mem.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/tracker.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "engine/script/game_cfuncs.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <gsl/pointers>

namespace park_editing {

bool g_collision_allowed{true};
namespace {
// NOT ORIGINAL grouping - park_editor_cfuncs.cpp's state (the original's globals). Made on first use, never destroyed
// (park_editor_cfuncs_state()).
struct ParkEditorCfuncsState {
    CellBox saved_grid_dims_{};
    uint32_t saved_grid_guard_{};
};

ParkEditorCfuncsState& park_editor_cfuncs_state()
{
    static core::NoDestroy<ParkEditorCfuncsState> state;
    return state.get();
}

constexpr uint32_t kCancelFlags = 0x049EC3F9;   // cancel_flags
constexpr uint32_t kTheme = 0x688A18F7;         // theme
constexpr uint32_t kMaxThemes = 0x7DDC0DA8;     // max_themes
constexpr uint32_t kTodScript = 0x4C72ED98;     // tod_script
constexpr uint32_t kPos = 0x7F261953;           // pos
constexpr uint32_t kSlot = 0x53F1DF98;          // slot
constexpr uint32_t kX = 0x7323E97C;             // x
constexpr uint32_t kZ = 0x9D2D8850;             // z
constexpr uint32_t kW = 0xE39CF4ED;             // w
constexpr uint32_t kL = 0x69F93D01;             // l
constexpr uint32_t kUp = 0xBC6B118F;            // up
constexpr uint32_t kWidth = 0x73E5BAD0;         // width
constexpr uint32_t kResizeArea = 0xB4F88771;    // (ResizePark's / CanCleanlyResizePark's area structure; name unknown)
constexpr uint32_t kSaveArea = 0x937EB5C8;      // (ResizePark: keep the area for later; name unknown)
constexpr uint32_t kUseSavedArea = 0x6F2C496C;  // (ResizePark: the area kept; name unknown)
constexpr uint32_t kResizeFailed = 0xA9967819;  // (run when there's no memory for the new size; name unknown)
constexpr uint32_t kExitTestPlay = 0x6B5A5BB6;  // (ExitParkEdTestPlay's script; name unknown)
constexpr uint32_t kParkedCam = 0x64716BEE;     // parked_cam
constexpr uint32_t kRailEditorCam = 0x5B509AD3; // Raileditor
constexpr uint32_t kParkedMenuUp = 0xBB9B790B;  // parked_menu_up
constexpr uint32_t kSetMenuContainer = 0xE25EE8C3; // set_menu_container
constexpr uint32_t kMenuEvent = 0x36B2EE74;        // the menus' pad events' source (name unknown)

constexpr float kRailMargin = 10.0f; // 0x00646710: how far inside the park's edge a rail's points must be

ParkLayout* hub() { return ParkLayout::singleton(); }
BuildPointer* get_cursor() { return BuildPointer::singleton(); }
ParkBuilderMode* get_editor() { return ParkBuilderMode::sole(); }
bool have_cursor() { return BuildPointer::singleton() != nullptr; }
GapRegistry* gap_registry() { return GapRegistry::singleton(); }
RailAuthoring* get_rail_editor() { return RailAuthoring::sole(); }
ObjectiveAuthoring* get_goal_editor() { return ObjectiveAuthoring::get_pull(); }

// GetParkEdMemStats's compiled-out report (the empty 0x005480A0, given the script).
inline void print_mem_stats(script::Script* /*script*/) {}

// ResizePark's area (`area` = {x, z, w, l}): x, z, w, l, one floor step high. (A missing one is 0 here - the
// original takes what's on its stack.)
CellBox area_from_structure(script::Struct* area)
{
    int x = 0;
    int z = 0;
    int w = 0;
    int l = 0;
    area->fetch_int_value(kX, &x, 0);
    area->fetch_int_value(kZ, &z, 0);
    area->fetch_int_value(kW, &w, 0);
    area->fetch_int_value(kL, &l, 0);
    return CellBox::get_make(static_cast<uint8_t>(x), 0, static_cast<uint8_t>(z), static_cast<uint8_t>(w), 1,
                             static_cast<uint8_t>(l));
}

// The world rectangles of `area` the rails and the goals must be within: its corner and the corner plus its size
// - the rails' 10 inside that.
struct PanelRect {
    float x0, z0, x1, z1;
};
void world_rectangles(CellBox area, PanelRect* rails, PanelRect* goals)
{
    maths::Vec4 size;
    const maths::Vec4 corner = hub()->cell_to_world_pos(area, &size);
    rails->x0 = static_cast<float>(static_cast<double>(corner.x) + kRailMargin);
    rails->z0 = static_cast<float>(static_cast<double>(corner.z) + kRailMargin);
    rails->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x - kRailMargin);
    rails->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z - kRailMargin);
    goals->x0 = corner.x;
    goals->z0 = corner.z;
    goals->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x);
    goals->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z);
}

// ResizePark's area kept by its kSaveArea flag (a static CellBox: 0x007CE5E0, its guard bit 0x007CE5E8 & 1).
CellBox& saved_area()
{
    uint32_t& guard = park_editor_cfuncs_state().saved_grid_guard_;
    CellBox& saved = park_editor_cfuncs_state().saved_grid_dims_;
    if (!(guard & 1)) {
        guard |= 1;
        saved.assign_value(0, 0, 0, 0, 0, 0);
    }
    return saved;
}

} // namespace

// 0x0058D730
bool cfunc_free_up_memory_for_playing_park(script::Struct*, script::Script*)
{
    hub()->create_gap_manager_copy();
    hub()->teardown(1);
    hub()->use_gap_manager_copy();
    return true;
}

// 0x0058D760
bool cfunc_is_park_unsaved(script::Struct*, script::Script*) { return !hub()->has_local_park_copy(); }

// 0x0058D780
bool cfunc_set_edited_park_gap_info(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    params->fetch_text("name", &name, 0);
    int score = -1;
    params->fetch_int_value("score", &score, 0);
    get_cursor()->set_gap_label_and_points(name, score);
    script::Struct* flags = nullptr;
    params->read_sub_struct(kCancelFlags, &flags, 0);
    get_cursor()->set_gap_void_mask(flags);
    return true;
}

// 0x0058D820
bool cfunc_park_editor_selection_area_too_big_to_copy(script::Struct*, script::Script*)
{
    return get_cursor()->is_area_selection_invalid();
}

// 0x0058D840
bool cfunc_copy_park_editor_selection_to_clipboard(script::Struct*, script::Script*)
{
    const bool copied = get_cursor()->copy_area_to_clipboard();
    get_cursor()->drop_area_marquee();
    return copied;
}

// 0x0058D870
bool cfunc_cut_park_editor_area_selection(script::Struct*, script::Script*)
{
    const bool copied = get_cursor()->copy_area_to_clipboard();
    if (copied) {
        get_cursor()->destroy_selection();
        get_cursor()->flatten_selection();
    }
    get_cursor()->drop_area_marquee();
    return copied;
}

// 0x0058D8C0
bool cfunc_park_editor_area_selection_delete_pieces(script::Struct*, script::Script*)
{
    return get_cursor()->destroy_selection();
}

// 0x0058D8E0
bool cfunc_continue_park_editor_area_selection(script::Struct*, script::Script*)
{
    get_cursor()->restart_area_selection();
    return true;
}

// 0x0058D900
bool cfunc_set_editor_theme(script::Struct* params, script::Script*)
{
    int theme = 0;
    params->fetch_int_value(kTheme, &theme, 0);
    hub()->set_shell_size(theme);
    return true;
}

// 0x0058D940
bool cfunc_set_custom_park_name(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    params->fetch_text("name", &name, 1);
    hub()->set_park_name(name);
    return true;
}

// 0x0058D980
bool cfunc_optimize_park_geometry(script::Struct*, script::Script*)
{
    hub()->rebuild_park_geometry(1);
    return true;
}

// 0x0058D9A0
bool cfunc_get_park_ed_mem_stats(script::Struct*, script::Script* script)
{
    print_mem_stats(script);
    return true;
}

// 0x0058D9B0
bool cfunc_park_ed_mem_snapshot(script::Struct* params, script::Script*)
{
    uint32_t marker = 0;
    if (params->read_hash_value(0u, &marker, 0))
        get_marker(marker);
    return true;
}

// 0x0058D9E0
bool cfunc_enable_park_ed_cursor(script::Struct* params, script::Script*)
{
    int on = 0;
    params->fetch_int_value(0u, &on, 0);
    if (have_cursor())
        get_cursor()->toggle_shown(on != 0);
    return true;
}

// 0x0058DA40
bool cfunc_exit_park_ed_test_play(script::Struct*, script::Script*)
{
    script::launch_script(kExitTestPlay, nullptr, nullptr, false, nullptr);
    mark_progress("Ending Test Play");
    return true;
}

// 0x0058DA70
bool cfunc_park_update_usage_info(script::Struct*, script::Script*)
{
    MemoryStats stats{};
    get_memory_stats(&stats);
    return true;
}

// 0x0058DFB0
bool cfunc_set_park_editor_time_of_day(script::Struct* params, script::Script*)
{
    uint32_t script = 0;
    params->read_hash_value(kTodScript, &script, 0);
    get_editor()->lighting_script_ = script;
    return true;
}

// 0x0058DFE0
bool cfunc_get_park_editor_time_of_day_script(script::Struct*, script::Script* script)
{
    const uint32_t tod = get_editor()->lighting_script_;
    if (!tod)
        return false;
    script->args_->put_name_key(kTodScript, tod);
    return true;
}

// 0x0058E010
bool cfunc_rebuild_park_node_array(script::Struct*, script::Script*)
{
    hub()->builder_->build_goal_node_array(); // the park's node array made again
    return true;
}

// 0x0058E020
bool cfunc_get_park_editor_cursor_pos(script::Struct*, script::Script* script)
{
    const maths::Vec4 pos = get_cursor()->at_point_;
    script->args_->put_vec3(kPos, pos.x, pos.y, pos.z);
    return true;
}

// 0x0058E070
bool cfunc_custom_park_mode(script::Struct* params, script::Script*)
{
    const ParkBuilderMode* editor = get_editor();
    if (params->has_bare_flag("editing"))
        return editor->cur_phase_ == ParkBuilderMode::kStateEdit;
    if (params->has_bare_flag("testing"))
        return editor->cur_phase_ == ParkBuilderMode::kStateTestPlay;
    if (params->has_bare_flag("just_using"))
        return editor->cur_phase_ != ParkBuilderMode::kStateOff;
    return false;
}

// 0x0058E0F0
bool cfunc_set_park_editor_max_players(script::Struct* params, script::Script*)
{
    int players = 1;
    params->fetch_int_value(0u, &players, 0);
    hub()->builder_->set_player_cap(players);
    return true;
}

// 0x0058E170
CellBox* get_resize_area(CellBox* out, script::Struct* params)
{
    CellBox area = hub()->build_area_;
    int w = area.w;
    int l = area.l;
    params->fetch_int_value("w", &w, 0);
    params->fetch_int_value("l", &l, 0);
    int inc_w = w - area.w;
    int inc_l = l - area.l;
    if (params->fetch_int_value("inc_w", &inc_w, 0))
        w = area.w + inc_w;
    if (params->fetch_int_value("inc_l", &inc_l, 0))
        l = area.l + inc_l;
    // (the corner moves by half the change, to keep the park centred)
    area.x = static_cast<uint8_t>(area.x - static_cast<uint8_t>(inc_w / 2));
    area.z = static_cast<uint8_t>(area.z - static_cast<uint8_t>(inc_l / 2));
    area.w = static_cast<uint8_t>(w);
    area.l = static_cast<uint8_t>(l);
    *out = area;
    return out;
}

// 0x0058E2D0
bool cfunc_can_cleanly_resize_park(script::Struct* params, script::Script*)
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    script::Struct* structure = nullptr;
    if (!params->read_sub_struct(kResizeArea, &structure, 0))
        return false;
    area = area_from_structure(structure);
    bool clean = true;
    if (hub()->has_pieces_outside(area))
        clean = false;
    PanelRect rails{};
    PanelRect goals{};
    world_rectangles(area, &rails, &goals);
    // 0x004F4600: whether a rail point lies outside the rectangle
    if (get_rail_editor()->any_point_outside(rails.x0, rails.z0, rails.x1, rails.z1))
        clean = false;
    // 0x004E8300: whether a goal of the park lies outside the rectangle
    if (get_goal_editor()->any_park_goal_outside(goals.x0, goals.z0, goals.x1, goals.z1))
        clean = false;
    return clean;
}

// 0x0058E490
void resize_to(CellBox area)
{
    mark_progress("ResizePark START--------");
    bool& collision_allowed = g_collision_allowed; // (the generator's rebuild_collision runs only when set)
    collision_allowed = false;
    hub()->rebuild_inner_shell(area);
    hub()->rebuild_area(nullptr, false);
    PanelRect rails{};
    PanelRect goals{};
    world_rectangles(area, &rails, &goals);
    // 0x004F74B0: the rails' points outside the rectangle removed
    get_rail_editor()->delete_points(rails.x0, rails.z0, rails.x1, rails.z1, 0);
    // 0x004E8390: the park's goals outside the rectangle removed
    get_goal_editor()->remove_park_goals_outside(goals.x0, goals.z0, goals.x1, goals.z1);
    if (have_cursor())
        get_cursor()->remove_clipboards_too_big();
    if (have_cursor())
        get_cursor()->toggle_shown(false);
    collision_allowed = true;
    hub()->builder_->rebuild_collision();
    mark_progress("ResizePark END--------");
}

// 0x0058E5E0
bool cfunc_resize_park(script::Struct* params, script::Script*)
{
    CellBox& saved = saved_area();
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    script::Struct* structure = nullptr;
    if (params->read_sub_struct(kResizeArea, &structure, 0)) {
        area = area_from_structure(structure);
    } else {
        CellBox asked{};
        area = *get_resize_area(&asked, params);
    }
    if (params->has_bare_flag(kSaveArea)) {
        saved = area;
        return true;
    }
    if (params->has_bare_flag(kUseSavedArea))
        area = saved;
    if (!hub()->can_resize_to(area)) {
        script::launch_script(kResizeFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    resize_to(area);
    return true;
}

// 0x0058E7E0
bool cfunc_get_current_park_bounds(script::Struct*, script::Script* script)
{
    const CellBox bounds = hub()->build_area_;
    script->args_->put_int(kX, bounds.x);
    script->args_->put_int(kZ, bounds.z);
    script->args_->put_int(kW, bounds.w);
    script->args_->put_int(kL, bounds.l);
    return true;
}

// 0x0058E870
bool cfunc_can_change_park_dimension(script::Struct* params, script::Script*)
{
    const CellBox bounds = hub()->build_area_;
    const CellBox shell = hub()->shell_bounds_;
    script::Array* size = nullptr;
    if (params->fetch_list(0u, &size, 0)) {
        // [w l]: whether it's another size than the park's
        const uint32_t w = size->fetch_part(0);
        const uint32_t l = size->fetch_part(1);
        return w != bounds.w || l != bounds.l;
    }
    const bool up = params->has_bare_flag(kUp);
    if (params->has_bare_flag(kWidth))
        return up ? bounds.w < shell.w : bounds.w > 16;
    return up ? bounds.l < shell.l : bounds.l > 16;
}

// 0x0058E9A0
bool cfunc_save_park_to_disk(script::Struct* params, script::Script*)
{
    int slot = 0;
    params->fetch_int_value(kSlot, &slot, 1);
    get_editor()->transfer_park_file(true, slot, false);
    return true;
}

// 0x0058E9E0
bool cfunc_load_park_from_disk(script::Struct* params, script::Script*)
{
    int slot = 0;
    params->fetch_int_value("slot", &slot, 1);
    const bool defer_regen = params->has_bare_flag("block_rebuild");
    get_editor()->transfer_park_file(false, slot, defer_regen);
    return true;
}

// 0x0058EA40
bool cfunc_fire_custom_park_gap(script::Struct* params, script::Script* script)
{
    int index = 0; // (the original's is whatever was on its stack when gap_index is missing)
    params->fetch_int_value("gap_index", &index, 1);
    gap_registry()->fire_gap(index, script);
    return true;
}

// 0x0058EA70
bool cfunc_get_edited_park_gap_name(script::Struct*, script::Script* script)
{
    script->args_->append_word("name", get_cursor()->get_gap_title());
    return true;
}

// 0x0058EAA0
bool cfunc_get_editor_theme(script::Struct*, script::Script* script)
{
    script->args_->put_int(kTheme, hub()->get_shell_size());
    return true;
}

// 0x0058EAD0
bool cfunc_get_editor_max_themes(script::Struct*, script::Script* script)
{
    script->args_->put_int(kMaxThemes, 5);
    return true;
}

// 0x0058EAF0
bool cfunc_get_custom_park_name(script::Struct* params, script::Script* script)
{
    int length = -1;
    params->fetch_int_value(0u, &length, 0);
    const char* name = hub()->get_park_name();
    if (length == -1) {
        script->args_->append_word("name", name ? name : "unnamed park");
        return true;
    }
    // The first `length` characters (no more than the buffer holds - as the original, which doesn't check).
    std::array<char, 0x40> shortened{};
    if (length > 0)
        std::memcpy(shortened.data(), name, static_cast<size_t>(length));
    core::at(shortened, length) = 0;
    script->args_->append_word("name", shortened.data());
    return true;
}

// 0x0058EB90
bool cfunc_is_custom_park(script::Struct*, script::Script*)
{
    return get_editor()->cur_phase_ != ParkBuilderMode::kStateOff;
}

// 0x0058EBB0
bool cfunc_is_park_full(script::Struct*, script::Script*)
{
    MemoryStats stats{};
    get_memory_stats(&stats);
    return get_attributes().max_usage_ >= 1.0f;
}

// 0x0058ED00
bool cfunc_switch_park_editor_menu_piece_to_most_recent_clipboard(script::Struct*, script::Script*)
{
    ParkBuilderMode* editor = get_editor();
    get_cursor()->switch_to_clipboard();
    editor->refresh_piece_menu();
    objects::get_tracker()->launch_event(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    return true;
}

// 0x0058F750
bool cfunc_switch_to_park_editor_camera(script::Struct*, script::Script*)
{
    auto* objects = static_cast<objects::ObjectHubBase*>(objects::g_composite_registry.get());
    if (!objects->look_up_entity(kParkedCam)) {
        // The camera object, from parkedcam_composite_structure.
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        params->put_name_key("name", kParkedCam);
        auto* composites = objects::g_composite_registry.get();
        composites->spawn_entity_from_node(script::fetch_list("parkedcam_composite_structure"), params, true);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    // 0x0059F890: the active camera - the rail editor's while placing rails, else parked_cam
    auto set_camera = [](uint32_t id, int viewport, int take_current_view) {
        commands::make_camera_active(id, viewport, take_current_view != 0);
    };
    if (have_cursor() && get_cursor()->mode_sel_ == BuildPointer::kModeRailPlacement)
        set_camera(kRailEditorCam, 0, 0);
    else
        set_camera(kParkedCam, 0, 0);
    return true;
}

// 0x0058F870
bool cfunc_bind_park_editor_to_controller(script::Struct* params, script::Script*)
{
    int controller = 0; // (the original's is whatever was on its stack when there's none)
    params->fetch_int_value(0u, &controller, 1);
    get_editor()->link_to_pad(controller);
    return true;
}

} // namespace park_editing
