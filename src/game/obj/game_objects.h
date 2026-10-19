#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

// Composite objects the game makes in code (0x005780E0-0x00578670): cars made by the script command that creates
// them (0x005A3260's caller), and the King of the Hill crown (Obj::CCrownObject): it sits on the head of the skater
// wearing it.
namespace Script {
class CStruct;
}

namespace Obj {

class CCompositeObjectManager;

// 0x005780E0: a car's components set up: the ones every object of this kind has (0x0057B2F0 / 0x0057B420, with
// `params`), its motion's speeds (+0x24 216, +0x28 384, +0x2C 528; +0x70 off) and an extra one (0x00423C90 /
// 0x00423510: 240 at +0x94), car_add_components run on it, Finalize, its model's update (0x00420480).
void InitCar(CCompositeObject* car, Script::CStruct* params);
// 0x00578170: a new car (OBJECT_TYPE_CAR) from `params`: a composite object with its base components
// (0x0057B250) and one more (0x58 bytes, 0x0041BAD0), at the node's position, registered with the composite
// object manager, then InitCar.
void CreateCar(Script::CStruct* params);

// The King of the Hill crown (0x114 bytes, vtable 0x0064EC44: CCompositeObject's but for the destructor and its own
// update).
class CCrownObject : public CCompositeObject {
public:
    // 0x00578240: the constructor: its base components (0x0057B250), type 0xD2156C50, id crown_object, not worn.
    // Returns this.
    CCrownObject* Construct();
    // 0x005782F0: the destructor body (not worn, then CCompositeObject's). 0x00578460: the scalar deleting
    // destructor (vtable slot 0).
    void DestroyBody();
    CCrownObject* Destroy(unsigned int flags);
    // 0x00578490: registered with `manager`, its components (the common ones, a model, the node's), id
    // crown_object, never suspended (the suspend component's distances 1000000-1000003), the model crown.mdl, at
    // the node's position, Finalize.
    void InitFromNode(CCompositeObjectManager* manager, Script::CStruct* node);
    // 0x005782B0: worn by `skater`. 0x005782D0: taken off. 0x005782E0: whether it's worn.
    void PutOn(CCompositeObject* skater);
    void TakeOff();
    bool IsWorn();
    // 0x00578310 (vtable +0x28, its own update): worn, it follows the skater's head bone: the bone's matrix turned
    // 90 degrees about x, raised 18 above the head, in the skater's frame, at the skater's position.
    void OwnUpdate();

    CCompositeObject* m_skater; // +0x108
    uint32_t m_unknown10C;
    bool m_worn;                // +0x110
    uint8_t pad111[3];
};
static_assert(offsetof(CCrownObject, m_skater) == 0x108 && offsetof(CCrownObject, m_worn) == 0x110 &&
                  sizeof(CCrownObject) == 0x114,
              "layout must match the original");

} // namespace Obj
