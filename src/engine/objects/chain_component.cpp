// The chain component (GameObjects::ChainComponent, chain_component.h): ropes of links hung from an object's bones.

#include "engine/objects/chain_component.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/gfx/rig.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>
#include <gsl/assert>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/pager_message.h"
#include "engine/objects/skeleton_component.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/gfx/weather.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/packed_colour.h"
#include "base/maths/maths.h"

namespace objects {

namespace {
constexpr float kInitialLinkValue = 3.0F;
constexpr float kFullLinkValue = 255.0F;

constexpr uint32_t kSuspend = 0xCE0CA665;  // the suspend component
constexpr uint32_t kSkeleton = 0x222756D5; // the skeleton component
constexpr uint32_t kModel = 0x286A8D26;    // the model component

constexpr uint32_t kCollisionInfo = 0xB41B899A; // collision_info
constexpr uint32_t kAxisFrom = 0x0078395B;
constexpr uint32_t kAxisTo = 0xD962E451;
constexpr uint32_t kPlanePoint = 0x94291927;
constexpr uint32_t kChains = 0x9FF11942;   // chains
constexpr uint32_t kSegments = 0xD93124D6; // segments
constexpr uint32_t kLength = 0xFE82614D;   // length
constexpr uint32_t kChainId = 0x62BF8E3F;  // chainid
constexpr uint32_t kMoveWithObject = 0xC96BA2C1;
constexpr uint32_t kBoneStart = 0xF2D1C4CA;    // bone_start
constexpr uint32_t kStartOffset = 0x6C3AA2BF;  // start_offset
constexpr uint32_t kBoneEnd = 0x7A12D81C;      // bone_end
constexpr uint32_t kObjEnd = 0xE5002867;       // obj_end
constexpr uint32_t kGravityForce = 0x2CCF354D; // gravity_force
constexpr uint32_t kDampForce = 0x84750D32;    // damp_force
constexpr uint32_t kIterations = 0x41E56E7B;
constexpr uint32_t kCollideBelow = 0x4BFBDC28;
constexpr uint32_t kPlaneOffset = 0x5F8314B3;
constexpr uint32_t kCollideFirst = 0x07EBDE63;
constexpr uint32_t kCollideEnd = 0xB721AB4A;
constexpr uint32_t kLineBlend = 0x54628ED7; // Blend
constexpr uint32_t kLineGrey = 0x80808080;
constexpr uint32_t kLineBlack = 0xFF000000;

// Member functions by name.
constexpr uint32_t kSetCollision = 0x286BA49C;
constexpr uint32_t kSetLink = 0x317A113A;
constexpr uint32_t kSetHidden = 0x982C70E5;
constexpr uint32_t kDist = 0x7E832F08; // dist
constexpr uint32_t kCollisionFirst = 0xE4D4B316;
constexpr uint32_t kCollisionEnd = 0x98382F87;
constexpr uint32_t kLinkPos = 0xEC13B28C;
constexpr uint32_t kLinkIndex = 0xD037C420;
constexpr uint32_t kFirstLink = 0x3EA1D93A;
constexpr uint32_t kNotLastLink = 0x5F78A252;
constexpr uint32_t kHidden = 0x5B6634D4;

bool manager_ready() { return memory::manager_ready_flag(); }
void* carve(uint32_t size)
{
    if (!manager_ready()) {
        memory::ensure_manager();
    }
    return memory::Hub::get_pull()->carve(size, 1, nullptr);
}

// A line's vertex colour and position.
void set_line_colour(render::PcPrimitive* line, int vertex, uint32_t colour)
{
    line->set_vertex_colour(vertex, colour::Packed{colour});
}
void set_line_vertex(render::PcPrimitive* line, int vertex, const maths::Vec4& pos)
{
    line->change_place(vertex, {.x = pos.x, .y = pos.y, .z = pos.z});
}

// The skeleton component's bone positions (no source yet): in the world (0x00432050), in the object (0x00431C60).
void joint_world_point(ObjectModule* skeleton, uint32_t bone, maths::Vec4* out)
{
    core::down_cast<BoneRigModule>(skeleton)->get_bone_position(bone, out);
}
void bone_position(ObjectModule* skeleton, uint32_t bone, maths::Vec4* out)
{
    core::down_cast<BoneRigModule>(skeleton)->get_local_bone_position(bone, out);
}
// The skeleton component's skeleton (its +0x18, the shared getter 0x005480F0) and that skeleton's bone matrix.
void skin_matrix_ptr(ObjectModule* skeleton, uint32_t bone, maths::Mat4* out)
{
    core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig()->read_joint_matrix(bone, out);
}

// std::list<Chain*>::erase(first, last) (0x004ECC30, the game's).
void erase_nodes(ChainComponent::ChainList* list, ChainComponent::Elem* first, ChainComponent::Elem* last)
{
    list->erase(first, last);
}
ChainComponent::Elem* get_first(ChainComponent::Elem* head) { return head ? head->next : nullptr; }

float get_rounded(double value) { return static_cast<float>(value); }

} // namespace

// --- a line, a link and a chain ---

// (0x00428270 - a blend mode setter at +0x0C, the chain's configure_from_data has it inline - is the primitive's
// blend_mode_ written directly.)

// 0x00428280
ChainLink::ChainLink() = default;

// 0x00428250
void ChainLink::destroy_body()
{
    if (line_ptr_) {
        gsl::owner<render::PcPrimitive*> line = memory::owned_element(line_ptr_);
        delete line;
        line_ptr_ = nullptr;
    }
}

// 0x00428290
// (the links made first by their constructors)
Chain::Chain() { start_offset_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; }

// 0x00428300
void Chain::update_lines()
{
    for (uint32_t i = 1; i < ints::as_unsigned(links_); ++i) {
        render::PcPrimitive* line = core::at(chain_link_, i - 1).line_ptr();
        line->set_ready(true);
        set_line_colour(line, 0, kLineBlack);
        set_line_vertex(line, 0, core::at(chain_link_, i - 1).at_point());
        set_line_colour(line, 1, kLineBlack);
        set_line_vertex(line, 1, core::at(chain_link_, i).at_point());
    }
}

// --- life ---

// 0x004298B0
gsl::owner<ObjectModule*> ChainComponent::fabricate() { return new ChainComponent; }

// 0x004296E0
ChainComponent::ChainComponent()
{
    chains_.size() = 0;
    auto* head = static_cast<Elem*>(carve(sizeof(Elem)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    chains_.set_head(head);
    set_kind_tag(kType);
    suspend_ = nullptr;
    bone_rig_ = nullptr;
    mesh_instance_ = nullptr;
    steps_ = 0;
    step_time_ = 0.0F;
    final_frame_ = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    axis_from_bone_ = 0;
    axis_to_bone_ = 0;
    plane_bone_ = 0;
    invisible_ = false;
}

// 0x00429790 (0x00429910 the deleting one)
ChainComponent::~ChainComponent()
{
    if (chains_.size() != 0) {
        for (Elem* node = get_first(chains_.head()); node != chains_.head();) {
            if (Chain* chain = node->value) {
                for (int i = Chain::kMaxLinks - 1; i >= 0; --i) {
                    core::at(chain->chain_link(), i).destroy_body();
                }
                memory::Hub::dealloc(chain);
                node->value = nullptr;
            }
            Elem* next = node->next;
            if (node != chains_.head()) {
                node->prev->next = next;
                node->next->prev = node->prev;
                memory::Hub::dealloc(node);
                --chains_.size();
            }
            node = next;
        }
        erase_nodes(&chains_, get_first(chains_.head()), chains_.head());
    }
    erase_nodes(&chains_, get_first(chains_.head()), chains_.head());
    memory::Hub::dealloc(chains_.head());
}

// 0x004296B0
void ChainComponent::ChainList::teardown()
{
    erase_nodes(this, get_first(head()), head());
    memory::Hub::dealloc(head());
}

// 0x00428370
void ChainComponent::seal()
{
    suspend_ = entity()->fetch_module(kSuspend);
    bone_rig_ = entity()->fetch_module(kSkeleton);
    mesh_instance_ = entity()->fetch_module(kModel);
}

// 0x004283B0
void ChainComponent::configure_from_data(script::Struct* params)
{
    script::Array* chains = nullptr;
    script::Struct* info = nullptr;
    if (params->read_sub_struct(kCollisionInfo, &info, 1)) {
        info->read_hash_value(kAxisFrom, &axis_from_bone_, 0);
        info->read_hash_value(kAxisTo, &axis_to_bone_, 0);
        info->read_hash_value(kPlanePoint, &plane_bone_, 0);
    }
    params->fetch_list(kChains, &chains, 1);
    for (uint32_t i = 0; i < chains->extent(); ++i) {
        auto* description = chains->struct_at(i);
        int segments = 0;
        description->fetch_int_value(kSegments, &segments, 1);
        float length = 0.0F;
        description->fetch_float(kLength, &length, 1);
        uint32_t id = 0;
        description->read_hash_value(kChainId, &id, 1);
        void* memory = carve(sizeof(Chain));
        Chain* chain = memory ? memory::construct_in<Chain>(memory) : nullptr;
        int value = 0;
        Expects(chain != nullptr);
        chain->set_ident(id);
        chain->set_links(segments + 1);
        chain->set_move_with_object(description->has_bare_flag(kMoveWithObject));
        description->read_hash_value(kBoneStart, &chain->bone_start(), 0);
        description->fetch_vec(kStartOffset, &chain->start_offset(), 1);
        description->read_hash_value(kBoneEnd, &chain->bone_end(), 1);
        description->read_hash_value(kObjEnd, &chain->obj_end(), 1);
        description->fetch_float(kGravityForce, &chain->sag_force(), 1);
        description->fetch_float(kDampForce, &chain->damping(), 1);
        description->fetch_int_value(kIterations, &value, 1);
        chain->set_iterations(value);
        chain->set_collide_below(description->has_bare_flag(kCollideBelow));
        description->fetch_float(kPlaneOffset, &chain->plane_offset(), 0);
        description->fetch_int_value(kCollideFirst, &value, 0); // (not given: the value before)
        chain->set_collide_first(value);
        description->fetch_int_value(kCollideEnd, &value, 0);
        chain->set_collide_end(value);
        for (int j = 0; j < chain->links(); ++j) {
            ChainLink& link = core::at(chain->chain_link(), j);
            link.set_value40(kInitialLinkValue);
            link.set_byte_count(static_cast<float>(static_cast<double>(length) / segments));
            link.at_point() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            link.last_pos() = link.at_point();
            link.next_pos() = link.at_point();
            if (j >= chain->links() - 1) {
                continue;
            }
            // (a grey line to the next link)
            auto* line = memory::make_owned<render::PcPrimitive>(1);
            link.set_line_ptr(line);
            line->set_blend_mode(kLineBlend);
            line->prepare_all({.layout_flags_ = 1, .primitive_type_ = 0, .count = 2}); // (through the jump 0x004A10A0)
            set_line_colour(line, 0, kLineGrey);
            set_line_vertex(line, 0, link.at_point());
            set_line_colour(line, 1, kLineGrey);
            set_line_vertex(line, 1, link.at_point());
            line->set_unknown14(1);
            line->get_ready();
        }
        // (onto the end of the list)
        Elem* head = chains_.head();
        auto* node = static_cast<Elem*>(carve(sizeof(Elem)));
        if (node) {
            node->value = chain;
            node->next = head;
            node->prev = head->prev;
        }
        ++chains_.size();
        head->prev = node;
        Expects(node != nullptr);
        node->prev->next = node;
    }
}

// 0x004294C0
int ChainComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == kSetCollision) {
        uint32_t id = 0;
        params->read_hash_value(kChainId, &id, 1);
        for (const Elem* node = get_first(chains_.head()); node != chains_.head(); node = node->next) {
            Chain* chain = node->value;
            if (chain->ident() != id) {
                continue;
            }
            int value = 0;
            params->fetch_float(kDist, &chain->plane_offset(), 0);
            if (params->fetch_int_value(kCollisionFirst, &value, 0)) {
                chain->set_collide_first(value);
            }
            if (params->fetch_int_value(kCollisionEnd, &value, 0)) {
                chain->set_collide_end(value);
            }
        }
        return 1;
    }
    if (name == kSetLink) {
        uint32_t id = 0;
        params->read_hash_value(kChainId, &id, 1);
        maths::Vec4 pos{};
        params->fetch_vec(kLinkPos, &pos, 1);
        for (const Elem* node = get_first(chains_.head()); node != chains_.head(); node = node->next) {
            Chain* chain = node->value;
            if (chain->ident() != id) {
                continue;
            }
            int index = 0;
            if (!params->fetch_int_value(kLinkIndex, &index, 0)) {
                if (params->has_bare_flag(kFirstLink)) {
                    index = 0;
                } else if (!params->has_bare_flag(kNotLastLink)) {
                    index = chain->links() - 1;
                }
            }
            core::at(chain->chain_link(), index).at_point() = pos;
        }
        return 1;
    }
    if (name == kSetHidden) {
        invisible_ = params->has_bare_flag(kHidden);
        return 1;
    }
    return 2;
}

// 0x004287A0: each chain stepped. The plane (collision_info): through the bone plane_bone_, across the axis from
// axis_from_bone_ to axis_to_bone_ (normalised; its w the difference's). For each chain: each link's step (carried by
// the object's velocity first with move_with_object_) from its velocity (from the last frame, damped) plus gravity;
// the ends pinned - the first link at bone_start (with start_offset in the bone's space, then into the world by the
// display matrix), the last at bone_end of the object obj_end (into the world by that object's display matrix,
// plus this object's velocity over the last frame; no such object: the end as the stack held it, in the
// original - here, as it was); iterations_ constraint passes a second (counted by step_time_ / steps_): each
// pair pulled half way to their rest length (the end's pair: only the one before the end), and the links in the
// collision range kept to their side of the plane (the plane moved plane_offset_ along the axis); then each link
// moved to its step and the lines updated (hidden objects' and hidden chains' lines switched off). The x87
// roundings are the original's.
void ChainComponent::tick_frame()
{
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    const maths::Vec4 velocity = entity()->vel();
    maths::Vec4 plane_point{};
    maths::Vec4 axis_from{};
    maths::Vec4 axis_to{};
    joint_world_point(bone_rig_, plane_bone_, &plane_point);
    joint_world_point(bone_rig_, axis_from_bone_, &axis_from);
    joint_world_point(bone_rig_, axis_to_bone_, &axis_to);
    // The plane's axis (y rounded before it's squared; w the rounded difference).
    const double ax = static_cast<double>(axis_to.x) - axis_from.x;
    const float ay = get_rounded(static_cast<double>(axis_to.y) - axis_from.y);
    const double az = static_cast<double>(axis_to.z) - axis_from.z;
    const float aw = get_rounded(static_cast<double>(axis_to.w) - axis_from.w);
    std::array<float, 3> axis = {{get_rounded(ax), ay, get_rounded(az)}};
    const double axis_length = std::sqrt(((az * az) + (static_cast<double>(ay) * ay)) + (ax * ax));
    if (axis_length > 0.0) {
        const double inverse = 1.0 / axis_length;
        core::at(axis, 0) = get_rounded(ax * inverse);
        core::at(axis, 1) = get_rounded(static_cast<double>(ay) * inverse);
        core::at(axis, 2) = get_rounded(az * inverse);
    }

    for (const Elem* node = get_first(chains_.head()); node != chains_.head(); node = node->next) {
        Chain* chain = node->value;
        // The steps.
        for (int i = chain->links() - 1; i >= 0; --i) {
            ChainLink& link = core::at(chain->chain_link(), i);
            if (chain->move_with_object()) {
                const double vx = static_cast<double>(velocity.x) * frame;
                const double vy = static_cast<double>(velocity.y) * frame;
                const double vz = static_cast<double>(velocity.z) * frame;
                const float vxf = get_rounded(vx);
                const float vzf = get_rounded(vz);
                const float vwf = get_rounded(static_cast<double>(velocity.w) * frame);
                link.at_point().x = get_rounded(static_cast<double>(vxf) + link.at_point().x);
                link.at_point().y = get_rounded(vy + link.at_point().y);
                link.at_point().z = get_rounded(vz + link.at_point().z);
                link.at_point().w = get_rounded(static_cast<double>(vwf) + link.at_point().w);
                link.last_pos().x = get_rounded(static_cast<double>(vxf) + link.last_pos().x);
                link.last_pos().y = get_rounded(vy + link.last_pos().y);
                link.last_pos().z = get_rounded(static_cast<double>(vzf) + link.last_pos().z);
                link.last_pos().w = get_rounded(static_cast<double>(vwf) + link.last_pos().w);
            }
            const double dx = static_cast<double>(link.at_point().x) - link.last_pos().x;
            const double dy = static_cast<double>(link.at_point().y) - link.last_pos().y;
            const float dz = get_rounded(static_cast<double>(link.at_point().z) - link.last_pos().z);
            const float dw = get_rounded(static_cast<double>(link.at_point().w) - link.last_pos().w);
            const float inverse = get_rounded(1.0 / final_frame_);
            float vx = get_rounded(dx * inverse);
            const double vy = dy * inverse;
            const double vz = static_cast<double>(inverse) * dz;
            const float vw = get_rounded(static_cast<double>(inverse) * dw);
            const double damping = static_cast<double>(frame) * chain->damping();
            const float damped_y = get_rounded(damping * vy);
            const float damped_z = get_rounded(damping * vz);
            const float damped_w = get_rounded(damping * vw);
            vx = get_rounded(static_cast<double>(vx) - (static_cast<double>(vx) * damping));
            const float vyf = get_rounded(vy - damped_y);
            const double vzd = vz - damped_z;
            const double vwd = static_cast<double>(vw) - damped_w;
            const float step_x = get_rounded(static_cast<double>(vx) * frame);
            const float step_y = get_rounded(static_cast<double>(vyf) * frame);
            const float step_z = get_rounded(vzd * frame);
            const double step_w = vwd * frame;
            link.next_pos().x = get_rounded(static_cast<double>(step_x) + link.at_point().x);
            link.next_pos().y = get_rounded(static_cast<double>(step_y) + link.at_point().y);
            link.next_pos().z = get_rounded(static_cast<double>(step_z) + link.at_point().z);
            link.next_pos().w = get_rounded(step_w + link.at_point().w);
            link.next_pos().y = get_rounded(static_cast<double>(link.next_pos().y) -
                                            (static_cast<double>(frame) * chain->sag_force() * frame));
        }

        // The start: bone_start's position plus start_offset turned by the bone, into the world.
        maths::Vec4 start{};
        bone_position(bone_rig_, chain->bone_start(), &start);
        maths::Mat4 bone{};
        skin_matrix_ptr(bone_rig_, chain->bone_start(), &bone);
        core::at(bone.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // (turned only)
        const maths::Vec4 turned = chain->start_offset() * bone;
        start = {
            .x = get_rounded(static_cast<double>(start.x) + turned.x), .y = get_rounded(static_cast<double>(start.y) + turned.y),
            .z = get_rounded(static_cast<double>(start.z) + turned.z), .w = get_rounded(static_cast<double>(start.w) + turned.w)};
        {
            const maths::Mat4& matrix = entity()->display_matrix();
            const maths::Vec4& at = entity()->at_point();
            const double x = ((static_cast<double>(start.z) * core::at(matrix.row, 2).x) +
                              (static_cast<double>(start.y) * core::at(matrix.row, 1).x)) +
                             (static_cast<double>(start.x) * core::at(matrix.row, 0).x);
            const double y = ((static_cast<double>(start.z) * core::at(matrix.row, 2).y) +
                              (static_cast<double>(start.y) * core::at(matrix.row, 1).y)) +
                             (static_cast<double>(start.x) * core::at(matrix.row, 0).y);
            const float z = get_rounded(((static_cast<double>(start.z) * core::at(matrix.row, 2).z) +
                                         (static_cast<double>(start.y) * core::at(matrix.row, 1).z)) +
                                        (static_cast<double>(start.x) * core::at(matrix.row, 0).z));
            start.x = get_rounded(x + at.x);
            start.y = get_rounded(y + at.y);
            start.z = get_rounded(static_cast<double>(z) + at.z);
            start.w = get_rounded(static_cast<double>(start.w) + at.w);
        }
        // The end: bone_end of obj_end, into that object's world, plus this object's velocity over the last frame.
        // (no obj_end: the original's end is whatever its stack held - zero here)
        maths::Vec4 end = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        if (auto* other = core::down_cast<GameEntity>(find_object(chain->obj_end()))) {
            bone_position(other->fetch_module(kSkeleton), chain->bone_end(), &end);
            const maths::Mat4& matrix = other->display_matrix();
            const maths::Vec4& at = other->at_point();
            const double x = ((static_cast<double>(end.y) * core::at(matrix.row, 1).x) +
                              (static_cast<double>(end.z) * core::at(matrix.row, 2).x)) +
                             (static_cast<double>(end.x) * core::at(matrix.row, 0).x);
            const double y = ((static_cast<double>(end.y) * core::at(matrix.row, 1).y) +
                              (static_cast<double>(end.z) * core::at(matrix.row, 2).y)) +
                             (static_cast<double>(end.x) * core::at(matrix.row, 0).y);
            const float z = get_rounded(((static_cast<double>(end.y) * core::at(matrix.row, 1).z) +
                                         (static_cast<double>(end.z) * core::at(matrix.row, 2).z)) +
                                        (static_cast<double>(end.x) * core::at(matrix.row, 0).z));
            end.x = get_rounded(x + at.x);
            end.y = get_rounded(y + at.y);
            end.z = get_rounded(static_cast<double>(z) + at.z);
            const double w = static_cast<double>(end.w) + at.w;
            const double last_frame = final_frame_;
            const double carried_x = static_cast<double>(velocity.x) * last_frame;
            const float carried_y = get_rounded(static_cast<double>(velocity.y) * last_frame);
            const float carried_z = get_rounded(static_cast<double>(velocity.z) * last_frame);
            const float carried_w = get_rounded(static_cast<double>(velocity.w) * last_frame);
            end.x = get_rounded(carried_x + end.x);
            end.y = get_rounded(static_cast<double>(carried_y) + end.y);
            end.z = get_rounded(static_cast<double>(carried_z) + end.z);
            end.w = get_rounded(static_cast<double>(carried_w) + w);
        }

        // The constraint passes due (iterations_ a second).
        const double done = static_cast<double>(frame) + step_time_;
        step_time_ = get_rounded(done);
        auto iterations = static_cast<double>(chain->iterations());
        if (chain->iterations() < 0) {
            iterations += maths::kUnsignedWrap;
        }
        auto steps = static_cast<double>(steps_);
        if (steps_ < 0) {
            steps += maths::kUnsignedWrap;
        }
        const int passes = static_cast<int>(static_cast<int64_t>((iterations * done) - steps));
        steps_ += passes;
        if (done >= 1.0) {
            step_time_ = get_rounded(done - 1.0);
            steps_ -= chain->iterations();
        }
        for (int pass = passes; pass != 0; --pass) {
            auto& link = chain->chain_link();
            const int last = chain->links() - 1;
            core::at(link, 0).next_pos() = start;
            core::at(link, last).next_pos() = end;
            // Each pair pulled (or pushed) half way to its rest length.
            for (int i = 1; i < last; ++i) {
                maths::Vec4& prev = core::at(link, i - 1).next_pos();
                maths::Vec4& cur = core::at(link, i).next_pos();
                const double dx = static_cast<double>(cur.x) - prev.x;
                const double dy = static_cast<double>(cur.y) - prev.y;
                const double dz = static_cast<double>(cur.z) - prev.z;
                const double length = std::sqrt(((dx * dx) + (dz * dz)) + (dy * dy));
                const float dzf = get_rounded(dz);
                const float dwf = get_rounded(static_cast<double>(cur.w) - prev.w);
                const float inverse = get_rounded(1.0 / length);
                const float nx = get_rounded(dx * inverse);
                const float ny = get_rounded(static_cast<double>(inverse) * dy);
                const float nz = get_rounded(static_cast<double>(inverse) * dzf);
                const float nw = get_rounded(static_cast<double>(inverse) * dwf);
                const float half = get_rounded((static_cast<double>(core::at(link, i).byte_count()) - length) * 0.5);
                const float cx = get_rounded(static_cast<double>(nx) * half);
                const double cy = static_cast<double>(half) * ny;
                const double cz = static_cast<double>(half) * nz;
                const float czf = get_rounded(cz);
                const float cw = get_rounded(static_cast<double>(half) * nw);
                cur.x = get_rounded(static_cast<double>(cx) + cur.x);
                cur.y = get_rounded(cy + cur.y);
                cur.z = get_rounded(cz + cur.z);
                cur.w = get_rounded(static_cast<double>(cw) + cur.w);
                prev.x = get_rounded(static_cast<double>(prev.x) - cx);
                prev.y = get_rounded(static_cast<double>(prev.y) - cy);
                prev.z = get_rounded(static_cast<double>(prev.z) - czf);
                prev.w = get_rounded(static_cast<double>(prev.w) - cw);
                core::at(link, i).set_value40(0.0F);
            }
            // The last pair: only the link before the end moves.
            {
                maths::Vec4& prev = core::at(link, last - 1).next_pos();
                const maths::Vec4& cur = core::at(link, last).next_pos();
                const double dx = static_cast<double>(cur.x) - prev.x;
                const double dy = static_cast<double>(cur.y) - prev.y;
                const double dz = static_cast<double>(cur.z) - prev.z;
                const double length = std::sqrt(((dz * dz) + (dy * dy)) + (dx * dx));
                const float dzf = get_rounded(dz);
                const float dwf = get_rounded(static_cast<double>(cur.w) - prev.w);
                const float inverse = get_rounded(1.0 / length);
                const float nx = get_rounded(dx * inverse);
                const double ny = dy * inverse;
                const float nz = get_rounded(static_cast<double>(inverse) * dzf);
                const float nw = get_rounded(static_cast<double>(inverse) * dwf);
                const float half = get_rounded((static_cast<double>(core::at(link, last).byte_count()) - length) * 0.5);
                const float cx = get_rounded(static_cast<double>(nx) * half);
                const double cy = static_cast<double>(half) * ny;
                const double cz = static_cast<double>(half) * nz;
                const float cw = get_rounded(static_cast<double>(half) * nw);
                prev.x = get_rounded(static_cast<double>(prev.x) - cx);
                prev.y = get_rounded(static_cast<double>(prev.y) - cy);
                prev.z = get_rounded(static_cast<double>(prev.z) - cz);
                prev.w = get_rounded(static_cast<double>(prev.w) - cw);
            }
            // The collision range kept to its side of the plane.
            for (int i = chain->collide_first(); i < chain->collide_end(); ++i) {
                core::at(link, i).set_value40(kFullLinkValue);
                const double offset = chain->plane_offset();
                const double px = (static_cast<double>(core::at(axis, 0)) * offset) + plane_point.x;
                const double py = static_cast<double>(plane_point.y) + get_rounded(offset * core::at(axis, 1));
                const float pz = get_rounded(static_cast<double>(plane_point.z) + get_rounded(offset * core::at(axis, 2)));
                maths::Vec4& cur = core::at(link, i).next_pos();
                const float dx = get_rounded(static_cast<double>(cur.x) - px);
                const float dy = get_rounded(static_cast<double>(cur.y) - py);
                const float dz = get_rounded(static_cast<double>(cur.z) - pz);
                float ux = dx;
                float uy = dy;
                double uz = dz;
                const double length = std::sqrt(((static_cast<double>(dx) * dx) + (static_cast<double>(dz) * dz)) +
                                                (static_cast<double>(dy) * dy));
                const float lengthf = get_rounded(length);
                if (length > 0.0) {
                    const double inverse = 1.0 / lengthf;
                    ux = get_rounded(dx * inverse);
                    uy = get_rounded(dy * inverse);
                    uz = inverse * dz;
                }
                const double side =
                    ((static_cast<double>(core::at(axis, 2)) * uz) + (static_cast<double>(uy) * core::at(axis, 1))) +
                    (static_cast<double>(ux) * core::at(axis, 0));
                const bool push = chain->collide_below() ? side > 0.0 : side < 0.0;
                if (!push) {
                    continue;
                }
                const double along =
                    ((static_cast<double>(dz) * core::at(axis, 2)) + (static_cast<double>(dy) * core::at(axis, 1))) +
                    (static_cast<double>(dx) * core::at(axis, 0));
                const double out_x = static_cast<double>(core::at(axis, 0)) * along;
                const float out_y = get_rounded(along * core::at(axis, 1));
                const float out_z = get_rounded(along * core::at(axis, 2));
                const float out_w = get_rounded(along * aw);
                cur.x = get_rounded(static_cast<double>(cur.x) - out_x);
                cur.y = get_rounded(static_cast<double>(cur.y) - out_y);
                cur.z = get_rounded(static_cast<double>(cur.z) - out_z);
                cur.w = get_rounded(static_cast<double>(cur.w) - out_w);
            }
        }

        // Each link moved to its step.
        for (int i = chain->links() - 1; i >= 0; --i) {
            ChainLink& link = core::at(chain->chain_link(), i);
            link.last_pos() = link.at_point();
            link.at_point() = link.next_pos();
        }
        if (((entity()->composite_flags() & kCompositeFlagHidden) == 0U) && !invisible_) {
            chain->update_lines();
        } else {
            for (uint32_t i = 1; i < ints::as_unsigned(chain->links()); ++i) {
                core::at(chain->chain_link(), i - 1).line_ptr()->set_ready(false);
            }
        }
    }
    final_frame_ = frame;
}

} // namespace objects

