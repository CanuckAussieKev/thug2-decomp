// The engine's scene lights: see scene_lights.h. The maths follows the original's x87 code (53-bit precision: double
// where it keeps values in registers, float where it stores them).

#include "game/gfx/scene_lights.h"

#include <cmath>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/sector.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/nx/dynamic_light.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr int kMaxSceneLights = 150;
constexpr float kPercent = 0.01f; // 0x0064544C

sSceneLight* SceneLights() { return &GameVariable<sSceneLight>(0x006FAF80); }
int& SceneLightCount() { return GameVariable<int>(0x006FAEF4); }

// The queue.
uint32_t*& QueuedNodes() { return GameVariable<uint32_t*>(0x006FAEF8); }
int& QueuedCount() { return GameVariable<int>(0x006FAEFC); }
int& QueueDone() { return GameVariable<int>(0x006FAF00); }
int& QueueFrames() { return GameVariable<int>(0x006FAF04); }
int& QueueFrame() { return GameVariable<int>(0x006FAF08); }
Script::CStruct*& QueueParams() { return GameVariable<Script::CStruct*>(0x006FAF0C); }

// The table's iteration, inlined in the original: the first / next value (null at the end).
void* NextValue(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstValue(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextValue(table);
}

sSceneLight* FindSceneLight(uint32_t name)
{
    for (int i = 0; i < SceneLightCount(); ++i) {
        if (SceneLights()[i].m_name == name)
            return &SceneLights()[i];
    }
    return nullptr;
}

CGeom* GeomOf(const CSector* sector) { return static_cast<CGeom*>(sector->mp_geom); }
uint32_t VertexCount(CGeom* geom) { return reinterpret_cast<uint32_t(__thiscall*)(CGeom*)>(geom->m_vtable[36])(geom); }
void GetVertices(CGeom* geom, Mth::Vector* out) { reinterpret_cast<void(__thiscall*)(CGeom*, Mth::Vector*)>(geom->m_vtable[37])(geom, out); }
void SetColours(CGeom* geom, const uint32_t* in) { reinterpret_cast<void(__thiscall*)(CGeom*, const uint32_t*)>(geom->m_vtable[40])(geom, in); }

// What a light adds at a vertex: (radius - distance) / radius x its intensity - when the vertex is within its radius
// (the squared distance rounded to a float first); its colour's bytes times that summed into r, g, b.
struct Sums {
    double r = 0.0, g = 0.0, b = 0.0;
    bool lit = false;
    void Add(const sSceneLight& light, double distanceSquared)
    {
        auto d2 = static_cast<float>(distanceSquared);
        double radius = light.m_radius;
        if (!(d2 < radius * radius))
            return;
        double f = (radius - std::sqrt(static_cast<double>(d2))) / radius * light.m_intensity;
        const auto* colour = reinterpret_cast<const uint8_t*>(&light.m_colour);
        r += colour[0] * f;
        g += colour[1] * f;
        b += colour[2] * f;
        lit = true;
    }
    // A colour's RGB with the sums added (green and blue rounded to floats first, as the original stores them),
    // clamped at 255 and truncated (_ftol: the low byte).
    uint32_t Apply(uint32_t colour) const
    {
        auto* bytes = reinterpret_cast<uint8_t*>(&colour);
        double sums[3] = {r, static_cast<float>(g), static_cast<float>(b)};
        for (int i = 0; i < 3; ++i) {
            double v = bytes[i] + sums[i];
            if (v > 255.0)
                v = 255.0;
            bytes[i] = static_cast<uint8_t>(static_cast<int64_t>(v));
        }
        return colour;
    }
};

// A position's offset from a light, squared: the original's two orders of summing.
double OffsetSquaredZXY(const float* p, const Mth::Vector& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y, dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dx * dx + dy * dy;
}
double OffsetSquaredZYX(const float* p, const Mth::Vector& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y, dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dy * dy + dx * dx;
}

} // namespace

// 0x0048FF40
sSceneLight* sSceneLight::Construct()
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    mp_sectors = memory ? new (memory) Lst::HashTable(4) : nullptr;
    auto* colour = reinterpret_cast<uint8_t*>(&m_colour);
    colour[0] = 0x80;
    colour[1] = 0x80;
    colour[2] = 0x80;
    m_radius = 0.0f;
    m_intensity = 0.0f;
    return this;
}

// 0x0048F1A0
void sSceneLight::SetRadius(float radius)
{
    if (radius == m_radius) // (NaN: set)
        return;
    m_radius = radius;
    CDynamicLightBase** slot = FindDynamicLight(m_name);
    if (!slot)
        return;
    (*slot)->radius = radius;
    (*slot)->inverseRadius = radius == 0.0f ? 1.0e10f : static_cast<float>(1.0 / radius);
}

// 0x0048F200
bool sSceneLight::AffectsSector(const CSector* sector)
{
    return mp_sectors->GetItem(reinterpret_cast<uint32_t>(sector)) != nullptr;
}

// 0x0048FFD0
void sSceneLight::ClearSectors()
{
    mp_sectors->FlushAllItems();
}

// 0x0048FFE0
void sSceneLight::FindSectors(Script::CArray* excluded)
{
    mp_sectors->FlushAllItems();
    CScene* scene = GetMainScene();
    if (!scene)
        return;
    Lst::HashTable* sectors = scene->mp_sectors;
    if (!sectors)
        return;
    for (auto* sector = static_cast<CSector*>(FirstValue(sectors)); sector;
         sector = static_cast<CSector*>(NextValue(sectors))) {
        if (sector->m_terrain == 0xED82767B || sector->m_terrain == 0xEC6542D3 || !sector->mp_geom)
            continue;
        const Mth::BBox* box = sector->GetBoundingBox();
        // The box's centre (x and y rounded to floats, z not), its half-diagonal, the distance to the light (its z
        // offset squared against itself rounded to a float).
        auto cx = static_cast<float>((static_cast<double>(box->max.x) + box->min.x) * 0.5);
        auto cy = static_cast<float>((static_cast<double>(box->max.y) + box->min.y) * 0.5);
        double cz = (static_cast<double>(box->max.z) + box->min.z) * 0.5;
        double hx = static_cast<double>(box->max.x) - cx, hy = static_cast<double>(box->max.y) - cy, hz = box->max.z - cz;
        double dx = static_cast<double>(cx) - m_pos.x, dy = static_cast<double>(cy) - m_pos.y, dz = cz - m_pos.z;
        auto distance = static_cast<float>(std::sqrt(dz * static_cast<float>(dz) + dy * dy + dx * dx));
        double half = std::sqrt(hz * hz + hy * hy + hx * hx);
        if (!(half + m_radius > distance))
            continue;
        uint32_t count = excluded ? excluded->m_size : 0;
        bool skip = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (excluded->GetElement(i) == sector->m_checksum) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        uint32_t key = reinterpret_cast<uint32_t>(sector);
        mp_sectors->PutItem(key, sector);
    }
}

// 0x0048F250
void __cdecl ApplySceneLightsToSector(CSector* sector)
{
    CGeom* geom = GeomOf(sector);
    if (!geom)
        return;
    int lights[8];
    int found = 0;
    for (int i = 0; i < SceneLightCount(); ++i) {
        sSceneLight& light = SceneLights()[i];
        if (light.m_intensity > 0.0f && light.AffectsSector(sector)) {
            lights[found] = i;
            if (++found >= 8)
                break;
        }
    }
    auto count = static_cast<int>(VertexCount(geom));
    if (count <= 0)
        return;
    if (found == 0) { // its own colours back
        auto* colours = static_cast<uint32_t*>(Mem::New(count * 4));
        geom->GetColors(colours);
        SetColours(geom, colours);
        Mem::Free(colours);
        return;
    }
    auto* vertices = static_cast<Mth::Vector*>(Mem::New(count * 16));
    auto* colours = static_cast<uint32_t*>(Mem::New(count * 4));
    GetVertices(geom, vertices);
    geom->GetColors(colours);
    for (int v = 0; v < count; ++v) {
        Sums sums;
        for (int l = 0; l < found; ++l) {
            sSceneLight& light = SceneLights()[lights[l]];
            sums.Add(light, OffsetSquaredZXY(&vertices[v].x, light.m_pos));
        }
        if (sums.lit)
            colours[v] = sums.Apply(colours[v]);
    }
    SetColours(geom, colours);
    Mem::Free(vertices);
    Mem::Free(colours);
}

// 0x0048F620
void __cdecl LightVertexColours(const uint32_t* in, uint32_t* out, const Mth::Vector* positions,
                                const Mth::Vector* normal, uint32_t count)
{
    int lights[3];
    int found = 0;
    for (int i = 0; i < SceneLightCount() && found < 3; ++i) {
        sSceneLight& light = SceneLights()[i];
        bool facing = true;
        if (normal) {
            double dx = static_cast<double>(positions->x) - light.m_pos.x;
            double dy = static_cast<double>(positions->y) - light.m_pos.y;
            double dz = static_cast<double>(positions->z) - light.m_pos.z;
            facing = dz * normal->z + dy * normal->y + dx * normal->x <= 0.0; // (NaN: not)
        }
        if (!(light.m_intensity > 0.0f))
            continue;
        double rr = static_cast<double>(light.m_radius) * light.m_radius;
        double d2 = OffsetSquaredZYX(&positions->x, light.m_pos);
        if (!(rr >= d2) || !facing)
            continue;
        lights[found++] = i;
    }
    if (found == 0) {
        std::memcpy(out, in, count * 4);
        return;
    }
    for (uint32_t v = 0; v < count; ++v) {
        Sums sums;
        for (int l = 0; l < found; ++l) {
            sSceneLight& light = SceneLights()[lights[l]];
            sums.Add(light, OffsetSquaredZYX(&positions[v].x, light.m_pos));
        }
        if (sums.lit)
            out[v] = sums.Apply(in[v]);
    }
}

// 0x0048FC00
void __cdecl ApplySceneLight(CScene* /*scene*/, uint32_t name)
{
    if (!name)
        return;
    sSceneLight* light = FindSceneLight(name);
    if (!light || !light->mp_sectors)
        return;
    Lst::HashTable* sectors = light->mp_sectors;
    for (auto* sector = static_cast<CSector*>(FirstValue(sectors)); sector;
         sector = static_cast<CSector*>(NextValue(sectors))) {
        if (sector->mp_geom)
            ApplySceneLightsToSector(sector);
    }
}

// 0x0048FCA0
void __cdecl DeleteSceneLight(uint32_t name)
{
    CDynamicLightBase** dynamic = &GameVariable<CDynamicLightBase*>(0x006FAAF0);
    int& dynamicCount = GameVariable<int>(0x006FAEF0);
    for (int i = 0; i < dynamicCount; ++i) {
        if (dynamic[i]->owner == name) { // (no null check, as the original)
            Mem::Manager::Get()->Free(dynamic[i]);
            dynamic[i] = dynamic[dynamicCount - 1];
            --dynamicCount;
            dynamic[dynamicCount] = nullptr;
            break;
        }
    }
    for (int i = 0; i < SceneLightCount(); ++i) {
        if (SceneLights()[i].m_name == name) {
            SceneLights()[i].ClearSectors(); // (its table isn't freed: the last light's is copied over it)
            --SceneLightCount();
            SceneLights()[i] = SceneLights()[SceneLightCount()];
            return;
        }
    }
}

// 0x0048FDA0
void __cdecl DeleteAllSceneLights()
{
    for (int i = 0; i < SceneLightCount(); ++i)
        SceneLights()[i].ClearSectors();
    SceneLightCount() = 0;
}

// 0x0048FDE0
void __cdecl ClearSceneLightQueue()
{
    if (QueuedNodes()) {
        Mem::FreeMalloc(QueuedNodes());
        QueuedNodes() = nullptr;
    }
    QueuedCount() = 0;
    QueueDone() = 0;
    QueueFrames() = 0;
    QueueFrame() = 0;
    if (Script::CStruct* params = QueueParams()) {
        params->Clear();
        Script::DeleteStruct(params);
        QueueParams() = nullptr;
    }
}

// 0x0048FE40
void __cdecl QueueSceneLights(const uint32_t* nodes, int count, int frames, Script::CStruct* params)
{
    ClearSceneLightQueue();
    Script::CStruct* copy = Script::NewStruct();
    if (copy)
        copy->Init();
    QueueParams() = copy;
    copy->AppendStructureIfAny(params);
    if (count) {
        QueuedNodes() = static_cast<uint32_t*>(Mem::Malloc(count * 4));
        for (int i = 0; i < count; ++i)
            QueuedNodes()[i] = nodes[i];
    }
    QueueFrames() = frames;
    QueuedCount() = count;
}

// 0x004904E0
void __cdecl UpdateSceneLightQueue()
{
    if (!QueuedNodes() || GameVariable<bool>(0x006FA7D4)) // (paused)
        return;
    if (QueueFrame() > QueueFrames()) {
        ClearSceneLightQueue();
        return;
    }
    int target = QueueFrames() ? QueuedCount() * QueueFrame() / QueueFrames() : QueuedCount();
    for (; QueueDone() < target; ++QueueDone()) {
        Script::CStruct* node = SkateScript::GetNode(QueuedNodes()[QueueDone()], true);
        UpdateSceneLight(SkateScript::GetNodeName(node), QueueParams());
    }
    ++QueueFrame();
}

// 0x004901D0
void __cdecl UpdateSceneLight(uint32_t name, Script::CStruct* params)
{
    CScene* scene = GetMainScene();
    sSceneLight* light = FindSceneLight(name);
    float value;
    int red, green = 0, blue = 0; // (the original leaves green and blue unset when they're missing)
    if (light) {
        if (params->GetFloat(0x9E497FC6u, &value, 0)) {
            value = static_cast<float>(value * static_cast<double>(kPercent));
            if (value != light->m_intensity) { // (NaN: set)
                light->m_intensity = value;
                if (CDynamicLightBase** slot = FindDynamicLight(light->m_name))
                    (*slot)->intensity = value;
            }
        }
        if (params->GetFloat(0x0999151Bu, &value, 0)) {
            light->SetRadius(value);
            light->FindSectors(nullptr);
        }
        if (params->GetInteger(0x059EA070u, &red, 0)) {
            params->GetInteger(0x2F6511DEu, &green, 1);
            params->GetInteger(0x61C9354Bu, &blue, 1);
            uint32_t colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                              static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
            light->m_colour = colour;
            if (CDynamicLightBase** slot = FindDynamicLight(light->m_name))
                (*slot)->colour = colour;
        }
        ApplySceneLight(scene, name);
        return;
    }
    CDynamicLightBase** slot = FindDynamicLight(name);
    if (!slot)
        return;
    if (params->GetFloat(0x9E497FC6u, &value, 0)) {
        value = static_cast<float>(value * static_cast<double>(kPercent));
        (*slot)->intensity = value;
    }
    if (params->GetFloat(0x0999151Bu, &value, 0))
        reinterpret_cast<CDynamicLight*>(*slot)->SetRadius(value);
    if (params->GetInteger(0x059EA070u, &red, 0)) {
        params->GetInteger(0x2F6511DEu, &green, 1);
        params->GetInteger(0x61C9354Bu, &blue, 1);
        (*slot)->colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                          static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
    }
}

// 0x00490400
sSceneLight* __cdecl AddSceneLight(uint32_t name, const Mth::Vector* pos, float brightness, uint32_t colour,
                                   float radius, Script::CArray* excluded)
{
    if (SceneLightCount() >= kMaxSceneLights)
        return nullptr;
    double intensity = brightness * static_cast<double>(kPercent);
    sSceneLight* light = &SceneLights()[SceneLightCount()++];
    light->m_name = name;
    light->m_pos = *pos;
    if (light->m_intensity != intensity) { // (the slot's old value; NaN: set)
        light->m_intensity = static_cast<float>(intensity);
        if (CDynamicLightBase** slot = FindDynamicLight(name))
            (*slot)->intensity = static_cast<float>(intensity);
    }
    light->m_colour = colour;
    if (CDynamicLightBase** slot = FindDynamicLight(light->m_name))
        (*slot)->colour = colour;
    light->SetRadius(radius);
    light->FindSectors(excluded);
    if (brightness > 0.0f)
        ApplySceneLight(GetMainScene(), name);
    return light;
}

// 0x00490570
void __cdecl CreateLightFromNode(Script::CStruct* node)
{
    bool dynamic = !node->ContainsFlag(0xE1509E4Fu);
    bool scene = !node->ContainsFlag(0x6F050E18u);
    uint32_t name = SkateScript::GetNodeName(node);
    Mth::Vector pos;
    SkateScript::GetPosition(node, &pos);
    float brightness = 100.0f;
    float unused = 300.0f; // (read and not used)
    float radius = 300.0f;
    node->GetFloat(0x2689291Cu, &brightness, 0);
    node->GetFloat(0x8D2C287Fu, &unused, 0);
    node->GetFloat(0x833950CCu, &radius, 0);
    node->ContainsFlag(0x7C2552B9u); // (the result isn't used)
    uint32_t colour = 0x00FFFFFF;
    Script::CArray* colourArray = nullptr;
    node->GetArray(0x99A9B716u, &colourArray, 0);
    if (colourArray) {
        auto* bytes = reinterpret_cast<uint8_t*>(&colour);
        bytes[0] = static_cast<uint8_t>(colourArray->GetElement(0));
        bytes[1] = static_cast<uint8_t>(colourArray->GetElement(1));
        bytes[2] = static_cast<uint8_t>(colourArray->GetElement(2));
    }
    uint32_t type = 0x485A0CDB;
    node->GetChecksum(0xC2737050u, &type, 0);
    if (dynamic) {
        if (CDynamicLightBase* light = NewDynamicLight(type == 0x485A0CDB ? 1 : 0)) {
            light->owner = name;
            std::memcpy(light->pos, &pos, sizeof(light->pos));
            light->intensity = static_cast<float>(brightness * static_cast<double>(kPercent));
            light->colour = colour;
            light->radius = radius;
            light->inverseRadius = radius == 0.0f ? 1.0e10f : static_cast<float>(1.0 / radius);
            light->unknown2C = true;
        }
    }
    if (scene) {
        Script::CArray* excluded = nullptr;
        node->GetArray(0xAF3F7288u, &excluded, 0);
        AddSceneLight(name, &pos, brightness, colour, radius, excluded);
    }
}

} // namespace Nx

REPLACE(0x0048FF40, &Nx::sSceneLight::Construct);
REPLACE(0x0048F1A0, &Nx::sSceneLight::SetRadius);
REPLACE(0x0048F200, &Nx::sSceneLight::AffectsSector);
REPLACE(0x0048FFD0, &Nx::sSceneLight::ClearSectors); // (a jump to FlushAllItems)
REPLACE(0x0048FFE0, &Nx::sSceneLight::FindSectors);
REPLACE(0x0048F250, Nx::ApplySceneLightsToSector);
REPLACE(0x0048F620, Nx::LightVertexColours);
REPLACE(0x0048FC00, Nx::ApplySceneLight);
REPLACE(0x0048FCA0, Nx::DeleteSceneLight);
REPLACE(0x0048FDA0, Nx::DeleteAllSceneLights);
REPLACE(0x0048FDE0, Nx::ClearSceneLightQueue);
REPLACE(0x0048FE40, Nx::QueueSceneLights);
REPLACE(0x004904E0, Nx::UpdateSceneLightQueue);
REPLACE(0x004901D0, Nx::UpdateSceneLight);
REPLACE(0x00490400, Nx::AddSceneLight);
REPLACE(0x00490570, Nx::CreateLightFromNode);
