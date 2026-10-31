// The engine's scene lights: see scene_lights.h. The maths follows the original's x87 code (53-bit precision: double
// where it keeps values in registers, float where it stores them).

#include "game/gfx/scene_lights.h"

#include <cmath>
#include <cstdint>
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
#include "game/obj/movie_manager.h"
#include "game/sys/startup_init.h"

namespace Engine3D {

int g_sceneLightCount;
sSceneLight g_sceneLights[150]; // (kMaxSceneLights)

uint32_t* g_queuedNodes;
int g_queuedCount;
int g_queueDone;
int g_queueFrames;
int g_queueFrame;
Scripts::CStruct* g_queueParams;

namespace {

constexpr int kMaxSceneLights = 150;
constexpr float kPercent = 0.01f; // 0x0064544C

sSceneLight* SceneLights() { return g_sceneLights; }
int& SceneLightCount() { return g_sceneLightCount; }

// The queue.
uint32_t*& QueuedNodes() { return g_queuedNodes; }
int& QueuedCount() { return g_queuedCount; }
int& QueueDone() { return g_queueDone; }
int& QueueFrames() { return g_queueFrames; }
int& QueueFrame() { return g_queueFrame; }
Scripts::CStruct*& QueueParams() { return g_queueParams; }

// The table's iteration, inlined in the original: the first / next value (null at the end).
void* NextValue(ListKit::KeyMap* table)
{
    if (!table->StepIterator() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstValue(ListKit::KeyMap* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextValue(table);
}

sSceneLight* FindSceneLight(uint32_t name)
{
    for (int i = 0; i < SceneLightCount(); ++i) {
        if (SceneLights()[i].m_label == name)
            return &SceneLights()[i];
    }
    return nullptr;
}

CGeom* GeomOf(const CWorldCell* sector) { return static_cast<CGeom*>(sector->m_geomHandle); }
uint32_t VertexCount(CGeom* geom) { return reinterpret_cast<uint32_t(__thiscall*)(CGeom*)>(geom->m_vtable[36])(geom); }
void GetVertices(CGeom* geom, Maths::Vec4* out) { reinterpret_cast<void(__thiscall*)(CGeom*, Maths::Vec4*)>(geom->m_vtable[37])(geom, out); }
void SetColours(CGeom* geom, const uint32_t* in) { reinterpret_cast<void(__thiscall*)(CGeom*, const uint32_t*)>(geom->m_vtable[40])(geom, in); }

// What a light adds at a vertex: (radius - distance) / radius x its intensity - when the vertex is within its radius
// (the squared distance rounded to a float first); its colour's bytes times that summed into r, g, b.
struct Sums {
    double r = 0.0, g = 0.0, b = 0.0;
    bool lit = false;
    void AppendOne(const sSceneLight& light, double distanceSquared)
    {
        auto d2 = static_cast<float>(distanceSquared);
        double radius = light.m_radius;
        if (!(d2 < radius * radius))
            return;
        double f = (radius - std::sqrt(static_cast<double>(d2))) / radius * light.m_strength;
        const auto* colour = reinterpret_cast<const uint8_t*>(&light.m_shade);
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
double OffsetSquaredZXY(const float* p, const Maths::Vec4& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y, dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dx * dx + dy * dy;
}
double OffsetSquaredZYX(const float* p, const Maths::Vec4& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y, dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dy * dy + dx * dx;
}

} // namespace

// 0x0048FF40
sSceneLight* sSceneLight::MakeNew()
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    mp_sectors = memory ? new (memory) ListKit::KeyMap(4) : nullptr;
    auto* colour = reinterpret_cast<uint8_t*>(&m_shade);
    colour[0] = 0x80;
    colour[1] = 0x80;
    colour[2] = 0x80;
    m_radius = 0.0f;
    m_strength = 0.0f;
    return this;
}

// 0x0048F1A0
void sSceneLight::SetReach(float radius)
{
    if (radius == m_radius) // (NaN: set)
        return;
    m_radius = radius;
    CDynamicLightBase** slot = FindDynamicLight(m_label);
    if (!slot)
        return;
    (*slot)->radius = radius;
    (*slot)->inverseRadius = radius == 0.0f ? 1.0e10f : static_cast<float>(1.0 / radius);
}

// 0x0048F200
bool sSceneLight::TouchesSector(const CWorldCell* sector)
{
    return mp_sectors->FetchEntry(reinterpret_cast<uint32_t>(sector)) != nullptr;
}

// 0x0048FFD0
void sSceneLight::ClearSectors()
{
    mp_sectors->EmptyTable();
}

// 0x0048FFE0
void sSceneLight::FindSectors(Scripts::CArray* excluded)
{
    mp_sectors->EmptyTable();
    CWorldScene* scene = GetMainScene();
    if (!scene)
        return;
    ListKit::KeyMap* sectors = scene->mp_sectors;
    if (!sectors)
        return;
    for (auto* sector = static_cast<CWorldCell*>(FirstValue(sectors)); sector;
         sector = static_cast<CWorldCell*>(NextValue(sectors))) {
        if (sector->m_surfaceType == 0xED82767B || sector->m_surfaceType == 0xEC6542D3 || !sector->m_geomHandle)
            continue;
        const Maths::BBox* box = sector->FetchBounds();
        // The box's centre (x and y rounded to floats, z not), its half-diagonal, the distance to the light (its z
        // offset squared against itself rounded to a float).
        auto cx = static_cast<float>((static_cast<double>(box->max.x) + box->min.x) * 0.5);
        auto cy = static_cast<float>((static_cast<double>(box->max.y) + box->min.y) * 0.5);
        double cz = (static_cast<double>(box->max.z) + box->min.z) * 0.5;
        double hx = static_cast<double>(box->max.x) - cx, hy = static_cast<double>(box->max.y) - cy, hz = box->max.z - cz;
        double dx = static_cast<double>(cx) - m_atPoint.x, dy = static_cast<double>(cy) - m_atPoint.y, dz = cz - m_atPoint.z;
        auto distance = static_cast<float>(std::sqrt(dz * static_cast<float>(dz) + dy * dy + dx * dx));
        double half = std::sqrt(hz * hz + hy * hy + hx * hx);
        if (!(half + m_radius > distance))
            continue;
        uint32_t count = excluded ? excluded->m_extent : 0;
        bool skip = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (excluded->FetchPart(i) == sector->m_checksum) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        uint32_t key = reinterpret_cast<uint32_t>(sector);
        mp_sectors->StoreEntry(key, sector);
    }
}

// 0x0048F250
void __cdecl ApplySceneLightsToSector(CWorldCell* sector)
{
    CGeom* geom = GeomOf(sector);
    if (!geom)
        return;
    int lights[8];
    int found = 0;
    for (int i = 0; i < SceneLightCount(); ++i) {
        sSceneLight& light = SceneLights()[i];
        if (light.m_strength > 0.0f && light.TouchesSector(sector)) {
            lights[found] = i;
            if (++found >= 8)
                break;
        }
    }
    auto count = static_cast<int>(VertexCount(geom));
    if (count <= 0)
        return;
    if (found == 0) { // its own colours back
        auto* colours = static_cast<uint32_t*>(MemSys::Alloc(count * 4));
        geom->FetchTints(colours);
        SetColours(geom, colours);
        MemSys::Dealloc(colours);
        return;
    }
    auto* vertices = static_cast<Maths::Vec4*>(MemSys::Alloc(count * 16));
    auto* colours = static_cast<uint32_t*>(MemSys::Alloc(count * 4));
    GetVertices(geom, vertices);
    geom->FetchTints(colours);
    for (int v = 0; v < count; ++v) {
        Sums sums;
        for (int l = 0; l < found; ++l) {
            sSceneLight& light = SceneLights()[lights[l]];
            sums.AppendOne(light, OffsetSquaredZXY(&vertices[v].x, light.m_atPoint));
        }
        if (sums.lit)
            colours[v] = sums.Apply(colours[v]);
    }
    SetColours(geom, colours);
    MemSys::Dealloc(vertices);
    MemSys::Dealloc(colours);
}

// 0x0048F620
void __cdecl LightVertexColours(const uint32_t* in, uint32_t* out, const Maths::Vec4* positions,
                                const Maths::Vec4* normal, uint32_t count)
{
    int lights[3];
    int found = 0;
    for (int i = 0; i < SceneLightCount() && found < 3; ++i) {
        sSceneLight& light = SceneLights()[i];
        bool facing = true;
        if (normal) {
            double dx = static_cast<double>(positions->x) - light.m_atPoint.x;
            double dy = static_cast<double>(positions->y) - light.m_atPoint.y;
            double dz = static_cast<double>(positions->z) - light.m_atPoint.z;
            facing = dz * normal->z + dy * normal->y + dx * normal->x <= 0.0; // (NaN: not)
        }
        if (!(light.m_strength > 0.0f))
            continue;
        double rr = static_cast<double>(light.m_radius) * light.m_radius;
        double d2 = OffsetSquaredZYX(&positions->x, light.m_atPoint);
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
            sums.AppendOne(light, OffsetSquaredZYX(&positions[v].x, light.m_atPoint));
        }
        if (sums.lit)
            out[v] = sums.Apply(in[v]);
    }
}

// 0x0048FC00
void __cdecl ApplySceneLight(CWorldScene* /*scene*/, uint32_t name)
{
    if (!name)
        return;
    sSceneLight* light = FindSceneLight(name);
    if (!light || !light->mp_sectors)
        return;
    ListKit::KeyMap* sectors = light->mp_sectors;
    for (auto* sector = static_cast<CWorldCell*>(FirstValue(sectors)); sector;
         sector = static_cast<CWorldCell*>(NextValue(sectors))) {
        if (sector->m_geomHandle)
            ApplySceneLightsToSector(sector);
    }
}

// 0x0048FCA0
void __cdecl DeleteSceneLight(uint32_t name)
{
    CDynamicLightBase** dynamic = g_dynamicLights;
    int& dynamicCount = g_dynamicLightCount;
    for (int i = 0; i < dynamicCount; ++i) {
        if (dynamic[i]->owner == name) { // (no null check, as the original)
            MemSys::Hub::Pull()->Dealloc(dynamic[i]);
            dynamic[i] = dynamic[dynamicCount - 1];
            --dynamicCount;
            dynamic[dynamicCount] = nullptr;
            break;
        }
    }
    for (int i = 0; i < SceneLightCount(); ++i) {
        if (SceneLights()[i].m_label == name) {
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
        MemSys::FreeMalloc(QueuedNodes());
        QueuedNodes() = nullptr;
    }
    QueuedCount() = 0;
    QueueDone() = 0;
    QueueFrames() = 0;
    QueueFrame() = 0;
    if (Scripts::CStruct* params = QueueParams()) {
        params->Clear();
        Scripts::DeleteStruct(params);
        QueueParams() = nullptr;
    }
}

// 0x0048FE40
void __cdecl QueueSceneLights(const uint32_t* nodes, int count, int frames, Scripts::CStruct* params)
{
    ClearSceneLightQueue();
    Scripts::CStruct* copy = Scripts::NewStruct();
    if (copy)
        copy->PrepareAll();
    QueueParams() = copy;
    copy->AppendStructureIfAny(params);
    if (count) {
        QueuedNodes() = static_cast<uint32_t*>(MemSys::AllocBlock(count * 4));
        for (int i = 0; i < count; ++i)
            QueuedNodes()[i] = nodes[i];
    }
    QueueFrames() = frames;
    QueuedCount() = count;
}

// 0x004904E0
void __cdecl UpdateSceneLightQueue()
{
    if (!QueuedNodes() || GameObjects::g_gamePaused) // (paused)
        return;
    if (QueueFrame() > QueueFrames()) {
        ClearSceneLightQueue();
        return;
    }
    int target = QueueFrames() ? QueuedCount() * QueueFrame() / QueueFrames() : QueuedCount();
    for (; QueueDone() < target; ++QueueDone()) {
        Scripts::CStruct* node = LevelNodes::FetchVertex(QueuedNodes()[QueueDone()], true);
        UpdateSceneLight(LevelNodes::GetNodeName(node), QueueParams());
    }
    ++QueueFrame();
}

// 0x004901D0
void __cdecl UpdateSceneLight(uint32_t name, Scripts::CStruct* params)
{
    CWorldScene* scene = GetMainScene();
    sSceneLight* light = FindSceneLight(name);
    float value;
    int red, green = 0, blue = 0; // (the original leaves green and blue unset when they're missing)
    if (light) {
        if (params->FetchFloat(0x9E497FC6u, &value, 0)) {
            value = static_cast<float>(value * static_cast<double>(kPercent));
            if (value != light->m_strength) { // (NaN: set)
                light->m_strength = value;
                if (CDynamicLightBase** slot = FindDynamicLight(light->m_label))
                    (*slot)->intensity = value;
            }
        }
        if (params->FetchFloat(0x0999151Bu, &value, 0)) {
            light->SetReach(value);
            light->FindSectors(nullptr);
        }
        if (params->FetchIntValue(0x059EA070u, &red, 0)) {
            params->FetchIntValue(0x2F6511DEu, &green, 1);
            params->FetchIntValue(0x61C9354Bu, &blue, 1);
            uint32_t colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                              static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
            light->m_shade = colour;
            if (CDynamicLightBase** slot = FindDynamicLight(light->m_label))
                (*slot)->colour = colour;
        }
        ApplySceneLight(scene, name);
        return;
    }
    CDynamicLightBase** slot = FindDynamicLight(name);
    if (!slot)
        return;
    if (params->FetchFloat(0x9E497FC6u, &value, 0)) {
        value = static_cast<float>(value * static_cast<double>(kPercent));
        (*slot)->intensity = value;
    }
    if (params->FetchFloat(0x0999151Bu, &value, 0))
        reinterpret_cast<CDynamicLight*>(*slot)->SetReach(value);
    if (params->FetchIntValue(0x059EA070u, &red, 0)) {
        params->FetchIntValue(0x2F6511DEu, &green, 1);
        params->FetchIntValue(0x61C9354Bu, &blue, 1);
        (*slot)->colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                          static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
    }
}

// 0x00490400
sSceneLight* __cdecl AddSceneLight(uint32_t name, const Maths::Vec4* pos, float brightness, uint32_t colour,
                                   float radius, Scripts::CArray* excluded)
{
    if (SceneLightCount() >= kMaxSceneLights)
        return nullptr;
    double intensity = brightness * static_cast<double>(kPercent);
    sSceneLight* light = &SceneLights()[SceneLightCount()++];
    light->m_label = name;
    light->m_atPoint = *pos;
    if (light->m_strength != intensity) { // (the slot's old value; NaN: set)
        light->m_strength = static_cast<float>(intensity);
        if (CDynamicLightBase** slot = FindDynamicLight(name))
            (*slot)->intensity = static_cast<float>(intensity);
    }
    light->m_shade = colour;
    if (CDynamicLightBase** slot = FindDynamicLight(light->m_label))
        (*slot)->colour = colour;
    light->SetReach(radius);
    light->FindSectors(excluded);
    if (brightness > 0.0f)
        ApplySceneLight(GetMainScene(), name);
    return light;
}

// 0x00490570
void __cdecl CreateLightFromNode(Scripts::CStruct* node)
{
    bool dynamic = !node->HasBareFlag(0xE1509E4Fu);
    bool scene = !node->HasBareFlag(0x6F050E18u);
    uint32_t name = LevelNodes::GetNodeName(node);
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(node, &pos);
    float brightness = 100.0f;
    float unused = 300.0f; // (read and not used)
    float radius = 300.0f;
    node->FetchFloat(0x2689291Cu, &brightness, 0);
    node->FetchFloat(0x8D2C287Fu, &unused, 0);
    node->FetchFloat(0x833950CCu, &radius, 0);
    node->HasBareFlag(0x7C2552B9u); // (the result isn't used)
    uint32_t colour = 0x00FFFFFF;
    Scripts::CArray* colourArray = nullptr;
    node->FetchList(0x99A9B716u, &colourArray, 0);
    if (colourArray) {
        auto* bytes = reinterpret_cast<uint8_t*>(&colour);
        bytes[0] = static_cast<uint8_t>(colourArray->FetchPart(0));
        bytes[1] = static_cast<uint8_t>(colourArray->FetchPart(1));
        bytes[2] = static_cast<uint8_t>(colourArray->FetchPart(2));
    }
    uint32_t type = 0x485A0CDB;
    node->ReadHashValue(0xC2737050u, &type, 0);
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
        Scripts::CArray* excluded = nullptr;
        node->FetchList(0xAF3F7288u, &excluded, 0);
        AddSceneLight(name, &pos, brightness, colour, radius, excluded);
    }
}

} // namespace Nx

namespace Engine3D {

// 0x00640BC0 (a static initialiser): the 150 scene lights (each with its table of sectors).
void ConstructSceneLights()
{
    for (sSceneLight& light : g_sceneLights)
        light.MakeNew();
}

} // namespace Nx
