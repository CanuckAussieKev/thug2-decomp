// NOT ORIGINAL - a texture dictionary's surface maps (surface_map_load.h).
#include "render_pc/surface_map_load.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/core/format.h"
#include "engine/file/data_overlay.h"
#include "engine/file/file_load.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/surface_maps.h"
#include "engine/gfx/texture_sets.h"
#include "formats/byte_stream.h"
#include "formats/surface_map_format.h"
#include "formats/texture_format.h"
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
        return;
    }
    read_originals(name);
}

// The maps were made from the game's own textures: where a texture set serves the dictionary (its upscaled texels),
// each texture is known by its checksum and its original's first level - the same dictionary read as the game has it.
void SurfacePack::read_originals(const char* name)
{
    std::array<char, file::kOverlayPathSize> relative{};
    frontend::data_overlay::overlay_name(name, relative);
    std::string set_file;
    if (!texture_sets::file_for(relative.data(), &set_file)) {
        return; // (the game's own dictionary: its texels are the originals)
    }
    set_ = texture_sets::active();
    delit_ = texture_sets::delit_textures(set_, relative.data()); // (sorted: binary searched in attach)
    const texture_sets::OriginalFiles originals;
    file::Hdl handle = file::unseal(name, "rb");
    if (handle == nullptr) {
        get_log("surface maps: %s - the original of %s's dictionary isn't there: its maps can't be matched", name,
                set_.c_str());
        return;
    }
    std::vector<uint8_t> bytes;
    const uint32_t size = file::get_file_size(handle);
    if (size > 0 && size <= kLargestFile) {
        bytes.resize(size);
        if (file::get_take_in(bytes.data(), static_cast<int>(size), 1, handle) != static_cast<int>(size)) {
            bytes.clear();
        }
    }
    file::shut(handle);
    util::ByteReader reader(std::span<const uint8_t>(bytes.data(), bytes.size()));
    texture_format::DictionaryHeader dictionary;
    if (!texture_format::read_dictionary_header(reader, &dictionary)) {
        return;
    }
    for (int32_t i = 0; i < dictionary.count; ++i) {
        texture_format::TextureHeader header;
        if (!texture_format::read_texture_header(reader, &header) || !reader.skip(header.palette_size)) {
            break;
        }
        bool whole = true;
        for (uint32_t level = 0; level < header.levels && whole; ++level) {
            const uint8_t* data = nullptr;
            uint32_t level_bytes = 0;
            whole = texture_format::read_level(reader, &data, &level_bytes);
            if (whole && level == 0) {
                originals_.insert_or_assign(header.checksum,
                                            smf::content_hash(std::span<const uint8_t>(data, level_bytes)));
            }
        }
        if (!whole) {
            break;
        }
    }
}

void SurfacePack::attach(GpuTexture* texture, uint32_t checksum, std::span<const uint8_t> first_level)
{
    surface::SurfaceMaps* maps = surface::surface_maps();
    if (!active() || maps == nullptr || texture == nullptr) {
        return;
    }
    // (a texture set's texture: known by its original's texels - read_originals)
    const auto original = originals_.find(checksum);
    const uint64_t hash = original != originals_.end() ? original->second : smf::content_hash(first_level);
    const smf::Entry* entry = smf::find(entries_, checksum, hash);
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
                                .f0 = entry->f0,
                                .checksum = entry->checksum,
                                .content_hash = entry->content_hash,
                                .flags = entry->flags,
                                .delit = std::ranges::binary_search(delit_, checksum)};
    if (maps->attach(texture, info, texels)) {
        ++attached_;
        attached_delit_ += info.delit ? 1U : 0U;
    }
}

void SurfacePack::report() const
{
    if (!active()) {
        return;
    }
    const std::string delit =
        attached_delit_ != 0 ? text::format("; %u came de-lit - no draw-time de-lighting", attached_delit_) : std::string();
    get_log("surface maps: %s - %u of %zu maps attached%s%s", name_.c_str(), attached_, entries_.size(),
            set_.empty() ? "" : (" (" + set_ + "'s textures, matched by their originals" + delit + ")").c_str(),
            stale_ != 0 ? " (some textures' texels differ from the maps': made again with unpack_game.ts --pbr-maps)" : "");
}

} // namespace render
