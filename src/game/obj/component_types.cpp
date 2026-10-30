// The component types every composite object can be made of, registered with the composite object manager.

#include "game/obj/component_types.h"

#include <cstdint>

#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "hook/game.h"
#include "hook/game_code.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// A component type: its checksum, the function that makes one and the one that registers its script member
// functions (if any). Names are given where the checksum's name is known.
struct ComponentType {
    uint32_t type;
    uint32_t create;
    uint32_t registerFunctions;
};

// In the original's order (CreateComponent finds the first of a type).
const ComponentType kComponentTypes[] = {
    {0x21FC3301, 0x005193B0, 0x00000000},
    {0xB5F1E9D2, 0x00513AD0, 0x00513860}, // SkaterLoopingSound
    {0x187574FA, 0x00518E20, 0x00000000}, // SkaterSound
    {0x11B4C67F, 0x00512C60, 0x00000000}, // SkaterGap
    {0x52D61A0D, 0x00517D40, 0x00000000}, // SkaterRotate
    {0xD9151D4D, 0x00517830, 0x005176A0}, // SkaterPhysicsControl
    {0x7CD3E6D5, 0x00513630, 0x00000000}, // SkaterLocalNetLogic
    {0x850EEF66, 0x005147F0, 0x00000000}, // SkaterNonLocalNetLogic
    {0xD262802F, 0x00518B60, 0x00000000}, // SkaterScore
    {0xBF518E27, 0x00514430, 0x00000000}, // SkaterMatrixQueries
    {0x125044E2, 0x00511810, 0x00000000}, // SkaterFloatingName
    {0x18223FD6, 0x0051AD30, 0x00000000}, // SkaterStateHistory
    {0x5BD63B29, 0x00505440, 0x004FBE80}, // SkaterCorePhysics
    {0x9F9CC949, 0x004F80E0, 0x00000000}, // SkaterAdjustPhysics
    {0x9B373E58, 0x00511280, 0x00000000}, // SkaterFinalizePhysics
    {0xD8CC3428, 0x004FBE20, 0x00000000}, // SkaterCleanupState
    {0xB1D5A3E1, 0x00510F90, 0x00000000}, // SkaterEndRun
    {0x589E5FB3, 0x004F9170, 0x004F8CD0}, // SkaterBalanceTrick (balance_trick.h)
    {0x488AF07A, 0x00511470, 0x00000000}, // SkaterFlipAndRotate
    {0x43686585, 0x00519740, 0x00519430}, // SkaterState
    {0x81F01058, 0x004EB7F0, 0x004E6FB0},
    {0x5B509AD3, 0x004F7FB0, 0x00000000},
    {0x1D5B1AE6, 0x004EC420, 0x004EC220},
    {0xAC8946E2, 0x004E5940, 0x00000000},
    {0x8BCE7E66, 0x005185D0, 0x00518240}, // SkaterRunTimer
    {0x1859F772, 0x0051B880, 0x0051AD90},
    {0x380D909A, 0x0051E280, 0x00000000},
    {0x5B512EDC, 0x0051DA80, 0x00000000}, // Tagging
    {0xD1515392, 0x004ECF20, 0x004EC960},
    {0x286A8D26, 0x00421490, 0x00000000}, // Model
    {0xE3FCA939, 0x00422DC0, 0x00000000}, // ModelLightUpdate
    {0x222756D5, 0x00431F60, 0x00431BE0}, // the skeleton
    {0x7149B753, reinterpret_cast<uint32_t>(GAME_CODE(0x0041A5B0, &CAnimBlenderComponent::Create)),
     reinterpret_cast<uint32_t>(GAME_CODE(0x00419690, &CAnimBlenderComponent::RegisterMemberFunctions))}, // the animation blender
    {0x07713C7B, 0x00433100, 0x00432C80}, // Sound
    {0xA432DC41, 0x0041C920, 0x00000000}, // Collision
    {0x5CB5CB7E, 0x0041FF20, 0x00000000},
    {0xC695136D, 0x0041BD90, 0x0041B910}, // CarPhysics
    {0x0A015E17, 0x00426650, 0x00423130}, // Motion
    {0xCE0CA665, 0x00434290, 0x00434610}, // Suspend
    {0x270F56E1, 0x00523490, 0x0051EA90}, // Trick
    {0x6EB050ED, 0x004E39E0, 0x004E3310}, // Avoid
    {0x6EFDDFB8, 0x004F0B10, 0x004ECFB0}, // PedLogic
    {0xFFB39248, 0x0042EF00, 0x0042BB00}, // Rigidbody
    {0x6DCFEC46, 0x004298B0, 0x00000000},
    {0x8A897DD2, 0x00431800, 0x00000000}, // Shadow
    {0x0F1641E3, 0x00433D90, 0x00433870}, // Stream
    {0xF27A294D, 0x0051BD80, 0x00000000}, // SpecialItem
    {0xE47F1B79, 0x0043AA30, 0x00434F00}, // Vehicle
    {0x2C747D8A, 0x005248C0, 0x00524420}, // VehicleCamera
    {0xC472ECC5, 0x00428010, 0x00000000},
    {0x1CDFB2A3, 0x0042B920, 0x00000000},
    {0x27443708, 0x00428130, 0x00000000},
    {0x3506CE64, 0x0051BA10, 0x00000000}, // Skitch
    {0xC4E311FA, 0x0041B840, 0x00000000}, // Camera
    {0x5E43A604, 0x004FA800, 0x00000000}, // SkaterCamera
    {0x404EB936, 0x0043D670, 0x0043D490},
    {0xE594F0A2, 0x00434B90, 0x00000000}, // Trigger
    {0xEADA1621, 0x0041E3C0, 0x00000000},
    {0x27D7CD28, 0x0041EEC0, 0x0041E6F0}, // Input
    {0xA9DB601E, 0x0042A000, 0x00429940}, // Particle
    {0xC5BCEB9E, 0x0041E1E0, 0x0041D6E0},
    {0x726E85AA, 0x0052A5C0, 0x00525E40}, // Walk
    {0xC7D37EC2, 0x00525DC0, 0x00000000}, // WalkCamera
    {0x7505D7CC, 0x00434870, 0x00000000},
    {0x81C7122D, 0x004E42E0, 0x00000000}, // CameraLookAround
    {0x408D2AA9, 0x00427AB0, 0x00000000}, // MovableContact
    {0x4FF8DAC6, 0x0051CB90, 0x0051C440},
    {0x28C97F3C, 0x0043CFC0, 0x00000000}, // VehicleSound
    {0x41272956, 0x0043D430, 0x00000000}, // Velocity
    {0x6259B52B, 0x0041C370, 0x00000000},
    {0x7767D6D7, 0x004F3010, 0x00000000},
    {0x1BBF844D, 0x004312A0, 0x00000000},
    {0x9649D4F9, 0x00433690, 0x00000000}, // StaticVehicle
    {0xCDC76A22, 0x0041B3C0, 0x0041B280},
    {0xB87C8E43, 0x0041ADC0, 0x00000000},
    {0x3618B878, 0x004F3550, 0x00000000},
    {0x7E747547, 0x004F3890, 0x00000000}, // (the projectile component)
    {0x03AC8065, 0x0043D930, 0x00000000},
    {0xD8BFC9F7, 0x0041D0F0, 0x00000000},
    {0x208353AC, 0x0041CB20, 0x00000000},
    {0x0301D67C, 0x0041D200, 0x0041D170},
};

} // namespace

// 0x004E4860
void __cdecl RegisterComponentTypes()
{
    CCompositeObjectManager* manager = CompositeObjectManager();
    for (const ComponentType& t : kComponentTypes) {
        manager->RegisterComponent(t.type, reinterpret_cast<ComponentCreateFunction>(t.create),
                                   reinterpret_cast<ComponentRegisterFunction>(t.registerFunctions));
    }
}

} // namespace Obj

REPLACE(0x004E4860, Obj::RegisterComponentTypes);
