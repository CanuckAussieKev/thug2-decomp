// The PC mesh: see pc_mesh.h.

#include "engine/gfx/pc_mesh.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/file/file_load.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/mesh_list.h"

namespace render {

namespace {

StripBreak* new_breaks(uint32_t count) { return static_cast<StripBreak*>(memory::alloc(count * sizeof(StripBreak))); }
// (the C runtime's qsort; the original's is the game's, 0x006242A0)
void sort_breaks(StripBreak* breaks, uint32_t count)
{
    std::qsort(breaks, count, sizeof(StripBreak), &compare_strip_breaks);
}

} // namespace

// 0x004B7AF0
int __cdecl compare_strip_breaks(const void* a, const void* b)
{
    uint32_t x = static_cast<const StripBreak*>(a)->mesh_and_index_;
    uint32_t y = static_cast<const StripBreak*>(b)->mesh_and_index_;
    if ((x >> 16) != (y >> 16))
        return (x >> 16) > (y >> 16) ? 1 : -1;
    if ((x & 0xFFFF) > (y & 0xFFFF))
        return 1;
    return (x & 0xFFFF) < (y & 0xFFFF) ? -1 : 0;
}

// 0x004B7D50
PcMesh::PcMesh(const char* name)
{
    if (std::strstr(name, "skin")) {
        char file[0x100];
        std::memcpy(file, name, std::strlen(name) + 1); // (unchecked, as the original)
        text::to_lower_text(file);
        if (char* extension = std::strstr(file, "skin.xbx")) {
            std::memcpy(extension, "cas.xbx", 8);
            load_skin_file(file);
        }
    }
}

// 0x004B7E30
PcMesh::~PcMesh()
{
    if (breaks)
        memory::Hub::get_pull()->dealloc(breaks);
    if (scene)
        engine::unload_scene(reinterpret_cast<WorldScene*>(scene), 1); // (the engine's UnloadScene)
}

// 0x004B7B50
bool PcMesh::load_skin_file(const char* name)
{
    file::Hdl file = file::unseal(name, "rb");
    uint32_t version;
    file::get_take_in(&version, 4, 1, file);
    if (version >= 2)
        file::get_take_in(&bone_count_, 4, 1, file);
    file::get_take_in(&break_count_, 4, 1, file);
    if (break_count_) {
        breaks = new_breaks(break_count_);
        for (uint32_t i = 0; i < break_count_; ++i) {
            file::get_take_in(&breaks[i], 12, 1, file);
            breaks[i].strip_index_ = 0;
        }
        sort_breaks(breaks, break_count_);
    }
    file::shut(file);
    return break_count_ != 0;
}

// 0x004B7C60
bool PcMesh::load_skin_from_memory(const uint8_t** data)
{
    const uint32_t* p = reinterpret_cast<const uint32_t*>(*data);
    uint32_t version = *p++;
    if (version >= 2)
        bone_count_ = *p++;
    break_count_ = *p++;
    if (break_count_) {
        breaks = new_breaks(break_count_);
        for (uint32_t i = 0; i < break_count_; ++i) {
            std::memcpy(&breaks[i], p, 12);
            p += 3;
            breaks[i].strip_index_ = 0;
        }
        sort_breaks(breaks, break_count_);
    }
    *data = reinterpret_cast<const uint8_t*>(p);
    return break_count_ != 0;
}

// 0x004B7EB0
void PcMesh::load_skin(const uint8_t* data)
{
    if (data)
        load_skin_from_memory(&data);
}

// 0x004B7ED0
void PcMesh::choose_scene(uint8_t* new_scene)
{
    scene = new_scene;
    auto* list = *reinterpret_cast<SceneRef**>(new_scene + 0x280);
    hierarchy_ = list->extra;
    hierarchy_count_ = list->unknown78;
}

} // namespace render
