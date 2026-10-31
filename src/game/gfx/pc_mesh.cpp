// The PC mesh: see pc_mesh.h.

#include "game/gfx/pc_mesh.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/file/file_load.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/core/code_address.h"

namespace Engine3D {

namespace {

extern void** const kPcMeshVtable;

void ConstructBase(CPcMesh* mesh) { reinterpret_cast<CSubMesh*>(mesh)->CSubMesh::MakeNew(); }

sStripBreak* NewBreaks(uint32_t count) { return static_cast<sStripBreak*>(MemSys::Alloc(count * sizeof(sStripBreak))); }
// (the C runtime's qsort; the original's is the game's, 0x006242A0)
void SortBreaks(sStripBreak* breaks, uint32_t count)
{
    std::qsort(breaks, count, sizeof(sStripBreak), &CompareStripBreaks);
}

} // namespace

// 0x004B7AF0
int __cdecl CompareStripBreaks(const void* a, const void* b)
{
    uint32_t x = static_cast<const sStripBreak*>(a)->meshAndIndex;
    uint32_t y = static_cast<const sStripBreak*>(b)->meshAndIndex;
    if ((x >> 16) != (y >> 16))
        return (x >> 16) > (y >> 16) ? 1 : -1;
    if ((x & 0xFFFF) > (y & 0xFFFF))
        return 1;
    return (x & 0xFFFF) < (y & 0xFFFF) ? -1 : 0;
}

// 0x004B7B30
CPcMesh* CPcMesh::MakeNew()
{
    ConstructBase(this);
    vtable = reinterpret_cast<void**>(kPcMeshVtable);
    return this;
}

// 0x004B7D50
CPcMesh* CPcMesh::ConstructFromFile(const char* name)
{
    ConstructBase(this);
    vtable = reinterpret_cast<void**>(kPcMeshVtable);
    if (std::strstr(name, "skin")) {
        char file[0x100];
        std::memcpy(file, name, std::strlen(name) + 1); // (unchecked, as the original)
        TextUtil::ToLowerText(file);
        if (char* extension = std::strstr(file, "skin.xbx")) {
            std::memcpy(extension, "cas.xbx", 8);
            LoadSkinFile(file);
        }
    }
    return this;
}

// 0x004B7E30
void CPcMesh::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcMeshVtable);
    if (breaks)
        MemSys::Hub::Pull()->Dealloc(breaks);
    if (scene)
        Engine::UnloadScene(reinterpret_cast<CWorldScene*>(scene), 1); // (the engine's UnloadScene)
    reinterpret_cast<CSubMesh*>(this)->CSubMesh::DestroyBody();
}

// 0x004B7F00
CPcMesh* CPcMesh::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B7B50
bool CPcMesh::LoadSkinFile(const char* name)
{
    FileLoad::Hdl file = FileLoad::Unseal(name, "rb");
    uint32_t version;
    FileLoad::TakeIn(&version, 4, 1, file);
    if (version >= 2)
        FileLoad::TakeIn(&boneCount, 4, 1, file);
    FileLoad::TakeIn(&breakCount, 4, 1, file);
    if (breakCount) {
        breaks = NewBreaks(breakCount);
        for (uint32_t i = 0; i < breakCount; ++i) {
            FileLoad::TakeIn(&breaks[i], 12, 1, file);
            breaks[i].stripIndex = 0;
        }
        SortBreaks(breaks, breakCount);
    }
    FileLoad::Shut(file);
    return breakCount != 0;
}

// 0x004B7C60
bool CPcMesh::LoadSkinFromMemory(const uint8_t** data)
{
    const uint32_t* p = reinterpret_cast<const uint32_t*>(*data);
    uint32_t version = *p++;
    if (version >= 2)
        boneCount = *p++;
    breakCount = *p++;
    if (breakCount) {
        breaks = NewBreaks(breakCount);
        for (uint32_t i = 0; i < breakCount; ++i) {
            std::memcpy(&breaks[i], p, 12);
            p += 3;
            breaks[i].stripIndex = 0;
        }
        SortBreaks(breaks, breakCount);
    }
    *data = reinterpret_cast<const uint8_t*>(p);
    return breakCount != 0;
}

// 0x004B7EB0
void CPcMesh::LoadSkin(const uint8_t* data)
{
    if (data)
        LoadSkinFromMemory(&data);
}

// 0x004B7ED0
void CPcMesh::ChooseScene(uint8_t* newScene)
{
    scene = newScene;
    auto* list = *reinterpret_cast<sceneRef**>(newScene + 0x280);
    hierarchy = list->extra;
    hierarchyCount = list->unknown78;
}

namespace {
static VtableSlot kPcMeshVtable_slots[] = {&Engine3D::CPcMesh::Teardown};
void** const kPcMeshVtable = reinterpret_cast<void**>(kPcMeshVtable_slots);
} // namespace

} // namespace Nx
