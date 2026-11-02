// level_nodes's node functions: a node's name, links, position, angles and orientation, by its structure or by its
// name (nodes.h).

#include "engine/script/nodes.h"

#include <cmath>
#include <cstdint>
#include <array>
#include <span>

#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/script/node_manager.h"
#include "engine/script/cache.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/script_cfuncs.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/core/checked_index.h"
#include "engine/script/cscript.h"

namespace level_nodes {

using script::Array;
using script::Struct;
using script::VectorValue;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;        // name
constexpr uint32_t kLinks = 0x2E7D5EE7;       // links
constexpr uint32_t kPos = 0x7F261953;         // pos
constexpr uint32_t kPosition = 0xB9D31B0A;    // position
constexpr uint32_t kAngles = 0x9D2D0915;      // angles
constexpr uint32_t kOrientation = 0xC97F3AA9; // orientation
constexpr uint32_t kScr = 0xA6D2D890;         // scr (SetEventHandler's script)
constexpr uint32_t kEndGap = 0xE5399FB2;      // EndGap
constexpr uint32_t kGapScript = 0x7C9E51AB;   // gapscript
constexpr int kDeepestLevel = 5;

// Script tokens (qb.h).
constexpr uint8_t kTokenEquals = 0x07;
constexpr uint8_t kTokenOpenParenthesis = 0x0E;
constexpr uint8_t kTokenKeyHash = 0x16;
constexpr uint8_t kTokenRepeat = 0x21;
constexpr uint8_t kTokenEndOfScript = 0x24;
constexpr uint8_t kTokenNot = 0x39;
constexpr uint8_t kTokenCFunction = 0x43;           // a C function already looked up
constexpr uint8_t kTokenMemberFunction = 0x44;      // a member function's name (no table entry)
constexpr uint8_t kTokenMemberFunctionEntry = 0x46; // a member function's table entry (its name first)
constexpr uint8_t kTokenIf = 0x47;                  // (a short jump)

NodeManager* hub() { return node_manager_instance().get(); }

VectorValue* as_value(maths::Vec4* v) { return v; }

// A quaternion (x, y, z, w) to a rotation matrix (0x0042BC40, maths::QuatRotation::fetch_mat).
void quat_to_matrix(const std::array<float, 4>& quat, maths::Mat4* matrix)
{
    maths::quat_to_matrix({.x = core::at(quat, 0), .y = core::at(quat, 1), .z = core::at(quat, 2), .w = core::at(quat, 3)}, matrix);
}

} // namespace

// 0x0045BE40
uint32_t get_node_name(Struct* node)
{
    uint32_t name = 0;
    node->read_hash_value(kName, &name, 0);
    return name;
}

// 0x0045C020
uint32_t get_link_count(Struct* node)
{
    Array* links = nullptr;
    node->fetch_list(kLinks, &links, 0);
    return links ? links->extent_ : 0;
}

// 0x0045BE10
uint32_t fetch_link(Struct* node, int index)
{
    Array* links = nullptr;
    node->fetch_list(kLinks, &links, 0);
    return links->fetch_part(index); // (no check that there were links)
}

// 0x0045C420
Struct* fetch_neighbour_node(Struct* node, int index) { return hub()->fetch_vertex(fetch_link(node, index), true); }

// 0x0045C4B0
bool has_link_to(Struct* node, Struct* target)
{
    Array* links = nullptr;
    node->fetch_list(kLinks, &links, 0);
    if (!links)
        return false;
    const int count = static_cast<int>(links->extent_);
    for (int i = 0; i < count; ++i) {
        if (hub()->fetch_vertex(links->fetch_part(i), true) == target)
            return true;
    }
    return false;
}

// 0x0045C050
bool fetch_place(Struct* node, maths::Vec4* out)
{
    if (node->fetch_vec(kPos, as_value(out), 0))
        return true;
    if (node->fetch_vec(kPosition, as_value(out), 0)) {
        out->z = -out->z; // (the files' position= is in the exporter's coordinates)
        return true;
    }
    *out = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    return false;
}

// 0x0045C180
bool read_turn_angles(Struct* node, maths::Vec4* out)
{
    *out = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    return node->fetch_vec(kAngles, as_value(out), 0);
}

// 0x0045C0B0
void fetch_facing(Struct* node, maths::Mat4* out)
{
    VectorValue v;
    if (!node->fetch_vec(kOrientation, &v, 0)) {
        for (int r = 0; r < 4; ++r)
            core::at(out->row, r) = {.x = r == 0 ? 1.0f : 0.0f, .y = r == 1 ? 1.0f : 0.0f, .z = r == 2 ? 1.0f : 0.0f, .w = r == 3 ? 1.0f : 0.0f};
        return;
    }
    // (x87, 53-bit: the squares summed z, y, x in double, w = sqrt(1 - min(sum, 1)) rounded once)
    double sum = static_cast<double>(v.z) * v.z + static_cast<double>(v.y) * v.y;
    sum += static_cast<double>(v.x) * v.x;
    if (sum > 1.0)
        sum = 1.0;
    const std::array<float, 4> quat = {{v.x, v.y, v.z, static_cast<float>(std::sqrt(1.0 - sum))}};
    quat_to_matrix(quat, out);
}

// 0x0045C1B0
Struct* fetch_vertex(uint32_t name, bool assert) { return hub()->fetch_vertex(name, assert); }

// 0x0045BE70
bool node_exists(uint32_t name) { return hub()->fetch_vertex(name, false) != nullptr; }

// 0x0045C3A0
uint32_t get_link_count(uint32_t name) { return get_link_count(hub()->fetch_vertex(name, true)); }

// 0x0045C3E0
uint32_t fetch_link(uint32_t name, int index) { return fetch_link(hub()->fetch_vertex(name, true), index); }

// 0x0045C460
Struct* fetch_neighbour_node(uint32_t name, int index)
{
    return fetch_neighbour_node(hub()->fetch_vertex(name, true), index);
}

// 0x0045C560
bool fetch_place(uint32_t name, maths::Vec4* out) { return fetch_place(hub()->fetch_vertex(name, true), out); }

// 0x0045C590
bool read_turn_angles(uint32_t name, maths::Vec4* out)
{
    return read_turn_angles(hub()->fetch_vertex(name, true), out);
}

// 0x0045C520
bool has_link_to(uint32_t name, uint32_t target)
{
    Struct* node = hub()->fetch_vertex(name, true);
    return has_link_to(node, hub()->fetch_vertex(target, true));
}

// 0x0045C2D0
Struct* find_closest_node_with_prefix(uint32_t prefix, const maths::Vec4* pos)
{
    Struct* nearest = nullptr;
    float best = 1e20f;
    const std::span<const uint32_t> names = node_prefixes()->get_nodes(prefix);
    if (names.empty())
        return nullptr;
    // (The original's position isn't reset between nodes, nor set before the first: a node without pos= is
    // measured where the one before it was. Here the first such node is at the origin.)
    VectorValue p = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    for (const uint32_t name : names) {
        Struct* node = hub()->fetch_vertex(name, true);
        node->fetch_vec(kPos, &p, 0);
        // (x87, 53-bit: the differences and squares in double, summed z, y, x)
        const double dx = static_cast<double>(p.x) - pos->x;
        const double dy = static_cast<double>(p.y) - pos->y;
        const double dz = static_cast<double>(p.z) - pos->z;
        double distance = dz * dz + dy * dy;
        distance += dx * dx;
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = node;
        }
    }
    return nearest;
}

// 0x0045C950
void for_each_script_call(uint32_t script, uint32_t target, ScriptCallCallback callback, Struct* parent_params,
                          int depth)
{
    if (depth > kDeepestLevel)
        return;
    script::ScriptStore* cache = script::g_script_store.get();
    script::Tokens token = script::tokens_at(cache->fetch_script(script));
    gsl::owner<Struct*> params = nullptr;
    if (void* memory = script::spawn_param_bag())
        params = memory::construct_in<script::Struct>(memory);
    token = script::skip_to_end_of_statement(token); // (past the script's first line)
    for (;;) {
        const script::Tokens call = token;
        switch (*token) {
        case kTokenKeyHash:
        case kTokenMemberFunction:
        case kTokenMemberFunctionEntry: {
            // A name at the start of a statement (or one already looked up): a script is followed into, a C or
            // member function that is `target` handed to the callback; EndGap's gapscript= is followed too.
            const uint32_t name = *token == kTokenMemberFunctionEntry
                                      ? script::member_entry_at(token + 1)->callsign_
                                      : script::fetch_word32(token + 1);
            token += 5;
            if (*token == kTokenEquals)
                break;
            const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
            if (!symbol)
                break;
            switch (symbol->type) {
            case script::kQbvalScriptBody:
                params->get_clear();
                token = script::parse_arguments(params, token, parent_params);
                if (name == target)
                    callback(params, call.address());
                else
                    token = script::skip_to_end_of_statement(token);
                for_each_script_call(name, target, callback, params, depth + 1);
                break;
            case script::kQbvalNativeFunc:
                if (name != target) {
                    token = script::skip_to_end_of_statement(token);
                    break;
                }
                params->get_clear();
                token = script::parse_arguments(params, token, parent_params);
                callback(params, call.address());
                break;
            case script::kQbvalNativeMethod:
                if (name != target) {
                    token = script::skip_to_end_of_statement(token);
                    break;
                }
                params->get_clear();
                token = script::parse_arguments(params, token, parent_params);
                callback(params, call.address());
                if (name == kEndGap) {
                    uint32_t gap_script = 0;
                    if (params->read_hash_value(kGapScript, &gap_script, 0))
                        for_each_script_call(gap_script, target, callback, nullptr, depth + 1);
                }
                break;
            default:
                break;
            }
            break;
        }
        case kTokenCFunction: {
            // A C function already looked up: SetEventHandler's scr= is followed.
            const script::NativeFunction function = script::native_function_at(token + 1);
            token += 5;
            if (function != &commands::cfunc_set_event_handler) {
                token = script::skip_to_end_of_statement(token);
                break;
            }
            params->get_clear();
            token = script::parse_arguments(params, token, parent_params);
            uint32_t handler = 0;
            params->read_hash_value(kScr, &handler, 0);
            if (handler)
                for_each_script_call(handler, target, callback, nullptr, depth + 1);
            break;
        }
        case kTokenRepeat:
            token = script::skip_to_end_of_statement(script::step_over_token(token));
            break;
        case kTokenIf:
            token = script::step_over_token(token);
            if (*token == kTokenNot)
                token = script::step_over_token(token);
            if (*token == kTokenOpenParenthesis)
                token = script::skip_to_end_of_statement(token);
            break;
        case kTokenEndOfScript:
            cache->release_script(script);
            if (params) {
                params->get_clear();
                script::delete_struct(params);
            }
            return;
        default:
            token = script::step_over_token(token);
            break;
        }
    }
}

// 0x0045CCA0
void for_each_node_script_call(uint32_t script_param, uint32_t target, ScriptCallCallback callback)
{
    script::clear_script_flag_6f_2564();
    hub()->start_walk(0);
    while (Struct* node = hub()->get_next_node()) {
        uint32_t script = 0;
        if (node->read_hash_value(script_param, &script, 0))
            for_each_script_call(script, target, callback, nullptr, 0);
    }
    hub()->end_walk();
    script::set_script_flag_6f_2564();
}

// 0x0045C1D0
void start_node_walk(uint32_t name) { hub()->start_walk(name); }
// 0x0045C1F0
Struct* get_next_node() { return hub()->get_next_node(); }
// 0x0045C200
void end_node_walk() { hub()->end_walk(); }
// 0x0045C210
Array* get_last_node_array() { return hub()->get_last_node_array(); }
// 0x0045C220
script::Array* get_node_owner_nodes() { return hub()->get_owner_nodes(); }
// 0x0045C230
uint32_t get_last_node_array_name() { return hub()->get_last_node_array_name(); }

} // namespace level_nodes
