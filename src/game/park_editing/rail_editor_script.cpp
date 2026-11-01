// The rail editor's script side (rail_editor.h): its update (the pad's buttons run the editor's scripts), its member
// functions, the debug info, showing / hiding it, and the rails written to / read from structures and as nodes.

#include "game/park_editing/rail_editor.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor_internal.h"
#include "engine/objects/control_pad.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/stubs.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

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

constexpr float kAngleScaleToDeg = 57.2957763671875f; // 0x00645468

// The input component's pad (+0x20) and its four buttons (X +0x1A0, Triangle +0x180, Square +0x160, Circle +0x140).
objects::PadSnapshot* pad_of(void* input)
{
    return raw::field_at<objects::PadSnapshot>(static_cast<uint8_t*>(input), 0x20);
}
objects::ControlButton* get_button(void* input, uint32_t offset)
{
    return raw::field_at<objects::ControlButton>(static_cast<uint8_t*>(input), 0x20 + offset);
}

script::Struct* spawn_param_bag()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
script::Array* alloc_script_array()
{
    script::Array* array = script::alloc_script_array();
    return array ? memory::construct_in<script::Array>(array) : nullptr;
}
void free_struct(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

int rail_pool_used()
{
    const memory::PoolPair& pools = rail_pools();
    return pools.pools[pools.current]->taken_count_;
}

// The editor camera's (0xAC8946E2) target: +0x3C its position, +0x78 its heading (radians), +0x88 the height over it.
const Vec4& viewer_location(void* camera)
{
    return *raw::field_at<const Vec4>(static_cast<uint8_t*>(camera), 0x3C);
}
float camera_heading(void* camera) { return *raw::field_at<const float>(static_cast<uint8_t*>(camera), 0x78); }
float camera_height(void* camera) { return *raw::field_at<const float>(static_cast<uint8_t*>(camera), 0x88); }

// The structure a script's member function writes its results to (CScript +0x14).
script::Struct* get_results(script::Script* script) { return script->args_; }

} // namespace

// 0x004F4810
void RailAuthoring::tick_frame()
{
    struct Press {
        uint32_t button;
        uint32_t script;
    };
    static const Press kPresses[4] = {
        {0x1A0, kRailEditorX}, {0x180, kRailEditorTriangle}, {0x160, kRailEditorSquare}, {0x140, kRailEditorCircle}};
    for (const Press& press : kPresses) {
        objects::ControlButton* button = get_button(input_ptr_, press.button);
        if (button->fired_) {
            button->reset_activation();
            script::launch_script(press.script, nullptr, entity_, false, nullptr);
        }
    }
    script::launch_script(kRailEditorEveryFrame, nullptr, entity_, false, nullptr);
}

// 0x004F6F90
void RailAuthoring::conceal(bool hide)
{
    ParkLayout* manager = ParkLayout::singleton();
    if (hide) {
        script::launch_script(kDestroyCursor, nullptr, nullptr, false, nullptr);
        manager->builder_->destroy_rails();
        un_highlight_all();
        delete_dotted_line();
        return;
    }
    script::launch_script(kCreateCursor, nullptr, nullptr, false, nullptr);
    manager->builder_->build_rails(manager->concrete_metas_);
    // The camera told whose it is (the input component's configure_from_data: player 0), the pad's events dropped.
    script::Struct* params = spawn_param_bag();
    params->put_int(kPlayer, 0);
    static_cast<objects::ObjectModule*>(input_ptr_)->configure_from_data(params);
    free_struct(params);
    pad_of(input_ptr_)->get_clear();
}

// 0x004F6C10
void RailAuthoring::fetch_diagnostics(script::Struct* info)
{
    info->put_int(kNumFreeRailPoints, free_points());
    info->put_int(kNumFreeRails, kMaxRails - rail_pool_used());
    if (active_node_)
        info->append_struct_ref(kCurrentRail, spawn_param_bag());
    else
        info->put_name_key(kCurrentRail, kNull);
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following_)
        ++count;
    if (count) {
        script::Array* rails = alloc_script_array();
        rails->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalStruct);
        uint32_t i = 0;
        for (const EditedRail* rail = rail_groups_; rail; rail = rail->following_)
            rails->set_element(i++, cell::of(spawn_param_bag()));
        info->append_array_ref(kRails, rails);
    }
    // (then the base's - the empty 0x005BAB90)
    stubs::ignore_argument(static_cast<int>(cell::of(info)));
}

// 0x004F6680
void RailAuthoring::write_to_structure(script::Struct* params)
{
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following_)
        ++count;
    script::Array* rails = alloc_script_array();
    rails->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalStruct);
    uint32_t r = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following_) {
        script::Struct* rail_info = spawn_param_bag();
        uint32_t point_count = 0;
        for (const EditedRailPoint* p = rail->point_chain_; p; p = p->following_)
            ++point_count;
        script::Array* points = alloc_script_array();
        points->configure_size_kind(point_count, script::kQbvalStruct);
        uint32_t i = 0;
        for (const EditedRailPoint* p = rail->point_chain_; p; p = p->following_) {
            script::Struct* point = spawn_param_bag();
            point->put_vec3(kPos, p->at_point_.x, p->at_point_.y, p->at_point_.z);
            // (a post drawn, not has_post_)
            if (p->post_)
                point->put_name_key(0u, kHasPost);
            points->set_element(i++, cell::of(point));
        }
        rail_info->append_array_ref(kPoints, points);
        rails->set_element(r++, cell::of(rail_info));
    }
    params->append_array_ref(kCreatedRails, rails);
}

// 0x004F7ED0
void RailAuthoring::decode_from_struct(script::Struct* params)
{
    get_clear();
    script::Array* rails = nullptr;
    params->fetch_list(kCreatedRails, &rails, 0);
    if (!rails)
        return;
    for (uint32_t r = 0; r < rails->extent_; ++r) {
        auto* rail_info = rails->struct_at(r);
        get_new_rail();
        script::Array* points = nullptr;
        rail_info->fetch_list(kPoints, &points, 1);
        for (uint32_t i = 0; i < points->extent_; ++i) {
            EditedRailPoint* p = active_node_->append_rail_vertex();
            auto* point = points->struct_at(i);
            // (a full pool: written through null, as the original)
            point->fetch_vec(kPos, &p->at_point_, 0);
            if (point->has_bare_flag(kHasPost))
                p->has_post_ = true;
        }
    }
}

// 0x004F6920
void RailAuthoring::write_nodes(script::Array* nodes, int* index, uint32_t name)
{
    int next = *index;
    int rail_index = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following_, ++rail_index) {
        for (const EditedRailPoint* p = rail->point_chain_; p; p = p->following_) {
            auto* node = nodes->struct_at(static_cast<uint32_t>(next));
            ++next;
            node->put_vec3(kPos, p->at_point_.x, p->at_point_.y, p->at_point_.z);
            node->put_name_key(kName, name++);
            node->put_name_key(0u, kRailNodeClass);
            node->put_name_key(0u, kTrickObject);
            char cluster[52];
            std::snprintf(cluster, sizeof(cluster), "CreatedRailCluster%d", rail_index);
            node->put_name_key(kCluster, checksums::hash_from_text(cluster));
            node->put_name_key(kSurfaceKindKey, kMetalSmooth);
            if (p->following_) {
                // linked to the next point's node (the next one written)
                script::Array* links = alloc_script_array();
                links->configure_size_kind(1, script::kQbvalInteger);
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
        script::Struct* text = spawn_param_bag();
        text->put_name_key(kMode, kRailPlacement);
        script::launch_script(kHelperTextMode, text, nullptr, false, nullptr);
        free_struct(text);
        return 1;
    }
    case kDeleteRail: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        if (EditedRail* rail = find_rail(id))
            delete_rail(rail);
        return 1;
    }
    case kAddNewPosition: {
        Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
        params->fetch_vec(kPos, &pos, 0);
        if (!active_node_)
            return 0;
        const EditedRailPoint* head = active_node_->point_chain_;
        if (head && !rail_segment_valid(&pos, &head->at_point_))
            return 1;
        if (!ParkBuilderMode::is_inside_park(pos, 10.0f))
            return head ? 1 : 0;
        ParkBuilderMode* editor = ParkBuilderMode::fabricate(false);
        if (!editor->is_park_full()) {
            if (EditedRailPoint* p = active_node_->append_rail_vertex()) {
                p->at_point_ = pos;
                p->level_y_ =
                    static_cast<float>(static_cast<double>(pos.y) - EditedRailPoint::ground_height(&p->at_point_));
                p->has_post_ = params->has_bare_flag(kAddPost);
                const Vec4& zero = g_rail_origin;
                p->draw_segment(&zero, &zero, 0.0f);
                p->draw_post(&zero, &zero, 0.0f);
                ParkBuilderMode::get_release();
                return 1;
            }
        }
        ParkBuilderMode::get_release();
        return 0;
    }
    case kGetEditingMode:
        get_results(script)->put_name_key(kMode, mode_sel_);
        return 1;
    case kDeleteRailPoint: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        EditedRail* rail = find_rail(id);
        if (!rail)
            return 0;
        int index = 0;
        params->fetch_int_value(kRailPointIndex, &index, 0);
        EditedRailPoint* p = rail->fetch_vertex_at(index);
        if (!p)
            return 0;
        cut_rail(rail, p);
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
        EditedRail* rail = find_rail(id);
        if (!rail)
            return 1;
        const bool update = params->has_bare_flag(kUpdateSuperSectors);
        return rail->move_point(index, &pos, update) ? 1 : 0;
    }
    case kHighlightRailPoint: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        int index = 0;
        params->fetch_int_value(kRailPointIndex, &index, 0);
        const EditedRail* rail = find_rail(id);
        if (!rail)
            return 1;
        EditedRailPoint* p = rail->fetch_vertex_at(index);
        const bool include_end_posts = params->has_bare_flag(kIncludeEndPosts);
        const bool flash = params->has_bare_flag(kFlash);
        p->spotlight(flash, include_end_posts);
        return 1;
    }
    case kGetCursorPosition: {
        const Vec4& target = viewer_location(view_camera_);
        auto y = static_cast<float>(static_cast<double>(target.y) + camera_height(view_camera_));
        script::Struct* results = get_results(script);
        results->put_vec3(kPos, target.x, y, target.z);
        results->push_float(kAngle,
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
        if (free_points() == 0)
            return 0;
        get_new_rail();
        return 1;
    case kUnHighlightAllRails:
        un_highlight_all();
        return 1;
    case kDestroyEditedRailSectors:
        get_update_super_sectors() = true;
        if (params->has_bare_flag(kDoNotUpdateSuperSectors))
            get_update_super_sectors() = false;
        delete_all_segments();
        delete_all_posts();
        get_update_super_sectors() = true;
        return 1;
    case kGetEditedRailInfo: {
        uint32_t id = 0;
        params->read_hash_value(kRailId, &id, 0);
        const EditedRail* rail = find_rail(id);
        if (params->has_bare_flag(kCurrentRail))
            rail = active_node_;
        if (!rail)
            return 0;
        get_results(script)->put_name_key(kRailId, rail->ident_);
        get_results(script)->put_int(kNumPoints, rail->rail_point_total());
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace park_editing
