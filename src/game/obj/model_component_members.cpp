// The model component's script members (GameObjects::CMeshInstanceModule::InvokeMethodByName, 0x00421AE0). From its disassembly
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

namespace GameObjects {

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
void SetAmbientColor(void* lights, const uint8_t* rgba) { static_cast<Engine3D::CModelLights*>(lights)->SetAmbientColor(reinterpret_cast<const uint32_t*>(rgba)); }
void EnableAmbient(void* lights, bool on) { reinterpret_cast<void(__thiscall*)(void*, bool)>(&Engine3D::VirtualSlot9Jump)(lights, on); }
void SetDiffuseColor(void* lights, int index, const uint8_t* rgba) { static_cast<Engine3D::CModelLights*>(lights)->SetDiffuseColor(index, reinterpret_cast<const uint32_t*>(rgba)); }
void EnableDiffuse(void* lights, int index, bool on) { reinterpret_cast<void(__thiscall*)(void*, int, bool)>(&Engine3D::VirtualSlot10Jump)(lights, index, on); }
void SetLightDirection(void* lights, int index, const Maths::Vector* direction)
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
int CMeshInstanceModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    bool result = true;
    switch (name) {
    case kSwitchOnAtomic: {
        uint32_t geom;
        params->ReadHashValue(0u, &geom, 1);
        ConcealMesh(geom, false, true);
        break;
    }
    case kSwitchOffAtomic: {
        uint32_t geom;
        params->ReadHashValue(0u, &geom, 1);
        ConcealMesh(geom, true, true);
        break;
    }
    case kAtomicIsHidden: {
        uint32_t geom;
        params->ReadHashValue(0u, &geom, 1);
        bool hidden = m_meshInstance->IsGeomHidden(geom);
        script->m_params->PutInt(kHidden, hidden ? 1 : 0);
        break;
    }
    case kObjUpdateBrightness:
        UpdateLightsBrightness(ModelLights(m_meshInstance));
        break;
    case kObjGetModelName:
        script->m_params->PutNameKey(kFilename, ModelFileChecksum(m_meshInstance));
        break;
    case kObjUpdateModel:
        CObjectModule::Update(); // (through the vtable)
        break;
    case kObjSetVisibility:
    case kObjClearVisibility: {
        // The viewports the model is drawn in (a bit each).
        int viewport = 0;
        params->FetchIntValue(kViewport, &viewport, false);
        uint32_t bit = 1u << viewport;
        uint32_t mask = m_meshInstance->GetVisibility();
        m_meshInstance->ToggleShown(name == kObjSetVisibility ? mask | bit : mask & ~bit);
        break;
    }
    case kObjEnableScaling:
        if (m_meshInstance)
            AllowResizing(m_meshInstance, true);
        break;
    case kObjDisableScaling:
        if (m_meshInstance)
            AllowResizing(m_meshInstance, false);
        break;
    case kObjApplyScaling:
        if (m_meshInstance) {
            Maths::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
            if (GetScale(&scale, params)) {
                ChangeScale(m_meshInstance, &scale);
                AllowResizing(m_meshInstance, true);
            }
        }
        break;
    case kObjGetScaling:
        if (m_meshInstance) {
            Maths::Vector scale = reinterpret_cast<VectorAt1C*>(m_meshInstance)->Get();
            script->m_params->PutVec3(kScaling, Scripts::VectorValue{scale.x, scale.y, scale.z, scale.w});
        }
        break;
    case kObjSetLightAmbientColor: {
        int r, g, b; // (unset when not given)
        params->FetchIntValue(kR, &r, 0);
        params->FetchIntValue(kG, &g, 0);
        params->FetchIntValue(kB, &b, 0);
        uint8_t rgba[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
        SetAmbientColor(ModelLights(m_meshInstance), rgba);
        EnableAmbient(ModelLights(m_meshInstance), true);
        break;
    }
    case kObjEnableAmbientLight:
        EnableAmbient(ModelLights(m_meshInstance), true);
        break;
    case kObjDisableAmbientLight:
        EnableAmbient(ModelLights(m_meshInstance), false);
        break;
    case kObjSetLightDiffuseColor: {
        int index, r, g, b; // (unset when not given)
        params->FetchIntValue(kIndex, &index, 0);
        params->FetchIntValue(kR, &r, 0);
        params->FetchIntValue(kG, &g, 0);
        params->FetchIntValue(kB, &b, 0);
        uint8_t rgba[4] = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 0x80};
        SetDiffuseColor(ModelLights(m_meshInstance), index, rgba);
        EnableDiffuse(ModelLights(m_meshInstance), index, true);
        break;
    }
    case kObjEnableDiffuseLight:
    case kObjDisableDiffuseLight: {
        int index; // (unset when not given)
        params->FetchIntValue(kIndex, &index, 0);
        EnableDiffuse(ModelLights(m_meshInstance), index, name == kObjEnableDiffuseLight);
        break;
    }
    case kObjSetLightDirection: {
        int index; // (unset when not given)
        params->FetchIntValue(kIndex, &index, 0);
        Maths::Vector direction = {0.0f, 0.0f, 1.0f, 0.0f};
        if (params->GetVector(kDirection, reinterpret_cast<Scripts::VectorValue*>(&direction), 0)) {
            direction.w = 0.0f;
        } else {
            float heading, pitch;
            if (params->GetFloat(kHeading, &heading, 0) && params->GetFloat(kPitch, &pitch, 0)) {
                direction.RotateAboutX(pitch * kDegreesToRadians);
                direction.RotateAboutY(heading * kDegreesToRadians);
            }
        }
        SetLightDirection(ModelLights(m_meshInstance), index, &direction);
        break;
    }
    case kCancelRotateDisplay: {
        m_twistRate[0].Clear();
        m_twistRate[1].Clear();
        m_twistRate[2].Clear();
        void* manager = NetManager();
        void* player = PlayerByObjectId(manager, m_object->m_id);
        if (player && IsOwnPlayer(player)) {
            NetLink::NetMessageInfo message;
            message.Construct();
            NetLink::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
            uint8_t object = static_cast<uint8_t>(m_object->m_id);
            message.m_data = &object;
            message.m_byteCount = 1;
            message.m_id = kMsgCancelRotateDisplay;
            app->SendToHost(&message);
        }
        break;
    }
    case kRotateDisplay: {
        float duration = 0.0f;
        params->GetFloat(kDuration, &duration, 0);
        if (params->HasBareFlag(kSeconds) || params->HasBareFlag(kSecond))
            duration *= kSecondsToMs;
        else if (params->HasBareFlag(kFrames) || params->HasBareFlag(kFrame))
            duration *= kFramesToMs;
        float start = 0.0f;
        params->GetFloat(kStartAngle, &start, 0);
        float end = 0.0f;
        params->GetFloat(kEndAngle, &end, 0);
        int power = 0;
        params->FetchIntValue(kSinePower, &power, 0);
        m_pivot = {0.0f, 30.0f, 0.0f, 1.0f};
        params->GetVector(kRotationOffset, reinterpret_cast<Scripts::VectorValue*>(&m_pivot), 0);
        bool hold = params->HasBareFlag(kHoldOnLastAngle);
        bool dontPause = params->HasBareFlag(kDontPause);
        uint32_t now = dontPause ? FrameClock::GetTime()
                                 : static_cast<uint32_t>(FrameClock::GameClockElapsed() + g_clockOffset);
        uint8_t axes = 0;
        static const uint32_t kAxes[3] = {kX, kY, kZ};
        for (int axis = 0; axis < 3; ++axis) {
            if (!params->HasBareFlag(kAxes[axis]))
                continue;
            m_twistRate[axis].Add(duration, now, start, end - start, power, hold, dontPause);
            axes |= static_cast<uint8_t>(1 << axis);
        }
        void* manager = NetManager();
        void* player = PlayerByObjectId(manager, m_object->m_id);
        if (player && IsOwnPlayer(player)) {
            NetLink::NetMessageInfo message;
            message.Construct();
            NetLink::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
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
            message.m_byteCount = 0x10;
            message.m_id = kMsgRotateDisplay;
            app->SendToHost(&message);
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
        if (m_meshInstance) {
            Maths::Vector sphere = {0.0f, 0.0f, 0.0f, radius};
            m_meshInstance->ChangeBoundSphere(&sphere);
        }
        break;
    }
    case kObjRestoreBoundingSphere:
        if (m_meshInstance)
            m_meshInstance->ChangeBoundSphere(&m_enclosingBall);
        break;
    case kClearGeom: {
        uint32_t geom; // (unset when not given)
        if (!params->ReadHashValue(kName, &geom, 0))
            params->ReadHashValue(0u, &geom, 1);
        m_meshInstance->DetachGeom(geom);
        break;
    }
    case kObjClearGeoms:
        if (m_meshInstance)
            m_meshInstance->DropGeoms();
        break;
    case kAddGeom: {
        uint32_t geom; // (unset when not given)
        if (!params->ReadHashValue(kName, &geom, 0))
            params->ReadHashValue(0u, &geom, 1);
        const char* file;
        params->GetString(kModel, &file, true);
        LoadAsset(m_meshInstance, Checksums::HashFromText(file), geom, false);
        break;
    }
    case kObjSetUVOffset: {
        uint32_t material; // (unset when not given)
        params->ReadHashValue(kMaterial, &material, 0);
        float u, v; // (unset when not given)
        params->GetFloat(kU, &u, 0);
        params->GetFloat(kV, &v, 0);
        int pass = 0;
        params->FetchIntValue(kPass, &pass, 0);
        m_meshInstance->ChooseUvShift(material, pass, u, v);
        break;
    }
    case kObjSetUVParams: {
        uint32_t material; // (unset when not given)
        params->ReadHashValue(kMaterial, &material, 0);
        int pass; // (unset when not given)
        params->FetchIntValue(kPass, &pass, 0);
        Maths::Matrix uv;
        uv.ResetToIdentity();
        float uScale = 1.0f, vScale = 1.0f;
        bool scaled = params->GetFloat(kUScale, &uScale, 0);
        if (params->GetFloat(kVScale, &vScale, 0) || scaled) {
            uv.row[0].x *= uScale;
            uv.row[1].y *= vScale;
        }
        float rot;
        if (params->GetFloat(kRot, &rot, 0))
            uv.SpinAroundZ(rot * kDegreesToRadians);
        float uOff = 0.0f, vOff = 0.0f;
        bool offset = params->GetFloat(kUOff, &uOff, 0);
        if (params->GetFloat(kVOff, &vOff, 0) || offset) {
            uv.row[3].x = uOff;
            uv.row[3].y = vOff;
        }
        m_meshInstance->ChooseUvMatrix(material, pass, &uv);
        break;
    }
    case kObjClearColor:
        if (m_meshInstance)
            m_meshInstance->ClearGeomColor(kAll);
        break;
    case kObjSetColor:
        if (m_meshInstance) {
            int h, s, v;
            if (params->FetchIntValue(kH, &h, false) && params->FetchIntValue(kS, &s, false) &&
                params->FetchIntValue(kV, &v, false)) {
                m_meshInstance->SetGeomColorHsv(kAll, static_cast<float>(h), static_cast<float>(static_cast<double>(s) * kPercent), static_cast<float>(static_cast<double>(v) * kPercent));
            }
        }
        break;
    case kObjSetColorRGB:
        if (m_meshInstance) {
            int r, g, b;
            if (params->FetchIntValue(kR, &r, false) && params->FetchIntValue(kG, &g, false) &&
                params->FetchIntValue(kB, &b, false))
                m_meshInstance->SetColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(0xFF));
        }
        break;
    case kObjHasModelLights:
        return ModelLights(m_meshInstance) ? 1 : 0;
    case kObjInitModel:
        SetUpMesh(params);
        break;
    case kObjInitModelFromProfile:
        if (m_meshInstance) {
            int texDictOffset = 0;
            params->FetchIntValue(kTexDictOffset, &texDictOffset, 0);
            int viaResourceStore = 0;
            params->FetchIntValue(kUseAssetManager, &viaResourceStore, 0);
            Scripts::CStruct* profile;
            params->ReadSubStruct(kStruct, &profile, 1);
            uint32_t buildScript = 0;
            params->ReadHashValue(kBuildScript, &buildScript, 0);
            Appearance appearance;
            reinterpret_cast<Gfx2D::CModelLook*>(&appearance)->Construct();
            reinterpret_cast<Gfx2D::CModelLook*>(&appearance)->Load(profile, 1 != 0);
            reinterpret_cast<void(__thiscall*)(CMeshInstanceModule*, void*, bool, int, uint32_t)>(m_vtable[13])(
                this, &appearance, viaResourceStore != 0, texDictOffset, buildScript);
            reinterpret_cast<Gfx2D::CModelLook*>(&appearance)->DestroyBody();
        }
        break;
    case kObjReplaceTexture: {
        result = false;
        Scripts::CArray* array;
        if (params->GetArray(kArray, &array, 0)) {
            // Each of the array's structures.
            for (uint32_t i = 0; i < array->m_size; ++i) {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(array->GetElement(i));
                const char* src;
                const char* dest;
                entry->GetString(kSrc, &src, true);
                entry->GetString(kDest, &dest, true);
                uint32_t in = kAll;
                entry->ReadHashValue(kIn, &in, 0);
                if (m_meshInstance && m_meshInstance->SwapTexture(in, src, dest))
                    result = true;
            }
        } else {
            const char* src;
            const char* dest;
            params->GetString(kSrc, &src, true);
            params->GetString(kDest, &dest, true);
            uint32_t in = kAll;
            params->ReadHashValue(kIn, &in, 0);
            if (m_meshInstance && m_meshInstance->SwapTexture(in, src, dest))
                result = true;
        }
        break;
    }
    case kObjReplaceSpriteTexture: {
        // (dest: a checksum here, passed on as the name)
        const char* src;
        params->GetString(kSrc, &src, true);
        uint32_t dest = 0;
        params->ReadHashValue(kDest, &dest, 1);
        uint32_t in = kAll;
        params->ReadHashValue(kIn, &in, 0);
        result = m_meshInstance && m_meshInstance->ReplaceTextureWith(in, src, dest);
        break;
    }
    case kObjTestModelCollision:
    case kObjGetBoneVolume: {
        if (name == kObjTestModelCollision) {
            uint32_t arg = 0;
            params->ReadHashValue(kCollisionArg, &arg, false);
            Scripts::CStruct* lineParams = nullptr;
            if (params->ReadSubStruct(kLine, &lineParams, false)) {
                Maths::Vector line[2];
                lineParams->GetVector(kStart, reinterpret_cast<Scripts::VectorValue*>(&line[0]), 1);
                lineParams->GetVector(kEnd, reinterpret_cast<Scripts::VectorValue*>(&line[1]), 1);
                int hit = TestCollision(line, arg, 0, 0, 0);
                if (hit)
                    script->m_params->PutNameKey(kCollisionResult, static_cast<uint32_t>(hit));
            }
        }
        // (the line test goes on into the bone's volume)
        uint32_t bone;
        params->ReadHashValue(kBone, &bone, 1);
        void* skeleton = SkeletonOf(m_boneRig);
        Maths::Matrix matrix;
        GetDrawMatrix(&matrix);
        float volume = static_cast<float>(
            m_meshInstance->GetBoneVolume(reinterpret_cast<uint32_t>(skeleton), reinterpret_cast<uint32_t>(&matrix), bone));
        script->m_params->PushFloat(kBoneVolume, volume);
        break;
    }
    default:
        return 2;
    }
    return result ? 1 : 0;
}

// (no address: the tail jump folded at 0x004E3F90 - model_component_nx.h)
void __fastcall ModelNx::SetLightsDirection(void* lights, void*, int index, const Maths::Vector* direction)
{
    reinterpret_cast<void(__thiscall*)(void*, int, const Maths::Vector*)>((*static_cast<void***>(lights))[2])(lights, index,
                                                                                                      direction);
}

} // namespace Obj
