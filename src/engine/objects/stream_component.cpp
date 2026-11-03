// The stream component (engine/objects/stream_component.h).

#include "engine/objects/stream_component.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

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
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"
#include "engine/maths/maths.h"
namespace objects {
namespace {
constexpr int kDefaultPriority = 50;
constexpr uint32_t kObjPlayStream = 0xDB05A233; // Obj_PlayStream
constexpr uint32_t kStream = 0x0F1641E3; // Stream
constexpr uint32_t kObjAddStream = 0xC66E1B17; // Obj_AddStream
constexpr uint32_t kObjRemoveStream = 0x82EC1DDD; // Obj_RemoveStream
constexpr uint32_t kObjStopStream = 0xA738AEB8; // Obj_StopStream
constexpr uint32_t kProximNode = 0x08470F2E; // ProximNode
constexpr uint32_t kVol = 0xF6A36814; // Vol
constexpr uint32_t kPitch = 0xD8604126; // Pitch
constexpr uint32_t kPriority = 0x9D5923D8; // Priority
constexpr uint32_t kNumLoops = 0xE46111E0; // num_loops
constexpr uint32_t kId = 0x40C698AF; // id
constexpr uint32_t kDropoff = 0xFF2020EC; // Dropoff
constexpr uint32_t kDropoffFunction = 0x0C6AC50A; // dropoff_function
constexpr uint32_t kEmitter = 0x8A7132CE; // Emitter
constexpr uint32_t kUsePosInfo = 0xC9B0DEB7; // use_pos_info
constexpr uint32_t kAdd = 0x02E58C18; // Add
constexpr uint32_t kRemove = 0x977FE2CF; // Remove
} // namespace

std::array<script::MemberFunctionEntry, kStreamComponentMemberFunctionCount>& stream_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, kStreamComponentMemberFunctionCount> stream_component_member_functions{{
        {.callsign_ = kObjPlayStream, .owner_kind_ = kStream}, {.callsign_ = kObjAddStream, .owner_kind_ = kStream}, {.callsign_ = kObjRemoveStream, .owner_kind_ = kStream}, {.callsign_ = kObjStopStream, .owner_kind_ = kStream},
        {.callsign_ = 0, .owner_kind_ = kStream}}};
    return stream_component_member_functions;
}

namespace {


const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }

} // namespace

// 0x00433D90
gsl::owner<ObjectModule*> AudioStreamModule::fabricate() { return new AudioStreamModule; }

// 0x00433870
void AudioStreamModule::publish_method_table()
{
    script::register_member_function_table(stream_component_member_functions());
}

// 0x00433C10
AudioStreamModule::AudioStreamModule() { set_kind_tag(kType); }

// 0x00433890 (0x00433C40 the deleting one)
AudioStreamModule::~AudioStreamModule()
{
    for (const uint32_t stream : streams_) {
        if (stream)
            audio::halt_stream(stream);
    }
}

// 0x00433900
void AudioStreamModule::configure_from_data(script::Struct* params)
{
    uint32_t node = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    if (params->read_hash_value(kProximNode, &node, 0))  // ProximNode
        proxim_ = proxim_get_node(node);
}

// 0x00433CD0
maths::Vec4* AudioStreamModule::get_stream_position(maths::Vec4* out, void* camera)
{
    if (spewer_ && camera) {
        nearest_point_on(*spewer_, out, viewer_location(camera));
        return out;
    }
    *out = entity()->at_point();
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
    for (const uint32_t stream : streams_)
        room = room || !stream;
    if (!room)
        return false;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    int priority = kDefaultPriority, loops = 0, falloff = 0;
    uint32_t id = 0;
    params->fetch_float(kVol, &volume, 0);       // vol
    params->fetch_float(kPitch, &pitch, 0);        // pitch
    params->fetch_int_value(kPriority, &priority, 0); // priority
    params->fetch_int_value(kNumLoops, &loops, 0);    // num_loops
    params->read_hash_value(kId, &id, 0);       // id
    if (params->fetch_float(kDropoff, &dropoff, 0))  // dropoff (feet)
        dropoff = dropoff * maths::kInchesPerFoot;
    uint32_t function = 0;                                 // (uninitialised in the original: only read when found)
    if (params->read_hash_value(kDropoffFunction, &function, 0)) // dropoff_function
        falloff = sound::falloff_from_checksum(function);
    uint32_t emitter = 0;
    if (params->read_hash_value(kEmitter, &emitter, 0)) // emitter
        spewer_ = emitter_get_node(emitter);
    else
        spewer_ = nullptr;
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    int use_pos_info = 1;                                  // (read into the parameters' own argument slot)
    params->fetch_int_value(kUsePosInfo, &use_pos_info, 0); // use_pos_info
    return audio::play_stream_at_object(this, checksums::Name{stream}, {.dropoff = dropoff, .volume = volume, .pitch = pitch, .option = loops, .priority = priority, .use_pos_info_ = use_pos_info, .falloff_curve_ = falloff},
                                        id) != 0;
}

// 0x00433B30
bool AudioStreamModule::add_stream(script::Struct* params, script::Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    uint32_t name = 0;                                  // (read into the parameters' own argument slot, cleared first)
    if (!params->read_hash_value(kAdd, &name, 0)) // Add
        return false;
    return audio::add_stream({.stream = stream, .name = name}, this);
}

// 0x00433BA0
bool AudioStreamModule::remove_stream(script::Struct* params, script::Script* /*script*/)
{
    uint32_t stream = 0;
    if (!params->read_hash_value(0u, &stream, 0))
        return false;
    uint32_t name = 0;                                  // (read into the parameters' own argument slot, cleared first)
    if (!params->read_hash_value(kRemove, &name, 0)) // remove
        return false;
    return audio::remove_stream({.id_or_name_ = stream, .entry = name}, this);
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
