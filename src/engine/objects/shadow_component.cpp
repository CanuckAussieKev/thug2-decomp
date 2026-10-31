// The shadow component (engine/objects/shadow_component.h).

#include "engine/objects/shadow_component.h"
#include "engine/gfx/engine_model_lights.h"

#include <cstdint>

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

namespace objects {

namespace {

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kShadowType = 0x9AC24B18;       // ShadowType
constexpr uint32_t kDetailed = 0x76A54CD1;         // detailed
constexpr uint32_t kSimple = 0x3E84C2FD;           // simple
constexpr uint32_t kVolume = 0x46653221;           // Volume
constexpr uint32_t kNone = 0x806FFF30;             // none
constexpr uint32_t kStayPut = 0x7E773A11;          // (a flag: the shadow doesn't follow the object; name unknown)
constexpr uint32_t kShadowModel = 0x545F8172;      // ShadowModel
constexpr uint32_t kBlobSizeFactor = 0x6F8CD62F;   // ShadowScale
constexpr uint32_t kOffset = 0xA6F5352F;           // offset
constexpr uint32_t kCastNoShadow = 0xAAF6E513;     // Obj_ShadowOff
constexpr uint32_t kEnableShadowCast = 0xF272C43A; // Obj_ShadowOn

enum ShadowKind { kShadowDetailed = 1, kShadowSimple = 2, kShadowVolume = 3 };

// The detailed and Volume shadows are cast along this over this distance.
const maths::Vec4 kDown = {0.0f, -1.0f, 0.0f, 1.0f};
constexpr float kCastDistance = 72.0f;
// A detailed shadow is placed this far up the object's up.
constexpr float kDetailedHeight = 36.0f;

// The renderer's shadow object's kind (engine_shadows.h).
int shadow_kind_of(const render::ShadowBase* shadow) { return shadow->kind_tag_; }
// A simple shadow's scale and offset (the kind checked by the caller - or not, as the original).
render::BlobShadow* as_blob(render::ShadowBase* shadow) { return static_cast<render::BlobShadow*>(shadow); }

// The model's +0x2C object (vtable slot 5 gives a vector).

// The model component's model (+0x208), and that model's flags (+0x44: 0x2 visible, 0x4 hidden), its +0x1C (a
// simple shadow's first scale), its checksum (+0x30) and its +0x2C object (vtable slot 5 gives a vector).
uint8_t* model_of(ObjectModule* component)
{
    return reinterpret_cast<uint8_t*>(static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_);
}
// 0x004949B0 (not rewritten yet): whether the model casts a shadow (its flag 0x10).
void set_model_shadow(uint8_t* model, bool on) { reinterpret_cast<render::RenderModel*>(model)->activate_shadow(on); }
// 0x00493040 (not rewritten yet): the model's hierarchy (0x50-byte objects from +0x50; the root's matrix at +0x10).
const uint8_t* hierarchy_of(uint8_t* model)
{
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<render::RenderModel*>(model)->get_asset_hierarchy());
}

} // namespace

// 0x00431800
ObjectModule* __cdecl GroundShadowModule::fabricate() { return new GroundShadowModule; }

// 0x00431380
GroundShadowModule::GroundShadowModule()
{
    kind_tag_ = kType;
    shadow_handle_ = nullptr;
    surface_dir_ = {0.0f, 1.0f, 0.0f, 1.0f};
    shadow_kind_ = kNone;
    at_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    follow_object_ = true;
}

// 0x00431300 (0x004313D0 the deleting one)
GroundShadowModule::~GroundShadowModule()
{
    if (shadow_handle_) {
        delete shadow_handle_;
        shadow_handle_ = nullptr;
    }
}

// 0x00431400
void GroundShadowModule::seal()
{
    mesh_instance_ = entity_->fetch_module(kModel);
    uint32_t checksum = *reinterpret_cast<uint32_t*>(model_of(mesh_instance_) + 0x30);
    void* asset = assets::g_asset_manager->get_asset_data(checksum, 0);
    if (shadow_handle_)
        shadow_handle_->change_mesh(reinterpret_cast<render::RenderModel*>(model_of(mesh_instance_)),
                                    static_cast<uint8_t*>(asset));
}

// 0x00431450
void GroundShadowModule::refresh_shadow()
{
    if (!shadow_handle_ || !mesh_instance_)
        return;
    uint8_t* model = model_of(mesh_instance_);
    if (!model)
        return;
    uint8_t flags = model[0x44];
    if (!(flags & 0x2) || (flags & 0x4)) {
        shadow_handle_->hide();
        return;
    }
    switch (shadow_kind_of(shadow_handle_)) {
    case kShadowDetailed: {
        const GameEntity* object = entity_;
        maths::Vec4 above = object->at_point_ + object->basis_frame_.row[1] * kDetailedHeight;
        // (the model's lights' first direction - their slot 5 -, turned round - and not used)
        if (auto* lights = *reinterpret_cast<render::ModelLights**>(model + 0x2C)) {
            const maths::Vec4* v = lights->read_light_aim(0);
            maths::Vec4 unused = *v * -1.0f;
            (void)unused;
        }
        shadow_handle_->change_place(above);
        break;
    }
    case kShadowSimple:
        as_blob(shadow_handle_)->size_factor_ = *reinterpret_cast<float*>(model + 0x1C);
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
    uint8_t* model = model_component ? model_of(model_component) : nullptr;
    switch (type) {
    case kShadowDetailed:
        shadow_handle_ = new render::FullShadow(reinterpret_cast<render::RenderModel*>(model), kDown, kCastDistance);
        return;
    case kShadowSimple: {
        auto* shadow = new render::BlobShadow();
        // (no shadow - out of memory - is written through regardless, as the original)
        shadow->size_factor_ = *reinterpret_cast<float*>(model + 0x1C);
        shadow->load_mesh(model_name);
        shadow_handle_ = shadow;
        break;
    }
    case kShadowVolume:
        shadow_handle_ = new render::ShadowVolume(reinterpret_cast<render::RenderModel*>(model), kDown, kCastDistance);
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
    delete shadow_handle_;
    shadow_handle_ = nullptr;
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
        as_blob(shadow_handle_)->size_factor_ = scale;
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
            uint8_t* model = mesh_instance_ ? model_of(mesh_instance_) : nullptr;
            if (model && hierarchy_of(model)) {
                // The model's root: its matrix times the display matrix, at the object's position plus its translation.
                const float* root = reinterpret_cast<const float*>(hierarchy_of(model) + 0x40);
                pos = {root[0], root[1], root[2], 0.0f};
                maths::Mat4 base_mat = *reinterpret_cast<const maths::Mat4*>(hierarchy_of(model) + 0x10);
                (void)base_mat; // (copied and not used: the product reads the hierarchy's again)
                pos += entity_->at_point_;
                matrix = maths::MatrixProduct{reinterpret_cast<const maths::Mat4*>(hierarchy_of(model) + 0x10),
                                              &entity_->display_matrix_};
            }
            matrix.row[3] = pos;
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
    uint32_t type; // (read into the parameters' own argument slot)
    if (!params->read_hash_value(kShadowType, &type, 0))
        params->put_name_key(kShadowType, kSimple);
    configure_from_data(params);
    if (shadow_handle_ && shadow_kind_of(shadow_handle_) == kShadowSimple) {
        float offset = 0.2f; // (read into the name's argument slot)
        params->fetch_float(kOffset, &offset, 0);
        as_blob(shadow_handle_)->shift_ = offset;
    }
    return 1;
}

} // namespace objects
