// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960 (see
// debug_cfuncs.h).

#include "engine/script/debug_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/cscript.h"
#include "engine/sys/mlp.h"
#include "engine/script/param_struct.h"
#include "engine/script/script_startup.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/game_config.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/stream_component.h"
#include "engine/core/bits_as.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag

namespace commands {

uint32_t g_level_qb_files[15];

namespace {

constexpr uint32_t kId = 0x40C698AF;                      // "id"
constexpr uint32_t kFreeMem = 0x06ADEBFC;                 // "free_mem"
constexpr uint32_t kMinFreeMem = 0xE7FB8DC5;              // "min_free_mem"
constexpr uint32_t kUpdateDisplayFreeMemory = 0xF1E14224; // "UpdateDisplayFreeMemory"
constexpr uint32_t kLevelSpecific = 0x6DD1DECF;           // "LevelSpecific"
constexpr uint32_t kFileName = 0xC3F4169A;                // "FileName"
constexpr uint32_t kSound = 0x07713C7B;                   // "sound" (the component type)
constexpr uint32_t kStream = 0x0F1641E3;                  // "stream" (the component type)

// The game's language (0x007CEFA0): 2 English, 3 French, 4 Spanish, 5 German, 6 Italian.
int get_language() { return game_config::g_language; }

// A .qb file loaded (0x005A5960; `level_specific`: unloaded with the level) / unloaded by its name's checksum
// (0x005A59B0).
void load_qb_file(const char* name, bool level_specific) { script::load_qb_file(name, level_specific ? 1 : 0); }
void unload_qb_file(uint32_t name) { script::unload_qb_file(name); }

// A screenshot written to `name` (0x0048C100, a jump to 0x004B24E0).
void take_screen_shot(const char* name, int flags) { render::screen_shot(name, flags); }
// The renderer's metrics display switched on / off (0x0048C0F0: the flag 0x006FA7D6 toggled).
void toggle_metrics_display() { render::engine::toggle_metrics(); }

// `count` bytes as "%02x " each, then a new line.
void print_bytes(const void* data, int count)
{
    auto* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < count; ++i)
        debug::printf("%02x ", bytes[i]);
    debug::printf("\n");
}

} // namespace

uint32_t* level_specific_qb_files() { return g_level_qb_files; }

// 0x004489B0: DisplayFreeMem: for every heap of the memory manager, run UpdateDisplayFreeMemory with id=<the
// heap's name checksum> free_mem=<the space left in its region> min_free_mem=<the least there has been>.
bool cfunc_display_free_mem(script::Struct*, script::Script*)
{
    memory::Hub* manager = memory::Hub::get_pull();
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    for (memory::MemSource* heap = manager->first_zone(); heap; heap = manager->following_zone(heap)) {
        memory::Zone* region = heap->region_;
        params->get_clear();
        params->put_name_key(kId, checksums::hash_from_text(heap->label_));
        params->put_int(kFreeMem, region->free_byte_count());
        params->put_int(kMinFreeMem, region->min_available_);
        script::launch_script(kUpdateDisplayFreeMemory, params, nullptr, false, nullptr);
    }
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    return true;
}

// 0x00449110: Test: prints the bytes of a symbol table entry (a structure named 0xC1C935B2,
// flags 4) after setting flag bits 1 and 2 in turn, then of a component (byte 0x57, word 1000, name 0x480A2607,
// value 0x01020304) - a check of the layouts. Always true.
bool cfunc_test(script::Struct*, script::Script*)
{
    script::SymbolTableEntry entry;
    // NOLINTNEXTLINE(bugprone-raw-memory-call-on-non-trivial-type): the original's layout test zeroes the record
    std::memset(&entry, 0, sizeof(entry));
    entry.unknown0 = 4;
    entry.type = script::kQbvalStruct;
    entry.name = 0xC1C935B2;
    debug::printf("CSymbolTableEntry:\n");
    print_bytes(&entry, sizeof(entry));
    entry.unknown0 |= 1;
    print_bytes(&entry, sizeof(entry));
    entry.unknown0 |= 2;
    print_bytes(&entry, sizeof(entry));
    entry.unknown0 &= ~4;

    script::Component component;
    script::construct_component(&component);
    std::memset(&component, 0, sizeof(component));
    component.type_and_flag_ = 0x57;
    component.unknown2 = 1000;
    component.name = 0x480A2607;
    component.raw = 0x01020304;
    debug::printf("CComponent:\n");
    print_bytes(&component, sizeof(component));
    return true;
}

// 0x004493B0: LoadQB "<file>" [LevelSpecific]: the .qb file loaded; LevelSpecific ones are noted in the first free
// slot of the table (by the name's checksum; all 15 taken: not noted, loaded all the same).
bool cfunc_load_qb(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    params->fetch_text(0u, &name, 0);
    if (!params->has_bare_flag(kLevelSpecific)) {
        load_qb_file(name, false);
        return true;
    }
    uint32_t* files = level_specific_qb_files();
    for (int i = 0; i < kLevelSpecificQbFiles; ++i) {
        if (!files[i]) {
            files[i] = checksums::hash_from_text(name);
            break;
        }
    }
    load_qb_file(name, true);
    return true;
}

// 0x00449450: UnloadQB "<file>": the .qb file unloaded, and forgotten by the LevelSpecific table (every slot).
bool cfunc_unload_qb(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    params->fetch_text(0u, &name, 0);
    const uint32_t checksum = checksums::hash_from_text(name);
    unload_qb_file(checksum);
    uint32_t* files = level_specific_qb_files();
    for (int i = 0; i < kLevelSpecificQbFiles; ++i) {
        if (files[i] == checksum)
            files[i] = 0;
    }
    return true;
}

// 0x004494A0: ScreenShot [FileName="<name>" ("screen")].
bool cfunc_screen_shot(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    if (!params->fetch_text(kFileName, &name, 0))
        name = "screen";
    take_screen_shot(name, 0);
    return true;
}

// 0x004494E0: ToggleMetrics.
bool cfunc_toggle_metrics(script::Struct*, script::Script*)
{
    toggle_metrics_display();
    return true;
}

// 0x004494F0 / 0x00449500 / 0x00449510 / 0x00449520 / 0x00449530: German / French / Spanish / Italian / English:
// whether the game runs in that language.
bool cfunc_german(script::Struct*, script::Script*) { return get_language() == 5; }
bool cfunc_french(script::Struct*, script::Script*) { return get_language() == 3; }
bool cfunc_spanish(script::Struct*, script::Script*) { return get_language() == 4; }
bool cfunc_italian(script::Struct*, script::Script*) { return get_language() == 6; }
bool cfunc_english(script::Struct*, script::Script*) { return get_language() == 2; }

// 0x004498F0: ProximCleanup: every sound and stream component's proximity node cleared (what they keep of
// the proximity nodes), then every proximity node freed.
bool cfunc_proxim_cleanup(script::Struct*, script::Script*)
{
    for (objects::ObjectModule* c = objects::object_hub_slot()->find_first_module_of_kind(kSound); c;
         c = c->next_of_type_)
        static_cast<objects::AudioEmitterModule*>(c)->proxim_ = nullptr;
    for (objects::ObjectModule* c = objects::object_hub_slot()->find_first_module_of_kind(kStream); c;
         c = c->next_of_type_)
        static_cast<objects::AudioStreamModule*>(c)->proxim_ = nullptr;
    objects::nearby_zone_shutdown();
    return true;
}

// 0x00449950: Inside: whether the proximity node script running now was started by entering (not leaving). (No
// check that the proximity manager exists.)
bool cfunc_inside(script::Struct*, script::Script*)
{
    return memory::bits_as<objects::ProximityRegistry*>(objects::g_proximity_hub)->entering_;
}

// 0x00449960: ResetEngine: the main loop ends after this frame (and the game starts again).
bool cfunc_reset_engine(script::Struct*, script::Script*)
{
    assets::g_main_loop_manager->stop_loop();
    return true;
}

} // namespace commands
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
