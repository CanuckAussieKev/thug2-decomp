// Pieces of the trick object code, 0x005880E0-0x0058818C. See trick_objects.h.

#include "game/obj/trick_objects.h"

#include <cstdint>

#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

// 0x005880E0
void ReleaseTrickObjectManager()
{
    uint32_t& count = GameVariable<uint32_t>(0x007CE504);
    if (count > 1) {
        --count;
        return;
    }
    void** instance = GameVariable<void**>(0x007CE500);
    if (instance)
        reinterpret_cast<void*(__thiscall*)(void*, int)>(static_cast<void**>(*instance)[0])(instance, 1);
    GameVariable<void*>(0x007CE500) = nullptr;
    count = 0;
}

// 0x00588120
ClusterCount* __cdecl CountObjectInCluster(uint8_t* object, ClusterCount* counter)
{
    if (object[0x40] && counter->cluster == *reinterpret_cast<uint32_t*>(object + 0x3C))
        counter->count += 1;
    return counter;
}

// 0x00588150
void CTrickCluster::SetParams(Script::CStruct* params)
{
    m_params->Clear();
    if (params)
        m_params->AppendStructure(params);
}

// 0x00588170
CTrickObjectList* CTrickObjectList::Construct()
{
    m_count = 0;
    return this;
}

// 0x00588180
bool CTrickObjectList::Clear()
{
    m_count = 0;
    return true;
}

} // namespace Obj

REPLACE(0x005880E0, Obj::ReleaseTrickObjectManager);
REPLACE(0x00588120, Obj::CountObjectInCluster);
REPLACE(0x00588150, &Obj::CTrickCluster::SetParams);
REPLACE(0x00588170, &Obj::CTrickObjectList::Construct);
REPLACE(0x00588180, &Obj::CTrickObjectList::Clear);
