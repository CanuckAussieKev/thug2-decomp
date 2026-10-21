#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's matrix queries component (SkaterMatrixQueries, type 0xBF518E27, vtable 0x0064BF70, 0x60 bytes): the
// scripts' questions about how the skater is turned - its heading, slope, pitch, roll and yaw - answered from a copy
// of the core physics' matrix taken each frame. From the disassembly of 0x00513FD0-0x005144A1 (0x00514040 isn't a
// Ghidra function).
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 12 CallMemberFunction.
namespace Obj {

class CSkaterMatrixQueriesComponent : public CBaseComponent {
public:
    // 0x00514430 (registered by RegisterComponentTypes): a new one (0 if out of memory), no core physics yet.
    static CBaseComponent* __cdecl Create();
    // 0x00513FD0: the destructor body (the base's).
    void DestroyBody();
    // 0x00513FE0 (vtable slot 0)
    CSkaterMatrixQueriesComponent* Destroy(unsigned int flags);
    // 0x00514010 (vtable +0x0C): the core physics component found.
    void Finalize();
    // 0x00514030 (vtable +0x04): the core physics' matrix (+0x18) copied.
    void Update();
    // 0x00514040 (vtable +0x30): the script member functions (the unnamed parameter an angle in degrees):
    // GetHeading (heading 0-360 from the at row's x - turned for a negative z -, cosine, sine into the script's
    // parameters), GetSlope (Slope - the at row's angle above the horizontal, negative when it points down - and
    // ChangeInSlope since the last call), AbsolutePitchGreaterThan / PitchGreaterThan (the up row's y / its dot
    // product with the core physics' +0x78 vector below the angle's cosine), RollGreaterThan (the right row turned
    // into the plane across that vector more than the angle away), YawingLeft / YawingRight (the velocity, turned
    // into the up row's plane, to that side of the at row), YawBetween (more than the pair's first angle and not
    // more than its second away from the at row). 1 / 0; 2 for any other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    Mth::Matrix m_matrix;   // +0x18 the core physics' (rows: right, up, at, position)
    float m_lastSlope;      // +0x58 GetSlope's last
    uint8_t* m_corePhysics; // +0x5C SkaterCorePhysics (0x5BD63B29)
};
static_assert(offsetof(CSkaterMatrixQueriesComponent, m_matrix) == 0x18 &&
                  offsetof(CSkaterMatrixQueriesComponent, m_lastSlope) == 0x58 &&
                  offsetof(CSkaterMatrixQueriesComponent, m_corePhysics) == 0x5C &&
                  sizeof(CSkaterMatrixQueriesComponent) == 0x60,
              "layout must match the original (0x00514430 allocates 0x60 bytes)");

} // namespace Obj
