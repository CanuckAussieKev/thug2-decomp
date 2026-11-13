// NOT ORIGINAL - a texture dictionary's surface maps (surface_map_load.h).
#include "render_pc/surface_map_load.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/file/file_load.h"
#include "engine/gfx/surface_maps.h"
#include "formats/surface_map_format.h"
#include "platform/run_log.h"

namespace render {

namespace {

namespace smf = surface_map_format;

constexpr const char* kSuffix = ".pbr";
constexpr uint32_t kLargestFile = 512U * 1024U * 1024U;

} // namespace

SurfacePack::SurfacePack(const char* name)
{
    if (surface::surface_maps() == nullptr || name == nullptr) {
        return;
    }
    name_ = std::string(name) + kSuffix;
    if (!file::is_present(name_.c_str())) {
        return;
    }
    file::Hdl handle = file::unseal(name_.c_str(), "rb");
    if (handle == nullptr) {
        return;
    }
    const uint32_t size = file::get_file_size(handle);
    if (size > 0 && size <= kLargestFile) {
        file_.resize(size);
        if (file::get_take_in(file_.data(), static_cast<int>(size), 1, handle) != static_cast<int>(size)) {
            file_.clear();
        }
    }
    file::shut(handle);
    if (!smf::read_entries(file_, &entries_)) {
        get_log("surface maps: %s isn't a surface-map file (or is damaged) - not used", name_.c_str());
        entries_.clear();
        file_.clear();
    }
}

void SurfacePack::attach(GpuTexture* texture, uint32_t checksum, std::span<const uint8_t> first_level)
{
    surface::SurfaceMaps* maps = surface::surface_maps();
    if (!active() || maps == nullptr || texture == nullptr) {
        return;
    }
    const smf::Entry* entry = smf::find(entries_, checksum, smf::content_hash(first_level));
    if (entry == nullptr) {
        for (const smf::Entry& other : entries_) {
            stale_ += other.checksum == checksum ? 1U : 0U;
        }
        return;
    }
    const std::span<const uint8_t> texels = std::span<const uint8_t>(file_).subspan(entry->offset, entry->bytes);
    const surface::MapInfo info{.width = entry->width,
                                .height = entry->height,
                                .material_class = static_cast<uint8_t>(entry->material_class),
                                .f0 = entry->f0};
    if (maps->attach(texture, info, texels)) {
        ++attached_;
    }
}

void SurfacePack::report() const
{
    if (!active()) {
        return;
    }
    get_log("surface maps: %s - %u of %zu maps attached%s", name_.c_str(), attached_, entries_.size(),
            stale_ != 0 ? " (some textures' texels differ from the maps': made again with unpack_game.ts --pbr-maps)" : "");
}

} // namespace render
