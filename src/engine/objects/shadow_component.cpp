// The shadow component (engine/objects/shadow_component.h).

#include "engine/objects/shadow_component.h"
#include "engine/gfx/engine_model_lights.h"

#include <memory>
#include <cstdint>
#include <gsl/pointers>

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_shadows.h"
#include "engine/gfx/model.h"
#include "engine/maths/mat4.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/scene_load.h"

namespace objects {

namespace {

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kShadowType = 0x9AC24B18;       // ShadowType
constexpr uint32_t kDetailed = 0x76A54CD1;         // detailed
constexpr uint32_t kSimple = 0x3E84C2FD;           // simple
constexpr uint32_t kVolume = 0x46653221;           // Volume
constexpr uint32_t kNone = GroundShadowModule::kNoShadow; // none
constexpr uint32_t kStayPut = 0x7E773A11;          // (a flag: the shadow doesn't follow the object; name unknown)
constexpr uint32_t kShadowModel = 0x545F8172;      // ShadowModel
constexpr uint32_t kBlobSizeFactor = 0x6F8CD62F;   // ShadowScale
constexpr uint32_t kOffset = 0xA6F5352F;           // offset
constexpr uint32_t kCastNoShadow = 0xAAF6E513;     // Obj_ShadowOff
constexpr uint32_t kEnableShadowCast = 0xF272C43A; // Obj_ShadowOn

// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum ShadowKind : uint8_t { kShadowDetailed = 1, kShadowSimple = 2, kShadowVolume = 3 };

// The detailed and Volume shadows are cast along this over this distance.
const maths::Vec4 kDown = {0.0f, -1.0f, 0.0f, 1.0f};
constexpr float kCastDistance = 72.0f;
// A detailed shadow is placed this far up the object's up.
constexpr float kDetailedHeight = 36.0f;

// The renderer's shadow object's kind (engine_shadows.h).
int shadow_kind_of(const render::ShadowBase* shadow) { return shadow->kind_tag_; }
// A simple shadow's scale and offset (the kind checked by the caller - or not, as the original).
render::BlobShadow* as_blob(render::ShadowBase* shadow) { return core::down_cast<render::BlobShadow>(shadow); }

// The model's +0x2C object (vtable slot 5 gives a vector).

// The model component's model (+0x208), and that model's flags (+0x44: 0x2 visible, 0x4 hidden), its +0x1C (a
// simple shadow's first scale), its checksum (+0x30) and its +0x2C object (vtable slot 5 gives a vector).
render::RenderModel* model_of(ObjectModule* component)
{
    return core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance_;
}
// 0x004949B0 (not rewritten yet): whether the model casts a shadow (its flag 0x10).
void set_model_shadow(render::RenderModel* model, bool on) { model->activate_shadow(on); }
// 0x00493040 (not rewritten yet): the model's hierarchy (0x50-byte objects from +0x50; the root's matrix at +0x10).
const render::HierarchyNode* hierarchy_of(render::RenderModel* model)
{
    return model->get_asset_hierarchy();
}

} // namespace

// 0x00431800
gsl::owner<ObjectModule*> GroundShadowModule::fabricate() { return new GroundShadowModule; }

// 0x00431380
GroundShadowModule::GroundShadowModule() : shadow_handle_(nullptr)
{
    kind_tag_ = kType;
    surface_dir_ = {0.0f, 1.0f, 0.0f, 1.0f};
    at_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00431300 (0x004313D0 the deleting one)
GroundShadowModule::~GroundShadowModule()
{
    if (shadow_handle_) {
        shadow_handle_.reset();
    }
}

// 0x00431400
void GroundShadowModule::seal()
{
    mesh_instance_ = entity_->fetch_module(kModel);
    const uint32_t checksum = model_of(mesh_instance_)->source_path_;
    void* asset = assets::g_asset_manager->get_asset_data(checksum, 0);
    if (shadow_handle_)
        shadow_handle_->change_mesh(model_of(mesh_instance_),
                                    static_cast<uint8_t*>(asset));
}

// 0x00431450
void GroundShadowModule::refresh_shadow()
{
    if (!shadow_handle_ || !mesh_instance_)
        return;
    const render::RenderModel* model = model_of(mesh_instance_);
    if (!model)
        return;
    const uint8_t flags = model->flag_bits_;
    if (!(flags & 0x2) || (flags & 0x4)) {
        shadow_handle_->hide();
        return;
    }
    switch (shadow_kind_of(shadow_handle_.get())) {
    case kShadowDetailed: {
        const GameEntity* object = entity_;
        const maths::Vec4 above = object->at_point_ + core::at(object->basis_frame_.row, 1) * kDetailedHeight;
        // (the model's lights' first direction - their slot 5 -, turned round - and not used)
        if (auto* lights =
                (model->lights_ptr_)) {
            const maths::Vec4* v = lights->read_light_aim(0);
            const maths::Vec4 unused = *v * -1.0f;
            (void)unused;
        }
        shadow_handle_->change_place(above);
        break;
    }
    case kShadowSimple:
        as_blob(shadow_handle_.get())->size_factor_ = model->size_factor_.x;
        [[fallthrough]];
    case kShadowVolume:
        shadow_handle_->tick_frame(&at_point_, &entity_->basis_frame_, surface_dir_);
        break;
    default:
        break;
    }
    shadow_handle_->show();
}

// 0x00431570
void GroundShadowModule::conceal(bool hide)
{
    if (!shadow_handle_)
        return;
    if (hide)
        shadow_handle_->hide();
    else
        shadow_handle_->show();
}

// 0x004315A0
void GroundShadowModule::build_blob_shadow(int type, const char* model_name)
{
    if (shadow_handle_)
        return;
    ObjectModule* model_component = entity_->fetch_module(kModel);
    render::RenderModel* model = model_component ? model_of(model_component) : nullptr;
    switch (type) {
    case kShadowDetailed:
        shadow_handle_ =
            std::make_unique<render::FullShadow>(model, kDown, kCastDistance);
        return;
    case kShadowSimple: {
        auto* shadow = new render::BlobShadow();
        // (no shadow - out of memory - is written through regardless, as the original)
        shadow->size_factor_ = model->size_factor_.x;
        shadow->load_mesh(model_name);
        shadow_handle_.reset(shadow);
        break;
    }
    case kShadowVolume:
        shadow_handle_ =
            std::make_unique<render::ShadowVolume>(model, kDown, kCastDistance);
        break;
    default:
        return;
    }
    shadow_handle_->tick_frame(&at_point_, &entity_->basis_frame_, surface_dir_);
}

// 0x00431780
void GroundShadowModule::destroy_shadow()
{
    if (!shadow_handle_)
        return;
    if (mesh_instance_)
        set_model_shadow(model_of(mesh_instance_), false);
    shadow_handle_.reset();
}

// 0x004317C0
void GroundShadowModule::change_place(const maths::Vec4* position) { at_point_ = *position; }

// 0x004317E0
void GroundShadowModule::set_normal(const maths::Vec4* normal) { surface_dir_ = *normal; }

// 0x00431360
void GroundShadowModule::set_light_direction(const maths::Vec4* direction)
{
    if (shadow_handle_)
        shadow_handle_->set_light_direction(direction);
}

// 0x00431860
void GroundShadowModule::configure_from_data(script::Struct* params)
{
    shadow_kind_ = kSimple;
    params->read_hash_value(kShadowType, &shadow_kind_, 0);
    if (params->has_bare_flag(kStayPut))
        follow_object_ = false;
    destroy_shadow();
    switch (shadow_kind_) {
    case kDetailed:
        if (!shadow_handle_)
            build_blob_shadow(kShadowDetailed, nullptr);
        break;
    case kSimple: {
        const char* model = "Ped_Shadow";
        params->fetch_text(kShadowModel, &model, 0);
        if (!shadow_handle_)
            build_blob_shadow(kShadowSimple, model);
        float scale = 1.0f;
        params->fetch_float(kBlobSizeFactor, &scale, 0);
        as_blob(shadow_handle_.get())->size_factor_ = scale;
        shadow_handle_->tick_frame(&at_point_, &entity_->basis_frame_, surface_dir_);
        break;
    }
    case kVolume:
        if (!shadow_handle_)
            build_blob_shadow(kShadowVolume, nullptr);
        break;
    case kNone:
        if (shadow_handle_)
            destroy_shadow();
        break;
    default:
        break;
    }
}

// 0x004319D0
void GroundShadowModule::tick_frame()
{
    if (follow_object_) {
        at_point_ = entity_->at_point_;
        if (shadow_handle_) {
            maths::Mat4 matrix = entity_->display_matrix_;
            maths::Vec4 pos = entity_->at_point_;
            render::RenderModel* model = mesh_instance_ ? model_of(mesh_instance_) : nullptr;
            if (model && hierarchy_of(model)) {
                // The model's root: its matrix times the display matrix, at the object's position plus its translation.
                const maths::Vec4& root = core::at(hierarchy_of(model)->matrix.row, 3);
                pos = {root.x, root.y, root.z, 0.0f};
                const maths::Mat4 base_mat = hierarchy_of(model)->matrix;
                (void)base_mat; // (copied and not used: the product reads the hierarchy's again)
                pos += entity_->at_point_;
                matrix = maths::MatrixProduct{&hierarchy_of(model)->matrix,
                                              &entity_->display_matrix_};
            }
            core::at(matrix.row, 3) = pos;
            shadow_handle_->change_mat(&matrix);
        }
    }
    refresh_shadow();
}

// 0x00431B10
int GroundShadowModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == kCastNoShadow) {
        destroy_shadow();
        return 1;
    }
    if (name != kEnableShadowCast)
        return 2;
    uint32_t type = 0; // (read into the parameters' own argument slot)
    if (!params->read_hash_value(kShadowType, &type, 0))
        params->put_name_key(kShadowType, kSimple);
    configure_from_data(params);
    if (shadow_handle_.get() && shadow_kind_of(shadow_handle_.get()) == kShadowSimple) {
        float offset = 0.2f; // (read into the name's argument slot)
        params->fetch_float(kOffset, &offset, 0);
        as_blob(shadow_handle_.get())->shift_ = offset;
    }
    return 1;
}

} // namespace objects
