// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960 (see
// debug_cfuncs.h).

#include "engine/script/debug_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
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
#include "engine/core/down_cast.h"
#include "engine/script/script_state.h"
#include "engine/core/language.h"
#include <gsl/pointers>
#include <span>
#include <gsl/assert>

namespace {
// The record layout test: values the dumps are built from.
constexpr uint32_t kTestSymbolName = 0xC1C935B2U; // text unknown
constexpr uint8_t kTestComponentType = 0x57;
constexpr uint16_t kTestComponentUnknown2 = 1000;
constexpr uint32_t kTestComponentWord = 0x01020304;

// Names (the checksums of their text).
constexpr uint32_t kBlaa = 0x480A2607U; // blaa

} // namespace

namespace commands {

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
int get_language() { return game_config::get_language(); }

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
    const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(data), static_cast<std::size_t>(count));
    for (const uint8_t byte : bytes) {
        debug::printf("%02x ", byte);
    }
    debug::printf("\n");
}

} // namespace

// 0x004489B0: DisplayFreeMem: for every heap of the memory manager, run UpdateDisplayFreeMemory with id=<the
// heap's name checksum> free_mem=<the space left in its region> min_free_mem=<the least there has been>.
bool cfunc_display_free_mem(script::Struct* /*params*/, script::Script* /*script*/)
{
    memory::Hub* manager = memory::Hub::get_pull();
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    for (memory::MemSource* heap = manager->first_zone(); heap; heap = manager->following_zone(heap)) {
        memory::Zone* region = heap->region();
        Expects(params != nullptr);
        params->get_clear();
        params->put_name_key(checksums::Name{kId}, checksums::hash_from_text(heap->label()));
        params->put_int(checksums::Name{kFreeMem}, region->free_byte_count());
        params->put_int(checksums::Name{kMinFreeMem}, region->min_available());
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
bool cfunc_test(script::Struct* /*params*/, script::Script* /*script*/)
{
    script::SymbolTableEntry entry; // (all zero, as the original's memset of it: every field starts at 0)
    entry.unknown0() = 4;
    entry.set_type(script::QbValueKind::kStruct);
    entry.set_name(kTestSymbolName);
    debug::printf("CSymbolTableEntry:\n");
    print_bytes(&entry, sizeof(entry));
    entry.unknown0() |= 1;
    print_bytes(&entry, sizeof(entry));
    entry.unknown0() |= 2;
    print_bytes(&entry, sizeof(entry));
    entry.unknown0() &= ~4;

    script::Component component{};
    script::construct_component(&component);
    component.type_and_flag_ = kTestComponentType;
    component.unknown2 = kTestComponentUnknown2;
    component.name = kBlaa;
    component.value.set_word(kTestComponentWord);
    debug::printf("CComponent:\n");
    print_bytes(&component, sizeof(component));
    return true;
}

// 0x004493B0: LoadQB "<file>" [LevelSpecific]: the .qb file loaded; LevelSpecific ones are noted in the first free
// slot of the table (by the name's checksum; all 15 taken: not noted, loaded all the same).
bool cfunc_load_qb(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr;
    params->fetch_text(0U, &name, 0);
    if (!params->has_bare_flag(kLevelSpecific)) {
        load_qb_file(name, false);
        return true;
    }
    for (uint32_t& file : script::script_state().level_qb_files()) {
        if (file == 0U) {
            file = checksums::hash_from_text(name);
            break;
        }
    }
    load_qb_file(name, true);
    return true;
}

// 0x00449450: UnloadQB "<file>": the .qb file unloaded, and forgotten by the LevelSpecific table (every slot).
bool cfunc_unload_qb(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr;
    params->fetch_text(0U, &name, 0);
    const uint32_t checksum = checksums::hash_from_text(name);
    unload_qb_file(checksum);
    for (uint32_t& file : script::script_state().level_qb_files()) {
        if (file == checksum) {
            file = 0;
        }
    }
    return true;
}

// 0x004494A0: ScreenShot [FileName="<name>" ("screen")].
bool cfunc_screen_shot(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr;
    if (!params->fetch_text(kFileName, &name, 0)) {
        name = "screen";
    }
    take_screen_shot(name, 0);
    return true;
}

// 0x004494E0: ToggleMetrics.
bool cfunc_toggle_metrics(script::Struct* /*params*/, script::Script* /*script*/)
{
    toggle_metrics_display();
    return true;
}

// 0x004494F0 / 0x00449500 / 0x00449510 / 0x00449520 / 0x00449530: German / French / Spanish / Italian / English:
// whether the game runs in that language.
bool cfunc_german(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_language() == core::kLanguageGerman;
}
bool cfunc_french(script::Struct* /*params*/, script::Script* /*script*/) { return get_language() == 3; }
bool cfunc_spanish(script::Struct* /*params*/, script::Script* /*script*/) { return get_language() == 4; }
bool cfunc_italian(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_language() == core::kLanguageItalian;
}
bool cfunc_english(script::Struct* /*params*/, script::Script* /*script*/) { return get_language() == 2; }

// 0x004498F0: ProximCleanup: every sound and stream component's proximity node cleared (what they keep of
// the proximity nodes), then every proximity node freed.
bool cfunc_proxim_cleanup(script::Struct* /*params*/, script::Script* /*script*/)
{
    for (objects::ObjectModule* sound_module = objects::object_hub_slot()->find_first_module_of_kind(kSound);
         sound_module; sound_module = sound_module->next_of_type()) {
        core::down_cast<objects::AudioEmitterModule>(sound_module)->set_proxim(nullptr);
    }
    for (objects::ObjectModule* stream_module = objects::object_hub_slot()->find_first_module_of_kind(kStream);
         stream_module; stream_module = stream_module->next_of_type()) {
        core::down_cast<objects::AudioStreamModule>(stream_module)->set_proxim(nullptr);
    }
    objects::nearby_zone_shutdown();
    return true;
}

// 0x00449950: Inside: whether the proximity node script running now was started by entering (not leaving). (No
// check that the proximity manager exists.)
bool cfunc_inside(script::Struct* /*params*/, script::Script* /*script*/)
{
    return objects::proximity_hub()->entering_;
}

// 0x00449960: ResetEngine: the main loop ends after this frame (and the game starts again).
bool cfunc_reset_engine(script::Struct* /*params*/, script::Script* /*script*/)
{
    assets::get_main_loop_manager()->stop_loop();
    return true;
}

} // namespace commands
