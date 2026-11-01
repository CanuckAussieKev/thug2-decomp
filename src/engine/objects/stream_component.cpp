// The stream component (engine/objects/stream_component.h).

#include "engine/objects/stream_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/proximity.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

uint32_t g_stream_component_member_functions[30]{0xDB05A233, 0x0F1641E3, 0, 0, 0, 0, 0xC66E1B17, 0x0F1641E3, 0, 0, 0, 0,
                                                 0x82EC1DDD, 0x0F1641E3, 0, 0, 0, 0, 0xA738AEB8, 0x0F1641E3, 0, 0, 0, 0,
                                                 0,          0x0F1641E3, 0, 0, 0, 0};

namespace {

constexpr float kInchesPerFoot = 12.0f; // 0x0064546C

const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }

} // namespace

// 0x00433D90
memory::owner<ObjectModule*> __cdecl AudioStreamModule::fabricate() { return new AudioStreamModule; }

// 0x00433870
void __cdecl AudioStreamModule::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_stream_component_member_functions, 0x18);
}

// 0x00433C10
AudioStreamModule::AudioStreamModule()
{
    spewer_ = nullptr;
    proxim_ = nullptr;
    kind_tag_ = kType;
}

// 0x00433890 (0x00433C40 the deleting one)
AudioStreamModule::~AudioStreamModule()
{
    for (uint32_t stream : streams_) {
        if (stream)
            audio::halt_stream(stream);
    }
}

// 0x00433900
void AudioStreamModule::configure_from_data(script::Struct* params)
{
    uint32_t node = reinterpret_cast<uint32_t>(params); // (read into the parameters' own argument slot)
    if (params->read_hash_value(0x08470F2E, &node, 0))  // ProximNode
        proxim_ = proxim_get_node(node);
}

// 0x00433CD0
maths::Vec4* AudioStreamModule::get_stream_position(maths::Vec4* out, void* camera)
{
    if (spewer_ && camera) {
        spewer_->nearest_point_on(out, viewer_location(camera));
        return out;
    }
    *out = entity_->at_point_;
    return out;
}

// 0x00433D30
bool AudioStreamModule::get_stream_extent(void* camera, maths::Vec4* out)
{
    if (!camera || !proxim_)
        return false;
    maths::Vec4 nearest;
    *out = *proxim_->nearest_point_on(&nearest, viewer_location(camera));
    return true;
}

// 0x00433970
bool AudioStreamModule::play_stream(script::Struct* params, script::Script* /*script*/)
{
    bool room = false;
    for (uint32_t stream : streams_)
        room = room || !stream;
    if (!room)
        return false;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    int priority = 50, loops = 0, falloff = 0;
    uint32_t id = 0;
    params->fetch_float(0xF6A36814, &volume, 0);       // vol
    params->fetch_float(0xD8604126, &pitch, 0);        // pitch
    params->fetch_int_value(0x9D5923D8, &priority, 0); // priority
    params->fetch_int_value(0xE46111E0, &loops, 0);    // num_loops
    params->read_hash_value(0x40C698AF, &id, 0);       // id
    if (params->fetch_float(0xFF2020EC, &dropoff, 0))  // dropoff (feet)
        dropoff = dropoff * kInchesPerFoot;
    uint32_t function = 0;                                 // (uninitialised in the original: only read when found)
    if (params->read_hash_value(0x0C6AC50A, &function, 0)) // dropoff_function
        falloff = sound::falloff_from_checksum(function);
    uint32_t emitter = 0;
    if (params->read_hash_value(0x8A7132CE, &emitter, 0)) // emitter
        spewer_ = emitter_get_node(emitter);
    else
        spewer_ = nullptr;
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    int use_pos_info = 1;                                  // (read into the parameters' own argument slot)
    params->fetch_int_value(0xC9B0DEB7, &use_pos_info, 0); // use_pos_info
    return audio::play_stream_at_object(this, stream, dropoff, volume, pitch, loops, priority, use_pos_info, falloff,
                                        id) != 0;
}

// 0x00433B30
bool AudioStreamModule::add_stream(script::Struct* params, script::Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    uint32_t name = 0;                                  // (read into the parameters' own argument slot, cleared first)
    if (!params->read_hash_value(0x02E58C18, &name, 0)) // Add
        return false;
    return audio::add_stream(stream, name, this);
}

// 0x00433BA0
bool AudioStreamModule::remove_stream(script::Struct* params, script::Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    uint32_t name = 0;                                  // (read into the parameters' own argument slot, cleared first)
    if (!params->read_hash_value(0x977FE2CF, &name, 0)) // remove
        return false;
    return audio::remove_stream(stream, name, this);
}

// 0x00433C70
bool AudioStreamModule::obj_play_stream(script::Struct* params, script::Script* script)
{
    play_stream(params, script);
    return true;
}

// 0x00433C90
bool AudioStreamModule::obj_add_stream(script::Struct* params, script::Script* script)
{
    add_stream(params, script);
    return true;
}

// 0x00433CB0
bool AudioStreamModule::obj_remove_stream(script::Struct* params, script::Script* script)
{
    remove_stream(params, script);
    return true;
}

// 0x00433930
bool AudioStreamModule::obj_stop_stream(script::Struct* /*params*/, script::Script* /*script*/)
{
    for (uint32_t& stream : streams_) {
        if (stream) {
            audio::halt_stream(stream);
            stream = 0;
        }
    }
    return true;
}

} // namespace objects
