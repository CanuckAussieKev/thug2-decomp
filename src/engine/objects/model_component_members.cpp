// The model component's script members (GameObjects::MeshInstanceModule::invoke_method_by_name, 0x00421AE0). From its disassembly
// (0x00421AE0-0x00422CF4: a binary search on the name; each member's code below in the order of their names).
// Parameters the original reads without giving them a default are left as they were (unset) - noted where so.

#include <cstdint>
#include <array>

#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/maths/mat4.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/model_component.h"
#include "engine/objects/model_component_nx.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/core/checksums.h"
#include "engine/objects/control_pad.h"
#include "engine/gfx/engine_geom.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"
#include <cstring>

namespace objects {

using namespace model_nx;

namespace {

// The members (their names' checksums).
constexpr uint32_t kSwitchOnAtomic = 0x07D0C128;
constexpr uint32_t kObjUpdateBrightness = 0x1C16332E;
constexpr uint32_t kObjGetModelName = 0x1C2E9E21;
constexpr uint32_t kObjUpdateModel = 0x1C31AC56;
constexpr uint32_t kObjSetVisibility = 0x220884B6;
constexpr uint32_t kObjEnableScaling = 0x347FF11B;
constexpr uint32_t kObjSetLightAmbientColor = 0x3CF63049;
constexpr uint32_t kCancelRotateDisplay = 0x4424C267;
constexpr uint32_t kObjSetBoundingSphere = 0x452FB315;
constexpr uint32_t kObjSetLightDirection = 0x46A1F4A4;
constexpr uint32_t kDisableDisplayFlip = 0x4AF3B1F7;
constexpr uint32_t kObjInitModel = 0x4D8E11AB;
constexpr uint32_t kObjGetScaling = 0x57B8D888;
constexpr uint32_t kObjSetUvOffset = 0x57EB306E;
constexpr uint32_t kObjRestoreBoundingSphere = 0x5B41DA8F;
constexpr uint32_t kClearGeom = 0x5BE3432A;
constexpr uint32_t kObjClearColor = 0x6052480A;
constexpr uint32_t kObjApplyScaling = 0x657FF1ED;
constexpr uint32_t kObjSetLightDiffuseColor = 0x70E6466B;
constexpr uint32_t kEnableDisplayFlip = 0x721184C7;
constexpr uint32_t kObjSetColor = 0x76ECFA1C;
constexpr uint32_t kObjTestModelCollision = 0x7ADD91D2;
constexpr uint32_t kObjSetUvParams = 0x812FF44D;
constexpr uint32_t kObjReplaceTexture = 0x83F9BE15;
constexpr uint32_t kAddGeom = 0x890E57ED;
constexpr uint32_t kObjDisableAmbientLight = 0x8A445E26;
constexpr uint32_t kObjSetColorRgb = 0x8CA6BC2C;
constexpr uint32_t kObjInitModelFromProfile = 0x98ED5C8B;
constexpr uint32_t kObjReplaceSpriteTexture = 0x9A478AFE;
constexpr uint32_t kObjEnableAmbientLight = 0xA14D6372;
constexpr uint32_t kRotateDisplay = 0xA4C25C2F;
constexpr uint32_t kObjGetBoneVolume = 0xA6CD4402;
constexpr uint32_t kObjClearVisibility = 0xA98B0648;
constexpr uint32_t kObjClearGeoms = 0xBD18E2E3;
constexpr uint32_t kObjDisableDiffuseLight = 0xC6542804;
constexpr uint32_t kObjHasModelLights = 0xE11F85E8;
constexpr uint32_t kSwitchOffAtomic = 0xE48FD084;
constexpr uint32_t kAtomicIsHidden = 0xE7FA7DD0;
constexpr uint32_t kObjEnableDiffuseLight = 0xED5D1550;
constexpr uint32_t kObjDisableScaling = 0xF951AA64;

// Their parameters and results.
constexpr uint32_t kFilename = 0xC3F4169A;
constexpr uint32_t kViewport = 0x9FD29151;
constexpr uint32_t kR = 0x93F60062, kG = 0xFE2BE489, kB = 0x8E411006;
constexpr uint32_t kH = 0x6E94F918, kS = 0xE4F130F4, kV = 0x949BC47B;
constexpr uint32_t kIndex = 0x7F8C98FE;
constexpr uint32_t kDirection = 0xC1B52E4C;
constexpr uint32_t kHeading = 0xFD4BC03E;
constexpr uint32_t kPitch = 0xD8604126;
constexpr uint32_t kScaling = 0x8B7FC3F8;
constexpr uint32_t kMaterial = 0x83418A6A;
constexpr uint32_t kU = 0x0D9295C1;
constexpr uint32_t kPass = 0x318F2BDB;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kAll = 0xC4E78E22;
constexpr uint32_t kUScale = 0x3FB610B7, kVScale = 0xB9226219;
constexpr uint32_t kRot = 0xE2C6589E;
constexpr uint32_t kUOff = 0x56A7F41C, kVOff = 0x44125BF2;
constexpr uint32_t kCollisionArg = 0x7B66ECC5; // (passed on to the line test; its name isn't in THUG2's scripts)
constexpr uint32_t kLine = 0x2EEB4B09;
constexpr uint32_t kStart = 0x6086AA70, kEnd = 0xFF03CC4E;
constexpr uint32_t kCollisionResult = 0xED615AF5; // (the line test's result; its name isn't in THUG2's scripts)
constexpr uint32_t kBone = 0xCAB94088;
constexpr uint32_t kBoneVolume = 0x6D433FDD;
constexpr uint32_t kArray = 0x5EF31148;
constexpr uint32_t kSrc = 0x9FBBDB72, kDest = 0x7799D66C, kIn = 0xA01371B1;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kTexDictOffset = 0xF891AC27, kUseAssetManager = 0xC63C8D38;
constexpr uint32_t kStruct = 0x456D28D1, kBuildScript = 0x299EE352;
constexpr uint32_t kDuration = 0x79A07F3F;
constexpr uint32_t kSeconds = 0xD029F619, kSecond = 0x49E0EE96, kFrames = 0x019176C5, kFrame = 0x4A07C332;
constexpr uint32_t kStartAngle = 0x9DE2A72F, kEndAngle = 0x8C9946A9, kSinePower = 0xF164F03D;
constexpr uint32_t kRotationOffset = 0x8F38F109, kHoldOnLastAngle = 0xE14BC9AC, kDontPause = 0x4B309471;
constexpr uint32_t kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850;
constexpr uint32_t kHidden = 0x77A21642;

constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kFramesToMs = 16.666666f;          // 0x006460DC
constexpr float kSecondsToMs = 1000.0f;            // 0x006453C0
constexpr float kPercent = 0.01f;                  // 0x0064544C

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The model's lights (not rewritten yet): ambient colour (0x0048E450) and on (0x0048DE30), a diffuse light's colour
// (0x0048E480), on (0x0048E4C0) and direction (0x004E3F90).
void change_ambient_tint(void* lights, const uint8_t* rgba)
{
    uint32_t packed = 0;
    std::memcpy(&packed, rgba, sizeof(packed)); // (four bytes: R, G, B, A)
    static_cast<render::ModelLights*>(lights)->change_ambient_tint(&packed);
}
void enable_ambient(void* lights, bool on) { static_cast<render::ModelLights*>(lights)->switch_ambient_light(on); }
void change_diffuse_hue(void* lights, int index, const uint8_t* rgba)
{
    uint32_t packed = 0;
    std::memcpy(&packed, rgba, sizeof(packed)); // (four bytes: R, G, B, A)
    static_cast<render::ModelLights*>(lights)->change_diffuse_hue(index, &packed);
}
void enable_diffuse(void* lights, int index, bool on)
{
    static_cast<render::ModelLights*>(lights)->switch_diffuse_light(index, on);
}
void set_light_direction(void* lights, int index, const maths::Vec4* direction)
{
    model_nx::set_lights_direction(lights, index, direction);
}

} // namespace

// 0x00421AE0
int MeshInstanceModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    bool result = true;
    switch (name) {
    case kSwitchOnAtomic: {
        uint32_t geom = 0;
        params->read_hash_value(0u, &geom, 1);
        conceal_mesh(geom, false, true);
        break;
    }
    case kSwitchOffAtomic: {
        uint32_t geom = 0;
        params->read_hash_value(0u, &geom, 1);
        conceal_mesh(geom, true, true);
        break;
    }
    case kAtomicIsHidden: {
        uint32_t geom = 0;
        params->read_hash_value(0u, &geom, 1);
        const bool hidden = mesh_instance_->is_part_concealed(geom);
        script->args_->put_int(kHidden, hidden ? 1 : 0);
        break;
    }
    case kObjUpdateBrightness:
        update_lights_brightness(model_lights(mesh_instance_.get()));
        break;
    case kObjGetModelName:
        script->args_->put_name_key(kFilename, model_file_checksum(mesh_instance_.get()));
        break;
    case kObjUpdateModel:
        tick_frame();
        break;
    case kObjSetVisibility:
    case kObjClearVisibility: {
        // The viewports the model is drawn in (a bit each).
        int viewport = 0;
        params->fetch_int_value(kViewport, &viewport, false);
        const uint32_t bit = 1u << viewport;
        const uint32_t mask = mesh_instance_->fetch_shown();
        mesh_instance_->toggle_shown(name == kObjSetVisibility ? mask | bit : mask & ~bit);
        break;
    }
    case kObjEnableScaling:
        if (mesh_instance_.get())
            allow_resizing(mesh_instance_.get(), true);
        break;
    case kObjDisableScaling:
        if (mesh_instance_.get())
            allow_resizing(mesh_instance_.get(), false);
        break;
    case kObjApplyScaling:
        if (mesh_instance_.get()) {
            maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
            if (fetch_magnification(&scale, params)) {
                change_scale(mesh_instance_.get(), &scale);
                allow_resizing(mesh_instance_.get(), true);
            }
        }
        break;
    case kObjGetScaling:
        if (mesh_instance_.get()) {
            const maths::Vec4 scale = mesh_instance_->size_factor_; // (0x0041A680: its +0x1C)
            script->args_->put_vec3(kScaling, script::VectorValue{scale.x, scale.y, scale.z, scale.w});
        }
        break;
    case kObjSetLightAmbientColor: {
        int r = 0, g = 0, b = 0; // (unset when not given)
        params->fetch_int_value(kR, &r, 0);
        params->fetch_int_value(kG, &g, 0);
        params->fetch_int_value(kB, &b, 0);
        std::array<uint8_t, 4> rgba = {{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80}};
        change_ambient_tint(model_lights(mesh_instance_.get()), rgba.data());
        enable_ambient(model_lights(mesh_instance_.get()), true);
        break;
    }
    case kObjEnableAmbientLight:
        enable_ambient(model_lights(mesh_instance_.get()), true);
        break;
    case kObjDisableAmbientLight:
        enable_ambient(model_lights(mesh_instance_.get()), false);
        break;
    case kObjSetLightDiffuseColor: {
        int index = 0, r = 0, g = 0, b = 0; // (unset when not given)
        params->fetch_int_value(kIndex, &index, 0);
        params->fetch_int_value(kR, &r, 0);
        params->fetch_int_value(kG, &g, 0);
        params->fetch_int_value(kB, &b, 0);
        std::array<uint8_t, 4> rgba = {{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80}};
        change_diffuse_hue(model_lights(mesh_instance_.get()), index, rgba.data());
        enable_diffuse(model_lights(mesh_instance_.get()), index, true);
        break;
    }
    case kObjEnableDiffuseLight:
    case kObjDisableDiffuseLight: {
        int index = 0; // (unset when not given)
        params->fetch_int_value(kIndex, &index, 0);
        enable_diffuse(model_lights(mesh_instance_.get()), index, name == kObjEnableDiffuseLight);
        break;
    }
    case kObjSetLightDirection: {
        int index = 0; // (unset when not given)
        params->fetch_int_value(kIndex, &index, 0);
        maths::Vec4 direction = {0.0f, 0.0f, 1.0f, 0.0f};
        if (params->fetch_vec(kDirection, &direction, 0)) {
            direction.w = 0.0f;
        } else {
            float heading = 0.0f, pitch = 0.0f;
            if (params->fetch_float(kHeading, &heading, 0) && params->fetch_float(kPitch, &pitch, 0)) {
                direction.rotate_about_x(pitch * kRadiansPerDegree);
                direction.rotate_about_y(heading * kRadiansPerDegree);
            }
        }
        set_light_direction(model_lights(mesh_instance_.get()), index, &direction);
        break;
    }
    case kCancelRotateDisplay: {
        core::at(twist_rate_, 0).get_clear();
        core::at(twist_rate_, 1).get_clear();
        core::at(twist_rate_, 2).get_clear();
        void* manager = net_session_slot();
        void* player = find_player_slot_by_obj(manager, entity_->ident_);
        if (player && is_own_player(player)) {
            net::NetMessageInfo message;
            net::Guest* app = client_app(manager, player_skater_index(player));
            auto object = static_cast<uint8_t>(entity_->ident_);
            message.content_ = &object;
            message.byte_count_ = 1;
            message.ident_ = online::kMsgCancelSpins;
            app->send_to_host(&message);
        }
        break;
    }
    case kRotateDisplay: {
        float duration = 0.0f;
        params->fetch_float(kDuration, &duration, 0);
        if (params->has_bare_flag(kSeconds) || params->has_bare_flag(kSecond))
            duration *= kSecondsToMs;
        else if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame))
            duration *= kFramesToMs;
        float start = 0.0f;
        params->fetch_float(kStartAngle, &start, 0);
        float end = 0.0f;
        params->fetch_float(kEndAngle, &end, 0);
        int power = 0;
        params->fetch_int_value(kSinePower, &power, 0);
        pivot_ = {0.0f, 30.0f, 0.0f, 1.0f};
        params->fetch_vec(kRotationOffset, &pivot_, 0);
        const bool hold = params->has_bare_flag(kHoldOnLastAngle);
        const bool dont_pause = params->has_bare_flag(kDontPause);
        const uint32_t now = dont_pause ? frame_clock::get_time()
                                        : static_cast<uint32_t>(frame_clock::game_clock_elapsed() + g_clock_offset);
        uint8_t axes = 0;
        static const std::array<uint32_t, 3> kAxes = {{kX, kY, kZ}};
        for (int axis = 0; axis < 3; ++axis) {
            if (!params->has_bare_flag(core::at(kAxes, axis)))
                continue;
            core::at(twist_rate_, axis).append_one(duration, now, start, end - start, power, hold, dont_pause);
            axes |= static_cast<uint8_t>(1 << axis);
        }
        void* manager = net_session_slot();
        void* player = find_player_slot_by_obj(manager, entity_->ident_);
        if (player && is_own_player(player)) {
            net::NetMessageInfo message;
            net::Guest* app = client_app(manager, player_skater_index(player));
            online::SpinModelMessage sent;
            sent.period = get_truncated(duration);
            sent.start = static_cast<int16_t>(get_truncated(start));
            sent.range = static_cast<int16_t>(get_truncated(static_cast<double>(end) - start));
            sent.power = power;
            sent.object_id = static_cast<uint8_t>(entity_->ident_);
            sent.hold = hold;
            sent.axes = axes;
            std::array<uint8_t, online::kSpinModelSize> data{};
            util::ByteWriter writer(data.data(), sizeof(data));
            online::write_spin_model(writer, sent);
            message.content_ = data.data();
            message.byte_count_ = sizeof(data);
            message.ident_ = online::kMsgSpinModel;
            app->send_to_host(&message);
        }
        break;
    }
    case kEnableDisplayFlip:
        flip_ = true;
        break;
    case kDisableDisplayFlip:
        flip_ = false;
        break;
    case kObjSetBoundingSphere: {
        float radius = 0.0f;
        params->fetch_float(0u, &radius, 1);
        if (mesh_instance_.get()) {
            const maths::Vec4 sphere = {0.0f, 0.0f, 0.0f, radius};
            mesh_instance_->change_bound_sphere(&sphere);
        }
        break;
    }
    case kObjRestoreBoundingSphere:
        if (mesh_instance_.get())
            mesh_instance_->change_bound_sphere(&enclosing_ball_);
        break;
    case kClearGeom: {
        uint32_t geom = 0; // (unset when not given)
        if (!params->read_hash_value(kName, &geom, 0))
            params->read_hash_value(0u, &geom, 1);
        mesh_instance_->detach_geom(geom);
        break;
    }
    case kObjClearGeoms:
        if (mesh_instance_.get())
            mesh_instance_->drop_geoms();
        break;
    case kAddGeom: {
        uint32_t geom = 0; // (unset when not given)
        if (!params->read_hash_value(kName, &geom, 0))
            params->read_hash_value(0u, &geom, 1);
        const char* file = nullptr;
        params->fetch_text(kModel, &file, true);
        load_asset(mesh_instance_.get(), checksums::hash_from_text(file), geom, false);
        break;
    }
    case kObjSetUvOffset: {
        uint32_t material = 0; // (unset when not given)
        params->read_hash_value(kMaterial, &material, 0);
        float u = 0.0f, v = 0.0f; // (unset when not given)
        params->fetch_float(kU, &u, 0);
        params->fetch_float(kV, &v, 0);
        int pass = 0;
        params->fetch_int_value(kPass, &pass, 0);
        mesh_instance_->choose_uv_shift(material, pass, u, v);
        break;
    }
    case kObjSetUvParams: {
        uint32_t material = 0; // (unset when not given)
        params->read_hash_value(kMaterial, &material, 0);
        int pass = 0; // (unset when not given)
        params->fetch_int_value(kPass, &pass, 0);
        maths::Mat4 uv;
        uv.reset_to_identity();
        float u_scale = 1.0f, v_scale = 1.0f;
        const bool scaled = params->fetch_float(kUScale, &u_scale, 0);
        if (params->fetch_float(kVScale, &v_scale, 0) || scaled) {
            core::at(uv.row, 0).x *= u_scale;
            core::at(uv.row, 1).y *= v_scale;
        }
        float rot = 0.0f;
        if (params->fetch_float(kRot, &rot, 0))
            uv.spin_around_z(rot * kRadiansPerDegree);
        float u_off = 0.0f, v_off = 0.0f;
        const bool offset = params->fetch_float(kUOff, &u_off, 0);
        if (params->fetch_float(kVOff, &v_off, 0) || offset) {
            core::at(uv.row, 3).x = u_off;
            core::at(uv.row, 3).y = v_off;
        }
        mesh_instance_->choose_uv_matrix(material, pass, &uv);
        break;
    }
    case kObjClearColor:
        if (mesh_instance_.get())
            mesh_instance_->clear_geom_color(kAll);
        break;
    case kObjSetColor:
        if (mesh_instance_.get()) {
            int h = 0, s = 0, v = 0;
            if (params->fetch_int_value(kH, &h, false) && params->fetch_int_value(kS, &s, false) &&
                params->fetch_int_value(kV, &v, false)) {
                mesh_instance_->set_geom_color_hsv(kAll, static_cast<float>(h),
                                                   static_cast<float>(static_cast<double>(s) * kPercent),
                                                   static_cast<float>(static_cast<double>(v) * kPercent));
            }
        }
        break;
    case kObjSetColorRgb:
        if (mesh_instance_.get()) {
            int r = 0, g = 0, b = 0;
            if (params->fetch_int_value(kR, &r, false) && params->fetch_int_value(kG, &g, false) &&
                params->fetch_int_value(kB, &b, false))
                mesh_instance_->change_tint(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                                            static_cast<uint8_t>(0xFF));
        }
        break;
    case kObjHasModelLights:
        return model_lights(mesh_instance_.get()) ? 1 : 0;
    case kObjInitModel:
        set_up_mesh(params);
        break;
    case kObjInitModelFromProfile:
        if (mesh_instance_.get()) {
            int tex_dict_offset = 0;
            params->fetch_int_value(kTexDictOffset, &tex_dict_offset, 0);
            int via_resource_store = 0;
            params->fetch_int_value(kUseAssetManager, &via_resource_store, 0);
            script::Struct* profile = nullptr;
            params->read_sub_struct(kStruct, &profile, 1);
            uint32_t build_script = 0;
            params->read_hash_value(kBuildScript, &build_script, 0);
            gfx::ModelLook appearance; // (destroyed at the end of the block)
            appearance.read_in(profile, true);
            set_up_mesh_from_profile(&appearance, via_resource_store != 0, tex_dict_offset, build_script);
        }
        break;
    case kObjReplaceTexture: {
        result = false;
        script::Array* array = nullptr;
        if (params->fetch_list(kArray, &array, 0)) {
            // Each of the array's structures.
            for (uint32_t i = 0; i < array->extent_; ++i) {
                auto* entry = array->struct_at(i);
                const char* src = nullptr;
                const char* dest = nullptr;
                entry->fetch_text(kSrc, &src, true);
                entry->fetch_text(kDest, &dest, true);
                uint32_t in = kAll;
                entry->read_hash_value(kIn, &in, 0);
                if (mesh_instance_.get() && mesh_instance_->swap_texture(in, src, dest))
                    result = true;
            }
        } else {
            const char* src = nullptr;
            const char* dest = nullptr;
            params->fetch_text(kSrc, &src, true);
            params->fetch_text(kDest, &dest, true);
            uint32_t in = kAll;
            params->read_hash_value(kIn, &in, 0);
            if (mesh_instance_.get() && mesh_instance_->swap_texture(in, src, dest))
                result = true;
        }
        break;
    }
    case kObjReplaceSpriteTexture: {
        // (dest: a checksum here, passed on as the name)
        const char* src = nullptr;
        params->fetch_text(kSrc, &src, true);
        uint32_t dest = 0;
        params->read_hash_value(kDest, &dest, 1);
        uint32_t in = kAll;
        params->read_hash_value(kIn, &in, 0);
        result = mesh_instance_.get() && mesh_instance_->replace_texture_with(in, src, dest);
        break;
    }
    case kObjTestModelCollision:
    case kObjGetBoneVolume: {
        if (name == kObjTestModelCollision) {
            uint32_t arg = 0;
            params->read_hash_value(kCollisionArg, &arg, false);
            script::Struct* line_params = nullptr;
            if (params->read_sub_struct(kLine, &line_params, false)) {
                maths::Seg line{maths::Vec4{}, maths::Vec4{}};
                line_params->fetch_vec(kStart, &line.start, 1);
                line_params->fetch_vec(kEnd, &line.end, 1);
                const int hit = test_collision(&line, render::BoneLineTest{.bone = arg});
                if (hit)
                    script->args_->put_name_key(kCollisionResult, static_cast<uint32_t>(hit));
            }
        }
        // (the line test goes on into the bone's volume)
        uint32_t bone = 0;
        params->read_hash_value(kBone, &bone, 1);
        gfx::Rig* skeleton = skeleton_of(bone_rig_);
        maths::Mat4 matrix;
        get_draw_matrix(&matrix);
        auto volume = static_cast<float>(mesh_instance_->get_bone_volume(skeleton, &matrix, bone));
        script->args_->push_float(kBoneVolume, volume);
        break;
    }
    default:
        return 2;
    }
    return result ? 1 : 0;
}

// (no address: the tail jump folded at 0x004E3F90 - model_component_nx.h)
void model_nx::set_lights_direction(void* lights, int index, const maths::Vec4* direction)
{
    static_cast<render::ModelLights*>(lights)->put_light_aim(index, direction); // (the original's slot 2)
}

} // namespace objects
