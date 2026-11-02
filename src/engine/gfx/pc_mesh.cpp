// The PC mesh: see pc_mesh.h.

#include "engine/gfx/pc_mesh.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/file/file_load.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/scene_file_stream.h"
#include "formats/scene_format.h"
#include "engine/gfx/pc_scene.h"

namespace render {

namespace {

StripBreak* new_breaks(uint32_t count) { return static_cast<StripBreak*>(memory::alloc(count * sizeof(StripBreak))); }
// (the C runtime's qsort; the original's is the game's, 0x006242A0)
void sort_breaks(StripBreak* breaks, uint32_t count)
{
    std::qsort(breaks, count, sizeof(StripBreak), &compare_strip_breaks);
}

// The skin's strip breaks (scene_format.h) into the mesh, sorted. Not original - a skin cut short, or with more
// breaks than are there, is dropped (none: the original read on past its end).
template <typename Stream>
bool read_breaks(PcMesh* mesh, Stream& in)
{
    scene_format::Skin skin;
    if (!scene_format::read_skin(in, &skin)) {
        debug::printf("skin damaged: %u strip breaks, cut short\n", skin.break_count);
        return false;
    }
    if (skin.version >= 2)
        mesh->bone_count_ = skin.bone_count;
    mesh->break_count_ = skin.break_count;
    if (mesh->break_count_) {
        mesh->breaks = new_breaks(mesh->break_count_);
        for (uint32_t i = 0; i < mesh->break_count_; ++i) {
            StripBreak& strip_break = core::at(std::span<StripBreak>(mesh->breaks, mesh->break_count_), i);
            std::memcpy(&strip_break, std::span<const uint8_t>(skin.breaks).subspan(i * scene_format::kStripBreakSize).data(),
                        scene_format::kStripBreakSize);
            strip_break.strip_index_ = 0;
        }
        sort_breaks(mesh->breaks, mesh->break_count_);
    }
    return mesh->break_count_ != 0;
}

} // namespace

// 0x004B7AF0
int compare_strip_breaks(const void* a, const void* b)
{
    const uint32_t x = static_cast<const StripBreak*>(a)->mesh_and_index_;
    const uint32_t y = static_cast<const StripBreak*>(b)->mesh_and_index_;
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
        std::array<char, 0x100> file{};
        std::memcpy(file.data(), name, std::strlen(name) + 1); // (unchecked, as the original)
        text::to_lower_text(file.data());
        if (char* extension = std::strstr(file.data(), "skin.xbx")) {
            std::memcpy(extension, "cas.xbx", 8);
            load_skin_file(file.data());
        }
    }
}

// 0x004B7E30
PcMesh::~PcMesh()
{
    if (breaks)
        memory::Hub::get_pull()->dealloc(breaks);
    if (scene)
        engine::unload_scene(scene, 1); // (the engine's UnloadScene)
}

// 0x004B7B50
bool PcMesh::load_skin_file(const char* name)
{
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return false; // Not original - no file (the original read through null)
    scene_format::FileStream in(file);
    const bool found = read_breaks(this, in);
    file::shut(file);
    return found;
}

// 0x004B7C60
bool PcMesh::load_skin_from_memory(const uint8_t* data, size_t size)
{
    scene_format::MemoryStream in(data, size);
    return read_breaks(this, in);
}

// 0x004B7EB0
void PcMesh::load_skin(const uint8_t* data, size_t size)
{
    if (data)
        load_skin_from_memory(data, size);
}

// 0x004B7ED0
void PcMesh::choose_scene(PcScene* new_scene)
{
    scene = new_scene;
    const SceneRef* list = new_scene->meshes;
    hierarchy_ = list->hierarchy;
    hierarchy_count_ = list->unknown78;
}

} // namespace render
