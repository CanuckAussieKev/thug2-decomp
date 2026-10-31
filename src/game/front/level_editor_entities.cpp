// NOT ORIGINAL - the level editor's entities (ours). See level_editor_entities.h.

#include "game/front/level_editor_entities.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/gfx/camera.h"
#include "game/gfx/screen.h"
#include "game/gfx/viewport.h"
#include "game/mth/matrix.h"
#include "game/obj/compositemgr.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/load.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "game/sys/log.h"

namespace Front::LevelEditor::Entities {

// --- the pure parts -----------------------------------------------------------------------------------------------

std::string SetStem(const std::string& name)
{
    size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1])))
        --end;
    while (end > 0 && name[end - 1] == '_')
        --end;
    return end == 0 ? name : name.substr(0, end);
}

V3 SetAnchor(const std::vector<V3>& places)
{
    if (places.empty())
        return {0.0f, 0.0f, 0.0f};
    float minX = places[0].x, maxX = places[0].x, minZ = places[0].z, maxZ = places[0].z, minY = places[0].y;
    for (const V3& p : places) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
        minY = std::min(minY, p.y);
    }
    return {(minX + maxX) * 0.5f, minY, (minZ + maxZ) * 0.5f};
}

V3 TurnBoxExtents(V3 extents, float turn)
{
    double c = std::fabs(std::cos(static_cast<double>(turn))), s = std::fabs(std::sin(static_cast<double>(turn)));
    // (a quarter turn exactly: x and z swapped - cos(pi/2) in floats isn't 0)
    if (c < 1e-6)
        c = 0.0;
    if (s < 1e-6)
        s = 0.0;
    return {static_cast<float>(c * extents.x + s * extents.z), extents.y,
            static_cast<float>(s * extents.x + c * extents.z)};
}

ParticleEmitter TurnParticle(const ParticleEmitter& origin, V3 from, V3 pos, float turn)
{
    ParticleEmitter out = origin;
    if (origin.hasMid)
        out.mid = pos + TurnAboutUp(origin.mid - from, turn);
    if (origin.hasEnd)
        out.end = pos + TurnAboutUp(origin.end - from, turn);
    for (int i = 0; i < 3; ++i) {
        if (origin.hasBox[i])
            out.box[i] = TurnBoxExtents(origin.box[i], turn);
    }
    return out;
}

namespace {
float Length(V3 v) { return std::sqrt(Dot(v, v)); }
} // namespace

float PathLength(const std::vector<V3>& points, bool closed)
{
    float total = 0.0f;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        total += Length(points[i + 1] - points[i]);
    if (closed && points.size() > 2)
        total += Length(points.front() - points.back());
    return total;
}

bool PathSample(const std::vector<V3>& points, bool closed, float distance, V3* pos, V3* dir)
{
    float total = PathLength(points, closed);
    if (points.size() < 2 || !(total > 0.0f))
        return false;
    float d = std::fmod(distance, total);
    if (d < 0.0f)
        d += total;
    size_t segments = points.size() - 1 + (closed && points.size() > 2 ? 1 : 0);
    for (size_t i = 0; i < segments; ++i) {
        V3 a = points[i], b = points[(i + 1) % points.size()];
        float length = Length(b - a);
        if (length <= 0.0f)
            continue;
        if (d <= length || i + 1 == segments) {
            float t = std::min(1.0f, d / length);
            *pos = a + (b - a) * t;
            *dir = (b - a) * (1.0f / length);
            return true;
        }
        d -= length;
    }
    return false;
}

std::string TypeKey(uint32_t cls, uint32_t type, uint32_t profile, const std::string& model)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%08X:%08X:%08X:", cls, type, profile);
    std::string key = text;
    for (char c : model)
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(c == '/' ? '\\' : c)));
    return key;
}

bool SameState(const State& a, const State& b)
{
    if (a.speeds.size() != b.speeds.size() || a.emitters.size() != b.emitters.size())
        return false;
    for (size_t i = 0; i < a.speeds.size(); ++i) {
        if (a.speeds[i].name != b.speeds[i].name || a.speeds[i].mph != b.speeds[i].mph)
            return false;
    }
    for (size_t i = 0; i < a.emitters.size(); ++i) {
        const EmitterSound &x = a.emitters[i], &y = b.emitters[i];
        if (x.name != y.name || x.sound != y.sound || x.volume != y.volume || x.radius != y.radius)
            return false;
    }
    return true;
}

bool Empty(const State& state) { return state.speeds.empty() && state.emitters.empty(); }

const Speed* SpeedOf(const State& state, uint32_t name)
{
    for (const Speed& s : state.speeds) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

const EmitterSound* EmitterOf(const State& state, uint32_t name)
{
    for (const EmitterSound& e : state.emitters) {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}

void SetSpeed(State* state, uint32_t name, float mph)
{
    auto it = std::find_if(state->speeds.begin(), state->speeds.end(), [&](const Speed& s) { return s.name == name; });
    if (!(mph > 0.0f)) {
        if (it != state->speeds.end())
            state->speeds.erase(it);
        return;
    }
    mph = std::clamp(mph, kMinMph, kMaxMph);
    if (it != state->speeds.end())
        it->mph = mph;
    else
        state->speeds.push_back({name, mph});
}

void SetEmitter(State* state, const EmitterSound& emitter)
{
    auto it = std::find_if(state->emitters.begin(), state->emitters.end(),
                           [&](const EmitterSound& e) { return e.name == emitter.name; });
    if (!emitter.sound && emitter.radius <= 0.0f) {
        if (it != state->emitters.end())
            state->emitters.erase(it);
        return;
    }
    EmitterSound e = emitter;
    e.volume = std::clamp(e.volume, kMinVolume, kMaxVolume);
    if (e.radius > 0.0f)
        e.radius = std::clamp(e.radius, kMinRadius, kMaxRadius);
    else
        e.radius = 0.0f;
    if (it != state->emitters.end())
        *it = e;
    else
        state->emitters.push_back(e);
}

namespace {

void Number(std::ostringstream& out, float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", value);
    out << text;
}

void Hex(std::ostringstream& out, uint32_t value)
{
    char text[12];
    std::snprintf(text, sizeof(text), "%08X", value);
    out << text;
}

bool ParseHex(const std::string& text, uint32_t* out)
{
    if (text.size() != 8)
        return false;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    *out = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
    return true;
}

bool ParseNumber(const std::string& text, float* out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
        return false;
    *out = static_cast<float>(v);
    return true;
}

} // namespace

std::string WriteRecords(const State& state)
{
    std::ostringstream out;
    for (const Speed& s : state.speeds) {
        out << "speed ";
        Hex(out, s.name);
        out << ' ';
        Number(out, s.mph);
        out << '\n';
    }
    for (const EmitterSound& e : state.emitters) {
        out << "emitter ";
        Hex(out, e.name);
        out << " sound=";
        Hex(out, e.sound);
        out << " vol=";
        Number(out, e.volume);
        out << " radius=";
        Number(out, e.radius);
        out << '\n';
    }
    return out.str();
}

bool IsRecord(const std::string& kind) { return kind == "speed" || kind == "emitter"; }

bool ReadRecord(const std::string& kind, std::istringstream& in, State* state)
{
    std::string nameText;
    uint32_t name = 0;
    if (!(in >> nameText) || !ParseHex(nameText, &name) || !name)
        return false;
    if (kind == "speed") {
        std::string value;
        float mph = 0.0f;
        if (!(in >> value) || !ParseNumber(value, &mph) || mph < kMinMph || mph > kMaxMph || SpeedOf(*state, name))
            return false;
        state->speeds.push_back({name, mph});
        return true;
    }
    if (kind != "emitter" || EmitterOf(*state, name))
        return false;
    EmitterSound e;
    e.name = name;
    std::set<std::string> seen;
    std::string word;
    while (in >> word) {
        if (word[0] == '#')
            break; // (a comment: the rest of the line)
        size_t eq = word.find('=');
        if (eq == std::string::npos || eq == 0)
            return false;
        std::string key = word.substr(0, eq), value = word.substr(eq + 1);
        if (!seen.insert(key).second)
            return false;
        if (key == "sound") {
            if (!ParseHex(value, &e.sound))
                return false;
        } else if (key == "vol") {
            if (!ParseNumber(value, &e.volume) || e.volume < kMinVolume || e.volume > kMaxVolume)
                return false;
        } else if (key == "radius") {
            if (!ParseNumber(value, &e.radius) || (e.radius != 0.0f && (e.radius < kMinRadius || e.radius > kMaxRadius)))
                return false;
        } // (other keys: a later version's - skipped)
    }
    state->emitters.push_back(e);
    return true;
}

// --- the game side ------------------------------------------------------------------------------------------------


namespace {

uint32_t Key(const char* name) { return Crc::GenerateCRCFromString(name); }
const uint32_t kClass = 0x12B4E660, kName = 0xA1DC81F9, kPos = 0x7F261953;
const uint32_t kLevelObject = 0xB7B3BD86, kGameObject = 0xEF59C100, kPedestrian = 0xA0DFAC98, kVehicle = 0xE47F1B79,
               kParticleObject = 0x9E7D469E;

uint32_t KType() { static const uint32_t k = Key("Type"); return k; }
uint32_t KProfile() { static const uint32_t k = Key("profile"); return k; }
uint32_t KModel() { static const uint32_t k = Key("model"); return k; }
uint32_t KBouncy() { static const uint32_t k = Key("bouncy"); return k; }
uint32_t KTexture() { static const uint32_t k = Key("Texture"); return k; }
uint32_t KWaypoint() { static const uint32_t k = Key("Waypoint"); return k; }
uint32_t KRadius() { static const uint32_t k = Key("Radius"); return k; }
uint32_t KEdSound() { static const uint32_t k = Key("ed_sound"); return k; }
uint32_t KEdVolume() { static const uint32_t k = Key("ed_volume"); return k; }
uint32_t KEdPathSpeed() { static const uint32_t k = Key("ed_path_speed"); return k; }
uint32_t KSetPathVelocity() { static const uint32_t k = Key("Obj_SetPathVelocity"); return k; }
uint32_t KMph() { static const uint32_t k = Key("mph"); return k; }

double Now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Script::CStruct* NodeStruct(uint32_t name)
{
    return name && SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
}

uint32_t ClassOf(Script::CStruct* node)
{
    uint32_t cls = 0;
    if (node)
        node->GetChecksum(kClass, &cls, 0);
    return cls;
}

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

std::string ModelOf(Script::CStruct* node)
{
    const char* model = nullptr;
    return node && node->GetString(KModel(), &model, 0) && model ? std::string(model) : std::string();
}

bool InLevel() { return SkateScript::NodeManagerInstance() != nullptr && !Nodes::All().empty(); }

// A node of the editor's copies' array whose copy was deleted (its node stays until the level goes): not a ghost.
bool DeletedCopy(const Nodes::Node& node)
{
    static const uint32_t kCopies = Crc::GenerateCRCFromString("level_editor_copy_nodes");
    return Nodes::ArrayName(node) == kCopies && !Objects::IsCopy(node.name);
}

// The state the layout keeps, the baked fields' (read when a level loads; not saved), the undo step under way.
State g_state, g_baked;
State g_undoBefore;
bool g_haveUndo;
bool g_uiActive; // (one of the Inspector's fields is being dragged: the undo step waits)
std::vector<std::function<void()>> g_queue;
int g_levelLoads = -1;
bool g_scanned;

// Sounds playing on emitters.
struct Playing {
    uint32_t name, sound, id;
};
std::vector<Playing> g_playing;

// Speeds put on objects (the object each was put on, so one made again gets it again).
std::unordered_map<uint32_t, std::pair<const void*, float>> g_speedApplied;
double g_nextSlowTick;

// Objects made again: killed, and once the game has removed them, made from their nodes (put back where they were).
struct Remake {
    uint32_t name;
    bool keep;
    Objects::State state;
    double due;
    bool killed;
};
std::vector<Remake> g_remakes;

// A particle node's emitter as it was first seen (the level's - or a copy's as made), the place and heading it was at
// then, and the place / heading it was last made for (map4: turned too).
struct ParticleOrigin {
    ParticleEmitter emitter;
    V3 from = {};
    float fromYaw = 0.0f;
    V3 at = {};
    float atYaw = 0.0f;
};
std::unordered_map<uint32_t, ParticleOrigin> g_particleAt;

// Objects whose model the editor swapped: the model each was made with.
std::unordered_map<uint32_t, std::string> g_madeWith;

// The preview.
struct Preview {
    uint32_t id = 0;
    Objects::State saved;
    std::vector<V3> points;
    bool closed = false;
    float distance = 0.0f;
    bool playing = false;
};
Preview g_preview;
double g_lastTick;

bool g_showUncreated;

// The emitters' state in effect: the layout's over the baked fields.
std::vector<EmitterSound> EffectiveEmitters()
{
    std::vector<EmitterSound> out = g_state.emitters;
    for (const EmitterSound& e : g_baked.emitters) {
        if (!EmitterOf(g_state, e.name))
            out.push_back(e);
    }
    return out;
}
std::vector<Speed> EffectiveSpeeds()
{
    std::vector<Speed> out = g_state.speeds;
    for (const Speed& s : g_baked.speeds) {
        if (!SpeedOf(g_state, s.name))
            out.push_back(s);
    }
    return out;
}

void NoteUndo()
{
    if (!g_haveUndo) {
        g_undoBefore = g_state;
        g_haveUndo = true;
    }
}

void QueueRemake(uint32_t name, bool keep, double delay)
{
    Obj::CCompositeObject* object = LiveObject(name);
    if (!object)
        return; // (nothing made of it now: its node is what a later create reads)
    for (Remake& r : g_remakes) {
        if (r.name == name && !r.killed) {
            r.due = Now() + delay;
            if (keep)
                r.state = Objects::Save(object);
            return;
        }
    }
    Remake r = {name, keep, keep ? Objects::Save(object) : Objects::State{}, Now() + delay, false};
    g_remakes.push_back(r);
}

void RunRemakes()
{
    double now = Now();
    for (auto it = g_remakes.begin(); it != g_remakes.end();) {
        Remake& r = *it;
        if (!r.killed) {
            if (now < r.due) {
                ++it;
                continue;
            }
            if (Obj::CCompositeObject* object = LiveObject(r.name)) {
                if (r.keep)
                    r.state = Objects::Save(object);
                object->MarkAsDead();
            }
            r.killed = true;
        }
        if (Obj::FindObject(r.name)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (Script::CStruct* node = NodeStruct(r.name)) {
            CFuncs::CreateFromNode(node);
            if (r.keep)
                Objects::Restore(r.state);
            g_speedApplied.erase(r.name); // (its speed put on it again)
            if (DebugEnabled())
                Log("level editor: %s made again from its node", Objects::Label(r.name).c_str());
        }
        it = g_remakes.erase(it);
    }
}

// --- sounds on emitters ---

Sfx::CSfxManager* SfxManager() { return Sfx::g_sfxManager; }
void* NearestCamera(const Mth::Vector* position, float* distance)
{
    return Nx::GetClosestCamera(position, distance);
}
const Mth::Vector* CameraPosition(void* camera)
{
    return static_cast<Gfx::Camera*>(camera)->GetPos();
}
Obj::EmitterNode* Emitter(uint32_t name)
{
    return Obj::g_emitterTable ? Obj::Emitter_GetNode(name) : nullptr;
}

void StopPlaying(Playing* p)
{
    if (Sfx::CSfxManager* m = SfxManager(); m && p->id && !Sfx::NoSound())
        m->StopSound(p->id);
    p->id = 0;
}

void StopAllSounds()
{
    for (Playing& p : g_playing)
        StopPlaying(&p);
    g_playing.clear();
}

void UpdateSounds()
{
    Sfx::CSfxManager* m = SfxManager();
    std::vector<EmitterSound> emitters = EffectiveEmitters();
    bool off = !m || Sfx::NoSound() || !InLevel() || Layout::NetworkActive();
    // (those no longer wanted, or of another sound now: stopped)
    for (auto it = g_playing.begin(); it != g_playing.end();) {
        auto e = std::find_if(emitters.begin(), emitters.end(), [&](const EmitterSound& x) { return x.name == it->name; });
        if (off || e == emitters.end() || e->sound != it->sound) {
            StopPlaying(&*it);
            it = g_playing.erase(it);
        } else {
            ++it;
        }
    }
    if (off)
        return;
    for (const EmitterSound& e : emitters) {
        if (!e.sound)
            continue;
        auto p = std::find_if(g_playing.begin(), g_playing.end(), [&](const Playing& x) { return x.name == e.name; });
        if (p == g_playing.end()) {
            g_playing.push_back({e.name, e.sound, 0});
            p = g_playing.end() - 1;
        }
        Obj::EmitterNode* emitter = Emitter(e.name);
        Sfx::WaveTableEntry* wave = m->GetWaveTableEntry(e.sound);
        if (!emitter || !emitter->m_active || !wave || m->IsPaused()) {
            StopPlaying(&*p);
            continue;
        }
        // As a sound played from an emitter: from the shape's nearest point to the listener, the sound's own dropoff.
        Mth::Vector centre = emitter->m_pos;
        void* camera = NearestCamera(&centre, nullptr);
        if (!camera) {
            StopPlaying(&*p);
            continue;
        }
        Mth::Vector listener = *CameraPosition(camera), heard = centre;
        if (emitter->m_shape != Obj::EMITTER_SHAPE_OTHER)
            emitter->GetClosestPoint(&heard, &listener);
        Sfx::sVolume volume;
        volume.Init();
        if (!m->GetVolume(&volume, &heard, wave->dropoff, 0, camera, nullptr)) {
            StopPlaying(&*p); // (out of range)
            continue;
        }
        volume.PercentageAdjustment(e.volume);
        if (p->id && m->IsSoundPlaying(p->id, nullptr)) {
            m->SetSoundParamsScaled(p->id, &volume, 100.0f);
        } else if (!volume.IsSilent()) {
            p->id = m->PlaySound(e.sound, &volume, 100.0f, e.name, nullptr, 0); // (again when it stopped: a loop)
        } else {
            p->id = 0;
        }
    }
}

// A sphere emitter's radius kept as the state says (its node's Radius, the emitter manager's copy - an added emitter's
// node is made again from its spec when the overlay changes).
void ApplyRadius(const EmitterSound& e)
{
    if (e.radius <= 0.0f)
        return;
    if (Obj::EmitterNode* emitter = Emitter(e.name); emitter && emitter->m_shape == Obj::EMITTER_SHAPE_SPHERE &&
                                                     emitter->m_radius != e.radius) {
        emitter->m_radius = e.radius;
        emitter->m_radiusSquared = e.radius * e.radius;
    }
    if (Script::CStruct* node = NodeStruct(e.name); node && !NodeCreate::IsAdded(e.name)) {
        float now = 0.0f;
        if (!node->GetFloat(KRadius(), &now, 0) || now != e.radius) {
            node->RemoveComponent(KRadius());
            node->AddFloat(KRadius(), e.radius);
        }
    }
}

// --- speeds ---

// Our script LevelEditor_PathSpeed - `Obj_SetPathVelocity <speed> mph` - defined (once) by the game's loader from QB
// made here (as level_editor_triggers.cpp defines its trigger scripts), so the member function runs from a script of
// the object's, as a level's scripts run it.
uint32_t PathSpeedScript()
{
    static const uint32_t name = Key("LevelEditor_PathSpeed");
    if (Script::FindSymbol(name))
        return name;
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
    name4(name);
    qb.push_back(0x01);
    name4(KSetPathVelocity());
    qb.push_back(0x2D); // <speed>
    name4(Key("speed"));
    name4(KMph());
    qb.push_back(0x01);
    qb.push_back(0x24); // endscript
    qb.push_back(0x01);
    for (const char* text : {"LevelEditor_PathSpeed", "Obj_SetPathVelocity", "speed", "mph"}) {
        qb.push_back(0x2B);
        put4(Key(text));
        qb.insert(qb.end(), text, text + std::strlen(text));
        qb.push_back(0x00);
    }
    qb.push_back(0x00);
    Script::LoadQBData("level_editor_entities.qb", qb.data(), 0, 0, 0);
    return name;
}

void ApplySpeed(Obj::CCompositeObject* object, float mph)
{
    Script::CStruct* params = Script::NewStruct()->Init();
    params->AddFloat(Key("speed"), mph);
    Script::RunScript(PathSpeedScript(), params, object, false, nullptr);
    params->Clear();
    Script::DeleteStruct(params);
}

void UpdateSpeeds()
{
    if (!InLevel() || Layout::NetworkActive())
        return;
    for (const Speed& s : EffectiveSpeeds()) {
        Obj::CCompositeObject* object = LiveObject(s.name);
        if (!object)
            continue;
        auto it = g_speedApplied.find(s.name);
        if (it != g_speedApplied.end() && it->second.first == object && it->second.second == s.mph)
            continue;
        ApplySpeed(object, s.mph);
        g_speedApplied[s.name] = {object, s.mph};
        if (DebugEnabled())
            Log("level editor: %s's path speed %.1f mph", Objects::Label(s.name).c_str(), s.mph);
    }
}

// The baked fields (tools/pipeline/game/apply_layout.ts): ed_sound / ed_volume on emitters, ed_path_speed on moving objects.
void ScanBaked()
{
    g_baked = State{};
    if (Layout::NetworkActive())
        return;
    for (const Nodes::Node& n : Nodes::All()) {
        Script::CStruct* s = Nodes::StructOf(n);
        if (!s || !n.name)
            continue;
        uint32_t sound = 0;
        if (n.family == Nodes::Family::Sound && s->GetChecksum(KEdSound(), &sound, 0) && sound) {
            EmitterSound e;
            e.name = n.name;
            e.sound = sound;
            float volume = 100.0f;
            if (s->GetFloat(KEdVolume(), &volume, 0))
                e.volume = std::clamp(volume, kMinVolume, kMaxVolume);
            g_baked.emitters.push_back(e);
        }
        float mph = 0.0f;
        if (s->GetFloat(KEdPathSpeed(), &mph, 0) && mph >= kMinMph && mph <= kMaxMph)
            g_baked.speeds.push_back({n.name, mph});
    }
    if (!Empty(g_baked))
        Log("level editor: the level's baked fields: %u emitter sounds, %u path speeds",
            static_cast<unsigned>(g_baked.emitters.size()), static_cast<unsigned>(g_baked.speeds.size()));
}

// --- the preview ---

// The path an object's node starts: its first link, then each waypoint's first link (a loop closes it).
bool PathOf(uint32_t name, std::vector<V3>* points, bool* closed)
{
    points->clear();
    *closed = false;
    Script::CStruct* node = NodeStruct(name);
    if (!node || SkateScript::GetLinkCount(node) == 0)
        return false;
    uint32_t next = SkateScript::GetLink(node, 0);
    std::vector<uint32_t> seen;
    while (next && points->size() < 512) {
        if (std::find(seen.begin(), seen.end(), next) != seen.end()) {
            *closed = next == seen.front();
            break;
        }
        Script::CStruct* point = NodeStruct(next);
        if (!point || ClassOf(point) != KWaypoint())
            break;
        Mth::Vector pos;
        SkateScript::GetPosition(point, &pos);
        points->push_back({pos.x, pos.y, pos.z});
        seen.push_back(next);
        next = SkateScript::GetLinkCount(point) ? SkateScript::GetLink(point, 0) : 0;
    }
    return points->size() >= 2;
}

float PreviewMph(uint32_t name)
{
    for (const Speed& s : EffectiveSpeeds()) {
        if (s.name == name)
            return s.mph;
    }
    uint32_t cls = ClassOf(NodeStruct(name));
    return cls == kVehicle ? 25.0f : cls == kPedestrian ? 3.0f : 10.0f;
}

float Heading(const Obj::CCompositeObject* object)
{
    const Mth::Vector& at = object->m_matrix.row[2];
    return std::atan2(at.x, at.z);
}

void PreviewPlace(float distance)
{
    Obj::CCompositeObject* object = LiveObject(g_preview.id);
    V3 pos, dir;
    if (!object || !PathSample(g_preview.points, g_preview.closed, distance, &pos, &dir))
        return;
    Objects::Place(object, pos);
    if (dir.x != 0.0f || dir.z != 0.0f)
        Objects::TurnAbout(object, pos, std::atan2(dir.x, dir.z) - Heading(object));
}

void PreviewStart(uint32_t id)
{
    StopPreview();
    Obj::CCompositeObject* object = LiveObject(id);
    if (!object || !PathOf(id, &g_preview.points, &g_preview.closed))
        return;
    g_preview.id = id;
    g_preview.saved = Objects::Save(object);
    g_preview.distance = 0.0f;
    g_preview.playing = true;
    PreviewPlace(0.0f);
}

void PreviewTick(double seconds)
{
    if (!g_preview.id || !g_preview.playing)
        return;
    g_preview.distance += MphToUnits(PreviewMph(g_preview.id)) * static_cast<float>(std::min(seconds, 0.1));
    PreviewPlace(g_preview.distance);
}

// --- the palette ---

enum class SourceKind : uint8_t { Copy, Set, Sound };
struct Source {
    SourceKind kind;
    std::vector<uint32_t> names; // (copies: the node(s) copied)
    uint32_t sound = 0;
};
std::vector<PaletteEntry> g_palette;
std::vector<Source> g_sources;
uint32_t g_paletteGeneration = 0xFFFFFFFF;

// Loaded models (the model= of every object the level has made), for the model swap.
std::vector<std::string> g_models;
uint32_t g_modelsGeneration = 0xFFFFFFFF;

const char* Short(const std::string& path)
{
    const char* text = path.c_str();
    const char* base = std::max(std::strrchr(text, '\\'), std::strrchr(text, '/'));
    return base ? base + 1 : text;
}

void BuildPalette()
{
    g_palette.clear();
    g_sources.clear();
    if (!InLevel())
        return;
    std::set<std::string> types;
    std::map<std::string, std::vector<uint32_t>> sets;
    std::vector<std::pair<std::string, Source>> peds, cars, particles;
    for (const Nodes::Node& n : Nodes::All()) {
        Script::CStruct* s = Nodes::StructOf(n);
        if (!s || !n.name || n.hidden || NodeCreate::IsAdded(n.name) || Objects::IsCopy(n.name))
            continue;
        uint32_t cls = ClassOf(s);
        if (cls == kPedestrian || cls == kVehicle) {
            if (!LiveObject(n.name)) // (only types the level has made: their assets are loaded)
                continue;
            uint32_t type = 0, profile = 0;
            s->GetChecksum(KType(), &type, 0);
            s->GetChecksum(KProfile(), &profile, 0);
            std::string model = ModelOf(s);
            if (!types.insert(TypeKey(cls, type, profile, model)).second)
                continue;
            std::string label = cls == kPedestrian ? "Ped: " : "Car: ";
            label += Nodes::Label(type);
            if (profile && profile != type)
                label += " / " + Nodes::Label(profile);
            if (!model.empty())
                label += std::string(" (") + Short(model) + ")";
            label += "  like " + Nodes::Label(n.name);
            (cls == kPedestrian ? peds : cars).push_back({label, Source{SourceKind::Copy, {n.name}}});
        } else if ((cls == kLevelObject || cls == kGameObject) && s->ContainsFlag(KBouncy()) && LiveObject(n.name)) {
            sets[SetStem(Nodes::Label(n.name))].push_back(n.name);
        } else if (cls == kParticleObject) {
            uint32_t type = 0, texture = 0;
            s->GetChecksum(KType(), &type, 0);
            s->GetChecksum(KTexture(), &texture, 0);
            if (!types.insert(TypeKey(cls, type, texture, "")).second)
                continue;
            std::string label = "Particles: " + Nodes::Label(texture ? texture : type) + "  like " + Nodes::Label(n.name);
            particles.push_back({label, Source{SourceKind::Copy, {n.name}}});
        }
    }
    auto add = [](std::vector<std::pair<std::string, Source>>& list, uint32_t colour) {
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& e : list) {
            g_palette.push_back({e.first, colour, ""});
            g_sources.push_back(e.second);
        }
    };
    add(peds, 0xFFE0A0FF);
    add(cars, 0xFF60B0FF);
    for (auto& [stem, names] : sets) {
        if (names.size() < 2)
            continue; // (a single bouncy object: the browser's objects copy it)
        if (names.size() > 24)
            names.resize(24);
        char label[160];
        std::snprintf(label, sizeof(label), "Bouncy set: %s (%u)", stem.c_str(), static_cast<unsigned>(names.size()));
        g_palette.push_back({label, 0xFFFFB060, ""});
        g_sources.push_back({SourceKind::Set, names});
    }
    add(particles, 0xFFFF9050);
    // The level's loaded sounds (its wave table): an emitter with that sound.
    const char* why = NodeCreate::WhyNot(NodeCreate::Kind::Sound, NodeCreate::Team::Red);
    auto* waves = Sfx::g_levelWaves;
    int count = std::clamp(Sfx::g_levelCount, 0, 256);
    std::vector<std::pair<std::string, Source>> sounds;
    for (int i = 0; i < count && !Sfx::NoSound(); ++i) {
        std::string label = "Sound emitter: " + Nodes::Label(waves[i].checksum);
        if (waves[i].looping)
            label += " (loop)";
        sounds.push_back({label, Source{SourceKind::Sound, {}, waves[i].checksum}});
    }
    std::sort(sounds.begin(), sounds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& e : sounds) {
        g_palette.push_back({e.first, 0xFF4FD1C5, why ? why : ""});
        g_sources.push_back(e.second);
    }
}

void BuildModels()
{
    g_models.clear();
    std::set<std::string> seen;
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.family != Nodes::Family::Object || !LiveObject(n.name))
            continue;
        std::string model = ModelOf(Nodes::StructOf(n));
        std::string key = TypeKey(0, 0, 0, model);
        if (!model.empty() && seen.insert(key).second)
            g_models.push_back(model);
    }
    std::sort(g_models.begin(), g_models.end(), [](const std::string& a, const std::string& b) {
        return std::strcmp(Short(a), Short(b)) < 0;
    });
}

} // namespace

// --- the state ---

State Current()
{
    State out;
    for (const Speed& s : g_state.speeds) {
        if (NodeStruct(s.name))
            out.speeds.push_back(s);
    }
    for (const EmitterSound& e : g_state.emitters) {
        if (NodeStruct(e.name))
            out.emitters.push_back(e);
    }
    return out;
}

void SetState(const State& state)
{
    // (radii no longer set: back to the node's own - an added emitter's spec, a level emitter's Radius as loaded is
    // what its node had; the radius written into a level node stays until the level loads again)
    g_state = state;
    g_speedApplied.clear();
    for (const EmitterSound& e : g_state.emitters)
        ApplyRadius(e);
}

bool TakeUndo(State* before)
{
    if (!g_haveUndo || g_uiActive)
        return false;
    g_haveUndo = false;
    if (SameState(g_undoBefore, g_state))
        return false;
    *before = g_undoBefore;
    return true;
}

void Queue(std::function<void()> action) { g_queue.push_back(std::move(action)); }

void Start() { StopPreview(); }

void Stop() { StopPreview(); }

void StopPreview()
{
    if (!g_preview.id)
        return;
    Objects::Restore(g_preview.saved);
    g_preview = Preview{};
}

void PreviewSuspend()
{
    if (g_preview.id)
        Objects::Restore(g_preview.saved);
}

void PreviewResume()
{
    if (g_preview.id)
        PreviewPlace(g_preview.distance);
}

void Tick(bool editorActive, uint32_t selected, bool dragging)
{
    double now = Now();
    double seconds = g_lastTick > 0.0 ? now - g_lastTick : 0.0;
    g_lastTick = now;
    // (a new level: read its baked fields once it is up)
    if (g_levelLoads != Layout::LevelLoads()) {
        g_levelLoads = Layout::LevelLoads();
        g_scanned = false;
    }
    if (!g_scanned && InLevel()) {
        g_scanned = true;
        ScanBaked();
    }
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    if (g_preview.id && (!editorActive || dragging || selected != g_preview.id))
        StopPreview();
    PreviewTick(seconds);
    RunRemakes();
    // The objects whose model was swapped: made again when their node's model= isn't the one they were made with
    // (the swap, its undo, Home).
    for (auto& [id, made] : g_madeWith) {
        std::string model = ModelOf(NodeStruct(id));
        if (model != made && LiveObject(id)) {
            made = model;
            QueueRemake(id, true, 0.0);
        }
    }
    UpdateSounds();
    if (now >= g_nextSlowTick) {
        g_nextSlowTick = now + 0.5;
        UpdateSpeeds();
        for (const EmitterSound& e : g_state.emitters)
            ApplyRadius(e);
    }
}

// --- particles and ghosts (the node edits) ---

void NodeMoved(uint32_t name, V3 pos, V3 angles)
{
    int index = Nodes::Find(name);
    if (index < 0 || Nodes::All()[index].family != Nodes::Family::Particle)
        return;
    Script::CStruct* node = NodeStruct(name);
    if (!node)
        return;
    static const uint32_t kMid = Key("MidPosition"), kEnd = Key("EndPosition");
    static const uint32_t kBoxes[3] = {Key("BoxDimsStart"), Key("BoxDimsMid"), Key("BoxDimsEnd")};
    auto it = g_particleAt.find(name);
    if (it == g_particleAt.end()) {
        // (first seen: the node's emitter now is the one its origin - the level's place, or a copy's as made - had)
        const NodeEdit::Values* origin = NodeEdit::Origin(name);
        ParticleOrigin o;
        o.from = origin ? origin->pos : pos;
        o.fromYaw = origin ? origin->angles.y : angles.y;
        Script::VectorValue v;
        if ((o.emitter.hasMid = node->GetVector(kMid, &v, 0)))
            o.emitter.mid = {v.x, v.y, v.z};
        if ((o.emitter.hasEnd = node->GetVector(kEnd, &v, 0)))
            o.emitter.end = {v.x, v.y, v.z};
        for (int i = 0; i < 3; ++i) {
            if ((o.emitter.hasBox[i] = node->GetVector(kBoxes[i], &v, 0)))
                o.emitter.box[i] = {v.x, v.y, v.z};
        }
        o.at = o.from;
        o.atYaw = o.fromYaw;
        it = g_particleAt.emplace(name, o).first;
    }
    ParticleOrigin& o = it->second;
    if (pos.x == o.at.x && pos.y == o.at.y && pos.z == o.at.z && angles.y == o.atYaw)
        return;
    // Its emitter's other places (world places in the node) moved and turned with it, its boxes turned - from the
    // emitter as first seen (no drift over many turns).
    ParticleEmitter now = TurnParticle(o.emitter, o.from, pos, angles.y - o.fromYaw);
    auto put = [&](uint32_t key, bool has, V3 v) {
        if (!has)
            return;
        node->RemoveComponent(key);
        node->AddVector(key, v.x, v.y, v.z);
    };
    put(kMid, now.hasMid, now.mid);
    put(kEnd, now.hasEnd, now.end);
    for (int i = 0; i < 3; ++i)
        put(kBoxes[i], now.hasBox[i], now.box[i]);
    o.at = pos;
    o.atYaw = angles.y;
    QueueRemake(name, false, 0.3); // (once the drag rests)
}

bool MovableNode(uint32_t name, int family)
{
    auto f = static_cast<Nodes::Family>(family);
    int index = Nodes::Find(name);
    if (index < 0 || DeletedCopy(Nodes::All()[index]))
        return false;
    if (f == Nodes::Family::Particle)
        return true;
    if (f == Nodes::Family::Object)
        return !LiveObject(name);
    return false;
}

const char* WhenApplied(uint32_t name, int family)
{
    auto f = static_cast<Nodes::Family>(family);
    if (f == Nodes::Family::Particle)
        return "Live (rebuilt)";
    if (f == Nodes::Family::Object && !LiveObject(name))
        return "On script spawn";
    return "";
}

// --- the palette ---

const std::vector<PaletteEntry>& Palette()
{
    // (made again when the node arrays change - not on a timer: an armed entry keeps its index)
    if (g_paletteGeneration != Nodes::Generation()) {
        g_paletteGeneration = Nodes::Generation();
        BuildPalette();
    }
    return g_palette;
}

void RefreshPalette()
{
    g_paletteGeneration = 0xFFFFFFFF;
    g_modelsGeneration = 0xFFFFFFFF;
}

Placed Place(int index, V3 at, float yaw)
{
    Placed placed;
    if (index < 0 || index >= static_cast<int>(g_sources.size()))
        return placed;
    const Source& source = g_sources[index];
    if (source.kind == SourceKind::Sound) {
        placed.emitter = true;
        placed.sound = source.sound;
        return placed;
    }
    if (source.kind == SourceKind::Copy) {
        Obj::CCompositeObject* copy = Objects::MakeCopy(source.names.front(), at);
        if (!copy) {
            placed.message = "Couldn't copy " + Nodes::Label(source.names.front());
            return placed;
        }
        uint32_t cls = ClassOf(NodeStruct(source.names.front()));
        if (cls == kPedestrian || cls == kVehicle)
            Objects::TurnAbout(copy, at, yaw - Heading(copy)); // (facing the way the camera looks)
        placed.copies.push_back(copy->m_id);
        placed.message = "Placed a copy of " + Nodes::Label(source.names.front()) +
                         (cls == kPedestrian || cls == kVehicle
                              ? ""
                              : "");
        return placed;
    }
    // A bouncy set: each member copied, about the anchor as the level placed them.
    std::vector<V3> places;
    for (uint32_t name : source.names) {
        Mth::Vector p;
        SkateScript::GetPosition(NodeStruct(name), &p);
        places.push_back({p.x, p.y, p.z});
    }
    V3 anchor = SetAnchor(places);
    for (size_t i = 0; i < source.names.size(); ++i) {
        if (Obj::CCompositeObject* copy = Objects::MakeCopy(source.names[i], at + (places[i] - anchor)))
            placed.copies.push_back(copy->m_id);
    }
    char text[96];
    std::snprintf(text, sizeof(text), "Placed a bouncy set: %u objects", static_cast<unsigned>(placed.copies.size()));
    placed.message = text;
    return placed;
}

void AttachSound(uint32_t emitter, uint32_t sound)
{
    EmitterSound e;
    if (const EmitterSound* had = EmitterOf(g_state, emitter))
        e = *had;
    e.name = emitter;
    e.sound = sound;
    SetEmitter(&g_state, e);
}

bool& ShowUncreated() { return g_showUncreated; }

void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts)
{
    auto toPixel = [&](V3 p, float* x, float* y) {
        float nx, ny;
        if (!Project(camera, frame, lens, p, nearZ, &nx, &ny))
            return false;
        NdcToPixel(viewport, nx, ny, x, y);
        return true;
    };
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!ClipToNear(camera, frame, nearZ, &a, &b))
            return;
        float ax, ay, bx, by;
        if (toPixel(a, &ax, &ay) && toPixel(b, &bx, &by))
            lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    // The previewed path.
    if (g_preview.id) {
        for (size_t i = 0; i + 1 < g_preview.points.size(); ++i)
            line(g_preview.points[i], g_preview.points[i + 1], 0xFF60FF90, 2.0f);
        if (g_preview.closed && g_preview.points.size() > 2)
            line(g_preview.points.back(), g_preview.points.front(), 0xFF60FF90, 2.0f);
    }
    if (!g_showUncreated)
        return;
    // Ghosts: object nodes the level hasn't made, nearest first (within 4000 units, 200 at most).
    std::vector<std::pair<float, const Nodes::Node*>> near;
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.family != Nodes::Family::Object || !n.hasPos || n.hidden || LiveObject(n.name) || DeletedCopy(n))
            continue;
        float d = Length(n.pos - camera);
        if (d < 4000.0f)
            near.push_back({d, &n});
    }
    std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > 200)
        near.resize(200);
    constexpr uint32_t kGhost = 0xB0B0A0FF;
    for (const auto& [d, n] : near) {
        V3 at = n->forward, up = {0.0f, 1.0f, 0.0f};
        V3 right = Normalize(Cross(up, at));
        V3 axes[3] = {right * 20.0f, up * 40.0f, at * 20.0f};
        V3 corners[8];
        BoxCorners(n->pos, axes, {-1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, corners);
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if (!(i & bit))
                    line(corners[i], corners[i | bit], kGhost, 1.0f);
            }
        }
        line(n->pos, n->pos + at * 30.0f, kGhost, 1.5f);
        float x, y;
        if (d < 1500.0f && toPixel(n->pos + up * 48.0f, &x, &y))
            texts->push_back({x, y, kGhost, Nodes::Label(n->name) + " (uncreated)"});
    }
}

void ForgetLevel()
{
    StopAllSounds();
    g_preview = Preview{};
    g_state = State{};
    g_baked = State{};
    g_haveUndo = false;
    g_queue.clear();
    g_remakes.clear();
    g_particleAt.clear();
    g_madeWith.clear();
    g_speedApplied.clear();
    g_scanned = false;
    g_paletteGeneration = 0xFFFFFFFF;
    g_modelsGeneration = 0xFFFFFFFF;
}

// --- for the UI (level_editor_entities_ui.cpp) ---

namespace Detail {
const std::vector<std::string>& Models()
{
    if (g_modelsGeneration != Nodes::Generation()) {
        g_modelsGeneration = Nodes::Generation();
        BuildModels();
    }
    return g_models;
}
void WatchModel(uint32_t id, const std::string& made)
{
    if (!g_madeWith.count(id))
        g_madeWith[id] = made;
}
} // namespace Detail

void ModelEdited(uint32_t id)
{
    // (a copy is made from its source's node, then a layout's field edit gives it its own model: made again)
    if (Objects::IsCopy(id))
        g_madeWith[id] = "\x01";
}

namespace Detail {
bool HasPath(uint32_t id)
{
    std::vector<V3> points;
    bool closed;
    return PathOf(id, &points, &closed);
}
float SpeedSet(uint32_t id)
{
    const Speed* s = SpeedOf(g_state, id);
    if (s)
        return s->mph;
    s = SpeedOf(g_baked, id);
    return s ? s->mph : 0.0f;
}
float DefaultMph(uint32_t id) { return PreviewMph(id); }
void ChangeSpeed(uint32_t id, float mph)
{
    NoteUndo();
    SetSpeed(&g_state, id, mph);
}
EmitterSound EmitterNow(uint32_t name)
{
    if (const EmitterSound* e = EmitterOf(g_state, name))
        return *e;
    if (const EmitterSound* e = EmitterOf(g_baked, name))
        return *e;
    EmitterSound e;
    e.name = name;
    return e;
}
void ChangeEmitter(const EmitterSound& e)
{
    NoteUndo();
    SetEmitter(&g_state, e);
    ApplyRadius(e);
}
float EmitterRadius(uint32_t name, bool* sphere)
{
    Obj::EmitterNode* emitter = Emitter(name);
    *sphere = emitter && emitter->m_shape == Obj::EMITTER_SHAPE_SPHERE;
    return emitter ? emitter->m_radius : 0.0f;
}
std::vector<std::pair<uint32_t, std::string>> Sounds()
{
    std::vector<std::pair<uint32_t, std::string>> out;
    if (Sfx::NoSound())
        return out;
    auto* waves = Sfx::g_levelWaves;
    int count = std::clamp(Sfx::g_levelCount, 0, 256);
    for (int i = 0; i < count; ++i)
        out.push_back({waves[i].checksum, Nodes::Label(waves[i].checksum) + (waves[i].looping ? " (loop)" : "")});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}
void SetUiActive(bool active) { g_uiActive = active; }
bool Previewing(uint32_t id) { return g_preview.id == id && id; }
bool PreviewPlaying() { return g_preview.playing; }
void PreviewPlay(uint32_t id)
{
    if (g_preview.id == id)
        g_preview.playing = !g_preview.playing;
    else
        PreviewStart(id);
}
void PreviewStep(uint32_t id)
{
    if (g_preview.id != id)
        PreviewStart(id);
    if (!g_preview.id)
        return;
    g_preview.playing = false;
    g_preview.distance += MphToUnits(PreviewMph(id)) * 0.5f; // (half a second along)
    PreviewPlace(g_preview.distance);
}
float PreviewDistance() { return g_preview.distance; }
float PreviewLength() { return PathLength(g_preview.points, g_preview.closed); }
} // namespace Detail

} // namespace Front::LevelEditor::Entities
