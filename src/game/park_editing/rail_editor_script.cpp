// The rail editor's script side (rail_editor.h): its update (the pad's buttons run the editor's scripts), its member
// functions, the debug info, showing / hiding it, and the rails written to / read from structures and as nodes.

#include "game/park_editing/rail_editor.h"

#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/slab_bank.h"
#include "engine/script/cell.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor_internal.h" // IWYU pragma: keep - the namespace of the using-directive
#include "engine/objects/control_pad.h"
#include "engine/objects/editor_camera.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/stubs.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "game/park_editing/park_editing_state.h"

namespace park_editing {

using namespace rail_editor_internal;
using maths::Vec4;

namespace {

// Scripts
constexpr uint32_t kRailEditorX = 0x415F2AFA, kRailEditorTriangle = 0x3AD21C4E, kRailEditorSquare = 0xD8D956C9,
                   kRailEditorCircle = 0xC18D5B19, kRailEditorEveryFrame = 0x97153F0E;
constexpr uint32_t kCreateCursor = 0x52D1CE6F;   // rail_editor_create_cursor
constexpr uint32_t kDestroyCursor = 0x0EC0A515;  // rail_editor_destroy_cursor
constexpr uint32_t kHelperTextMode = 0x9DB065AD; // parked_set_helper_text_mode

// Member functions
constexpr uint32_t kGetCursorPosition = 0x8F3E0B97, kGetEditingMode = 0x63DD1204, kAddNewPosition = 0x5828E852,
                   kDeleteRail = 0x353EA2A9, kSetEditingMode = 0x1CC3A173, kNoFreeRailPoints = 0x31F66E6B,
                   kDeleteRailPoint = 0x6716227C, kUpdateRailPointPosition = 0x6D2EEE1B,
                   kHighlightRailPoint = 0x8798E959, kNewRail = 0xD2C98192, kDeleteDottedLine = 0x9386DCD9,
                   kClearRailEditor = 0x9C0762C4, kDrawDottedLine = 0xAA438D71, kUnHighlightAllRails = 0xD5A87EEE,
                   kDestroyEditedRailSectors = 0xD70781C3, kGetEditedRailInfo = 0xFB6A0888;

// Parameters and structure names
constexpr uint32_t kMode = 0x6835B854, kRailId = 0xA61E7CD9, kRailPointIndex = 0x00AB3C14, kPos = 0x7F261953,
                   kAddPost = 0xCB4B4880, kUpdateSuperSectors = 0xDDF9A2BB, kIncludeEndPosts = 0x1A52E4B8,
                   kFlash = 0x5031A0FC, kCurrentRail = 0xE234F2B2, kNumPoints = 0x6164266E,
                   kDoNotUpdateSuperSectors = 0x23A11CBC, kPlayer = 0x67E6859A, kNumFreeRailPoints = 0x8C75A6C1,
                   kNumFreeRails = 0xE954B3D4, kNull = 0xDA3403B0, kRails = 0x7E17DFA9, kCreatedRails = 0x244550A6,
                   kPoints = 0xD84571D6, kHasPost = 0xDCDED772, kName = 0xA1DC81F9,
                   kRailNodeClass = 0x4953E02A /* ncomp_railnode_park */, kTrickObject = 0x1645B830,
                   kCluster = 0x1A3A966B, kSurfaceKindKey = 0x54CF8532,
                   kMetalSmooth = 0xA9ECF4E9 /* TERRAIN_METALSMOOTH */, kLinks = 0x2E7D5EE7, kAngle = 0xFF7EBAF6,
                   kRailPlacement = 0xFFD81C08;

constexpr float kAngleScaleToDeg = 57.2957763671875F; // 0x00645468

// The input component's pad and its four buttons (X +0x1A0, Triangle +0x180, Square +0x160, Circle +0x140 in it).
objects::PadSnapshot* pad_of(objects::ObjectModule* input)
{
    return &core::down_cast<objects::PadInputModule>(input)->pad_primary();
}
objects::ControlButton* get_button(objects::ObjectModule* input, uint32_t offset)
{
    return pad_of(input)->button_at_offset(offset);
}

int rail_pool_used()
{
    const memory::PoolPair& pools = park_editing_state().rail_pools();
    return core::at(pools.pools(), pools.current())->taken_count();
}

// The editor camera's (0xAC8946E2) target: its cursor's position, its heading (radians), the height over it.
const objects::EditorViewModule* editor_view(objects::ObjectModule* camera)
{
    return core::down_cast<objects::EditorViewModule>(camera);
}
const Vec4& viewer_location(objects::ObjectModule* camera) { return editor_view(camera)->cursor(); }
float camera_heading(objects::ObjectModule* camera) { return editor_view(camera)->heading(); }
float camera_height(objects::ObjectModule* camera) { return editor_view(camera)->level_y(); }


} // namespace

// 0x004F4810
void RailAuthoring::tick_frame()
{
    struct Press {
        uint32_t button;
        uint32_t script;
    };
    static const std::array<Press, 4> kPresses = {{
        {.button = 0x1A0, .script = kRailEditorX}, {.button = 0x180, .script = kRailEditorTriangle}, {.button = 0x160, .script = kRailEditorSquare}, {.button = 0x140, .script = kRailEditorCircle}}};
    for (const Press& press : kPresses) {
        objects::ControlButton* button = get_button(input_ptr_, press.button);
        if (button->fired() != 0U) {
            button->reset_activation();
            script::launch_script(press.script, nullptr, entity(), false, nullptr);
        }
    }
    script::launch_script(kRailEditorEveryFrame, nullptr, entity(), false, nullptr);
}

// 0x004F6F90
void RailAuthoring::conceal(bool hide)
{
    const ParkLayout* manager = ParkLayout::singleton();
    if (hide) {
        script::launch_script(kDestroyCursor, nullptr, nullptr, false, nullptr);
        manager->builder()->destroy_rails();
        un_highlight_all();
        delete_dotted_line();
        return;
    }
    script::launch_script(kCreateCursor, nullptr, nullptr, false, nullptr);
    manager->builder()->build_rails(manager->concrete_metas());
    // The camera told whose it is (the input component's configure_from_data: player 0), the pad's events dropped.
    script::StructPtr params = script::make_struct();
    params->put_int(checksums::Name{kPlayer}, 0);
    static_cast<objects::ObjectModule*>(input_ptr_)->configure_from_data(params.get());
    params.reset();
    pad_of(input_ptr_)->get_clear();
}

// 0x004F6C10
void RailAuthoring::fetch_diagnostics(script::Struct* info)
{
    info->put_int(checksums::Name{kNumFreeRailPoints}, free_points());
    info->put_int(checksums::Name{kNumFreeRails}, kMaxRails - rail_pool_used());
    if (active_node_) {
        info->append_struct_ref(kCurrentRail, script::new_struct());
    } else {
        info->put_name_key(checksums::Name{kCurrentRail}, kNull);
    }
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        ++count;
    }
    if (count != 0) {
        gsl::owner<script::Array*> rails = script::new_array();
        rails->configure_size_kind(static_cast<uint32_t>(count), script::QbValueKind::kStruct);
        uint32_t index = 0;
        for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
            gsl::owner<script::Struct*> entry = script::new_struct(); // (the array's)
            rails->set_cell(index++, script::Cell::of(entry));
        }
        info->append_array_ref(kRails, rails);
    }
    // (then the base's - the empty 0x005BAB90)
    stubs::ignore_argument(info);
}

// 0x004F6680
void RailAuthoring::write_to_structure(script::Struct* params)
{
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        ++count;
    }
    gsl::owner<script::Array*> rails = script::new_array();
    rails->configure_size_kind(static_cast<uint32_t>(count), script::QbValueKind::kStruct);
    uint32_t rail_index = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        gsl::owner<script::Struct*> rail_info = script::new_struct();
        uint32_t point_count = 0;
        for (const EditedRailPoint* rail_point = rail->point_chain(); rail_point;
             rail_point = rail_point->following()) {
            ++point_count;
        }
        gsl::owner<script::Array*> points = script::new_array();
        points->configure_size_kind(point_count, script::QbValueKind::kStruct);
        uint32_t point_index = 0;
        for (const EditedRailPoint* rail_point = rail->point_chain(); rail_point;
             rail_point = rail_point->following()) {
            gsl::owner<script::Struct*> point = script::new_struct();
            point->put_vec3(
                checksums::Name{kPos},
                {.x = rail_point->at_point().x, .y = rail_point->at_point().y, .z = rail_point->at_point().z});
            // (a post drawn, not has_post_)
            if (rail_point->post()) {
                point->put_name_key(checksums::Name{0U}, kHasPost);
            }
            points->set_cell(point_index++, script::Cell::of(point));
        }
        rail_info->append_array_ref(kPoints, points);
        rails->set_cell(rail_index++, script::Cell::of(rail_info));
    }
    params->append_array_ref(kCreatedRails, rails);
}

// 0x004F7ED0
void RailAuthoring::decode_from_struct(script::Struct* params)
{
    get_clear();
    script::Array* rails = nullptr;
    params->fetch_list(kCreatedRails, &rails, 0);
    if (!rails) {
        return;
    }
    for (uint32_t index = 0; index < rails->extent(); ++index) {
        auto* rail_info = rails->struct_at(index);
        get_new_rail();
        script::Array* points = nullptr;
        rail_info->fetch_list(kPoints, &points, 1);
        for (uint32_t i = 0; i < points->extent(); ++i) {
            EditedRailPoint* rail_point = active_node_->append_rail_vertex();
            auto* point = points->struct_at(i);
            // (a full pool: written through null, as the original)
            point->fetch_vec(kPos, &rail_point->at_point(), 0);
            if (point->has_bare_flag(kHasPost)) {
                rail_point->set_has_post(true);
            }
        }
    }
}

// 0x004F6920
void RailAuthoring::write_nodes(script::Array* nodes, int* index, uint32_t name)
{
    int next = *index;
    int rail_index = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following(), ++rail_index) {
        for (const EditedRailPoint* rail_point = rail->point_chain(); rail_point;
             rail_point = rail_point->following()) {
            auto* node = nodes->struct_at(static_cast<uint32_t>(next));
            ++next;
            node->put_vec3(
                checksums::Name{kPos},
                {.x = rail_point->at_point().x, .y = rail_point->at_point().y, .z = rail_point->at_point().z});
            node->put_name_key(checksums::Name{kName}, name++);
            node->put_name_key(checksums::Name{0U}, kRailNodeClass);
            node->put_name_key(checksums::Name{0U}, kTrickObject);
            std::array<char, kClusterNameSize> cluster{};
            text::format_to(cluster, "CreatedRailCluster%d", rail_index);
            node->put_name_key(checksums::Name{kCluster}, checksums::hash_from_text(cluster.data()));
            node->put_name_key(checksums::Name{kSurfaceKindKey}, kMetalSmooth);
            if (rail_point->following()) {
                // linked to the next point's node (the next one written)
                gsl::owner<script::Array*> links = script::new_array();
                links->configure_size_kind(1, script::QbValueKind::kInteger);
                links->set_element(0, static_cast<uint32_t>(next));
                node->append_array_ref(kLinks, links);
            }
        }
    }
    *index = next;
}

// 0x004F75F0
int RailAuthoring::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kNoFreeRailPoints: // (name unknown) 1 when no rail point is free
        return free_points() == 0 ? 1 : 0;
    case kSetEditingMode: {
        params->read_hash_value(kMode, &mode_sel_, 0);
        script::StructPtr text = script::make_struct();
        text->put_name_key(checksums::Name{kMode}, kRailPlacement);
        script::launch_script(kHelperTextMode, text.get(), nullptr, false, nullptr);
        text.reset();
        return 1;
    }
    case kDeleteRail: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        if (EditedRail* rail = find_rail(id)) {
            delete_rail(rail);
        }
        return 1;
    }
    case kAddNewPosition: {
        Vec4 pos = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        params->fetch_vec(kPos, &pos, 0);
        if (!active_node_) {
            return 0;
        }
        const EditedRailPoint* head = active_node_->point_chain();
        if (head && !rail_segment_valid(&pos, &head->at_point())) {
            return 1;
        }
        if (!ParkBuilderMode::is_inside_park(pos, kRailPlacementMargin)) {
            return head ? 1 : 0;
        }
        ParkBuilderMode::fabricate(false);
        if (park_editing::ParkBuilderMode::is_park_full() == 0) {
            if (EditedRailPoint* rail_point = active_node_->append_rail_vertex()) {
                rail_point->at_point() = pos;
                rail_point->set_level_y(static_cast<float>(static_cast<double>(pos.y) -
                                                           EditedRailPoint::ground_height(&rail_point->at_point())));
                rail_point->set_has_post(params->has_bare_flag(kAddPost));
                const Vec4& zero = park_editing_state().rail_origin();
                rail_point->draw_segment(&zero, &zero, 0.0F);
                rail_point->draw_post(&zero, &zero, 0.0F);
                ParkBuilderMode::get_release();
                return 1;
            }
        }
        ParkBuilderMode::get_release();
        return 0;
    }
    case kGetEditingMode:
        script->args().get()->put_name_key(checksums::Name{kMode}, mode_sel_);
        return 1;
    case kDeleteRailPoint: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        EditedRail* rail = find_rail(id);
        if (!rail) {
            return 0;
        }
        int index = 0;
        params->fetch_int_value(kRailPointIndex, &index, 0);
        EditedRailPoint* rail_point = rail->fetch_vertex_at(index);
        if (!rail_point) {
            return 0;
        }
        cut_rail(rail, rail_point);
        delete_short_rails();
        return 1;
    }
    case kUpdateRailPointPosition: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        int index = 0;
        params->fetch_int_value(kRailPointIndex, &index, 0);
        Vec4 pos; // (left unset without a pos, as the original's)
        params->fetch_vec(kPos, &pos, 0);
        const EditedRail* rail = find_rail(id);
        if (!rail) {
            return 1;
        }
        const bool update = params->has_bare_flag(kUpdateSuperSectors);
        return rail->move_point(index, &pos, update) ? 1 : 0;
    }
    case kHighlightRailPoint: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        int index = 0;
        params->fetch_int_value(kRailPointIndex, &index, 0);
        const EditedRail* rail = find_rail(id);
        if (!rail) {
            return 1;
        }
        EditedRailPoint* rail_point = rail->fetch_vertex_at(index);
        const bool include_end_posts = params->has_bare_flag(kIncludeEndPosts);
        const bool flash = params->has_bare_flag(kFlash);
        rail_point->spotlight(flash, include_end_posts);
        return 1;
    }
    case kGetCursorPosition: {
        const Vec4& target = viewer_location(view_camera_);
        auto y = static_cast<float>(static_cast<double>(target.y) + camera_height(view_camera_));
        script::Struct* results = script->args().get();
        results->put_vec3(checksums::Name{kPos}, {.x = target.x, .y = y, .z = target.z});
        results->push_float(checksums::Name{kAngle},
                            static_cast<float>(static_cast<double>(camera_heading(view_camera_)) * kAngleScaleToDeg));
        return 1;
    }
    case kDrawDottedLine: {
        Vec4 pos; // (left unset without a pos, as the original's)
        params->fetch_vec(kPos, &pos, 0);
        draw_dotted_line(&pos);
        return 1;
    }
    case kClearRailEditor:
        get_clear();
        return 1;
    case kDeleteDottedLine:
        delete_dotted_line();
        return 1;
    case kNewRail:
        if (free_points() == 0) {
            return 0;
        }
        get_new_rail();
        return 1;
    case kUnHighlightAllRails:
        un_highlight_all();
        return 1;
    case kDestroyEditedRailSectors:
        park_editing_state().set_rail_editor_flag(true);
        if (params->has_bare_flag(kDoNotUpdateSuperSectors)) {
            park_editing_state().set_rail_editor_flag(false);
        }
        delete_all_segments();
        delete_all_posts();
        park_editing_state().set_rail_editor_flag(true);
        return 1;
    case kGetEditedRailInfo: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        const EditedRail* rail = find_rail(id);
        if (params->has_bare_flag(kCurrentRail)) {
            rail = active_node_;
        }
        if (!rail) {
            return 0;
        }
        script->args().get()->put_name_key(checksums::Name{kRailId}, rail->ident());
        script->args().get()->put_int(checksums::Name{kNumPoints}, rail->rail_point_total());
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace park_editing
