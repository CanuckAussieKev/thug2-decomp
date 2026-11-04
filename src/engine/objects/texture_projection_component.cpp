// The texture projection component (engine/objects/texture_projection_component.h).

#include "engine/objects/texture_projection_component.h"

#include <cstdint>

#include "engine/memory/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/gfx/engine_texture_projection.h"
#include "engine/gfx/pc_texture_projection.h"
#include "engine/objects/movie_manager.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/camera_lookaround.h"
#include <gsl/pointers>
#include "engine/memory/arena_alloc.h"

namespace objects {

namespace {
constexpr uint32_t kProjectionLabel = 0x1EE8B7B9; // text unknown (the projection label)
constexpr uint32_t kSuspend = 0xCE0CA665; // Suspend
constexpr uint32_t kSkeleton = 0x222756D5; // Skeleton
constexpr uint32_t kModel = 0x286A8D26; // Model

constexpr uint32_t kTextureProjections = 0xFF7E0DAD; // texture_projections

// A projection (the renderer's, 0xC8 bytes; a list node with its checksum at +0x14): made (0x004C7E80), set up from
// its structure (0x004A04C0) and updated from an object (0x0049FFE0).
gsl::owner<render::TextureProjection*> new_projection() { return new render::PcTextureProjection; }

bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }

} // namespace

// 0x00434870
gsl::owner<ObjectModule*> TextureProjectionComponent::fabricate() { return new TextureProjectionComponent; }

// (the constructor: inlined in fabricate in the original)
TextureProjectionComponent::TextureProjectionComponent()
{
    projections_.make_head();
    set_kind_tag(kType);
}

// 0x004346B0 (0x00434840 the deleting one)
TextureProjectionComponent::~TextureProjectionComponent()
{
    lists::delete_each<render::TextureProjection>(&projections_,
                                                  [](render::TextureProjection* projection) {
                                                      gsl::owner<render::TextureProjection*> owned =
                                                          memory::owned_element(projection); // (the list's)
                                                      delete owned;
                                                  });
    projections_.uncouple();
}

// 0x00434730
void TextureProjectionComponent::configure_from_data(script::Struct* params)
{
    script::Array* projections = nullptr;
    params->fetch_list(kTextureProjections, &projections, 1);
    memory::ListNode& head = projections_;
    for (uint32_t i = 0; i < projections->extent(); ++i) {
        auto* settings = projections->struct_at(i);
        uint32_t name = 0; // (read into the parameters' own argument slot)
        settings->read_hash_value(kProjectionLabel, &name, 1);
        gsl::owner<render::TextureProjection*> projection = new_projection();
        // (out of memory: written through null, as the original)
        projection->set_label(name);
        projection->configure_from_data(settings);
        // At the front.
        memory::ListNode* node = &projection->list_link();
        node->set_prev(&head);
        node->set_next(head.next());
        head.next()->set_prev(node);
        head.set_next(node);
    }
}

// 0x00434630
void TextureProjectionComponent::seal()
{
    suspend_ = entity()->fetch_module(kSuspend);
    bone_rig_ = entity()->fetch_module(kSkeleton);
    mesh_instance_ = entity()->fetch_module(kModel);
}

// 0x00434810
void TextureProjectionComponent::tick_frame()
{
    // (the head test before the null test, as the original)
    for (const memory::ListNode* node = projections_.next(); !is_head(node) && node; node = node->next()) {
        static_cast<render::TextureProjection*>(node->data())->tick_frame(entity());
    }
}

} // namespace objects
