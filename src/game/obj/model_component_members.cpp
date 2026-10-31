// The model component's script members (Obj::CModelComponent::CallMemberFunction, 0x00421AE0). From its disassembly
// (0x00421AE0-0x00422CF4: a binary search on the name; each member's code below in the order of their names).
// Parameters the original reads without giving them a default are left as they were (unset) - noted where so.

#include <cstdint>

#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/mth/matrix.h"
#include "game/obj/anim_blender.h"
#include "game/obj/model_component.h"
#include "game/obj/model_component_nx.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/core/crc.h"
#include "game/obj/control_pad.h"
#include "game/gfx/engine_geom.h"

namespace Obj {

using namespace ModelNx;

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
constexpr uint32_t kObjSetUVOffset = 0x57EB306E;
constexpr uint32_t kObjRestoreBoundingSphere = 0x5B41DA8F;
constexpr uint32_t kClearGeom = 0x5BE3432A;
constexpr uint32_t kObjClearColor = 0x6052480A;
constexpr uint32_t kObjApplyScaling = 0x657FF1ED;
constexpr uint32_t kObjSetLightDiffuseColor = 0x70E6466B;
constexpr uint32_t kEnableDisplayFlip = 0x721184C7;
constexpr uint32_t kObjSetColor = 0x76ECFA1C;
constexpr uint32_t kObjTestModelCollision = 0x7ADD91D2;
constexpr uint32_t kObjSetUVParams = 0x812FF44D;
constexpr uint32_t kObjReplaceTexture = 0x83F9BE15;
constexpr uint32_t kAddGeom = 0x890E57ED;
constexpr uint32_t kObjDisableAmbientLight = 0x8A445E26;
constexpr uint32_t kObjSetColorRGB = 0x8CA6BC2C;
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
constexpr uint32_t kCollisionArg = 0x7B66ECC5;    // (passed on to the line test; its name isn't in THUG2's scripts)
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

constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kFramesToMs = 16.666666f;          // 0x006460DC
constexpr float kSecondsToMs = 1000.0f;            // 0x006453C0
constexpr float kPercent = 0.01f;                  // 0x0064544C
constexpr uint8_t kMsgRotateDisplay = 0x76;
constexpr uint8_t kMsgCancelRotateDisplay = 0x8C;

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The model's lights (not rewritten yet): ambient colour (0x0048E450) and on (0x0048DE30), a diffuse light's colour
// (0x0048E480), on (0x0048E4C0) and direction (0x004E3F90).
void SetAmbientColor(void* lights, const uint8_t* rgba) { static_cast<Nx::CModelLights*>(lights)->SetAmbientColor(reinterpret_cast<const uint32_t*>(rgba)); }
void EnableAmbient(void* lights, bool on) { reinterpret_cast<void(__thiscall*)(void*, bool)>(&Nx::VirtualSlot9Jump)(lights, on); }
void SetDiffuseColor(void* lights, int index, const uint8_t* rgba) { static_cast<Nx::CModelLights*>(lights)->SetDiffuseColor(index, reinterpret_cast<const uint32_t*>(rgba)); }
void EnableDiffuse(void* lights, int index, bool on) { reinterpret_cast<void(__thiscall*)(void*, int, bool)>(&Nx::VirtualSlot10Jump)(lights, index, on); }
void SetLightDirection(void* lights, int index, const Mth::Vector* direction)
{
    ModelNx::SetLightsDirection(lights, nullptr, index, direction);
}

// The model appearance a profile is read into (0x00488EA0 constructor, 0x00488F40 loaded from a structure, 0x004890F0
// destructor).
struct Appearance {
    alignas(16) uint8_t bytes[0xB0];
};

} // namespace

// 0x00421AE0
int CModelComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    bool result = true;
    switch (name) {
    case kSwitchOnAtomic: {
        uint32_t geom;
        params->GetChecksum(0u, &geom, 1);
        HideGeom(geom, false, true);
        break;
    }
    case kSwitchOffAtomic: {
        uint32_t geom;
        params->GetChecksum(0u, &geom, 1);
        HideGeom(geom, true, true);
        break;
    }
    case kAtomicIsHidden: {
        uint32_t geom;
        params->GetChecksum(0u, &geom, 1);
        bool hidden = mp_model->IsGeomHidden(geom);
        script->m_params->AddInteger(kHidden, hidden ? 1 : 0);
        break;
    }
    case kObjUpdateBrightness:
        UpdateLightsBrightness(ModelLights(mp_model));
        break;
    case kObjGetModelName:
        script->m_params->AddChecksum(kFilename, ModelFileChecksum(mp_model));
        break;
    case kObjUpdateModel:
        CBaseComponent::Update(); // (through the vtable)
        break;
    case kObjSetVisibility:
    case kObjClearVisibility: {
        // The viewports the model is drawn in (a bit each).
        int viewport = 0;
        params->GetInteger(kViewport, &viewport, false);
        uint32_t bit = 1u << viewport;
        uint32_t mask = mp_model->GetVisibility();
        mp_model->SetVisibility(name == kObjSetVisibility ? mask | bit : mask & ~bit);
        break;
    }
    case kObjEnableScaling:
        if (mp_model)
            EnableScaling(mp_model, true);
        break;
    case kObjDisableScaling:
        if (mp_model)
            EnableScaling(mp_model, false);
        break;
    case kObjApplyScaling:
        if (mp_model) {
            Mth::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
            if (GetScale(&scale, params)) {
                SetScale(mp_model, &scale);
                EnableScaling(mp_model, true);
            }
        }
        break;
    case kObjGetScaling:
        if (mp_model) {
            Mth::Vector scale = reinterpret_cast<VectorAt1C*>(mp_model)->Get();
            script->m_params->AddVector(kScaling, Script::VectorValue{scale.x, scale.y, scale.z, scale.w});
        }
        break;
    case kObjSetLightAmbientColor: {
        int r, g, b; // (unset when not given)
        params->GetInteger(kR, &r, 0);
        params->GetInteger(kG, &g, 0);
        params->GetInteger(kB, &b, 0);
        uint8_t rgba[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
        SetAmbientColor(ModelLights(mp_model), rgba);
        EnableAmbient(ModelLights(mp_model), true);
        break;
    }
    case kObjEnableAmbientLight:
        EnableAmbient(ModelLights(mp_model), true);
        break;
    case kObjDisableAmbientLight:
        EnableAmbient(ModelLights(mp_model), false);
        break;
    case kObjSetLightDiffuseColor: {
        int index, r, g, b; // (unset when not given)
        params->GetInteger(kIndex, &index, 0);
        params->GetInteger(kR, &r, 0);
        params->GetInteger(kG, &g, 0);
        params->GetInteger(kB, &b, 0);
        uint8_t rgba[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
        SetDiffuseColor(ModelLights(mp_model), index, rgba);
        EnableDiffuse(ModelLights(mp_model), index, true);
        break;
    }
    case kObjEnableDiffuseLight:
    case kObjDisableDiffuseLight: {
        int index; // (unset when not given)
        params->GetInteger(kIndex, &index, 0);
        EnableDiffuse(ModelLights(mp_model), index, name == kObjEnableDiffuseLight);
        break;
    }
    case kObjSetLightDirection: {
        int index; // (unset when not given)
        params->GetInteger(kIndex, &index, 0);
        Mth::Vector direction = {0.0f, 0.0f, 1.0f, 0.0f};
        if (params->GetVector(kDirection, reinterpret_cast<Script::VectorValue*>(&direction), 0)) {
            direction.w = 0.0f;
        } else {
            float heading, pitch;
            if (params->GetFloat(kHeading, &heading, 0) && params->GetFloat(kPitch, &pitch, 0)) {
                direction.RotateAboutX(pitch * kDegreesToRadians);
                direction.RotateAboutY(heading * kDegreesToRadians);
            }
        }
        SetLightDirection(ModelLights(mp_model), index, &direction);
        break;
    }
    case kCancelRotateDisplay: {
        m_spin[0].Clear();
        m_spin[1].Clear();
        m_spin[2].Clear();
        void* manager = NetManager();
        void* player = PlayerByObjectId(manager, m_object->m_id);
        if (player && IsLocalPlayer(player)) {
            Net::MsgDesc message;
            message.Construct();
            Net::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
            uint8_t object = static_cast<uint8_t>(m_object->m_id);
            message.m_data = &object;
            message.m_length = 1;
            message.m_id = kMsgCancelRotateDisplay;
            app->EnqueueMessageToServer(&message);
        }
        break;
    }
    case kRotateDisplay: {
        float duration = 0.0f;
        params->GetFloat(kDuration, &duration, 0);
        if (params->ContainsFlag(kSeconds) || params->ContainsFlag(kSecond))
            duration *= kSecondsToMs;
        else if (params->ContainsFlag(kFrames) || params->ContainsFlag(kFrame))
            duration *= kFramesToMs;
        float start = 0.0f;
        params->GetFloat(kStartAngle, &start, 0);
        float end = 0.0f;
        params->GetFloat(kEndAngle, &end, 0);
        int power = 0;
        params->GetInteger(kSinePower, &power, 0);
        m_pivot = {0.0f, 30.0f, 0.0f, 1.0f};
        params->GetVector(kRotationOffset, reinterpret_cast<Script::VectorValue*>(&m_pivot), 0);
        bool hold = params->ContainsFlag(kHoldOnLastAngle);
        bool dontPause = params->ContainsFlag(kDontPause);
        uint32_t now = dontPause ? Tmr::GetTime()
                                 : static_cast<uint32_t>(Tmr::GameClockElapsed() + g_clockOffset);
        uint8_t axes = 0;
        static const uint32_t kAxes[3] = {kX, kY, kZ};
        for (int axis = 0; axis < 3; ++axis) {
            if (!params->ContainsFlag(kAxes[axis]))
                continue;
            m_spin[axis].Add(duration, now, start, end - start, power, hold, dontPause);
            axes |= static_cast<uint8_t>(1 << axis);
        }
        void* manager = NetManager();
        void* player = PlayerByObjectId(manager, m_object->m_id);
        if (player && IsLocalPlayer(player)) {
            Net::MsgDesc message;
            message.Construct();
            Net::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
            struct {
                int period;
                int power;
                int16_t start;
                int16_t range;
                uint8_t object;
                uint8_t hold;
                uint8_t axes;
                uint8_t pad;
            } data;
            data.period = Truncated(duration);
            data.start = static_cast<int16_t>(Truncated(start));
            data.range = static_cast<int16_t>(Truncated(static_cast<double>(end) - start));
            data.power = power;
            data.object = static_cast<uint8_t>(m_object->m_id);
            data.hold = hold;
            data.axes = axes;
            message.m_data = &data;
            message.m_length = 0x10;
            message.m_id = kMsgRotateDisplay;
            app->EnqueueMessageToServer(&message);
        }
        break;
    }
    case kEnableDisplayFlip:
        m_flip = true;
        break;
    case kDisableDisplayFlip:
        m_flip = false;
        break;
    case kObjSetBoundingSphere: {
        float radius;
        params->GetFloat(0u, &radius, 1);
        if (mp_model) {
            Mth::Vector sphere = {0.0f, 0.0f, 0.0f, radius};
            mp_model->SetBoundingSphere(&sphere);
        }
        break;
    }
    case kObjRestoreBoundingSphere:
        if (mp_model)
            mp_model->SetBoundingSphere(&m_boundingSphere);
        break;
    case kClearGeom: {
        uint32_t geom; // (unset when not given)
        if (!params->GetChecksum(kName, &geom, 0))
            params->GetChecksum(0u, &geom, 1);
        mp_model->RemoveGeom(geom);
        break;
    }
    case kObjClearGeoms:
        if (mp_model)
            mp_model->ClearGeoms();
        break;
    case kAddGeom: {
        uint32_t geom; // (unset when not given)
        if (!params->GetChecksum(kName, &geom, 0))
            params->GetChecksum(0u, &geom, 1);
        const char* file;
        params->GetString(kModel, &file, true);
        LoadAsset(mp_model, Crc::GenerateCRCFromString(file), geom, false);
        break;
    }
    case kObjSetUVOffset: {
        uint32_t material; // (unset when not given)
        params->GetChecksum(kMaterial, &material, 0);
        float u, v; // (unset when not given)
        params->GetFloat(kU, &u, 0);
        params->GetFloat(kV, &v, 0);
        int pass = 0;
        params->GetInteger(kPass, &pass, 0);
        mp_model->SetUVOffset(material, pass, u, v);
        break;
    }
    case kObjSetUVParams: {
        uint32_t material; // (unset when not given)
        params->GetChecksum(kMaterial, &material, 0);
        int pass; // (unset when not given)
        params->GetInteger(kPass, &pass, 0);
        Mth::Matrix uv;
        uv.Ident();
        float uScale = 1.0f, vScale = 1.0f;
        bool scaled = params->GetFloat(kUScale, &uScale, 0);
        if (params->GetFloat(kVScale, &vScale, 0) || scaled) {
            uv.row[0].x *= uScale;
            uv.row[1].y *= vScale;
        }
        float rot;
        if (params->GetFloat(kRot, &rot, 0))
            uv.RotateZ(rot * kDegreesToRadians);
        float uOff = 0.0f, vOff = 0.0f;
        bool offset = params->GetFloat(kUOff, &uOff, 0);
        if (params->GetFloat(kVOff, &vOff, 0) || offset) {
            uv.row[3].x = uOff;
            uv.row[3].y = vOff;
        }
        mp_model->SetUVMatrix(material, pass, &uv);
        break;
    }
    case kObjClearColor:
        if (mp_model)
            mp_model->ClearGeomColor(kAll);
        break;
    case kObjSetColor:
        if (mp_model) {
            int h, s, v;
            if (params->GetInteger(kH, &h, false) && params->GetInteger(kS, &s, false) &&
                params->GetInteger(kV, &v, false)) {
                mp_model->SetGeomColorHsv(kAll, static_cast<float>(h), static_cast<float>(static_cast<double>(s) * kPercent), static_cast<float>(static_cast<double>(v) * kPercent));
            }
        }
        break;
    case kObjSetColorRGB:
        if (mp_model) {
            int r, g, b;
            if (params->GetInteger(kR, &r, false) && params->GetInteger(kG, &g, false) &&
                params->GetInteger(kB, &b, false))
                mp_model->SetColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(0xFF));
        }
        break;
    case kObjHasModelLights:
        return ModelLights(mp_model) ? 1 : 0;
    case kObjInitModel:
        InitModel(params);
        break;
    case kObjInitModelFromProfile:
        if (mp_model) {
            int texDictOffset = 0;
            params->GetInteger(kTexDictOffset, &texDictOffset, 0);
            int useAssetManager = 0;
            params->GetInteger(kUseAssetManager, &useAssetManager, 0);
            Script::CStruct* profile;
            params->GetStructure(kStruct, &profile, 1);
            uint32_t buildScript = 0;
            params->GetChecksum(kBuildScript, &buildScript, 0);
            Appearance appearance;
            reinterpret_cast<Gfx::CModelAppearance*>(&appearance)->Construct();
            reinterpret_cast<Gfx::CModelAppearance*>(&appearance)->Load(profile, 1 != 0);
            reinterpret_cast<void(__thiscall*)(CModelComponent*, void*, bool, int, uint32_t)>(m_vtable[13])(
                this, &appearance, useAssetManager != 0, texDictOffset, buildScript);
            reinterpret_cast<Gfx::CModelAppearance*>(&appearance)->DestroyBody();
        }
        break;
    case kObjReplaceTexture: {
        result = false;
        Script::CArray* array;
        if (params->GetArray(kArray, &array, 0)) {
            // Each of the array's structures.
            for (uint32_t i = 0; i < array->m_size; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(array->GetElement(i));
                const char* src;
                const char* dest;
                entry->GetString(kSrc, &src, true);
                entry->GetString(kDest, &dest, true);
                uint32_t in = kAll;
                entry->GetChecksum(kIn, &in, 0);
                if (mp_model && mp_model->ReplaceTexture(in, src, dest))
                    result = true;
            }
        } else {
            const char* src;
            const char* dest;
            params->GetString(kSrc, &src, true);
            params->GetString(kDest, &dest, true);
            uint32_t in = kAll;
            params->GetChecksum(kIn, &in, 0);
            if (mp_model && mp_model->ReplaceTexture(in, src, dest))
                result = true;
        }
        break;
    }
    case kObjReplaceSpriteTexture: {
        // (dest: a checksum here, passed on as the name)
        const char* src;
        params->GetString(kSrc, &src, true);
        uint32_t dest = 0;
        params->GetChecksum(kDest, &dest, 1);
        uint32_t in = kAll;
        params->GetChecksum(kIn, &in, 0);
        result = mp_model && mp_model->ReplaceTextureWith(in, src, dest);
        break;
    }
    case kObjTestModelCollision:
    case kObjGetBoneVolume: {
        if (name == kObjTestModelCollision) {
            uint32_t arg = 0;
            params->GetChecksum(kCollisionArg, &arg, false);
            Script::CStruct* lineParams = nullptr;
            if (params->GetStructure(kLine, &lineParams, false)) {
                Mth::Vector line[2];
                lineParams->GetVector(kStart, reinterpret_cast<Script::VectorValue*>(&line[0]), 1);
                lineParams->GetVector(kEnd, reinterpret_cast<Script::VectorValue*>(&line[1]), 1);
                int hit = TestCollision(line, arg, 0, 0, 0);
                if (hit)
                    script->m_params->AddChecksum(kCollisionResult, static_cast<uint32_t>(hit));
            }
        }
        // (the line test goes on into the bone's volume)
        uint32_t bone;
        params->GetChecksum(kBone, &bone, 1);
        void* skeleton = SkeletonOf(mp_skeleton);
        Mth::Matrix matrix;
        GetDisplayMatrix(&matrix);
        float volume = static_cast<float>(
            mp_model->GetBoneVolume(reinterpret_cast<uint32_t>(skeleton), reinterpret_cast<uint32_t>(&matrix), bone));
        script->m_params->AddFloat(kBoneVolume, volume);
        break;
    }
    default:
        return 2;
    }
    return result ? 1 : 0;
}

// (no address: the tail jump folded at 0x004E3F90 - model_component_nx.h)
void __fastcall ModelNx::SetLightsDirection(void* lights, void*, int index, const Mth::Vector* direction)
{
    reinterpret_cast<void(__thiscall*)(void*, int, const Mth::Vector*)>((*static_cast<void***>(lights))[2])(lights, index,
                                                                                                      direction);
}

} // namespace Obj
