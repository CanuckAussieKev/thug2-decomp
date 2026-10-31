// Cutscenes: loading a cutscene file and playing it (the data's Load / finish_loading / is_loaded / Update / Play,
// the details' remaining virtuals and both deleting destructors; 0x00467710-0x004680DE). See cutscene.h.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/file/asset_bundle.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine_glue.h"
#include "engine/objects/cutscene.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/sys/game_config.h"

namespace audio {
// (stream.h; not included here: its SetMusicVolume clashes with music_player.h's)
void __cdecl sync_music_to_time(float seconds);
} // namespace audio

namespace objects {

char g_cutscene_shot_name[256];
bool g_cutscene_playing;
int g_cutscene_fixed_frame;

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kKeepSkeleton = 0x0CBEC8AC;
constexpr uint32_t kTypeModel = 0xFD8697E1;
constexpr uint32_t kFixedTime = 0x0C00425A;       // a global integer: the time from the frame counter
constexpr uint32_t kDiagnosticsHash = 0x5ECC0073; // a global integer: show the time
constexpr uint32_t kStartScript = 0xE794A164;
constexpr uint32_t kFieldOfView = 0x99529205; // a global float: the cutscene camera's
constexpr uint32_t kInfoScript = 0xD9157174;

// The frame counter while the time is fixed (one frame each update), and the screenshots' file name.
int& fixed_frame() { return g_cutscene_fixed_frame; }
char* shot_name() { return g_cutscene_shot_name; }

void get_run(uint32_t script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}
void delete_virtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
script::Struct* make_struct()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}

// The vblank count since a time, in seconds (the x87's unsigned 64-bit conversion).
float vblank_seconds(uint64_t vblanks, int rate)
{
    uint64_t low_part[1] = {vblanks & 0x7FFFFFFFFFFFFFFFull};
    uint64_t high_part[1] = {vblanks & 0x8000000000000000ull};
    float result;
    __asm {
        fild qword ptr [low_part]
        fild qword ptr [high_part]
        fchs
        faddp st(1), st
        fidiv dword ptr [rate]
        fstp dword ptr [result]
    }
    return result;
}

} // namespace

// 0x00467710
void CinematicTable::mark_loaded_models()
{
    int version = 0;
    call_args_->fetch_int_value(kVersion, &version, 1);
    script::Array* objects = nullptr;
    call_args_->fetch_list(kObjects, &objects, 1);
    for (uint32_t i = 0; i < objects->extent_; ++i) {
        auto* object = reinterpret_cast<script::Struct*>(objects->fetch_part(i));
        uint32_t model;
        if (object->read_hash_value(version >= 2 ? kName : kModel, &model, 0) &&
            bundle_ptr_->has_asset(model, kTypeModel))
            object->put_name_key(kUnnamed, kKeepSkeleton);
    }
    if (flag_34b_0_)
        add_objects(call_args_);
}

// 0x004677F0
bool CinematicTable::finish_loading(int /*unused*/)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(flag_34b_0_ ? manager->cinematic_low_heap_ : manager->bottom_up_heap_);
    load_from_bundle();
    mark_loaded_models();
    add_cutscene_lights();
    memory::Hub::get_pull()->leave_heap_scope();
    if (bundle_ptr_)
        delete_virtual(bundle_ptr_);
    bundle_ptr_ = nullptr;
    if (flag_34b_0_ && !ready_) {
        // the music: waiting for its stream to be ready, then started
        for (;;) {
            audio::tick_frame();
            if (audio::is_music_track_primed()) {
                audio::begin_primed_music_track(-1.0f);
                ready_ = true;
                break;
            }
            frame_clock::wait_for_vblank();
            if (ready_)
                break;
        }
    }
    return true;
}

// 0x004678A0
bool CinematicTable::read_in(const char* path, int a2, int /*unused*/, const uint32_t* settings)
{
    if (settings) {
        unknown34a8_ = reinterpret_cast<void*>(settings[0]);
        unknown34ac_ = reinterpret_cast<void*>(settings[1]);
        flag_34b_0_ = static_cast<uint8_t>(settings[2]);
    } else {
        unknown34a8_ = nullptr;
        unknown34ac_ = nullptr;
        flag_34b_0_ = 0;
    }
    if (flag_34b_0_)
        audio::stop_music();
    stubs::return_true(); // (0x005B6570: a jump to the shared stub, true)
    const char* name = cutscene_name_from_path(path);
    if (flag_34b_0_)
        std::memcpy(shot_name(), name, std::strlen(name) + 1);
    else
        std::memcpy(shot_name(), "dummy", 6); // (the game's 0x0064799C)
    label_ = checksums::hash_from_text(name);
    if (flag_34b_0_) {
        // the music: the cutscene's for the skater's model, else the cutscene's own; else none to wait for
        char stream[256];
        std::snprintf(stream, sizeof(stream), "%s_%s", name,
                      skater_model_name(static_cast<script::Struct*>(unknown34ac_)));
        if (!audio::prime_music_track(checksums::hash_from_text(stream))) {
            audio::stop_music();
            std::snprintf(stream, sizeof(stream), "%s", name);
            if (!audio::prime_music_track(checksums::hash_from_text(stream)))
                ready_ = true;
        }
        if (flag_34b_0_)
            set_up_cutscene_heaps();
    }
    g_cutscene_playing = true;
    memory::Hub* manager = memory::Hub::get_pull();
    memory::Arena* file_heap = flag_34b_0_ ? manager->cinematic_low_heap_ : manager->bottom_up_heap_;
    memory::Arena* asset_heap = flag_34b_0_ ? manager->cinematic_high_heap_ : manager->top_down_heap_;
    manager->enter_heap_scope(file_heap);
    void* memory = memory::alloc_zeroed(sizeof(file::AssetBundle));
    bundle_ptr_ = memory ? new (memory) file::AssetBundle : nullptr;
    bundle_ptr_->set_heaps(file_heap, asset_heap);
    memory::Hub::get_pull()->leave_heap_scope();
    memory::Hub::get_pull()->enter_heap_scope(asset_heap);
    bundle_ptr_->read_in(path, a2, 0);
    memory::Hub::get_pull()->leave_heap_scope();
    finish_loading(a2);
    ready_flag_ = true;
    return true;
}

// 0x00467B50
bool CinematicTable::is_loaded()
{
    if (ready_flag_)
        return true;
    if (flag_34b_0_) {
        if (!bundle_ptr_->is_loaded())
            return false;
        finish_loading(1);
        ready_flag_ = true;
        return true;
    }
    if (bundle_ptr_ && !bundle_ptr_->is_loaded())
        return false;
    return true;
}

// 0x00467BA0
void CinematicTable::tick_frame(void* camera, script::Struct* /*params*/)
{
    bool fixed = script::fetch_int_value(kFixedTime) != 0;
    if (!flag_34b_0_) {
        unknown3478_ = *reinterpret_cast<uint32_t*>(&clock_);
        clock_ = static_cast<float>(frame_clock::get_frame_seconds() + clock_);
    } else {
        if (!can_skip_) { // (the first update: started)
            can_skip_ = true;
            get_run(kStartScript);
            static_cast<gfx::ViewCam*>(camera)->change_h_fov(script::fetch_float(kFieldOfView));
            unknown3478_ = 0;
            paused_vblanks_ = frame_clock::fetch_retrace_count();
            fixed_frame() = 0;
        }
        int rate = game_config::g_frame_rate;
        audio::sync_music_to_time(vblank_seconds(frame_clock::fetch_retrace_count() - paused_vblanks_, rate));
        update_time(reinterpret_cast<int>(camera));
        check_skip();
        refresh_viewer(camera);
        update_subtitles();
    }
    update_object_poses();
    update_object_transforms();
    show_hidden_objects();
    if (!flag_34b_0_)
        return;
    if (script::fetch_int_value(kDiagnosticsHash) != 0 || fixed) {
        script::Struct* info = make_struct();
        int frame = fixed_frame();
        char line[128];
        std::snprintf(line, sizeof(line), "time = %02d : %02d : %02d", frame / 30 / 60, frame / 30 % 60, frame % 30);
        info->append_word("line1", line);
        char line2[128];
        std::snprintf(line2, sizeof(line2), "time = %f sec. / %f sec.", static_cast<double>(clock_),
                      static_cast<double>(end_time_));
        info->append_word("line2", line2);
        script::launch_script(kInfoScript, info, nullptr, false, nullptr);
        if (info) {
            info->get_clear();
            script::delete_struct(info);
        }
        if (fixed && fixed_frame() > 0) {
            char shot[128];
            std::snprintf(shot, sizeof(shot), "%s%04d", shot_name(), fixed_frame() - 1);
            render::screen_shot(shot, 1); // (a screenshot)
        }
    }
    ++fixed_frame();
}

// 0x00467EA0
void CinematicTable::perform(void* camera, script::Struct* params)
{
    g_cutscene_playing = true;
    if (!flag_34b_0_) {
        ready_ = true;
        tick_frame(camera, params);
        return;
    }
    refresh_objects();
    if (!ready_) {
        if (!audio::is_music_track_primed())
            return;
        audio::begin_primed_music_track(-1.0f);
        ready_ = true;
    }
    tick_frame(camera, params);
}

// 0x00467F30
CinematicEntry* CinematicEntry::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00467F60
void CinematicEntry::tick_frame()
{
    if (!content_)
        return;
    if (!owns_data_) {
        if (!begun_) {
            content_->restart_objects();
            if (begin_script_)
                get_run(begin_script_);
        }
        begun_ = true;
        content_->perform(viewer_, args_);
        return;
    }
    if (!content_->is_loaded())
        return;
    if (!begun_ && begin_script_) {
        get_run(begin_script_);
        begun_ = true;
    }
    if (flag_5d_) {
        content_->update_vblanks();
        content_->refresh_objects();
        content_->update_object_transforms();
    } else {
        content_->perform(viewer_, args_);
    }
    if (unknown60_ > -1 && --unknown60_ == 0) {
        unknown60_ = -1;
        reinterpret_cast<void(__thiscall*)(void*, int)>(vtable_[0x30 / 4])(this, 1);
    }
}

// 0x00468040
bool CinematicEntry::is_finished()
{
    if (is_finished_)
        return true;
    if (flag18_ || !content_)
        return false;
    return content_->is_loaded() && content_->is_finished();
}

// 0x004680B0
bool CinematicEntry::is_ready()
{
    CinematicTable* data = content_;
    return data && data->flag_34b_0_ && data->is_loaded() && data->ready_;
}

} // namespace objects
