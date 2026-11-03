// NOT ORIGINAL - the level editor's entities (ours). See level_editor_entities.h.

#include "editor/level_editor_entities.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/mat4.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/load.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/vec4.h"

namespace frontend::level_editor::entities {

// --- the pure parts -----------------------------------------------------------------------------------------------

std::string set_stem(const std::string& name)
{
    size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(core::at(name, end - 1))))
        --end;
    while (end > 0 && core::at(name, end - 1) == '_')
        --end;
    return end == 0 ? name : name.substr(0, end);
}

V3 pin_anchor_point(const std::vector<V3>& places)
{
    if (places.empty())
        return {.x = 0.0f, .y = 0.0f, .z = 0.0f};
    float min_x = core::at(places, 0).x, max_x = core::at(places, 0).x, min_z = core::at(places, 0).z, max_z = core::at(places, 0).z, min_y = core::at(places, 0).y;
    for (const V3& p : places) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_z = std::min(min_z, p.z);
        max_z = std::max(max_z, p.z);
        min_y = std::min(min_y, p.y);
    }
    return {.x = (min_x + max_x) * 0.5f, .y = min_y, .z = (min_z + max_z) * 0.5f};
}

V3 turn_box_extents(V3 extents, float turn)
{
    double c = std::fabs(std::cos(static_cast<double>(turn))), s = std::fabs(std::sin(static_cast<double>(turn)));
    // (a quarter turn exactly: x and z swapped - cos(pi/2) in floats isn't 0)
    if (c < 1e-6)
        c = 0.0;
    if (s < 1e-6)
        s = 0.0;
    return {.x = static_cast<float>(c * extents.x + s * extents.z), .y = extents.y,
            .z = static_cast<float>(s * extents.x + c * extents.z)};
}

ParticleEmitter turn_particle(const ParticleEmitter& origin, V3 from, V3 pos, float turn)
{
    ParticleEmitter out = origin;
    if (origin.has_mid_)
        out.mid = pos + turn_about_up(origin.mid - from, turn);
    if (origin.has_end_)
        out.end = pos + turn_about_up(origin.end - from, turn);
    for (int i = 0; i < 3; ++i) {
        if (core::at(origin.has_box_, i))
            core::at(out.box, i) = turn_box_extents(core::at(origin.box, i), turn);
    }
    return out;
}

namespace {
float len_of(V3 v) { return std::sqrt(get_dot(v, v)); }
} // namespace

float path_length(const std::vector<V3>& points, bool closed)
{
    float total = 0.0f;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        total += len_of(core::at(points, i + 1) - core::at(points, i));
    if (closed && points.size() > 2)
        total += len_of(points.front() - points.back());
    return total;
}

bool path_sample(const std::vector<V3>& points, bool closed, float distance, RayOut sample)
{
    auto* const pos = sample.origin;
    auto* const dir = sample.dir;
    const float total = path_length(points, closed);
    if (points.size() < 2 || !(total > 0.0f))
        return false;
    float d = std::fmod(distance, total);
    if (d < 0.0f)
        d += total;
    const size_t segments = points.size() - 1 + (closed && points.size() > 2 ? 1 : 0);
    for (size_t i = 0; i < segments; ++i) {
        const V3 a = core::at(points, i);
        const V3 b = core::at(points, (i + 1) % points.size());
        const float length = len_of(b - a);
        if (length <= 0.0f)
            continue;
        if (d <= length || i + 1 == segments) {
            const float t = std::min(1.0f, d / length);
            *pos = a + (b - a) * t;
            *dir = (b - a) * (1.0f / length);
            return true;
        }
        d -= length;
    }
    return false;
}

std::string type_key(uint32_t cls, uint32_t type, uint32_t profile, const std::string& model)
{
    std::array<char, 40> text{};
    text::format_to(text, "%08X:%08X:%08X:", cls, type, profile);
    std::string key = text.data();
    for (const char c : model)
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(c == '/' ? '\\' : c)));
    return key;
}

bool same_state(const State& a, const State& b)
{
    if (a.speeds.size() != b.speeds.size() || a.emitters.size() != b.emitters.size())
        return false;
    for (size_t i = 0; i < a.speeds.size(); ++i) {
        if (core::at(a.speeds, i).name != core::at(b.speeds, i).name || core::at(a.speeds, i).mph != core::at(b.speeds, i).mph)
            return false;
    }
    for (size_t i = 0; i < a.emitters.size(); ++i) {
        const EmitterSound &x = core::at(a.emitters, i), &y = core::at(b.emitters, i);
        if (x.name != y.name || x.sound != y.sound || x.volume != y.volume || x.radius != y.radius)
            return false;
    }
    return true;
}

bool vacant(const State& state) { return state.speeds.empty() && state.emitters.empty(); }

const Pace* speed_of(const State& state, uint32_t name)
{
    for (const Pace& s : state.speeds) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

const EmitterSound* emitter_of(const State& state, uint32_t name)
{
    for (const EmitterSound& e : state.emitters) {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}

void set_speed(State* state, uint32_t name, float mph)
{
    auto it = std::ranges::find_if(state->speeds, [&](const Pace& s) { return s.name == name; });
    if (!(mph > 0.0f)) {
        if (it != state->speeds.end())
            state->speeds.erase(it);
        return;
    }
    mph = std::clamp(mph, kMinMph, kMaxMph);
    if (it != state->speeds.end())
        it->mph = mph;
    else
        state->speeds.push_back({.name = name, .mph = mph});
}

void set_emitter(State* state, const EmitterSound& emitter)
{
    auto it = std::ranges::find_if(state->emitters, [&](const EmitterSound& e) { return e.name == emitter.name; });
    if (!emitter.sound && emitter.radius <= 0.0f) {
        if (it != state->emitters.end())
            state->emitters.erase(it);
        return;
    }
    EmitterSound e = emitter;
    e.volume = std::clamp(e.volume, kMinVolume, kLoudest);
    if (e.radius > 0.0f)
        e.radius = std::clamp(e.radius, kMinRadius, kRadiusCeiling);
    else
        e.radius = 0.0f;
    if (it != state->emitters.end())
        *it = e;
    else
        state->emitters.push_back(e);
}

namespace {

void get_number(std::ostringstream& out, float value)
{
    std::array<char, 32> text{};
    text::format_to(text, "%.9g", value);
    out << text.data();
}

void get_hex(std::ostringstream& out, uint32_t value)
{
    std::array<char, 12> text{};
    text::format_to(text, "%08X", value);
    out << text.data();
}

bool parse_hex(const std::string& text, uint32_t* out)
{
    if (text.size() != 8)
        return false;
    for (const char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    *out = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
    return true;
}

bool parse_number(const std::string& text, float* out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
        return false;
    *out = static_cast<float>(v);
    return true;
}

} // namespace

std::string write_records(const State& state)
{
    std::ostringstream out;
    for (const Pace& s : state.speeds) {
        out << "speed ";
        get_hex(out, s.name);
        out << ' ';
        get_number(out, s.mph);
        out << '\n';
    }
    for (const EmitterSound& e : state.emitters) {
        out << "emitter ";
        get_hex(out, e.name);
        out << " sound=";
        get_hex(out, e.sound);
        out << " vol=";
        get_number(out, e.volume);
        out << " radius=";
        get_number(out, e.radius);
        out << '\n';
    }
    return out.str();
}

bool is_record(const std::string& kind) { return kind == "speed" || kind == "emitter"; }

bool read_record(const std::string& kind, std::istringstream& in, State* state)
{
    std::string name_text;
    uint32_t name = 0;
    if (!(in >> name_text) || !parse_hex(name_text, &name) || !name)
        return false;
    if (kind == "speed") {
        std::string value;
        float mph = 0.0f;
        if (!(in >> value) || !parse_number(value, &mph) || mph < kMinMph || mph > kMaxMph || speed_of(*state, name))
            return false;
        state->speeds.push_back({.name = name, .mph = mph});
        return true;
    }
    if (kind != "emitter" || emitter_of(*state, name))
        return false;
    EmitterSound e;
    e.name = name;
    std::set<std::string> seen;
    std::string word;
    while (in >> word) {
        if (core::at(word, 0) == '#')
            break; // (a comment: the rest of the line)
        const size_t eq = word.find('=');
        if (eq == std::string::npos || eq == 0)
            return false;
        const std::string key = word.substr(0, eq);
        const std::string value = word.substr(eq + 1);
        if (!seen.insert(key).second)
            return false;
        if (key == "sound") {
            if (!parse_hex(value, &e.sound))
                return false;
        } else if (key == "vol") {
            if (!parse_number(value, &e.volume) || e.volume < kMinVolume || e.volume > kLoudest)
                return false;
        } else if (key == "radius") {
            if (!parse_number(value, &e.radius) ||
                (e.radius != 0.0f && (e.radius < kMinRadius || e.radius > kRadiusCeiling)))
                return false;
        } // (other keys: a later version's - skipped)
    }
    state->emitters.push_back(e);
    return true;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

uint32_t get_key(const char* name) { return checksums::hash_from_text(name); }
const uint32_t kClass = 0x12B4E660, kName = 0xA1DC81F9, kPos = 0x7F261953;
const uint32_t kLevelObject = 0xB7B3BD86, kGameObject = 0xEF59C100, kPedestrian = 0xA0DFAC98, kVehicle = 0xE47F1B79,
               kParticleObject = 0x9E7D469E;

uint32_t k_type()
{
    static const uint32_t kK = get_key("Type");
    return kK;
}
uint32_t k_profile()
{
    static const uint32_t kK = get_key("profile");
    return kK;
}
uint32_t k_model()
{
    static const uint32_t kK = get_key("model");
    return kK;
}
uint32_t k_bouncy()
{
    static const uint32_t kK = get_key("bouncy");
    return kK;
}
uint32_t k_texture()
{
    static const uint32_t kK = get_key("Texture");
    return kK;
}
uint32_t k_waypoint()
{
    static const uint32_t kK = get_key("Waypoint");
    return kK;
}
uint32_t k_radius()
{
    static const uint32_t kK = get_key("Radius");
    return kK;
}
uint32_t k_ed_sound()
{
    static const uint32_t kK = get_key("ed_sound");
    return kK;
}
uint32_t k_ed_volume()
{
    static const uint32_t kK = get_key("ed_volume");
    return kK;
}
uint32_t k_ed_path_speed()
{
    static const uint32_t kK = get_key("ed_path_speed");
    return kK;
}
uint32_t k_set_path_velocity()
{
    static const uint32_t kK = get_key("Obj_SetPathVelocity");
    return kK;
}
uint32_t k_mph()
{
    static const uint32_t kK = get_key("mph");
    return kK;
}

double get_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

script::Struct* look_up_node_params(uint32_t name)
{
    return name && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
}

uint32_t class_of(script::Struct* node)
{
    uint32_t cls = 0;
    if (node)
        node->read_hash_value(kClass, &cls, 0);
    return cls;
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags() & objects::kObjectFlagDead) ? object : nullptr;
}

std::string model_of(script::Struct* node)
{
    const char* model = nullptr;
    return node && node->fetch_text(k_model(), &model, 0) && model ? std::string(model) : std::string();
}

bool in_level() { return level_nodes::node_manager_instance() != nullptr && !nodes::get_all().empty(); }

// A node of the editor's copies' array whose copy was deleted (its node stays until the level goes): not a ghost.
bool deleted_copy(const nodes::Elem& node)
{
    static const uint32_t kCopies = checksums::hash_from_text("level_editor_copy_nodes");
    return nodes::get_array_name(node) == kCopies && !object_edit::is_copy(node.name);
}

// NOT ORIGINAL grouping - level_editor_entities.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_entities_state()).
struct LevelEditorEntitiesState {
    // The state the layout keeps, the baked fields' (read when a level loads; not saved), the undo step under way.
    State state_{};
    State baked_{};
    State undo_before_{};
    bool have_undo_{};
    bool ui_active_{}; // (one of the Inspector's fields is being dragged: the undo step waits)
    std::vector<std::function<void()>> queue_{};
    int level_loads_ = -1;
    bool scanned_{};
};

LevelEditorEntitiesState& level_editor_entities_state()
{
    static core::NoDestroy<LevelEditorEntitiesState> state;
    return state.get();
}

// Sounds playing on emitters.
struct Playing {
    uint32_t name, sound, id;
};
// NOT ORIGINAL grouping - the entities playing and the next slow tick (were globals). Made on first use, never
// destroyed (entity_playback()).
struct EntityPlayback {
    std::vector<Playing> playing_{};
    double next_slow_tick_{};
};

EntityPlayback& entity_playback()
{
    static core::NoDestroy<EntityPlayback> state;
    return state.get();
}

// Speeds put on objects (the object each was put on, so one made again gets it again).
std::unordered_map<uint32_t, std::pair<const void*, float>>& speed_applied()
{
    static std::unordered_map<uint32_t, std::pair<const void*, float>> value;
    return value;
}

// Objects made again: killed, and once the game has removed them, made from their nodes (put back where they were).
struct Remake {
    uint32_t name;
    bool keep;
    object_edit::State state;
    double due;
    bool killed;
};
std::vector<Remake>& remakes()
{
    static core::NoDestroy<std::vector<Remake>> remakes;
    return remakes.get();
}

// A particle node's emitter as it was first seen (the level's - or a copy's as made), the place and heading it was at
// then, and the place / heading it was last made for (map4: turned too).
struct ParticleOrigin {
    ParticleEmitter emitter;
    V3 from = {};
    float from_yaw_ = 0.0f;
    V3 at = {};
    float at_yaw_ = 0.0f;
};
std::unordered_map<uint32_t, ParticleOrigin>& particle_at()
{
    static std::unordered_map<uint32_t, ParticleOrigin> value;
    return value;
}

// Objects whose model the editor swapped: the model each was made with.
std::unordered_map<uint32_t, std::string>& made_with()
{
    static std::unordered_map<uint32_t, std::string> value;
    return value;
}

// The preview.
struct Preview {
    uint32_t id = 0;
    object_edit::State saved;
    std::vector<V3> points;
    bool closed = false;
    float distance = 0.0f;
    bool playing = false;
};
// NOT ORIGINAL grouping - the entity preview, its last tick and whether uncreated ones show (were globals). Made on
// first use, never destroyed (entity_preview()).
struct EntityPreview {
    Preview preview_{};
    double last_tick_{};
    bool show_uncreated_{};
};

EntityPreview& entity_preview()
{
    static core::NoDestroy<EntityPreview> state;
    return state.get();
}

// The emitters' state in effect: the layout's over the baked fields.
std::vector<EmitterSound> effective_emitters()
{
    std::vector<EmitterSound> out = level_editor_entities_state().state_.emitters;
    for (const EmitterSound& e : level_editor_entities_state().baked_.emitters) {
        if (!emitter_of(level_editor_entities_state().state_, e.name))
            out.push_back(e);
    }
    return out;
}
std::vector<Pace> effective_speeds()
{
    std::vector<Pace> out = level_editor_entities_state().state_.speeds;
    for (const Pace& s : level_editor_entities_state().baked_.speeds) {
        if (!speed_of(level_editor_entities_state().state_, s.name))
            out.push_back(s);
    }
    return out;
}

void note_undo()
{
    if (!level_editor_entities_state().have_undo_) {
        level_editor_entities_state().undo_before_ = level_editor_entities_state().state_;
        level_editor_entities_state().have_undo_ = true;
    }
}

void queue_remake(uint32_t name, bool keep, double delay)
{
    objects::GameEntity* object = live_object(name);
    if (!object)
        return; // (nothing made of it now: its node is what a later create reads)
    for (Remake& r : remakes()) {
        if (r.name == name && !r.killed) {
            r.due = get_now() + delay;
            if (keep)
                r.state = object_edit::store_away(object);
            return;
        }
    }
    const Remake r = {.name = name, .keep = keep, .state = keep ? object_edit::store_away(object) : object_edit::State{}, .due = get_now() + delay,
                      .killed = false};
    remakes().push_back(r);
}

void run_remakes()
{
    const double now = get_now();
    for (auto it = remakes().begin(); it != remakes().end();) {
        Remake& r = *it;
        if (!r.killed) {
            if (now < r.due) {
                ++it;
                continue;
            }
            if (objects::GameEntity* object = live_object(r.name)) {
                if (r.keep)
                    r.state = object_edit::store_away(object);
                object->flag_as_dead();
            }
            r.killed = true;
        }
        if (objects::find_object(r.name)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (script::Struct* node = look_up_node_params(r.name)) {
            commands::create_from_node(node);
            if (r.keep)
                object_edit::get_restore(r.state);
            speed_applied().erase(r.name); // (its speed put on it again)
            if (debug_enabled())
                get_log("level editor: %s made again from its node", object_edit::caption(r.name).c_str());
        }
        it = remakes().erase(it);
    }
}

// --- sounds on emitters ---

sound::AudioMixer* audio_mixer_handle() { return sound::get_audio_mixer(); }
void* nearest_camera(const maths::Vec4* position, float* distance)
{
    return render::fetch_nearest_camera(position, distance);
}
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
objects::EmitterNode* get_emitter(uint32_t name)
{
    return objects::emitter_table() ? objects::emitter_get_node(name) : nullptr;
}

void stop_playing(Playing* p)
{
    if (sound::AudioMixer* m = audio_mixer_handle(); m && p->id && !sound::no_sound())
        m->stop_sound(p->id);
    p->id = 0;
}

void stop_all_sounds()
{
    for (Playing& p : entity_playback().playing_)
        stop_playing(&p);
    entity_playback().playing_.clear();
}

void update_sounds()
{
    sound::AudioMixer* m = audio_mixer_handle();
    std::vector<EmitterSound> emitters = effective_emitters();
    const bool off = !m || sound::no_sound() || !in_level() || layout::network_active();
    // (those no longer wanted, or of another sound now: stopped)
    for (auto it = entity_playback().playing_.begin(); it != entity_playback().playing_.end();) {
        auto e = std::ranges::find_if(emitters, [&](const EmitterSound& x) { return x.name == it->name; });
        if (off || e == emitters.end() || e->sound != it->sound) {
            stop_playing(&*it);
            it = entity_playback().playing_.erase(it);
        } else {
            ++it;
        }
    }
    if (off)
        return;
    for (const EmitterSound& e : emitters) {
        if (!e.sound)
            continue;
        auto p = std::ranges::find_if(entity_playback().playing_, [&](const Playing& x) { return x.name == e.name; });
        if (p == entity_playback().playing_.end()) {
            entity_playback().playing_.push_back({.name = e.name, .sound = e.sound, .id = 0});
            p = entity_playback().playing_.end() - 1;
        }
        objects::EmitterNode* emitter = get_emitter(e.name);
        const sound::SoundWaveSlot* wave = m->fetch_wave_slot(e.sound);
        if (!emitter || !emitter->active_ || !wave || m->is_frozen()) {
            stop_playing(&*p);
            continue;
        }
        // As a sound played from an emitter: from the shape's nearest point to the listener, the sound's own dropoff.
        const maths::Vec4 centre = emitter->at_point_;
        void* camera = nearest_camera(&centre, nullptr);
        if (!camera) {
            stop_playing(&*p);
            continue;
        }
        const maths::Vec4 listener = *viewer_location(camera);
        maths::Vec4 heard = centre;
        if (emitter->form_ != objects::kEmitterShapeOther)
            nearest_point_on(*emitter, &heard, &listener);
        sound::VolumeRef volume;
        if (!m->fetch_loudness(&volume, &heard, {.dropoff = wave->dropoff, .falloff = 0}, camera, nullptr)) {
            stop_playing(&*p); // (out of range)
            continue;
        }
        volume.percent_tweak(e.volume);
        if (p->id && m->is_sound_playing(p->id, nullptr)) {
            m->set_sound_params_scaled(p->id, &volume, 100.0f);
        } else if (!volume.is_audio_off()) {
            p->id = m->emit_sound(e.sound, &volume, {.pitch = 100.0f, .unique_id_ = e.name}, nullptr, 0); // (again when it stopped: a loop)
        } else {
            p->id = 0;
        }
    }
}

// A sphere emitter's radius kept as the state says (its node's Radius, the emitter manager's copy - an added emitter's
// node is made again from its spec when the overlay changes).
void apply_radius(const EmitterSound& e)
{
    if (e.radius <= 0.0f)
        return;
    if (objects::EmitterNode* emitter = get_emitter(e.name);
        emitter && emitter->form_ == objects::kEmitterShapeSphere && emitter->radius_ != e.radius) {
        emitter->radius_ = e.radius;
        emitter->reach_sq_ = e.radius * e.radius;
    }
    if (script::Struct* node = look_up_node_params(e.name); node && !node_create::is_added(e.name)) {
        float now = 0.0f;
        if (!node->fetch_float(k_radius(), &now, 0) || now != e.radius) {
            node->erase_module(k_radius());
            node->push_float(checksums::Name{k_radius()}, e.radius);
        }
    }
}

// --- speeds ---

// Our script LevelEditor_PathSpeed - `Obj_SetPathVelocity <speed> mph` - defined (once) by the game's loader from QB
// made here (as level_editor_triggers.cpp defines its trigger scripts), so the member function runs from a script of
// the object's, as a level's scripts run it.
uint32_t path_speed_script()
{
    static const uint32_t kName = get_key("LevelEditor_PathSpeed");
    if (script::find_symbol(kName))
        return kName;
    std::vector<uint8_t> qb;
    auto put4 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            qb.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    auto name4 = [&](uint32_t v) {
        qb.push_back(0x16);
        put4(v);
    };
    qb.push_back(0x23); // script
    name4(kName);
    qb.push_back(0x01);
    name4(k_set_path_velocity());
    qb.push_back(0x2D); // <speed>
    name4(get_key("speed"));
    name4(k_mph());
    qb.push_back(0x01);
    qb.push_back(0x24); // endscript
    qb.push_back(0x01);
    for (const char* text : {"LevelEditor_PathSpeed", "Obj_SetPathVelocity", "speed", "mph"}) {
        qb.push_back(0x2B);
        put4(get_key(text));
        qb.insert(qb.end(), std::string_view(text).begin(), std::string_view(text).end());
        qb.push_back(0x00);
    }
    qb.push_back(0x00);
    script::load_qb_data("level_editor_entities.qb", qb, 0, 0, 0);
    return kName;
}

void apply_speed(objects::GameEntity* object, float mph)
{
    gsl::owner<script::Struct*> params = script::new_struct();
    params->push_float(checksums::Name{get_key("speed")}, mph);
    script::launch_script(path_speed_script(), params, object, false, nullptr);
    params->get_clear();
    script::delete_struct(params);
}

void update_speeds()
{
    if (!in_level() || layout::network_active())
        return;
    for (const Pace& s : effective_speeds()) {
        objects::GameEntity* object = live_object(s.name);
        if (!object)
            continue;
        auto it = speed_applied().find(s.name);
        if (it != speed_applied().end() && it->second.first == object && it->second.second == s.mph)
            continue;
        apply_speed(object, s.mph);
        speed_applied()[s.name] = {object, s.mph};
        if (debug_enabled())
            get_log("level editor: %s's path speed %.1f mph", object_edit::caption(s.name).c_str(), s.mph);
    }
}

// The baked fields (tools/game/apply_layout.ts): ed_sound / ed_volume on emitters, ed_path_speed on moving objects.
void scan_baked()
{
    level_editor_entities_state().baked_ = State{};
    if (layout::network_active())
        return;
    for (const nodes::Elem& n : nodes::get_all()) {
        script::Struct* s = nodes::struct_of(n);
        if (!s || !n.name)
            continue;
        uint32_t sound = 0;
        if (n.family == nodes::Family::kSound && s->read_hash_value(k_ed_sound(), &sound, 0) && sound) {
            EmitterSound e;
            e.name = n.name;
            e.sound = sound;
            float volume = 100.0f;
            if (s->fetch_float(k_ed_volume(), &volume, 0))
                e.volume = std::clamp(volume, kMinVolume, kLoudest);
            level_editor_entities_state().baked_.emitters.push_back(e);
        }
        float mph = 0.0f;
        if (s->fetch_float(k_ed_path_speed(), &mph, 0) && mph >= kMinMph && mph <= kMaxMph)
            level_editor_entities_state().baked_.speeds.push_back({.name = n.name, .mph = mph});
    }
    if (!vacant(level_editor_entities_state().baked_))
        get_log("level editor: the level's baked fields: %u emitter sounds, %u path speeds",
                static_cast<unsigned>(level_editor_entities_state().baked_.emitters.size()), static_cast<unsigned>(level_editor_entities_state().baked_.speeds.size()));
}

// --- the preview ---

// The path an object's node starts: its first link, then each waypoint's first link (a loop closes it).
bool path_of(uint32_t name, std::vector<V3>* points, bool* closed)
{
    points->clear();
    *closed = false;
    script::Struct* node = look_up_node_params(name);
    if (!node || level_nodes::get_link_count(node) == 0)
        return false;
    uint32_t next = level_nodes::fetch_link(node, 0);
    std::vector<uint32_t> seen;
    while (next && points->size() < 512) {
        if (std::ranges::find(seen, next) != seen.end()) {
            *closed = next == seen.front();
            break;
        }
        script::Struct* point = look_up_node_params(next);
        if (!point || class_of(point) != k_waypoint())
            break;
        maths::Vec4 pos;
        level_nodes::fetch_place(point, &pos);
        points->push_back({.x = pos.x, .y = pos.y, .z = pos.z});
        seen.push_back(next);
        next = level_nodes::get_link_count(point) ? level_nodes::fetch_link(point, 0) : 0;
    }
    return points->size() >= 2;
}

float preview_mph(uint32_t name)
{
    for (const Pace& s : effective_speeds()) {
        if (s.name == name)
            return s.mph;
    }
    const uint32_t cls = class_of(look_up_node_params(name));
    return cls == kVehicle ? 25.0f : cls == kPedestrian ? 3.0f : 10.0f;
}

float current_heading(const objects::GameEntity* object)
{
    const maths::Vec4& at = core::at(object->basis_frame().row, 2);
    return std::atan2(at.x, at.z);
}

void preview_place(float distance)
{
    objects::GameEntity* object = live_object(entity_preview().preview_.id);
    V3 pos{}, dir{};
    if (!object || !path_sample(entity_preview().preview_.points, entity_preview().preview_.closed, distance, {.origin = &pos, .dir = &dir}))
        return;
    object_edit::get_place(object, pos);
    if (dir.x != 0.0f || dir.z != 0.0f)
        object_edit::turn_about(object, pos, std::atan2(dir.x, dir.z) - current_heading(object));
}

void preview_start(uint32_t id)
{
    stop_preview();
    objects::GameEntity* object = live_object(id);
    if (!object || !path_of(id, &entity_preview().preview_.points, &entity_preview().preview_.closed))
        return;
    entity_preview().preview_.id = id;
    entity_preview().preview_.saved = object_edit::store_away(object);
    entity_preview().preview_.distance = 0.0f;
    entity_preview().preview_.playing = true;
    preview_place(0.0f);
}

void preview_tick(double seconds)
{
    if (!entity_preview().preview_.id || !entity_preview().preview_.playing)
        return;
    entity_preview().preview_.distance += mph_to_units(preview_mph(entity_preview().preview_.id)) * static_cast<float>(std::min(seconds, 0.1));
    preview_place(entity_preview().preview_.distance);
}

// --- the palette ---

enum class SourceKind : uint8_t { kDupFrom, kAssignValue, kSound };
struct Source {
    SourceKind kind;
    std::vector<uint32_t> names; // (copies: the node(s) copied)
    uint32_t sound = 0;
};
// NOT ORIGINAL grouping - the entity palette, its sources and models and their generations (were globals). Made on
// first use, never destroyed (entity_palette()).
struct EntityPalette {
    std::vector<PaletteEntry> palette_{};
    std::vector<Source> sources_{};
    uint32_t palette_generation_ = 0xFFFFFFFF;
    // Loaded models (the model= of every object the level has made), for the model swap.
    std::vector<std::string> models_{};
    uint32_t models_generation_ = 0xFFFFFFFF;
};

EntityPalette& entity_palette()
{
    static core::NoDestroy<EntityPalette> state;
    return state.get();
}

const char* short_name(const std::string& path)
{
    const char* text = path.c_str();
    const char* base = std::max(std::strrchr(text, '\\'), std::strrchr(text, '/'));
    return base ? std::next(base) : text;
}

void build_palette()
{
    entity_palette().palette_.clear();
    entity_palette().sources_.clear();
    if (!in_level())
        return;
    std::set<std::string> types;
    std::map<std::string, std::vector<uint32_t>> sets;
    std::vector<std::pair<std::string, Source>> peds, cars, particles;
    for (const nodes::Elem& n : nodes::get_all()) {
        script::Struct* s = nodes::struct_of(n);
        if (!s || !n.name || n.hidden || node_create::is_added(n.name) || object_edit::is_copy(n.name))
            continue;
        const uint32_t cls = class_of(s);
        if (cls == kPedestrian || cls == kVehicle) {
            if (!live_object(n.name)) // (only types the level has made: their assets are loaded)
                continue;
            uint32_t type = 0, profile = 0;
            s->read_hash_value(k_type(), &type, 0);
            s->read_hash_value(k_profile(), &profile, 0);
            const std::string model = model_of(s);
            if (!types.insert(type_key(cls, type, profile, model)).second)
                continue;
            std::string label = cls == kPedestrian ? "Ped: " : "Car: ";
            label += nodes::caption(type);
            if (profile && profile != type)
                label += " / " + nodes::caption(profile);
            if (!model.empty())
                label += std::string(" (") + short_name(model) + ")";
            label += "  like " + nodes::caption(n.name);
            (cls == kPedestrian ? peds : cars).emplace_back(label, Source{.kind = SourceKind::kDupFrom, .names = {n.name}});
        } else if ((cls == kLevelObject || cls == kGameObject) && s->has_bare_flag(k_bouncy()) && live_object(n.name)) {
            sets[set_stem(nodes::caption(n.name))].push_back(n.name);
        } else if (cls == kParticleObject) {
            uint32_t type = 0, texture = 0;
            s->read_hash_value(k_type(), &type, 0);
            s->read_hash_value(k_texture(), &texture, 0);
            if (!types.insert(type_key(cls, type, texture, "")).second)
                continue;
            const std::string label =
                "Particles: " + nodes::caption(texture ? texture : type) + "  like " + nodes::caption(n.name);
            particles.emplace_back(label, Source{.kind = SourceKind::kDupFrom, .names = {n.name}});
        }
    }
    auto add = [](std::vector<std::pair<std::string, Source>>& list, uint32_t colour) {
        std::ranges::sort(list, [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& e : list) {
            entity_palette().palette_.push_back({.label = e.first, .colour = colour, .why = ""});
            entity_palette().sources_.push_back(e.second);
        }
    };
    add(peds, 0xFFE0A0FF);
    add(cars, 0xFF60B0FF);
    for (auto& [stem, names] : sets) {
        if (names.size() < 2)
            continue; // (a single bouncy object: the browser's objects copy it)
        if (names.size() > 24)
            names.resize(24);
        std::array<char, 160> label{};
        text::format_to(label, "Bouncy set: %s (%u)", stem.c_str(), static_cast<unsigned>(names.size()));
        entity_palette().palette_.push_back({.label = label.data(), .colour = 0xFFFFB060, .why = ""});
        entity_palette().sources_.push_back({.kind = SourceKind::kAssignValue, .names = names});
    }
    add(particles, 0xFFFF9050);
    // The level's loaded sounds (its wave table): an emitter with that sound.
    const char* why = node_create::get_why_not(node_create::Kind::kSound, node_create::Team::kRed);
    const int count = std::clamp(sound::level_wave_count(), 0, 256);
    std::vector<std::pair<std::string, Source>> sounds;
    for (int i = 0; i < count && !sound::no_sound(); ++i) {
        const auto& wave = core::at(sound::loaded_level_waves(), i);
        std::string label = "Sound emitter: " + nodes::caption(wave.checksum);
        if (wave.looping)
            label += " (loop)";
        sounds.emplace_back(label, Source{.kind = SourceKind::kSound, .names = {}, .sound = wave.checksum});
    }
    std::ranges::sort(sounds, [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& e : sounds) {
        entity_palette().palette_.push_back({.label = e.first, .colour = 0xFF4FD1C5, .why = why ? why : ""});
        entity_palette().sources_.push_back(e.second);
    }
}

void build_models()
{
    entity_palette().models_.clear();
    std::set<std::string> seen;
    for (const nodes::Elem& n : nodes::get_all()) {
        if (n.family != nodes::Family::kObject || !live_object(n.name))
            continue;
        const std::string model = model_of(nodes::struct_of(n));
        const std::string key = type_key(0, 0, 0, model);
        if (!model.empty() && seen.insert(key).second)
            entity_palette().models_.push_back(model);
    }
    std::ranges::sort(entity_palette().models_, [](const std::string& a, const std::string& b) {
        return std::strcmp(short_name(a), short_name(b)) < 0;
    });
}

} // namespace

// --- the state ---

State at_hand()
{
    State out;
    for (const Pace& s : level_editor_entities_state().state_.speeds) {
        if (look_up_node_params(s.name))
            out.speeds.push_back(s);
    }
    for (const EmitterSound& e : level_editor_entities_state().state_.emitters) {
        if (look_up_node_params(e.name))
            out.emitters.push_back(e);
    }
    return out;
}

void change_phase(const State& state)
{
    // (radii no longer set: back to the node's own - an added emitter's spec, a level emitter's Radius as loaded is
    // what its node had; the radius written into a level node stays until the level loads again)
    level_editor_entities_state().state_ = state;
    speed_applied().clear();
    for (const EmitterSound& e : level_editor_entities_state().state_.emitters)
        apply_radius(e);
}

bool take_undo(State* before)
{
    if (!level_editor_entities_state().have_undo_ || level_editor_entities_state().ui_active_)
        return false;
    level_editor_entities_state().have_undo_ = false;
    if (same_state(level_editor_entities_state().undo_before_, level_editor_entities_state().state_))
        return false;
    *before = level_editor_entities_state().undo_before_;
    return true;
}

void get_queue(std::function<void()> action) { level_editor_entities_state().queue_.push_back(std::move(action)); }

void kickoff() { stop_preview(); }

void halt() { stop_preview(); }

void stop_preview()
{
    if (!entity_preview().preview_.id)
        return;
    object_edit::get_restore(entity_preview().preview_.saved);
    entity_preview().preview_ = Preview{};
}

void preview_suspend()
{
    if (entity_preview().preview_.id)
        object_edit::get_restore(entity_preview().preview_.saved);
}

void preview_resume()
{
    if (entity_preview().preview_.id)
        preview_place(entity_preview().preview_.distance);
}

void tick(bool editor_active, uint32_t selected, bool dragging)
{
    const double now = get_now();
    const double seconds = entity_preview().last_tick_ > 0.0 ? now - entity_preview().last_tick_ : 0.0;
    entity_preview().last_tick_ = now;
    // (a new level: read its baked fields once it is up)
    if (level_editor_entities_state().level_loads_ != layout::level_loads()) {
        level_editor_entities_state().level_loads_ = layout::level_loads();
        level_editor_entities_state().scanned_ = false;
    }
    if (!level_editor_entities_state().scanned_ && in_level()) {
        level_editor_entities_state().scanned_ = true;
        scan_baked();
    }
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_entities_state().queue_);
    for (auto& action : queue)
        action();
    if (entity_preview().preview_.id && (!editor_active || dragging || selected != entity_preview().preview_.id))
        stop_preview();
    preview_tick(seconds);
    run_remakes();
    // The objects whose model was swapped: made again when their node's model= isn't the one they were made with
    // (the swap, its undo, Home).
    for (auto& [id, made] : made_with()) {
        const std::string model = model_of(look_up_node_params(id));
        if (model != made && live_object(id)) {
            made = model;
            queue_remake(id, true, 0.0);
        }
    }
    update_sounds();
    if (now >= entity_playback().next_slow_tick_) {
        entity_playback().next_slow_tick_ = now + 0.5;
        update_speeds();
        for (const EmitterSound& e : level_editor_entities_state().state_.emitters)
            apply_radius(e);
    }
}

// --- particles and ghosts (the node edits) ---

void node_moved(uint32_t name, V3 pos, V3 angles)
{
    const int index = nodes::get_find(name);
    if (index < 0 || core::at(nodes::get_all(), index).family != nodes::Family::kParticle)
        return;
    script::Struct* node = look_up_node_params(name);
    if (!node)
        return;
    static const uint32_t kMid = get_key("MidPosition"), kEnd = get_key("EndPosition");
    static const std::array<uint32_t, 3> kBoxes = {{get_key("BoxDimsStart"), get_key("BoxDimsMid"), get_key("BoxDimsEnd")}};
    auto it = particle_at().find(name);
    if (it == particle_at().end()) {
        // (first seen: the node's emitter now is the one its origin - the level's place, or a copy's as made - had)
        const node_edit::Values* origin = node_edit::get_origin(name);
        ParticleOrigin o;
        o.from = origin ? origin->pos : pos;
        o.from_yaw_ = origin ? origin->angles.y : angles.y;
        script::VectorValue v{};
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
        if ((o.emitter.has_mid_ = node->fetch_vec(kMid, &v, 0)))
            o.emitter.mid = {.x = v.x, .y = v.y, .z = v.z};
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
        if ((o.emitter.has_end_ = node->fetch_vec(kEnd, &v, 0)))
            o.emitter.end = {.x = v.x, .y = v.y, .z = v.z};
        for (int i = 0; i < 3; ++i) {
            // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
            if ((core::at(o.emitter.has_box_, i) = node->fetch_vec(core::at(kBoxes, i), &v, 0)))
                core::at(o.emitter.box, i) = {.x = v.x, .y = v.y, .z = v.z};
        }
        o.at = o.from;
        o.at_yaw_ = o.from_yaw_;
        it = particle_at().emplace(name, o).first;
    }
    ParticleOrigin& o = it->second;
    if (pos.x == o.at.x && pos.y == o.at.y && pos.z == o.at.z && angles.y == o.at_yaw_)
        return;
    // Its emitter's other places (world places in the node) moved and turned with it, its boxes turned - from the
    // emitter as first seen (no drift over many turns).
    const ParticleEmitter now = turn_particle(o.emitter, o.from, pos, angles.y - o.from_yaw_);
    auto put = [&](uint32_t key, bool has, V3 v) {
        if (!has)
            return;
        node->erase_module(key);
        node->put_vec3(checksums::Name{key}, {.x = v.x, .y = v.y, .z = v.z});
    };
    put(kMid, now.has_mid_, now.mid);
    put(kEnd, now.has_end_, now.end);
    for (int i = 0; i < 3; ++i)
        put(core::at(kBoxes, i), core::at(now.has_box_, i), core::at(now.box, i));
    o.at = pos;
    o.at_yaw_ = angles.y;
    queue_remake(name, false, 0.3); // (once the drag rests)
}

bool picked_node_index(checksums::Name node_name, int family)
{
    const uint32_t name = node_name.value();
    auto f = static_cast<nodes::Family>(family);
    const int index = nodes::get_find(name);
    if (index < 0 || deleted_copy(core::at(nodes::get_all(), index)))
        return false;
    if (f == nodes::Family::kParticle)
        return true;
    if (f == nodes::Family::kObject)
        return !live_object(name);
    return false;
}

const char* when_applied(checksums::Name node_name, int family)
{
    const uint32_t name = node_name.value();
    auto f = static_cast<nodes::Family>(family);
    if (f == nodes::Family::kParticle)
        return "Live (rebuilt)";
    if (f == nodes::Family::kObject && !live_object(name))
        return "On script spawn";
    return "";
}

// --- the palette ---

const std::vector<PaletteEntry>& get_palette()
{
    // (made again when the node arrays change - not on a timer: an armed entry keeps its index)
    if (entity_palette().palette_generation_ != nodes::get_generation()) {
        entity_palette().palette_generation_ = nodes::get_generation();
        build_palette();
    }
    return entity_palette().palette_;
}

void refresh_palette()
{
    entity_palette().palette_generation_ = 0xFFFFFFFF;
    entity_palette().models_generation_ = 0xFFFFFFFF;
}

Placed get_place(int index, V3 at, float yaw)
{
    Placed placed;
    if (index < 0 || std::cmp_greater_equal(index, entity_palette().sources_.size()))
        return placed;
    const Source& source = core::at(entity_palette().sources_, index);
    if (source.kind == SourceKind::kSound) {
        placed.emitter = true;
        placed.sound = source.sound;
        return placed;
    }
    if (source.kind == SourceKind::kDupFrom) {
        objects::GameEntity* copy = object_edit::make_copy(source.names.front(), at);
        if (!copy) {
            placed.message = "Couldn't copy " + nodes::caption(source.names.front());
            return placed;
        }
        const uint32_t cls = class_of(look_up_node_params(source.names.front()));
        if (cls == kPedestrian || cls == kVehicle)
            object_edit::turn_about(copy, at, yaw - current_heading(copy)); // (facing the way the camera looks)
        placed.copies.push_back(copy->ident());
        placed.message = "Placed a copy of " + nodes::caption(source.names.front());
        return placed;
    }
    // A bouncy set: each member copied, about the anchor as the level placed them.
    std::vector<V3> places;
    for (const uint32_t name : source.names) {
        maths::Vec4 p;
        level_nodes::fetch_place(look_up_node_params(name), &p);
        places.push_back({.x = p.x, .y = p.y, .z = p.z});
    }
    const V3 anchor = pin_anchor_point(places);
    for (size_t i = 0; i < source.names.size(); ++i) {
        if (const objects::GameEntity* copy = object_edit::make_copy(core::at(source.names, i), at + (core::at(places, i) - anchor)))
            placed.copies.push_back(copy->ident());
    }
    std::array<char, 96> text{};
    text::format_to(text, "Placed a bouncy set: %u objects", static_cast<unsigned>(placed.copies.size()));
    placed.message = text.data();
    return placed;
}

void attach_sound(SoundAttachment attachment)
{
    const auto emitter = attachment.emitter;
    const auto sound = attachment.sound;
    EmitterSound e;
    if (const EmitterSound* had = emitter_of(level_editor_entities_state().state_, emitter))
        e = *had;
    e.name = emitter;
    e.sound = sound;
    set_emitter(&level_editor_entities_state().state_, e);
}

bool& show_uncreated() { return entity_preview().show_uncreated_; }

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    auto to_pixel = [&](V3 p, float* x, float* y) {
        float nx = 0.0f, ny = 0.0f;
        if (!cast_onto(camera, frame, lens, p, near_z, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera, frame, near_z, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (to_pixel(a, &ax, &ay) && to_pixel(b, &bx, &by))
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    };
    // The previewed path.
    if (entity_preview().preview_.id) {
        for (size_t i = 0; i + 1 < entity_preview().preview_.points.size(); ++i)
            line(core::at(entity_preview().preview_.points, i), core::at(entity_preview().preview_.points, i + 1), 0xFF60FF90, 2.0f);
        if (entity_preview().preview_.closed && entity_preview().preview_.points.size() > 2)
            line(entity_preview().preview_.points.back(), entity_preview().preview_.points.front(), 0xFF60FF90, 2.0f);
    }
    if (!entity_preview().show_uncreated_)
        return;
    // Ghosts: object nodes the level hasn't made, nearest first (within 4000 units, 200 at most).
    std::vector<std::pair<float, const nodes::Elem*>> near;
    for (const nodes::Elem& n : nodes::get_all()) {
        if (n.family != nodes::Family::kObject || !n.has_pos_ || n.hidden || live_object(n.name) || deleted_copy(n))
            continue;
        const float d = len_of(n.pos - camera);
        if (d < 4000.0f)
            near.emplace_back(d, &n);
    }
    std::ranges::sort(near, [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > 200)
        near.resize(200);
    constexpr uint32_t kGhost = 0xB0B0A0FF;
    for (const auto& [d, n] : near) {
        const V3 at = n->forward;
        const V3 up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        const V3 right = make_unit(perpendicular(up, at));
        std::array<V3, 3> axes = {{right * 20.0f, up * 40.0f, at * 20.0f}};
        std::array<V3, 8> corners{};
        box_corners(n->pos, axes, {.x = -1.0f, .y = 0.0f, .z = -1.0f}, {.x = 1.0f, .y = 1.0f, .z = 1.0f}, corners);
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if (!(i & bit))
                    line(core::at(corners, i), core::at(corners, i | bit), kGhost, 1.0f);
            }
        }
        line(n->pos, n->pos + at * 30.0f, kGhost, 1.5f);
        float x = 0.0f, y = 0.0f;
        if (d < 1500.0f && to_pixel(n->pos + up * 48.0f, &x, &y))
            texts->push_back({.x = x, .y = y, .argb = kGhost, .text = nodes::caption(n->name) + " (uncreated)"});
    }
}

void forget_level()
{
    stop_all_sounds();
    entity_preview().preview_ = Preview{};
    level_editor_entities_state().state_ = State{};
    level_editor_entities_state().baked_ = State{};
    level_editor_entities_state().have_undo_ = false;
    level_editor_entities_state().queue_.clear();
    remakes().clear();
    particle_at().clear();
    made_with().clear();
    speed_applied().clear();
    level_editor_entities_state().scanned_ = false;
    entity_palette().palette_generation_ = 0xFFFFFFFF;
    entity_palette().models_generation_ = 0xFFFFFFFF;
}

// --- for the UI (level_editor_entities_ui.cpp) ---

namespace detail {
const std::vector<std::string>& get_models()
{
    if (entity_palette().models_generation_ != nodes::get_generation()) {
        entity_palette().models_generation_ = nodes::get_generation();
        build_models();
    }
    return entity_palette().models_;
}
void watch_model(uint32_t id, const std::string& made)
{
    if (!made_with().count(id))
        made_with()[id] = made;
}
} // namespace detail

void model_edited(uint32_t id)
{
    // (a copy is made from its source's node, then a layout's field edit gives it its own model: made again)
    if (object_edit::is_copy(id))
        made_with()[id] = "\x01";
}

namespace detail {
bool has_path(uint32_t id)
{
    std::vector<V3> points;
    bool closed = false;
    return path_of(id, &points, &closed);
}
float speed_set(uint32_t id)
{
    const Pace* s = speed_of(level_editor_entities_state().state_, id);
    if (s)
        return s->mph;
    s = speed_of(level_editor_entities_state().baked_, id);
    return s ? s->mph : 0.0f;
}
float default_mph(uint32_t id) { return preview_mph(id); }
void change_speed(uint32_t id, float mph)
{
    note_undo();
    set_speed(&level_editor_entities_state().state_, id, mph);
}
EmitterSound emitter_now(uint32_t name)
{
    if (const EmitterSound* e = emitter_of(level_editor_entities_state().state_, name))
        return *e;
    if (const EmitterSound* e = emitter_of(level_editor_entities_state().baked_, name))
        return *e;
    EmitterSound e;
    e.name = name;
    return e;
}
void change_emitter(const EmitterSound& e)
{
    note_undo();
    set_emitter(&level_editor_entities_state().state_, e);
    apply_radius(e);
}
float emitter_radius(uint32_t name, bool* sphere)
{
    const objects::EmitterNode* emitter = get_emitter(name);
    *sphere = emitter && emitter->form_ == objects::kEmitterShapeSphere;
    return emitter ? emitter->radius_ : 0.0f;
}
std::vector<std::pair<uint32_t, std::string>> get_sounds()
{
    std::vector<std::pair<uint32_t, std::string>> out;
    if (sound::no_sound())
        return out;
    const int count = std::clamp(sound::level_wave_count(), 0, 256);
    for (int i = 0; i < count; ++i) {
        const auto& wave = core::at(sound::loaded_level_waves(), i);
        out.emplace_back(wave.checksum, nodes::caption(wave.checksum) + (wave.looping ? " (loop)" : ""));
    }
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}
void set_ui_active(bool active) { level_editor_entities_state().ui_active_ = active; }
bool get_previewing(uint32_t id) { return entity_preview().preview_.id == id && id; }
bool preview_playing() { return entity_preview().preview_.playing; }
void preview_play(uint32_t id)
{
    if (entity_preview().preview_.id == id)
        entity_preview().preview_.playing = !entity_preview().preview_.playing;
    else
        preview_start(id);
}
void preview_step(uint32_t id)
{
    if (entity_preview().preview_.id != id)
        preview_start(id);
    if (!entity_preview().preview_.id)
        return;
    entity_preview().preview_.playing = false;
    entity_preview().preview_.distance += mph_to_units(preview_mph(id)) * 0.5f; // (half a second along)
    preview_place(entity_preview().preview_.distance);
}
float preview_distance() { return entity_preview().preview_.distance; }
float preview_length() { return path_length(entity_preview().preview_.points, entity_preview().preview_.closed); }
} // namespace detail

} // namespace frontend::level_editor::entities
