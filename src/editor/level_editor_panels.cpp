// NOT ORIGINAL - the level editor's panels: the HUD text, the model the UI draws from, the help and the browser
// (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_actions.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/gfx/screen.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr std::size_t kPlaceTextSize = 64;
constexpr int kHintRefreshFrames = 6;       // the key hints are made again every this many frames
constexpr std::size_t kHintTextSize = 200;
constexpr int kHintStrandFirst = 5;         // the HUD strand of the first hint line
constexpr int kHintStrandSecond = 6;        // ... and of the second

constexpr uint32_t kRailColour = 0xFF7FB2D9;
constexpr uint32_t kPathColour = 0xFF8FD18F;
constexpr uint32_t kLadderColour = 0xFFC88FD9;
constexpr uint32_t kLedgeColour = 0xFFB98CFF;

constexpr int kModifierHintLimit = 9;       // modifier hints shown at most
constexpr double kHalfTurnDegrees = 180.0;
constexpr std::size_t kSizeTextSize = 48;
constexpr float kHelpPanelX = 96.0F; // the key help panel (before the UI scale)
constexpr float kHelpPanelY = 104.0F;
constexpr float kHelpPanelWidth = 448.0F;
constexpr float kBrowserPanelX = 380.0F; // the browser list panel
constexpr float kBrowserPanelY = 34.0F;
constexpr float kBrowserPanelWidth = 254.0F;

// --- the HUD (level_editor_hud.h): what you're doing, what's selected and under the cursor, and the keys for it ---

using hud::get_key;

} // namespace
namespace internal {

std::string describe_selection()
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        std::string text = object_edit::describe(objects.front());
        if (objects.size() > 1) {
            text += "  + " + std::to_string(objects.size() - 1) + " more";
        }
        return text;
    }
    if (scenery::has_selection()) {
        return scenery::describe_selection();
    }
    if (const uint32_t wall = wall_only()) {
        return "invisible wall " + object_edit::caption(wall) + " - " + walls::describe_state(wall);
    }
    if (picked_set().node_selected_ && std::cmp_less(picked_set().node_index_, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), picked_set().node_index_);
        return "node " + nodes::caption(node.name) + " (" + nodes::caption(node.cls) + ")";
    }
    return "Nothing";
}

const char* mode_name()
{
    if (stored_browser().open) {
        return "ADD";
    }
    if (drag_session().dragging_ && transform_session().drag_mode_ == xform::Mode::kSpinBy) {
        return "ROTATING";
    }
    if (drag_session().dragging_ && transform_session().drag_mode_ == xform::Mode::kScale) {
        return "SCALING";
    }
    if (drag_session().dragging_ && drag_session().axis_ == 0) {
        return "MOVING ALONG X";
    }
    if (drag_session().dragging_ && drag_session().axis_ == 1) {
        return "MOVING ALONG Y";
    }
    if (drag_session().dragging_ && drag_session().axis_ >= 2) {
        return "MOVING ALONG Z";
    }
    if (drag_session().dragging_) {
        return "MOVING";
    }
    if (have_selection() || picked_set().node_selected_) {
        return "SELECTED";
    }
    return "FLYING";
}

} // namespace internal
namespace {

// What's under the cursor - what a click would take (level_editor_picking.h): the next of a click-to-cycle with its
// place, or the piece a plain click passes (Alt+click takes it).
// The gizmo's handle under the cursor, in words (level_editor_transform.h).
std::string handle_hover_text(int handle)
{
    const char* axis = "";
    if (handle == 0) {
        axis = "X";
    } else if (handle == 1) {
        axis = "Y";
    } else if (handle == 2) {
        axis = "Z";
    }
    switch (gizmo_mode()) {
    case xform::Mode::kSpinBy:
        return "Rotate ring (Ctrl: snap)";
    case xform::Mode::kScale:
        return handle == xform::kCentre ? std::string("Uniform scale handle")
                                        : std::string("the ") + axis + " handle - drag to scale it along that axis";
    default:
        return std::string("the ") + axis + " arrow - drag to move along it";
    }
}
std::string hover_text()
{
    if (const uint32_t wall = view::hover_wall(); (wall != 0U) && drag_session().hover_axis_ < 0) {
        return "Invisible wall " + object_edit::caption(wall) + " (Shift+click)";
    }
    if (drag_session().hover_handle_ >= 0 && drag_session().hover_axis_ < 0) {
        return std::string("the trigger volume's ") + ((drag_session().hover_handle_ & 1) != 0 ? '+' : '-') +
               core::at(kAxisLetters, drag_session().hover_handle_ >> 1) +
               " face - drag to resize it";
    }
    std::string text = "-";
    if (drag_session().hover_axis_ >= 0) {
        text = handle_hover_text(drag_session().hover_axis_);
    } else if (picked_set().hover_sector_ != 0U) {
        text = scenery::describe_hover(picked_set().hover_sector_);
    } else if (get_live(picked_set().hover_)) {
        text = object_edit::describe(get_live(picked_set().hover_));
    }
    if (drag_session().hover_axis_ < 0 && pick_cycle().hover_cycle_ >= 0) {
        std::array<char, kPlaceTextSize> place{};
        text::format_to(place, "  (%d of %d under the cursor: click)", pick_cycle().hover_cycle_ + 1,
                        static_cast<int>(pick_cycle().cycle_.order.size()));
        text += place.data();
    } else if (drag_session().hover_axis_ < 0 && (picked_set().hover_sector_ == 0U) && !get_live(picked_set().hover_) &&
               (picked_set().hover_passed_ != 0U)) {
        text = "-  (passing " + scenery::describe_hover(picked_set().hover_passed_) + " - Alt+click takes it)";
    }
    return text;
}

} // namespace
namespace internal {

void update_hud()
{
    static int frames = 0;
    hud::tick();
    if (frames++ % kHintRefreshFrames != 0) {
        return;
    }
    std::array<char, kHintTextSize> text{};
    // (which level: always its edited copy - never the level as shipped)
    text::format_to(text, "EDITING %.24s (EDITED COPY)   %s%s%s", layout::level_title().c_str(), mode_name(),
                    unsaved_changes().unsaved_ ? "   (unsaved)" : "", unsaved_changes().offer_pending_ ? "   (autosave: Ctrl+R)" : "");
    hud::change_strand(1, text.data());
    hud::change_strand(2, "Selected: " + describe_selection());
    hud::change_strand(3, "Under the cursor: " + hover_text());
    text::format_to(text, "Camera (%.0f, %.0f, %.0f)  speed %.0f    %d changes    %d pieces moved", fly_camera().pos_.x, fly_camera().pos_.y,
                    fly_camera().pos_.z, fly_camera().speed_, unsaved_changes().moves_, scenery::get_moved_count());
    hud::change_strand(4, text.data());
    // (each line at most 90 characters with its colour codes - 6 a key)
    if (stored_browser().open) {
        hud::change_strand(kHintStrandFirst, get_key("Up/Down") + " choose   " + get_key("Left/Right") + " scenery / objects   " +
                                  get_key("Enter") + " place it");
        hud::change_strand(kHintStrandSecond, get_key("F") + " show me the original   " + get_key("B") + " or " + get_key("Esc") +
                                  " close");
    } else if (drag_session().dragging_) {
        if (drag_session().axis_ >= 0) {
            hud::change_strand(kHintStrandFirst, get_key("Mouse") + " moves it along the arrow   " + get_key("Ctrl") + " snap");
        } else {
            hud::change_strand(kHintStrandFirst, get_key("Mouse") + " move   " + get_key("Shift") + " up / down   " +
                                      get_key("Wheel") + " push / pull   " + get_key("Ctrl") + " snap");
        }
        hud::change_strand(kHintStrandSecond, get_key("Esc") + " put it back where it was");
    } else if (have_selection()) {
        hud::change_strand(kHintStrandFirst, get_key("Drag") + " move  " + get_key("Arrows") + " one axis  " + get_key("WASDQE") +
                                  " nudge  " + get_key("ZX") + " turn  " + get_key("[ ]") + " scale  " + get_key("G") +
                                  " drop");
        hud::change_strand(kHintStrandSecond, (get_primary() ? std::string() : get_key("Ctrl+click") + " remove one  ") +
                                  get_key("Ctrl+D") + " copy  " + get_key("Del") + " hide  " + get_key("Home") +
                                  " reset  " + get_key("Esc") + " deselect  " + get_key("F1") + " help");
    } else {
        hud::change_strand(kHintStrandFirst, get_key("WASD") + " fly  " + get_key("QE") + " down / up  " + get_key("Shift") +
                                  " fast  " + get_key("RMB") + " look  " + get_key("Wheel") + " dolly");
        hud::change_strand(kHintStrandSecond, get_key("Click") + " select  " + get_key("B") + " add  " + get_key("Ctrl+S") + " save  " +
                                  get_key("Ctrl+Z/Y") + " undo  " + get_key("F1") + " help");
    }
}

} // namespace internal
namespace {

// --- the UI (level_editor_ui.h): what it shows ---

std::vector<ui::BrowserItem>& ui_browser_items()
{
    static core::NoDestroy<std::vector<ui::BrowserItem>> ui_browser_items;
    return ui_browser_items.get();
}

// The keys for what you're doing now (the status bar - UX8's context hints, level_editor_hints.h: from the action
// registry's keys as bound now and the editor's state; the old HUD keeps its own lines).
std::vector<std::pair<std::string, std::string>> keys_now()
{
    hints::CtxInfo context;
    const input::KeyState keys = input::keys();
    if (ui::keys_to_editor()) {
        if ((core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U)) {
            context.mods |= actions::kCtrl;
        }
        if ((core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U)) {
            context.mods |= actions::kShift;
        }
        if ((core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U)) {
            context.mods |= actions::kAlt;
        }
    }
    context.rmb = stored_mouse().right;
    context.fly_always_ = xform::fly_always();
    if (picked_node_index() >= 0) {
        context.selection = (wall_only() != 0U) ? hints::Selection::kWall : hints::Selection::kPointNode;
    } else if (get_primary()) {
        context.selection = hints::Selection::kObject;
    } else if (wall_only() != 0U) {
        context.selection = hints::Selection::kWall;
    } else if (scenery::has_selection()) {
        context.selection = hints::Selection::kScenery;
    } else if (picked_set().node_selected_) {
        context.selection = hints::Selection::kOtherNode;
    }
    context.free_camera_ = fly_camera().free_camera_;
    context.tool = static_cast<int>(xform::at_hand().mode);
    if (drag_session().dragging_) {
        if (transform_session().drag_mode_ == xform::Mode::kSpinBy) {
            context.drag = hints::Drag::kSpinBy;
        } else if (transform_session().drag_mode_ == xform::Mode::kScale) {
            context.drag = hints::Drag::kScale;
        } else if (drag_session().axis_ >= 0) {
            context.drag = hints::Drag::kMoveAxis;
        } else {
            context.drag = hints::Drag::kShiftBy;
        }
    }
    context.modal = transform_session().modal_;
    if (drag_session().dragging_ && (!transform_session().typed_.text.empty() || transform_session().typed_.axis >= 0)) {
        context.typed = xform::describe(transform_session().drag_mode_, transform_session().typed_,
                                        drag_session().axis_ >= 0 && drag_session().axis_ < 3 ? drag_session().axis_ : -1);
    }
    context.carried = drag_session().dragging_ ? carry::total_of() : 0;
    context.browser = stored_browser().open;
    context.place_armed_ = pick_cycle().place_armed_ >= 0;
    context.ladder_drawing_ = chains::get_drawing() == chains::Blit::kLadder;
    context.chain_drawing_ = chains::get_drawing() != chains::Blit::kNone && !context.ladder_drawing_;
    context.chain_pick_ = chains::wants_pick();
    if (context.ladder_drawing_ || context.chain_pick_) {
        context.chain_waiting_ = chains::get_waiting();
    }
    context.ruler_armed_ = ruler::get_armed();
    context.ux9_tool_ = ux9::picking_tool();
    context.faces_mode_ = faces::get_picking();
    context.box_armed_ = organize::box_armed();
    context.chain_point_ = picked_node_index() >= 0 && node_edit::is_chain(picked_set().node_name_);
    context.following = ux8::get_following();
    context.walking = ux8::on_foot();
    context.pie_open_ = ux8::pie_open();
    context.pie_sticky_ = ux8::pie_sticky();
    context.pad = ux8::pad_mode() && ux8::pad_owns_cursor();
    context.simulating = play_test::get_simulating();
    if (context.mods != 0U) {
        context.modded = hints::modifier_hints(actions::get_all(), {.mods = context.mods, .limit = kModifierHintLimit});
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const hints::Hint& hint :
         hints::get_build(context, [](const char* id) { return actions::get_keys_text(actions::get_find(id)); })) {
        out.emplace_back(hint.keys, hint.what);
    }
    return out;
}

// The Outliner's markers (level_editor_ui.h's kMark*): by node name, what the editor has done to it, its object or its
// scenery - read from each module's record of its changes.
std::unordered_map<uint32_t, uint8_t> change_marks()
{
    std::unordered_map<uint32_t, uint8_t> marks;
    auto mark = [&](uint32_t name, uint8_t bits) {
        if (name) {
            marks[name] |= bits;
        }
    };
    for (const scenery::SectorState& sector : scenery::changed_sectors()) {
        const bool moved = sector.placement.quarters != 0 || !is_unit_scale(sector.placement.scale) ||
                           sector.placement.offset.x != 0.0F || sector.placement.offset.y != 0.0F ||
                           sector.placement.offset.z != 0.0F;
        mark(sector.checksum,
             static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (sector.hidden ? ui::kMarkHidden : 0)));
    }
    for (const scenery::RailState& rail : scenery::changed_rails()) {
        mark(rail.name, static_cast<uint8_t>(rail.hidden ? ui::kMarkHidden : ui::kMarkMoved));
    }
    for (const walls::WallState& w : walls::get_changed()) {
        const bool moved = !walls::is_home(w.placement);
        mark(w.checksum, static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (w.off ? ui::kMarkCollisionOff : 0)));
    }
    for (const uint32_t id : object_edit::get_touched()) {
        const object_edit::State* origin = object_edit::get_origin(id);
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!origin || !object) {
            continue;
        }
        const maths::Vec4& point_a = core::at(origin->matrix.row, 2);
        const maths::Vec4& point_b = core::at(object->basis_frame().row, 2);
        if (origin->pos.x != object->at_point().x || origin->pos.y != object->at_point().y ||
            origin->pos.z != object->at_point().z || point_a.x != point_b.x || point_a.z != point_b.z) {
            mark(id, ui::kMarkMoved);
        }
    }
    for (const node_edit::LinkRecord& link : node_edit::get_changed()) {
        mark(link.name, ui::kMarkMoved);
    }
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.added) {
            mark(elem.name, ui::kMarkAdded);
        }
        if (elem.hidden) {
            mark(elem.name, ui::kMarkHidden);
        }
    }
    for (const uint32_t id : view::hidden_ids()) {
        mark(id, ui::kMarkViewHidden);
    }
    for (const picking::Lock& lock : picking::get_locks()) {
        mark(lock.id, ui::kMarkClickThrough);
    }
    return marks;
}

// (the locks: their kinds and names; whether the selection can be locked / is)
void fill_locks(ui::ShapeMesh& model)
{
    for (const picking::Lock& lock : picking::get_locks()) {
        model.locks.push_back(
            {.kind = lock.kind, .id = lock.id,
             .label = std::string(lock.kind == picking::kObjects ? "object " : "scenery ") + object_edit::caption(lock.id)});
    }
    bool all = true;
    const std::vector<picking::Lock> selected = selection_locks();
    for (const picking::Lock& lock : selected) {
        all = all && picking::latched(lock.kind, lock.id);
    }
    model.selection_lockable_ = !selected.empty();
    model.selection_locked_ = !selected.empty() && all;
    model.view.can_hide_ = !selected.empty();
}

// (the view: collision, its counts, the selected wall, the selected scenery's collision switch)
void fill_view(ui::ShapeMesh& model)
{
    const view::Attributes stats = view::last_stats();
    model.view.collision = view::show_collision();
    model.view.triangles = stats.triangles;
    model.view.sectors = stats.sectors;
    model.view.walls = stats.walls;
    model.view.companions = stats.companions;
    model.view.taken = stats.taken;
    model.view.triggers = stats.triggers;
    model.view.capped = stats.capped;
    model.view.hidden = view::hidden_count();
    if (const uint32_t wall = view::selected_wall()) {
        model.view.wall = wall;
        model.view.wall_name_ = object_edit::caption(wall);
        model.view.wall_info_ = view::describe_wall(wall);
        model.view.wall_state_ = walls::describe_state(wall);
        model.view.wall_off_ = walls::disengaged(wall);
        Placement placement;
        model.view.wall_moved_ = walls::get_moved(wall, &placement);
    }
    model.view.off = stats.off;
    // (the Inspector's Collision checkbox: the selected scenery's switches - level_editor_walls.h)
    std::vector<bool> offs;
    if (!get_primary()) {
        for (const uint32_t checksum : scenery::get_selection()) {
            offs.push_back(walls::disengaged(checksum));
        }
    }
    model.view.scenery_collision_ = walls::get_combined(offs);
}

// (the selection: the first object or the scenery and its parts)
void fill_selection(ui::ShapeMesh& model)
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    ui::ShapeMesh::Selection& selection = model.selection;
    if (!objects.empty()) {
        objects::GameEntity* object = objects.front();
        selection.objects = static_cast<int>(objects.size());
        const char* cls = object_edit::class_of(object->ident());
        selection.kind = cls ? cls : "Object";
        selection.name = object_edit::caption(object->ident());
        selection.checksum = object->ident();
        selection.pos = object_edit::pos_of(object);
        const maths::Vec4& at = core::at(object->basis_frame().row, 2);
        selection.scalable = scale_tool::object_scalable(object);
        selection.scale = scale_tool::get_object_scale(object);
        selection.has_heading_ = true;
        selection.heading_degrees_ = static_cast<float>(
            std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * kHalfTurnDegrees / maths::kPi);
    } else if (scenery::has_selection()) {
        selection.sectors = static_cast<int>(scenery::get_selection().size());
        selection.kind = selection.sectors > 1 ? "Scenery group" : "Scenery";
        selection.checksum = scenery::get_selection().front();
        selection.name = object_edit::caption(selection.checksum);
        selection.pos = scenery::get_pivot();
        selection.scalable = true;
        selection.scale = scenery::selection_scale();
        if (selection.sectors > 1 || scenery::selected_rail_count() > 0) {
            for (const uint32_t checksum : scenery::get_selection()) {
                ui::ShapeMesh::Selection::Member member;
                member.id = checksum;
                member.name = object_edit::caption(checksum);
                member.shadow = scenery::is_shadow(checksum);
                OrientedBox box{};
                if (scenery::cuboid(checksum, &box)) {
                    std::array<char, kSizeTextSize> size{};
                    const V3 extent = box_size(box);
                    text::format_to(size, "%.0f x %.0f x %.0f", extent.x, extent.y, extent.z);
                    member.size = size.data();
                }
                selection.members.push_back(member);
            }
            for (const uint32_t name : scenery::selected_rail_names()) {
                ui::ShapeMesh::Selection::Member member;
                member.rail = true;
                member.id = name;
                member.name = nodes::caption(name);
                selection.members.push_back(member);
            }
        }
    }
}

// (the help, the browser and its preview)
void fill_help_and_browser(ui::ShapeMesh& model)
{
    model.help = tool_options().help_;
    model.browser = stored_browser().open;
    model.browser_tab_ = stored_browser().tab;
    model.browser_index_ = stored_browser().index;
    ui_browser_items().clear();
    for (const Browser::Item& item : stored_browser().items) {
        ui_browser_items().push_back({.id = item.id, .label = item.label});
    }
    model.browser_items_ = &ui_browser_items();
    if (!stored_browser().open || stored_browser().items.empty()) {
        model.preview.index = -1;
    } else if (stored_browser().preview >= 0 &&
               std::cmp_less(stored_browser().preview, stored_browser().items.size())) {
        model.preview.index = stored_browser().preview;
    } else {
        model.preview.index = stored_browser().index;
    }
}

// (the node selected: its place, its chain, added / hidden, a crown's order)
void fill_nodes(ui::ShapeMesh& model)
{
    model.nodes.families = tool_options().families_;
    model.nodes.selected = inspected_node();
    model.nodes.hovered = picked_set().hover_node_;
    model.nodes.node_only_ = picked_set().node_selected_ && model.nodes.selected >= 0;
    model.nodes.level_shared_ = nodes::level_is_shared(&model.nodes.shared_game_);
    node_edit::Values node;
    if (model.nodes.node_only_ && node_values(&node)) {
        model.nodes.movable = true;
        model.nodes.pos = node.pos;
        model.nodes.angles = node.angles;
        const node_edit::Values* origin = node_edit::get_origin(picked_set().node_name_);
        model.nodes.moved = (origin != nullptr) && !node_edit::get_same(node, *origin);
        model.nodes.when_applied_ = node_edit::when_applied(core::at(nodes::get_all(), model.nodes.selected));
        if (node_edit::is_chain(picked_set().node_name_)) {
            model.nodes.rail = true; // (a rail's, a ladder's or a path's point: moved alone or with its chain)
            if (chains::is_path(picked_set().node_name_)) {
                model.nodes.chain_noun_ = "path";
            } else if (chains::is_ladder(picked_set().node_name_)) {
                model.nodes.chain_noun_ = "ladder";
            } else {
                model.nodes.chain_noun_ = "rail";
            }
            model.nodes.entire_rail_ = pick_cycle().move_entire_rail_;
            model.nodes.rail_points_ = static_cast<int>(node_edit::moved_with(picked_set().node_name_, pick_cycle().move_entire_rail_).size());
        }
    }
    model.nodes.online_warning_ = picked_set().online_warning_shown_;
    if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& elem = core::at(nodes::get_all(), model.nodes.selected);
        model.nodes.added = elem.added;
        model.nodes.hidden = elem.hidden;
        const char* why = nullptr;
        model.nodes.can_hide_ = !elem.added && node_create::can_hide(elem, &why);
        model.nodes.why_no_hide_ = why ? why : "";
        if (elem.added && elem.family == nodes::Family::kCrown) {
            const node_create::LinkChain list = node_create::at_hand();
            std::string name;
            for (const node_create::Spec& spec : list) {
                if (node_create::name_hash(spec.name) == elem.name) {
                    name = spec.name;
                }
            }
            node_create::LinkChain up = list;
            node_create::LinkChain down = list;
            model.nodes.crown_up_ = node_create::move_crown(&up, name, -1);
            model.nodes.crown_down_ = node_create::move_crown(&down, name, 1);
        }
    }
}

void fill_level_props(ui::ShapeMesh& model, uint32_t scene)
{
    // Phase 6: the level's properties, its own values (read once a level: its structure is walked for them) and
    // its restarts.
    static int g_defaults_for = -1;
    static level_props::Props defaults;
    static bool g_have_defaults = false;
    if (g_defaults_for != layout::level_loads()) {
        g_defaults_for = layout::level_loads();
        g_have_defaults = level_props::level_defaults(&defaults);
    }
    model.level_props_.in_level_ = render::current_level() != 0 && scene != 0;
    model.level_props_.props = level_props::at_hand();
    model.level_props_.defaults = defaults;
    model.level_props_.have_defaults_ = g_have_defaults;
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.family == nodes::Family::kSpawn && !elem.hidden && (elem.name != 0U)) {
            model.level_props_.restarts.emplace_back(elem.name, nodes::caption(elem.name));
        }
    }
    if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& elem = core::at(nodes::get_all(), model.nodes.selected);
        if (elem.family == nodes::Family::kSpawn && !elem.hidden) {
            model.level_props_.selected_restart_ = elem.name;
        }
    }
}

// (the Place palette)
void fill_place(ui::ShapeMesh& model)
{
    model.place.open = pick_cycle().place_open_;
    model.place.armed = pick_cycle().place_armed_;
    if (pick_cycle().place_open_) {
        for (const PlaceEntry& entry : place_entries()) {
            const char* why = entry.lobby ? node_create::why_not_lobby_flag(entry.team)
                                          : node_create::get_why_not(entry.kind, entry.team);
            model.place.entries.push_back({.label = entry.label, .colour = entry.colour, .why = why ? why : ""});
        }
        // (phase 4: chains drawn point by point - level_editor_chains.h)
        model.place.entries.push_back({.label = "Rail", .colour = kRailColour, .why = ""});
        model.place.entries.push_back({.label = "Path", .colour = kPathColour, .why = ""});
        model.place.entries.push_back({.label = "Ladder", .colour = kLadderColour, .why = ""});
        model.place.entries.push_back({.label = "Ledge", .colour = kLedgeColour, .why = ""});
        // (peds, cars, bouncy sets, particle systems, sound emitters - level_editor_entities.h; place_entity)
        for (const entities::PaletteEntry& entry : entities::get_palette()) {
            model.place.entries.push_back({.label = entry.label, .colour = entry.colour, .why = entry.why});
        }
    }
}

// (the hide confirm window)
void fill_confirm(ui::ShapeMesh& model)
{
    if (pick_cycle().confirm_hide_ != 0U) {
        const int index = nodes::get_find(pick_cycle().confirm_hide_);
        if (index >= 0) {
            const nodes::Elem& elem = core::at(nodes::get_all(), index);
            model.confirm.open = true;
            model.confirm.name = nodes::caption(elem.name);
            std::string family = nodes::family_name(elem.family);
            if (!family.empty() && family.back() == 's') {
                family.pop_back(); // ("Crowns": "Crown")
            }
            for (char& ch : family) {
                ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            }
            model.confirm.family = family;
            model.confirm.crown = elem.family == nodes::Family::kCrown;
        } else {
            pick_cycle().confirm_hide_ = 0;
        }
    }
}

} // namespace
namespace internal {

ui::ShapeMesh assemble_model()
{
    ui::ShapeMesh model = {};
    model.carry = carry::is_switched_on();
    model.carried = drag_session().dragging_ || fly_camera().key_moving_ || picked_set().field_editing_
                        ? carry::total_of()
                        : 0; // (a move under way)
    model.marks = change_marks();
    const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
    model.level = (scene != 0U) ? object_edit::caption(scene) : "";
    model.level_title_ = layout::level_title();
    model.edited_saved_ = layout::has_saved_layout();
    model.confirm_reset_ = pick_cycle().confirm_reset_;
    model.mode = mode_name();
    model.selected = describe_selection();
    model.hover = hover_text();
    model.message = hud::current_message();
    model.keys = keys_now();
    model.camera = fly_camera().pos_;
    model.speed = fly_camera().speed_;
    model.changes = unsaved_changes().moves_;
    model.moved = scenery::get_moved_count();
    model.unsaved = unsaved_changes().unsaved_;
    model.offer = unsaved_changes().offer_pending_;
    model.can_undo_ = !undo_steps().undo_.empty();
    model.can_redo_ = !undo_steps().redo_.empty();
    model.snap_always_ = tool_options().snap_always_;
    model.pick_filter_ = tool_options().pick_filter_;
    fill_locks(model);
    fill_view(model);
    fill_selection(model);
    fill_help_and_browser(model);
    fill_nodes(model);
    fill_level_props(model, scene);
    fill_place(model);
    model.chain = (chain_target() != 0U) ? chains::view_of(chain_target()) : chains::View{};
    if (!model.chain.waiting) {
        model.chain.waiting = chains::get_waiting();
    }
    fill_confirm(model);
    return model;
}

void show_help()
{
    const std::vector<std::string> lines = {
        "LEVEL EDITOR - KEYS    (F1 or Esc closes this)",
        "Flying       W A S D move   Q / E down / up   Shift faster   right mouse looks",
        "             wheel forward / back   - / = flying speed",
        "Selecting    click an object or scenery (a piece of scenery takes its group)",
        "             floors, ceilings, rooms around you and decals are passed by a click",
        "             click again on the same spot: the next thing under the cursor",
        "             Shift+click adds / removes   Alt+click (middle click) one piece, any",
        "             Ctrl+click removes one piece from the selection (a group takes its shadows too)",
        "             L locks the selection (clicks pass it); L with nothing selected unlocks all",
        "             what clicks take: [editor] pick= in the ini (1 nodes 2 objects 4 scenery)",
        "             Esc or Backspace deselects",
        "Moving       hold and drag (Shift: up / down, wheel: push / pull, Ctrl: snap)",
        "             drag an arrow to move along that axis only",
        "             W A S D / Q E nudge   Z / X turn   G drops it to the ground",
        "             [ / ] scale scenery / an object down / up 10% (Shift: 1%), 0.25x to 4x",
        "             Alt+[ / ] stretch it along the axis the camera faces most (x, y or z)",
        "             a rail / ladder / path point moves with its whole chain - L: by itself",
        "             scenery carries what stands on it ([editor] carry=0 off; Alt while moving leaves it)",
        "Chains       N > Rail / Path: click points (Alt: no edge snap), Enter / right click finishes",
        "             on a point: Insert adds one after   Del deletes   End draws on   J links   U unlinks",
        "Editing      Ctrl+D duplicates   Del hides (a copy / added node: deletes)   Home puts back",
        "             Ctrl+Z undo   Ctrl+Y redo   Ctrl+Shift+Del twice: reset the level",
        "Adding       B lists the level's pieces and objects: Enter places a copy",
        "             N: new spawns, crowns, flags, lights... - pick one, click in the view",
        "Saving       Ctrl+S saves the edited copy (the level select's EDITED entry)",
        "             unsaved changes are autosaved: Ctrl+R restores, Ctrl+Shift+R discards",
        "Leaving      Esc (first it cancels a drag, closes a panel or deselects)",
        "View         H hides the selection while editing   I or / isolates it   Alt+H shows all",
        "             (never saved, never in play; the collision view, C, needs the UI)",
        "Triggers     T: kill zones, teleports, gaps - shown and added with the UI only",
        "Camera       P writes where the camera is to the log (position, back, up)",
    };
    hud::show_panel(lines, kHelpPanelX, kHelpPanelY, kHelpPanelWidth);
}

// --- the browser (B): the level's pieces and objects, a copy placed where the cursor points ---

void fill_browser()
{
    stored_browser().items.clear();
    if (stored_browser().tab == 0) {
        for (const uint32_t checksum : scenery::movable_pieces()) {
            OrientedBox box{};
            std::array<char, kSizeTextSize> size = {""};
            if (scenery::cuboid(checksum, &box)) {
                text::format_to(size, "  %.0f x %.0f x %.0f", box.max.x - box.min.x, box.max.y - box.min.y,
                                box.max.z - box.min.z);
            }
            stored_browser().items.push_back({.id = checksum, .label = object_edit::caption(checksum) + size.data()});
        }
        std::ranges::sort(stored_browser().items,
                          [](const Browser::Item& a, const Browser::Item& b) { return a.label < b.label; });
    } else {
        for (const object_edit::TableRow& entry : object_edit::copyable()) {
            stored_browser().items.push_back({.id = entry.name, .label = entry.label});
        }
    }
    stored_browser().index = std::min(stored_browser().index, std::max(0, static_cast<int>(stored_browser().items.size()) - 1));
    stored_browser().preview = -1;
}

void show_browser()
{
    std::vector<std::string> lines;
    std::array<char, kCaptionTextSize> title{};
    text::format_to(title, "ADD   %s / %s   (%d)", stored_browser().tab == 0 ? "SCENERY" : "scenery",
                    stored_browser().tab == 1 ? "OBJECTS" : "objects", static_cast<int>(stored_browser().items.size()));
    lines.emplace_back(title.data());
    const int count = static_cast<int>(stored_browser().items.size());
    stored_browser().top = std::min(stored_browser().top, stored_browser().index);
    if (stored_browser().index >= stored_browser().top + kBrowserRows) {
        stored_browser().top = stored_browser().index - kBrowserRows + 1;
    }
    for (int row = 0; row < kBrowserRows; ++row) {
        const int index = stored_browser().top + row;
        if (index >= count) {
            const char* empty_line =
                stored_browser().tab == 0 ? "(no pieces that can be moved)" : "(no objects that can be copied)";
            lines.emplace_back(row == 0 ? empty_line : "");
            continue;
        }
        const std::string label = core::at(stored_browser().items, index).label.substr(0, 40);
        lines.push_back(index == stored_browser().index ? get_key(("> " + label).c_str()) : "  " + label);
    }
    hud::show_panel(lines, kBrowserPanelX, kBrowserPanelY, kBrowserPanelWidth);
}

} // namespace internal
} // namespace frontend::level_editor
