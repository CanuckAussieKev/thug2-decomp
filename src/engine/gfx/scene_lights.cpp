// The engine's scene lights: see scene_lights.h. The maths follows the original's x87 code (53-bit precision: double
// where it keeps values in registers, float where it stores them).

#include "engine/gfx/scene_lights.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/sector.h"
#include "engine/lists/key_map.h"
#include "engine/maths/shape_tests.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/movie_manager.h"
#include "engine/sys/startup_init.h"

namespace render {

int g_scene_light_count;
LevelLamp g_level_lamps[150]; // (kMaxSceneLights)

uint32_t* g_queued_nodes;
int g_queued_count;
int g_queue_done;
int g_queue_frames;
int g_queue_frame;
script::Struct* g_queue_params;

namespace {

constexpr int kMaxSceneLights = 150;
constexpr float kPercent = 0.01f; // 0x0064544C

LevelLamp* scene_light_array() { return g_level_lamps; }
int& scene_light_count() { return g_scene_light_count; }

// The queue.
uint32_t*& queued_nodes() { return g_queued_nodes; }
int& queued_count() { return g_queued_count; }
int& queue_done() { return g_queue_done; }
int& queue_frames() { return g_queue_frames; }
int& queue_frame() { return g_queue_frame; }
script::Struct*& queue_params() { return g_queue_params; }

// The table's iteration, inlined in the original: the first / next value (null at the end).
void* next_value(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}
void* get_first_value(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_value(table);
}

LevelLamp* find_scene_light(uint32_t name)
{
    for (int i = 0; i < scene_light_count(); ++i) {
        if (scene_light_array()[i].label_ == name)
            return &scene_light_array()[i];
    }
    return nullptr;
}

Geom* geom_of(const WorldCell* sector) { return static_cast<Geom*>(sector->geom_handle_); }
uint32_t get_vertex_count(Geom* geom) { return geom->fetch_vertex_tally(); }
void get_vertices(Geom* geom, maths::Vec4* out) { geom->get_vertices(out); }
void set_colours(Geom* geom, const uint32_t* in) { geom->change_colors(reinterpret_cast<const uint8_t*>(in)); }

// What a light adds at a vertex: (radius - distance) / radius x its intensity - when the vertex is within its radius
// (the squared distance rounded to a float first); its colour's bytes times that summed into r, g, b.
struct Sums {
    double r = 0.0, g = 0.0, b = 0.0;
    bool lit = false;
    void append_one(const LevelLamp& light, double distance_squared)
    {
        auto d2 = static_cast<float>(distance_squared);
        double radius = light.radius_;
        if (!(d2 < radius * radius))
            return;
        double f = (radius - std::sqrt(static_cast<double>(d2))) / radius * light.strength_;
        const auto* colour = reinterpret_cast<const uint8_t*>(&light.shade_);
        r += colour[0] * f;
        g += colour[1] * f;
        b += colour[2] * f;
        lit = true;
    }
    // A colour's RGB with the sums added (green and blue rounded to floats first, as the original stores them),
    // clamped at 255 and truncated (_ftol: the low byte).
    uint32_t get_apply(uint32_t colour) const
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
double offset_squared_zxy(const float* p, const maths::Vec4& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y,
           dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dx * dx + dy * dy;
}
double offset_squared_zyx(const float* p, const maths::Vec4& l)
{
    double dx = static_cast<double>(p[0]) - l.x, dy = static_cast<double>(p[1]) - l.y,
           dz = static_cast<double>(p[2]) - l.z;
    return dz * dz + dy * dy + dx * dx;
}

} // namespace

// 0x0048FF40
LevelLamp* LevelLamp::make_new()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    sectors_ = memory ? new (memory) lists::KeyMap(4) : nullptr;
    auto* colour = reinterpret_cast<uint8_t*>(&shade_);
    colour[0] = 0x80;
    colour[1] = 0x80;
    colour[2] = 0x80;
    radius_ = 0.0f;
    strength_ = 0.0f;
    return this;
}

// 0x0048F1A0
void LevelLamp::set_reach(float radius)
{
    if (radius == radius_) // (NaN: set)
        return;
    radius_ = radius;
    DynamicLightBase** slot = find_dynamic_light(label_);
    if (!slot)
        return;
    (*slot)->radius = radius;
    (*slot)->inverse_radius_ = radius == 0.0f ? 1.0e10f : static_cast<float>(1.0 / radius);
}

// 0x0048F200
bool LevelLamp::touches_sector(const WorldCell* sector)
{
    return sectors_->fetch_entry(reinterpret_cast<uint32_t>(sector)) != nullptr;
}

// 0x0048FFD0
void LevelLamp::clear_sectors() { sectors_->empty_table(); }

// 0x0048FFE0
void LevelLamp::find_sectors(script::Array* excluded)
{
    sectors_->empty_table();
    WorldScene* scene = get_main_scene();
    if (!scene)
        return;
    lists::KeyMap* sectors = scene->sectors_;
    if (!sectors)
        return;
    for (auto* sector = static_cast<WorldCell*>(get_first_value(sectors)); sector;
         sector = static_cast<WorldCell*>(next_value(sectors))) {
        if (sector->light_group_ == 0xED82767B || sector->light_group_ == 0xEC6542D3 || !sector->geom_handle_)
            continue;
        const maths::BBox* box = sector->fetch_bounds();
        // The box's centre (x and y rounded to floats, z not), its half-diagonal, the distance to the light (its z
        // offset squared against itself rounded to a float).
        auto cx = static_cast<float>((static_cast<double>(box->max.x) + box->min.x) * 0.5);
        auto cy = static_cast<float>((static_cast<double>(box->max.y) + box->min.y) * 0.5);
        double cz = (static_cast<double>(box->max.z) + box->min.z) * 0.5;
        double hx = static_cast<double>(box->max.x) - cx, hy = static_cast<double>(box->max.y) - cy,
               hz = box->max.z - cz;
        double dx = static_cast<double>(cx) - at_point_.x, dy = static_cast<double>(cy) - at_point_.y,
               dz = cz - at_point_.z;
        auto distance = static_cast<float>(std::sqrt(dz * static_cast<float>(dz) + dy * dy + dx * dx));
        double half = std::sqrt(hz * hz + hy * hy + hx * hx);
        if (!(half + radius_ > distance))
            continue;
        uint32_t count = excluded ? excluded->extent_ : 0;
        bool skip = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (excluded->fetch_part(i) == sector->checksum_) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        uint32_t key = reinterpret_cast<uint32_t>(sector);
        sectors_->store_entry(key, sector);
    }
}

// 0x0048F250
void __cdecl apply_scene_lights_to_sector(WorldCell* sector)
{
    Geom* geom = geom_of(sector);
    if (!geom)
        return;
    int lights[8];
    int found = 0;
    for (int i = 0; i < scene_light_count(); ++i) {
        LevelLamp& light = scene_light_array()[i];
        if (light.strength_ > 0.0f && light.touches_sector(sector)) {
            lights[found] = i;
            if (++found >= 8)
                break;
        }
    }
    auto count = static_cast<int>(get_vertex_count(geom));
    if (count <= 0)
        return;
    if (found == 0) { // its own colours back
        auto* colours = static_cast<uint32_t*>(memory::alloc(count * 4));
        geom->fetch_tints(colours);
        set_colours(geom, colours);
        memory::dealloc(colours);
        return;
    }
    auto* vertices = static_cast<maths::Vec4*>(memory::alloc(count * 16));
    auto* colours = static_cast<uint32_t*>(memory::alloc(count * 4));
    get_vertices(geom, vertices);
    geom->fetch_tints(colours);
    for (int v = 0; v < count; ++v) {
        Sums sums;
        for (int l = 0; l < found; ++l) {
            LevelLamp& light = scene_light_array()[lights[l]];
            sums.append_one(light, offset_squared_zxy(&vertices[v].x, light.at_point_));
        }
        if (sums.lit)
            colours[v] = sums.get_apply(colours[v]);
    }
    set_colours(geom, colours);
    memory::dealloc(vertices);
    memory::dealloc(colours);
}

// 0x0048F620
void __cdecl light_vertex_colours(const uint32_t* in, uint32_t* out, const maths::Vec4* positions,
                                  const maths::Vec4* normal, uint32_t count)
{
    int lights[3];
    int found = 0;
    for (int i = 0; i < scene_light_count() && found < 3; ++i) {
        LevelLamp& light = scene_light_array()[i];
        bool facing = true;
        if (normal) {
            double dx = static_cast<double>(positions->x) - light.at_point_.x;
            double dy = static_cast<double>(positions->y) - light.at_point_.y;
            double dz = static_cast<double>(positions->z) - light.at_point_.z;
            facing = dz * normal->z + dy * normal->y + dx * normal->x <= 0.0; // (NaN: not)
        }
        if (!(light.strength_ > 0.0f))
            continue;
        double rr = static_cast<double>(light.radius_) * light.radius_;
        double d2 = offset_squared_zyx(&positions->x, light.at_point_);
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
            LevelLamp& light = scene_light_array()[lights[l]];
            sums.append_one(light, offset_squared_zyx(&positions[v].x, light.at_point_));
        }
        if (sums.lit)
            out[v] = sums.get_apply(in[v]);
    }
}

// 0x0048FC00
void __cdecl apply_scene_light(WorldScene* /*scene*/, uint32_t name)
{
    if (!name)
        return;
    LevelLamp* light = find_scene_light(name);
    if (!light || !light->sectors_)
        return;
    lists::KeyMap* sectors = light->sectors_;
    for (auto* sector = static_cast<WorldCell*>(get_first_value(sectors)); sector;
         sector = static_cast<WorldCell*>(next_value(sectors))) {
        if (sector->geom_handle_)
            apply_scene_lights_to_sector(sector);
    }
}

// 0x0048FCA0
void __cdecl delete_scene_light(uint32_t name)
{
    DynamicLightBase** dynamic = g_dynamic_lights;
    int& dynamic_count = g_dynamic_light_count;
    for (int i = 0; i < dynamic_count; ++i) {
        if (dynamic[i]->owner == name) { // (no null check, as the original)
            memory::Hub::get_pull()->dealloc(dynamic[i]);
            dynamic[i] = dynamic[dynamic_count - 1];
            --dynamic_count;
            dynamic[dynamic_count] = nullptr;
            break;
        }
    }
    for (int i = 0; i < scene_light_count(); ++i) {
        if (scene_light_array()[i].label_ == name) {
            scene_light_array()[i].clear_sectors(); // (its table isn't freed: the last light's is copied over it)
            --scene_light_count();
            scene_light_array()[i] = scene_light_array()[scene_light_count()];
            return;
        }
    }
}

// 0x0048FDA0
void __cdecl delete_all_scene_lights()
{
    for (int i = 0; i < scene_light_count(); ++i)
        scene_light_array()[i].clear_sectors();
    scene_light_count() = 0;
}

// 0x0048FDE0
void __cdecl clear_scene_light_queue()
{
    if (queued_nodes()) {
        memory::free_malloc(queued_nodes());
        queued_nodes() = nullptr;
    }
    queued_count() = 0;
    queue_done() = 0;
    queue_frames() = 0;
    queue_frame() = 0;
    if (script::Struct* params = queue_params()) {
        params->get_clear();
        script::delete_struct(params);
        queue_params() = nullptr;
    }
}

// 0x0048FE40
void __cdecl queue_scene_lights(const uint32_t* nodes, int count, int frames, script::Struct* params)
{
    clear_scene_light_queue();
    script::Struct* copy = script::spawn_param_bag();
    if (copy)
        new (copy) script::Struct;
    queue_params() = copy;
    copy->append_structure_if_any(params);
    if (count) {
        queued_nodes() = static_cast<uint32_t*>(memory::alloc_block(count * 4));
        for (int i = 0; i < count; ++i)
            queued_nodes()[i] = nodes[i];
    }
    queue_frames() = frames;
    queued_count() = count;
}

// 0x004904E0
void __cdecl update_scene_light_queue()
{
    if (!queued_nodes() || objects::g_simulation_halted) // (paused)
        return;
    if (queue_frame() > queue_frames()) {
        clear_scene_light_queue();
        return;
    }
    int target = queue_frames() ? queued_count() * queue_frame() / queue_frames() : queued_count();
    for (; queue_done() < target; ++queue_done()) {
        script::Struct* node = level_nodes::fetch_vertex(queued_nodes()[queue_done()], true);
        update_scene_light(level_nodes::get_node_name(node), queue_params());
    }
    ++queue_frame();
}

// 0x004901D0
void __cdecl update_scene_light(uint32_t name, script::Struct* params)
{
    WorldScene* scene = get_main_scene();
    LevelLamp* light = find_scene_light(name);
    float value;
    int red, green = 0, blue = 0; // (the original leaves green and blue unset when they're missing)
    if (light) {
        if (params->fetch_float(0x9E497FC6u, &value, 0)) {
            value = static_cast<float>(value * static_cast<double>(kPercent));
            if (value != light->strength_) { // (NaN: set)
                light->strength_ = value;
                if (DynamicLightBase** slot = find_dynamic_light(light->label_))
                    (*slot)->intensity = value;
            }
        }
        if (params->fetch_float(0x0999151Bu, &value, 0)) {
            light->set_reach(value);
            light->find_sectors(nullptr);
        }
        if (params->fetch_int_value(0x059EA070u, &red, 0)) {
            params->fetch_int_value(0x2F6511DEu, &green, 1);
            params->fetch_int_value(0x61C9354Bu, &blue, 1);
            uint32_t colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                              static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
            light->shade_ = colour;
            if (DynamicLightBase** slot = find_dynamic_light(light->label_))
                (*slot)->colour = colour;
        }
        apply_scene_light(scene, name);
        return;
    }
    DynamicLightBase** slot = find_dynamic_light(name);
    if (!slot)
        return;
    if (params->fetch_float(0x9E497FC6u, &value, 0)) {
        value = static_cast<float>(value * static_cast<double>(kPercent));
        (*slot)->intensity = value;
    }
    if (params->fetch_float(0x0999151Bu, &value, 0))
        (*slot)->set_reach(value);
    if (params->fetch_int_value(0x059EA070u, &red, 0)) {
        params->fetch_int_value(0x2F6511DEu, &green, 1);
        params->fetch_int_value(0x61C9354Bu, &blue, 1);
        (*slot)->colour = static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << 8 |
                          static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 | 0xFF000000u;
    }
}

// 0x00490400
LevelLamp* __cdecl register_scene_light(uint32_t name, const maths::Vec4* pos, float brightness, uint32_t colour,
                                        float radius, script::Array* excluded)
{
    if (scene_light_count() >= kMaxSceneLights)
        return nullptr;
    double intensity = brightness * static_cast<double>(kPercent);
    LevelLamp* light = &scene_light_array()[scene_light_count()++];
    light->label_ = name;
    light->at_point_ = *pos;
    if (light->strength_ != intensity) { // (the slot's old value; NaN: set)
        light->strength_ = static_cast<float>(intensity);
        if (DynamicLightBase** slot = find_dynamic_light(name))
            (*slot)->intensity = static_cast<float>(intensity);
    }
    light->shade_ = colour;
    if (DynamicLightBase** slot = find_dynamic_light(light->label_))
        (*slot)->colour = colour;
    light->set_reach(radius);
    light->find_sectors(excluded);
    if (brightness > 0.0f)
        apply_scene_light(get_main_scene(), name);
    return light;
}

// 0x00490570
void __cdecl create_light_from_node(script::Struct* node)
{
    bool dynamic = !node->has_bare_flag(0xE1509E4Fu);
    bool scene = !node->has_bare_flag(0x6F050E18u);
    uint32_t name = level_nodes::get_node_name(node);
    maths::Vec4 pos;
    level_nodes::fetch_place(node, &pos);
    float brightness = 100.0f;
    float unused = 300.0f; // (read and not used)
    float radius = 300.0f;
    node->fetch_float(0x2689291Cu, &brightness, 0);
    node->fetch_float(0x8D2C287Fu, &unused, 0);
    node->fetch_float(0x833950CCu, &radius, 0);
    node->has_bare_flag(0x7C2552B9u); // (the result isn't used)
    uint32_t colour = 0x00FFFFFF;
    script::Array* colour_array = nullptr;
    node->fetch_list(0x99A9B716u, &colour_array, 0);
    if (colour_array) {
        auto* bytes = reinterpret_cast<uint8_t*>(&colour);
        bytes[0] = static_cast<uint8_t>(colour_array->fetch_part(0));
        bytes[1] = static_cast<uint8_t>(colour_array->fetch_part(1));
        bytes[2] = static_cast<uint8_t>(colour_array->fetch_part(2));
    }
    uint32_t type = 0x485A0CDB;
    node->read_hash_value(0xC2737050u, &type, 0);
    if (dynamic) {
        if (DynamicLightBase* light = new_dynamic_light(type == 0x485A0CDB ? 1 : 0)) {
            light->owner = name;
            std::memcpy(light->pos, &pos, sizeof(light->pos));
            light->intensity = static_cast<float>(brightness * static_cast<double>(kPercent));
            light->colour = colour;
            light->radius = radius;
            light->inverse_radius_ = radius == 0.0f ? 1.0e10f : static_cast<float>(1.0 / radius);
            light->unknown2c_ = true;
        }
    }
    if (scene) {
        script::Array* excluded = nullptr;
        node->fetch_list(0xAF3F7288u, &excluded, 0);
        register_scene_light(name, &pos, brightness, colour, radius, excluded);
    }
}

} // namespace render

namespace render {

// 0x00640BC0 (a static initialiser): the 150 scene lights (each with its table of sectors).
void construct_scene_lights()
{
    for (LevelLamp& light : g_level_lamps)
        light.make_new();
}

} // namespace render
