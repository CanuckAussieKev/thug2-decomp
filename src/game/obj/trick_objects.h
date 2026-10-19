#pragma once

#include <cstdint>

// Pieces of the trick object code at the end of the viewer range (0x005880E0-0x0058817C); the rest of it (the trick
// object manager at 0x007CE500 and its clusters, 0x00588190 on) is another range.
namespace Script {
class CStruct;
}

namespace Obj {

// 0x005880E0: one count of the trick object manager (0x007CE500, counted at 0x007CE504) released; the last deletes it
// (its scalar deleting destructor, slot 0).
void ReleaseTrickObjectManager();

// 0x00588120: a cluster walk's callback (cdecl): counted when the object (+0x40 set) is in the cluster asked for.
struct ClusterCount {
    int count;
    uint32_t cluster;
};
ClusterCount* __cdecl CountObjectInCluster(uint8_t* object, ClusterCount* counter);

// A trick cluster's parameters (its structure at +0x38). 0x00588150: replaced by a copy of `params` (none: emptied).
struct CTrickCluster {
    uint8_t m_unknown00[0x38];
    Script::CStruct* m_params; // +0x38
    void SetParams(Script::CStruct* params);
};

// A list in the skater's trick component (its +0x13B4): 0x00588170 the constructor (no entries; its count at +0x800);
// 0x00588180 emptied (true).
struct CTrickObjectList {
    uint32_t m_items[0x200];
    int m_count; // +0x800
    CTrickObjectList* Construct();
    bool Clear();
};

} // namespace Obj
