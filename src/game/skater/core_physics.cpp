// The skater's core physics component, with PartyMod's PS2-style controls built in.

#include "game/skater/core_physics.h"

#include <cmath>

#include "game/mth/vector.h"
#include "game/script/struct.h"
#include "game/skater/controls.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The component's fields (offsets).
constexpr uint32_t kObject = 0xC;            // the skater's object (below)
constexpr uint32_t kGroundNormal = 0x68;     // (a vector the wall check turns the facing into)
constexpr uint32_t kFeeler = 0xA8;           // a collision feeler (its result is copied below)
constexpr uint32_t kSurfaceFlags = 0xFA;     // (a byte passed on when landing)
constexpr uint32_t kHitPoint = 0x120;        // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kOtherFeeler = 0x158;     // another feeler
constexpr uint32_t kFeelerStart = 0x208;     // the feeler's line
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kHitSkatable = 0x228;     // (set by the feeler check 0x005003A0)
constexpr uint32_t kHitFlag = 0x229;         // (likewise)
constexpr uint32_t kInput = 0x150C;          // the pad: a button every 0x20 bytes (pressed, then times)
constexpr uint32_t kTriggerComponent = 0x1510;
constexpr uint32_t kOtherComponent = 0x1514; // (+0x1C, +0x1D: bytes set on landing)
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kTimedFlags = 0x1528;     // bools, each followed by the time it last changed
constexpr uint32_t kTriggerState = 0x1530;   // +0x26: a trigger's script took over

// The object's fields.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kUp = 0x7C;      // the matrix's rows (at +0x6C): up, then the facing
constexpr uint32_t kFacing = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

// The input's buttons (offsets into the pad).
constexpr uint32_t kButton20 = 0x20;
constexpr uint32_t kButton40 = 0x40;
constexpr uint32_t kButton60 = 0x60;
constexpr uint32_t kButton80 = 0x80;
constexpr uint32_t kTrigger1 = 0xC0;
constexpr uint32_t kTrigger2 = 0x120;

Mth::Vector& VectorAt(void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global
// script value).
float PhysicsFloat(uint32_t name, void* object)
{
    return GameFunction<float(__cdecl*)(uint32_t, int, void*)>(0x00580400)(name, 1, object);
}
int PhysicsInteger(uint32_t name, void* object)
{
    return GameFunction<int(__cdecl*)(uint32_t, int, void*)>(0x00580440)(name, 1, object);
}

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
int Now()
{
    int time = GameFunction<int(__cdecl*)()>(0x0044ECE0)();
    return time + GameVariable<int>(0x00696D60);
}

// A timed flag: a bool and, after it, the time it last changed.
void SetTimedFlag(void* flags, uint32_t offset, uint8_t value)
{
    uint8_t& flag = At<uint8_t>(flags, offset);
    if (flag == value)
        return;
    flag = value;
    At<int>(flags, offset + 4) = Now();
}

// A button's times: since it was last pressed (0x0046B920) / released (0x0046B900).
int TimeSincePress(uint8_t* button) { return GameFunction<int(__thiscall*)(void*)>(0x0046B920)(button); }
int TimeSinceRelease(uint8_t* button) { return GameFunction<int(__thiscall*)(void*)>(0x0046B900)(button); }

// The two triggers: both held - or, under PartyMod's PS2 controls (its JZ -> JNZ at each check), either.
bool TriggersHeld(const uint8_t* input)
{
    if (Ps2Controls())
        return input[kTrigger1] || input[kTrigger2];
    return input[kTrigger1] && input[kTrigger2];
}

// velocity += direction * scale, the x component's product unrounded (the original keeps it on the x87 stack).
void Push(Mth::Vector& velocity, const Mth::Vector& direction, float scale)
{
    float y = direction.y * scale;
    float z = direction.z * scale;
    float w = direction.w * scale;
    velocity.x = static_cast<float>(velocity.x + static_cast<double>(direction.x) * scale);
    velocity.y += y;
    velocity.z += z;
    velocity.w += w;
}

} // namespace

// 0x004FCBA0
int CorePhysicsComponent::LipPushOff(Script::CStruct*, Script::CScript*)
{
    void* object = At<void*>(this, kObject);
    const Mth::Vector& facing = VectorAt(object, kFacing);
    Mth::Vector forward = {facing.x, 0.0f, facing.z, facing.w};
    double length = std::sqrt(static_cast<double>(forward.z) * forward.z + static_cast<double>(forward.x) * forward.x);
    if (length > 0.0) {
        double scale = 1.0 / length; // (w isn't scaled; y stays 0)
        forward.x = static_cast<float>(forward.x * scale);
        forward.z = static_cast<float>(forward.z * scale);
    }
    Mth::Vector side = {forward.z, 0.0f, -forward.x, 0.0f};

    uint8_t* input = At<uint8_t*>(this, kInput);
    if (input[kButton80])
        return 0;
    // (The original: both triggers. PartyMod's JZ -> JNZ at 0x004FCC59 makes it either.)
    if (TriggersHeld(input)) {
        float scale = PhysicsFloat(0x1F96C224, object);
        Push(VectorAt(At<void*>(this, kObject), kVelocity), forward, scale);
        return 1;
    }
    if (input[kButton60]) {
        int wait = PhysicsInteger(0x3080E9E7, object);
        if (TimeSincePress(input + kButton60) > wait) {
            float scale = PhysicsFloat(0xDC818B7E, object);
            Push(VectorAt(At<void*>(this, kObject), kVelocity), forward, scale);
            return 1;
        }
    }
    if (input[kButton20]) {
        int wait = PhysicsInteger(0x9B25142A, object);
        if (TimeSincePress(input + kButton20) > wait) {
            float scale = PhysicsFloat(0x1AB3809F, object);
            Mth::Vector& velocity = VectorAt(At<void*>(this, kObject), kVelocity);
            // velocity -= side * scale (x's product unrounded, as above)
            float y = scale * 0.0f;
            float z = -forward.x * scale;
            velocity.x = static_cast<float>(velocity.x - static_cast<double>(forward.z) * scale);
            velocity.y -= y;
            velocity.z -= z;
            velocity.w -= y;
            return 1;
        }
    }
    if (input[kButton40]) {
        int wait = PhysicsInteger(0x9B25142A, object);
        if (TimeSincePress(input + kButton40) > wait) {
            float scale = PhysicsFloat(0x1AB3809F, object);
            VectorAt(At<void*>(this, kObject), kVelocity) += side * scale;
            return 1;
        }
    }
    return 0;
}

// 0x00509BD0
void CorePhysicsComponent::CheckForGround()
{
    void* object = At<void*>(this, kObject);
    uint8_t* input = At<uint8_t*>(this, kInput);
    // The feeler: from above the skater (up * a physics value) to 200 below, along its up axis.
    float above = PhysicsFloat(0xE4D79235, object);
    Mth::Vector& start = VectorAt(this, kFeelerStart);
    Mth::Vector& end = VectorAt(this, kFeelerEnd);
    start = VectorAt(object, kUp) * above;
    end = VectorAt(object, kUp) * -200.0f;
    start += VectorAt(object, kPosition);
    end += VectorAt(object, kPosition);

    auto feel = GameFunction<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x005003A0);
    if (feel(this, 0x10, 0)) {
        const Mth::Vector& position = VectorAt(object, kPosition);
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        const Mth::Vector& up = VectorAt(object, kUp);
        double dx = static_cast<double>(position.x) - point.x;
        double dy = static_cast<double>(position.y) - point.y;
        double dz = static_cast<double>(position.z) - point.z;
        float distance = static_cast<float>(std::sqrt(dx * dx + dz * dz + dy * dy));
        float along = static_cast<float>(dz * up.z + dy * up.y + dx * up.x); // (how far above the hit, along up)

        if (!At<uint8_t>(this, kHitSkatable)) {
            // Not ground: if the skater is (nearly) at or below it, move it out by the hit's normal; then leave.
            if (along < 0.001f) {
                Mth::Vector& p = VectorAt(object, kPosition);
                const Mth::Vector& normal = VectorAt(this, kHitNormal);
                float x = point.x + normal.x, y = point.y + normal.y, z = point.z + normal.z, w = point.w + normal.w;
                p.z = z;
                p.w = w;
                p.x = x;
                p.y = y;
            }
        } else {
            // Ground. The facing turned into the hit's plane and into the ground's plane: a hit at too steep
            // an angle to the current ground (cos of a physics angle - one value with button +0x20 held,
            // another without), or too far away for how fast the skater moves, isn't kept to.
            Mth::Vector normal = VectorAt(this, kHitNormal);
            const Mth::Vector& facing = VectorAt(object, kFacing);
            Mth::Vector inHitPlane = facing;
            Mth::Vector inGroundPlane = facing;
            float facingIntoHit = static_cast<float>(static_cast<double>(facing.x) * normal.x +
                                                     static_cast<double>(facing.z) * normal.z +
                                                     static_cast<double>(facing.y) * normal.y);
            inHitPlane.RotateToPlane(normal);
            inGroundPlane.RotateToPlane(VectorAt(this, kGroundNormal));
            float turn = static_cast<float>(static_cast<double>(inGroundPlane.x) * inHitPlane.x +
                                            static_cast<double>(inGroundPlane.z) * inHitPlane.z +
                                            static_cast<double>(inGroundPlane.y) * inHitPlane.y);
            float angle = PhysicsFloat(input[kButton20] ? 0x4138283E : 0xEF161C2A, object);
            double limit = std::cos(static_cast<double>(angle) * 0.0174532924f); // (degrees to radians)
            bool keep = true;
            if (facingIntoHit > 0.0f && turn > 0.0f && turn < limit)
                keep = false;
            if (keep && along > 0.0f) {
                // How far the skater could have dropped this frame at that angle (at least a physics value).
                float cosine = turn > 1.0f ? 1.0f : (turn < -1.0f ? -1.0f : turn);
                const Mth::Vector& p = VectorAt(object, kPosition);
                const Mth::Vector& old = VectorAt(object, kOldPosition);
                float mx = p.x - old.x, my = p.y - old.y, mz = p.z - old.z;
                double moved = std::sqrt(static_cast<double>(mx) * mx + static_cast<double>(mz) * mz +
                                         static_cast<double>(my) * my);
                float reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
                float least = PhysicsFloat(0x899BA3D0, object);
                if (reach < least)
                    reach = least;
                if (distance > reach)
                    keep = false;
            }
            if (keep) {
                void* flags = At<void*>(this, kTimedFlags);
                SetTimedFlag(flags, 0x40, At<uint8_t>(this, kHitFlag));
                GameFunction<void(__thiscall*)(void*, Mth::Vector)>(0x004FE860)(this, normal);
                VectorAt(object, kPosition) = VectorAt(this, kHitPoint);
                flags = At<void*>(this, kTimedFlags);
                if (At<int>(flags, 0x18) == 0 && distance > 2.0f && turn > 0.99f)
                    SetTimedFlag(flags, 0x98, 1);
                uint8_t surface = At<uint8_t>(this, kSurfaceFlags);
                GameFunction<void(__thiscall*)(void*, uint8_t)>(0x005196A0)(At<void*>(this, kTimedFlags), surface);
                At<uint8_t>(At<uint8_t*>(this, kOtherComponent), 0x1C) = surface;
                GameFunction<void(__thiscall*)(void*)>(0x004FEA50)(this);
                GameFunction<void(__thiscall*)(void*)>(0x005021D0)(this);
                auto trigger = GameFunction<void(__thiscall*)(void*, const void*, void*)>(0x00434BF0);
                if (At<int>(this, 0x1AC) != At<int>(this, 0xFC)) {
                    trigger(At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064BBA4),
                            &At<uint8_t>(this, kOtherFeeler));
                    if (At<uint8_t>(At<uint8_t*>(this, kTriggerState), 0x26))
                        return;
                    trigger(At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064BBB0),
                            &At<uint8_t>(this, kFeeler));
                    if (At<uint8_t>(At<uint8_t*>(this, kTriggerState), 0x26))
                        return;
                }
                void* skater = At<void*>(this, kSkaterComponent);
                if (!At<uint8_t>(skater, 0x13B0))
                    GameFunction<void(__thiscall*)(void*, bool)>(0x005232A0)(skater, false);
                GameFunction<void(__thiscall*)(void*, void*)>(0x0043A930)(&At<uint8_t>(this, kOtherFeeler),
                                                                       &At<uint8_t>(this, kFeeler));
                At<uint8_t>(At<uint8_t*>(this, kOtherComponent), 0x1D) = At<uint8_t>(this, 0x1AA);
                return;
            }
        }
    }

    // Off the ground.
    GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(this, 1);
    GameFunction<void(__thiscall*)(void*, uint32_t, int, int)>(0x00460A20)(At<void*>(this, kObject), 0xD96F01F1, 0, 0);
    GameFunction<void(__thiscall*)(void*, uint32_t, int)>(0x004609F0)(At<void*>(this, kObject), 0x3B1001B6, 0);
    GameFunction<void(__thiscall*)(void*)>(0x004FEBA0)(this);
    void* flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x30)) {
        SetTimedFlag(flags, 0x48, 1);
        // The original: unless both triggers are held. PartyMod only turns the first check's JZ into a JNZ (at
        // 0x00509E47), so under its PS2 controls this is skipped only while the second is held without the first.
        bool skip = Ps2Controls() ? !input[kTrigger1] && input[kTrigger2] : input[kTrigger1] && input[kTrigger2];
        if (!skip)
            BreakVert(false);
        int wait = Script::GetInteger(0x6BB5B751);
        if (TimeSinceRelease(input + kButton20) > wait && TimeSincePress(input + kButton20) > wait)
            SetTimedFlag(At<void*>(this, kTimedFlags), 0x48, 0);
    } else if (TriggersHeld(input)) {
        // (The original: both triggers; PartyMod's JZ -> JNZ at 0x0050A257 makes it either.)
        uint8_t drop[40];
        void* o = At<void*>(this, kObject);
        if (GameFunction<bool(__thiscall*)(void*, int, void*, void*, void*, void*)>(0x00502240)(
                this, 1, &VectorAt(o, kPosition), &VectorAt(o, kOldPosition), &VectorAt(o, kVelocity), drop))
            GameFunction<void(__thiscall*)(void*, void*)>(0x00502F20)(this, drop);
    }
    GameFunction<void(__thiscall*)(void*, const void*, void*)>(0x00434BF0)(
        At<void*>(this, kTriggerComponent), reinterpret_cast<const void*>(0x0064BB80), &At<uint8_t>(this, kOtherFeeler));
}

} // namespace Skater

REPLACE(0x004FCBA0, &Skater::CorePhysicsComponent::LipPushOff);
REPLACE(0x00509BD0, &Skater::CorePhysicsComponent::CheckForGround);
