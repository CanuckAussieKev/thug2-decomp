// Cutscenes: starting and ending one (the details' configure_from_data / End), the data's assets from the loaded
// bundle and its destructor (0x00466620-0x00467707). See cutscene.h.

#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/scene_lights.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "engine/assets/resource_entry.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/dbg.h"
#include "engine/file/asset_bundle.h"
#include "engine/file/bundle_format.h"
#include "engine/util/byte_stream.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/node_objects.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/script/load.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/gfx/loading_console.h"
#include "engine/core/code_address.h"
#include "engine/gfx/engine.h"
#include "engine/objects/cutscene.h"

namespace objects {

uint8_t g_skin_colour_table[80]{0,   0,   0,   0,  0,   0,   0,   0,  0,   0,   0,   0,  0, 0, 128, 63,
                                239, 226, 125, 59, 80,  171, 108, 64, 205, 4,   143, 63, 0, 0, 128, 63,
                                239, 226, 125, 59, 180, 212, 5,   65, 87,  207, 247, 63, 0, 0, 128, 63,
                                153, 71,  126, 59, 185, 169, 157, 64, 74,  151, 190, 64, 0, 0, 128, 63,
                                153, 71,  126, 59, 46,  84,  117, 64, 72,  24,  102, 64, 0, 0, 128, 63};
float g_skin_colours[20]{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kFlagParam = 0x82C84419;

// configure_from_data's parameters and scripts (names unknown).
constexpr uint32_t kCutsceneName = 0xA1DC81F9;
constexpr uint32_t kNoMusicFlag = 0xBAFA9F1C;
constexpr uint32_t kForceFlag = 0x7DDDE00E;
constexpr uint32_t kStartTime = 0xAFD20A78;  // milliseconds
constexpr uint32_t kLoopLength = 0x45F71FC9; // milliseconds
constexpr uint32_t kEndTime = 0xF1948C44;

// The asset types in a cutscene's bundle.
constexpr uint32_t kTypeStructure = 0x508AE2F2; // the parameters
constexpr uint32_t kTypeQb = 0x2BBEA5C3;
constexpr uint32_t kTypeModel = 0xFD8697E1; // (also 0xE7308C1F and 0x0524FD4E)
constexpr uint32_t kTypeSkin = 0xE7308C1F;
constexpr uint32_t kTypeModel2 = 0x0524FD4E;
constexpr uint32_t kTypeCameraAnim = 0x05CA1497;
constexpr uint32_t kTypeTextures = 0x1512808D;
constexpr uint32_t kTypeColours = 0x2CD4107D;
constexpr uint32_t kTypeObjectAnim = 0x2E4BF21B;
constexpr uint32_t kTypeImage = 0xEDD8D75F;
constexpr uint32_t kTypeObjects = 0x5AC14717;
constexpr uint32_t kTypeAnim = 0xEAB51346;
constexpr uint32_t kTypeExtra = 0xFFC529F4;
// The skater's appearance parts and the colours a skinned model takes from them.
constexpr uint32_t kPartA = 0x650FAB6D, kPartB = 0x0FC85BAE;
constexpr uint32_t kColourNames[4] = {0xA28FAB7F, 0xB90F08D0, 0x1DBCFAE8, 0x0442D3C1};

assets::ResourceRegistry* get_assets() { return assets::g_asset_manager; }
// The asset manager's load (0x00408060: not permanent, copied), unload (0x00407B90 + 0x00407B20) and find.
void* load_asset_data(uint32_t name, int type, uint32_t textures, const void* data, uint32_t size, void* params)
{
    return get_assets()->read_resource_from_stream(name, type, textures, const_cast<void*>(data), size, false, true,
                                                   params, 0);
}
void discard_loaded_item(uint32_t name)
{
    assets::Resource* asset = get_assets()->obtain_loaded_item(name, 1);
    get_assets()->discard_loaded_item(asset);
}
void delete_source(void* source) { delete static_cast<render::FrameSource*>(source); }
void* new_anim_source() { return new render::FrameSource; }
script::Struct* make_struct()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void free_struct(script::Struct* s)
{
    if (!s)
        return;
    s->get_clear();
    script::delete_struct(s);
}
void get_run(uint32_t script, script::Struct* params = nullptr, Entity* object = nullptr)
{
    script::launch_script(script, params, object, false, nullptr);
}

// The asset manager's load parameters (0x00409E90 constructs them).
struct LoadParams {
    uint8_t flag0;
    uint8_t pad1[3];
    int texture_set_;     // +0x04
    void* extra;          // +0x08
    uint32_t extra_size_; // +0x0C
    uint8_t is_model_;    // +0x10
    uint8_t flag11;
};
// The textures' load parameters (filled in place).
struct TextureParams {
    uint8_t flag0;
    uint8_t pad1[3];
    int texture_set_;  // +0x04
    uint8_t is_model_; // +0x08
    uint8_t flag9;
};
// A skinned model's colours (for 0x0048C090): the colour table, its entries, where the colours go.
struct SkinColours {
    const uint8_t* end;
    const uint8_t* start;
    void* table;    // (0x0067D810)
    float* colours; // (0x0067D860)
};

// (debug output, compiled out)
inline void print_cutscene_details(void*) {}

} // namespace

// 0x00466620
bool CinematicEntry::read_in(script::Struct* params)
{
    char file_name[256] = "none"; // (from the game's 0x0064797C)
    uint32_t name = 0;
    if (owns_data_) {
        const char* path;
        params->fetch_text(kCutsceneName, &path, 1);
        name = checksums::hash_from_text(cutscene_name_from_path(path));
        asset_ = checksums::hash_from_text(path);
        std::memcpy(file_name, path, std::strlen(path) + 1); // (unbounded, as the original)
    } else {
        params->read_hash_value(kCutsceneName, &name, 1);
        asset_ = 0;
    }
    script::Struct* settings = make_struct();
    settings->append_structure_if_any(params);
    configure_from_data(settings);
    if (script::Struct* defaults = script::read_sub_struct(checksums::fold_text_into_checksum(name, "_params")))
        args_->append_structure_if_any(defaults);
    begin_script_ = checksums::fold_text_into_checksum(name, "_begin");
    end_script_ = checksums::fold_text_into_checksum(name, "_end");
    bool loaded = false;
    if (!owns_data_) {
        content_ = static_cast<CinematicTable*>(get_assets()->get_asset_data(name, 0));
        loaded = content_ != nullptr;
    } else {
        // the loading screen; the music volumes kept to give back at the end, then muted
        render::display_loading_screen(nullptr, true, false);
        gfx::loading_console::screen_shown(true, false); // not original: the loading console (gfx/loading_console.h)
        music_volume_ = render::fetch_ambient_scale(true);
        stream_volume0_ = render::fetch_diffuse_scale(0, true);
        stream_volume1_ = render::fetch_diffuse_scale(1, true);
        render::change_ambient_scale(0.0f, 0.0f);
        render::change_diffuse_scale(0, 0.0f, 0.0f);
        render::change_diffuse_scale(1, 0.0f, 0.0f);
        render::update_scene_light_queue();
        assets::ResourceRegistry* assets = get_assets();
        if (!assets->get_asset_data_by_name(file_name, 0)) {
            int music = args_->has_bare_flag(kNoMusicFlag) ? 0 : 1;
            int streams = args_->has_bare_flag(kNoMusicFlag) ? 0 : 1;
            if (params->has_bare_flag(kForceFlag)) {
                music = 1;
                streams = 1;
            }
            args_->put_int(0xCDF2B13E, music);
            args_->put_int(0x9A4BDFF5, streams);
            args_->put_int(0xED064302, 1);
            args_->put_int(0xBABF2DC9, 1);
            args_->append_structure_if_any(settings);
            get_run(0x722D9CA7);
            gfx::hide_models_for_cutscene();
            get_run(0xE1CE826A, args_);
            content_ =
                static_cast<CinematicTable*>(assets->load_asset(file_name, 0, 0, 0, false, false, false, settings_, 0));
            float start;
            if (params->fetch_float(kStartTime, &start, 0)) {
                CinematicTable* data = content_;
                float seconds = static_cast<float>(start * static_cast<double>(0.0010000000474974513f));
                audio::pause_music(1);
                data->unknown3474_ = 1;
                data->clock_ = seconds;
            }
            float loop;
            if (params->fetch_float(kLoopLength, &loop, 0)) {
                loop = static_cast<float>(loop * static_cast<double>(0.0010000000474974513f));
                CinematicTable* data = content_;
                data->unknown3474_ = 1;
                data->unknown3488_ = *reinterpret_cast<uint32_t*>(&data->clock_);
                data->unknown348c_ = static_cast<float>(static_cast<double>(loop) + data->clock_);
                audio::pause_music(1);
            }
            if (script::Array* subtitles = script::fetch_list(checksums::fold_text_into_checksum(name, "_subtitles")))
                content_->set_subtitles(subtitles);
            loaded = true;
        }
    }
    free_struct(settings);
    return loaded;
}

// 0x00466A10
void CinematicEntry::stop()
{
    if (!owns_data_) {
        if (content_)
            content_->destroy_objects();
        if (end_script_)
            get_run(end_script_);
        print_cutscene_details(this);
        return;
    }
    get_run(0xE7BC7EFD);
    get_run(0x4C0FD1D7);
    reinterpret_cast<render::FlexibleParticleManager*&>(render::g_flexible_particle_manager)->delete_temp_heap();
    if (content_) {
        discard_loaded_item(asset_);
        content_ = nullptr;
    }
    render::clear_scene_light_queue();
    if (end_script_)
        get_run(end_script_);
    get_run(0xF49A8C9C);
    get_run(0xDEFF69FF);
    memory::Hub::get_pull()->delete_cutscene_heaps();
    auto* skater = static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider());
    if (skater) {
        script::Struct* flag = make_struct();
        flag->put_name_key(kUnnamed, kFlagParam);
        get_run(0xABF617EE, flag, reinterpret_cast<Entity*>(skater));
        free_struct(flag);
    }
    get_run(0xBCA1791C, args_);
    gfx::show_models_after_cutscene();
    get_run(0x94D0D528);
    print_cutscene_details(this);
    get_run(0x15674315);
}

// 0x00466C20
CinematicTable::~CinematicTable()
{
    g_cutscene_playing = false;
    bundle_ptr_.reset();
    if (camera_source_)
        delete_source(camera_source_);
    camera_source_ = nullptr;
    if (object_source_)
        delete_source(object_source_);
    object_source_ = nullptr;
    for (int i = 0; i < animation_count_; ++i) {
        if (animations_[i].source)
            delete_source(animations_[i].source);
        animations_[i].data = nullptr; // (the source's pointer left as it is, as the original)
    }
    destroy_objects();
    remove_cutscene_lights();
    for (int i = 0; i < asset_count_; ++i) {
        discard_loaded_item(assets_[i]);
        assets_[i] = 0;
    }
    if (flag_34b_0_) {
        audio::stop_music();
        if (flag_34b_0_) {
            get_run(0x7216D8DA);
            char file_name[256];
            std::snprintf(file_name, sizeof(file_name), "%08x.qb", label_);
            script::unload_qb(checksums::hash_from_text(file_name));
        }
    }
    if (call_args_) {
        free_struct(call_args_);
        call_args_ = nullptr;
    }
}

// 0x00466DD0
void CinematicTable::load_from_bundle()
{
    file::AssetBundle* bundle = bundle_ptr_.get();
    int count = bundle->num_;
    for (int i = 0; i < count; ++i) {
        file::AssetBundle::TableRow* entry = bundle_ptr_->get_entry(i);
        void* data = bundle_ptr_->obtain_loaded_item(i);
        uint32_t type = entry->type;
        switch (type) {
        case kTypeStructure:
            call_args_->get_clear();
            // (not original: bounded by the asset's size)
            script::decode_from_bytes(call_args_, static_cast<const uint8_t*>(data), entry->size);
            break;
        case kTypeQb:
            if (flag_34b_0_) {
                char file_name[256];
                std::snprintf(file_name, sizeof(file_name), "%08x.qb", label_);
                script::load_qb_data(file_name, static_cast<const uint8_t*>(data), 1, 0, 0);
            }
            break;
        case kTypeCameraAnim: {
            uint32_t name = hash_of_name("_CAM", static_cast<int>(entry->name)); // (the game's 0x00647914)
            load_asset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++camera_parts_;
            if (!camera_source_) {
                camera_source_ = new_anim_source();
                static_cast<render::FrameSource*>(camera_source_)->set_done(true);
            }
            if (!camera_anim_) {
                camera_anim_ = static_cast<render::FrameSource*>(camera_source_)->play_clip(name);
                static_cast<gfx::JointClipFrames*>(camera_anim_)->arm_events();
            }
            if (!object_anims_)
                object_anims_ = camera_anim_;
            break;
        }
        case kTypeObjectAnim: {
            uint32_t name = hash_of_name("_OBA", static_cast<int>(entry->name)); // (the game's 0x0064790C)
            load_asset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++object_parts_;
            if (!object_source_) {
                object_source_ = new_anim_source();
                static_cast<render::FrameSource*>(object_source_)->set_done(true);
            }
            if (!anim_list_) {
                anim_list_ = static_cast<render::FrameSource*>(object_source_)->play_clip(name);
                static_cast<gfx::JointClipFrames*>(anim_list_)->arm_events();
            }
            break;
        }
        case kTypeImage: {
            uint32_t name = entry->name;
            load_asset_data(name, 6, 0, data, entry->size, nullptr);
            assets_[asset_count_] = name;
            ++asset_count_;
            break;
        }
        case kTypeObjects:
            call_args_->get_clear();
            objects_from_table(call_args_, static_cast<const uint32_t*>(data));
            break;
        case kTypeAnim:
            load_animation(entry->name, reinterpret_cast<uint32_t>(data), entry->size);
            break;
        case kTypeModel:
        case kTypeSkin:
        case kTypeModel2:
            ingest_mesh(i, entry, data);
            continue; // (it unloads its own assets)
        case kTypeTextures:
        case kTypeColours:
        case kTypeExtra:
            continue; // (loaded with their models)
        default:
            break;
        }
        bundle_ptr_->free_asset(entry->name, entry->type);
    }
    if (!call_args_->fetch_float(kEndTime, &end_time_, 0))
        end_time_ = *static_cast<float*>(flag_34b_0_ ? camera_anim_ : anim_list_);
}

// A model in the bundle (with its textures, a skinned model's colours): loaded unless it's a skin variant that
// isn't the skater's (variants: name + 1 ... name + 4 by face and sex).
void CinematicTable::ingest_mesh(int index, file::AssetBundle::TableRow* entry, void* data)
{
    uint32_t model_name = entry->name + kTypeModel; // (the original prints name + type, compiled out)
    LoadParams params;
    reinterpret_cast<assets::SkinParams*>(&params)->get_clear(); // (the skinned model's load parameters, 0x14 bytes)
    void* texture_data = bundle_ptr_->find_asset(entry->name, kTypeTextures, 1);
    file::AssetBundle::TableRow* textures = bundle_ptr_->find_entry(entry->name, kTypeTextures, 1);
    uint32_t texture_size = textures->size;
    uint32_t textures_name = textures->type + textures->name;
    uint8_t is_model = entry->type == kTypeModel;
    params.extra = nullptr;
    params.extra_size_ = 0;
    params.is_model_ = is_model;
    params.flag0 = 0;
    params.texture_set_ = entry->type == kTypeSkin ? 0 : index + 200;
    TextureParams texture_params;
    texture_params.texture_set_ = params.texture_set_;
    void* assets = get_assets();
    texture_params.flag0 = 0;
    texture_params.flag9 = 0;
    texture_params.is_model_ = is_model;
    bool load = static_cast<int>(entry->size) >= 0x96;
    if (const uint8_t* colours = static_cast<const uint8_t*>(bundle_ptr_->find_asset(entry->name, kTypeColours, 0))) {
        // the skater's colours from its appearance (part A or B, else the other)
        auto* part1 = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part(kPartA));
        if (!part1)
            part1 = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part(kPartB));
        auto* part2 = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part_option(kPartA));
        if (!part2)
            part2 = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part_option(kPartB));
        float* skin = g_skin_colours;
        skin[0] = skin[1] = skin[2] = skin[3] = 1.0f;
        for (int k = 0; k < 4; ++k) {
            float colour[4];
            script::Struct* from = part1->has_entry_named(kColourNames[k]) ? part1 : part2;
            std::memcpy(skin + 4 * (k + 1), read_colour(colour, from, kColourNames[k]), 16);
        }
        // Not original - the colours read within their asset (bundle_format.h): a count past it reads none (the
        // original's table ran past the asset).
        util::ByteReader reader(colours, bundle_ptr_->find_entry(entry->name, kTypeColours, 0)->size);
        bundle_format::SkinColours read;
        if (!bundle_format::read_skin_colours(reader, &read)) {
            debug::printf("cutscene skin colours damaged: %d entries\n", read.count);
            read.count = 0;
        }
        SkinColours table;
        table.start = read.entries;
        table.end = table.start + read.count * bundle_format::kSkinColourSize;
        table.table = g_skin_colour_table;
        table.colours = skin;
        // which skin this is: the next names along say (none after it: the last)
        bool male = false, has_face = false;
        if (!bundle_ptr_->find_entry(entry->name + 1, kTypeModel, 0)) {
            male = false;
            has_face = true;
        } else if (!bundle_ptr_->find_entry(entry->name + 2, kTypeModel, 0)) {
            male = false;
            has_face = false;
        } else if (!bundle_ptr_->find_entry(entry->name + 3, kTypeModel, 0)) {
            male = true;
            has_face = true;
        } else if (!bundle_ptr_->find_entry(entry->name + 4, kTypeModel, 0)) {
            male = true;
            has_face = false;
        }
        void* face = appearance_face(unknown34a8_);
        int skater_face =
            face && reinterpret_cast<bool(__thiscall*)(void*)>(code_address(&gfx::FaceSkin::is_sound))(face) ? 1 : 0;
        int skater_male;
        static_cast<script::Struct*>(unknown34ac_)->fetch_int_value(0x3F813177, &skater_male, 1);
        load = static_cast<int>(has_face) == skater_face && static_cast<int>(male) == skater_male;
        if (load)
            render::set_screen_clip(&table);
    }
    if (load) {
        params.extra = bundle_ptr_->find_asset(entry->name, kTypeExtra, 0);
        if (params.extra)
            params.extra_size_ = bundle_ptr_->find_entry(entry->name, kTypeExtra, 1)->size;
        static_cast<assets::ResourceRegistry*>(assets)->read_resource_from_stream(
            textures_name, 3, 0, texture_data, texture_size, false, true, &texture_params, 0);
        static_cast<assets::ResourceRegistry*>(assets)->read_resource_from_stream(model_name, 7, textures_name, data,
                                                                                  entry->size, false, true, &params, 0);
        assets_[asset_count_] = model_name;
        ++asset_count_;
        assets_[asset_count_] = textures_name;
        ++asset_count_;
    }
    bundle_ptr_->free_asset(entry->name, entry->type);
    bundle_ptr_->free_asset(entry->name, kTypeTextures);
    bundle_ptr_->free_asset(entry->name, kTypeExtra);
    bundle_ptr_->free_asset(entry->name, kTypeColours);
}

} // namespace objects
