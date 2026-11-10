// NOT ORIGINAL - TypeScript scripts in the game: QuickJS, the runtime's JavaScript (scripting/*.js, embedded), the
// modules' loading and the natives the runtime's game mode calls (globalThis.__qbGame). See ts_bridge.h.
//
// Values cross as the runtime's value objects (scripting/values.js): {type: 1 | 2 | 13, bits}, {type: 3 | 4, bytes},
// {type: 5 | 6, coordinates}, {type: 10, members}, {type: 12, elementType, elements}; a parameter list is an array of
// [name, value]. Structure / array references (20 / 21) arrive as their copies (10 / 12). The kinds a value can't be
// in a script (script bodies, functions) are a fault, never dropped.

#include "script_qb/ts_bridge.h"
#include "script_qb/script_check.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <quickjs.h>

#include "base/core/checksums.h"
#include "base/maths/random.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/script/cell.h"
#include "engine/script/context.h"
#include "engine/script/load.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/token_reader.h"
#include "engine/script/tokens.h"
#include "platform/run_log.h"
#include "ts_sources.h"

namespace script::qb::ts {

namespace {

constexpr size_t kMemoryLimit = 512U * 1024U * 1024U;
constexpr size_t kStackLimit = 1024U * 1024U;
// Interrupt checks one resume may take (QuickJS asks every few thousand operations): a level that never stops is a
// fault, not a hang.
constexpr uint32_t kInterruptBudget = 200000;
// The budget each run of QuickJS gets (the script check lowers it: a loop with no calls - `while (true)` of a begin /
// repeat with no count - runs out there as QB's runs out of steps, not after minutes).
uint32_t& interrupt_budget()
{
    static uint32_t budget = kInterruptBudget;
    return budget;
}
constexpr size_t kMaxDepth = 64;

struct Engine {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JSValue qb = JS_UNDEFINED; // globalThis.qb
    uint32_t budget = 0;
    uint32_t next_level = 0;
    // The .qb file whose module is loading (its symbols are tagged with it, as the loader tags a file's).
    std::string loading_file;
    bool loading_temporary = false;
    // Each level's parameter list as the level last saw it (fingerprint): a resume sends the list only when it has
    // changed since (a command's results, a callee's return values...).
    std::unordered_map<uint32_t, uint64_t> synced;
    // [debug] ts_types=1: the types each site sees, saved every few seconds (save_types).
    bool recording = false;
    std::chrono::steady_clock::time_point types_saved = std::chrono::steady_clock::now();
};

// [debug] ts_profile=1: where the time goes, logged every few seconds of activity (resumes, the parameter lists'
// conversions each way, the time in QuickJS, symbol lookups).
struct Profile {
    bool on = false;
    uint64_t resumes = 0, symbols = 0, symbol_ns = 0, to_js_ns = 0, js_ns = 0, from_js_ns = 0, total_ns = 0;
    uint64_t loads = 0, load_ns = 0; // modules run (on_qb_loaded)
    uint64_t begins = 0, begin_ns = 0, start_ns = 0; // levels begun; QuickJS and the runtime started
    std::unordered_map<uint32_t, uint32_t> level_names;       // level -> its script
    std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> by_script; // script -> (resumes, ns)
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
};
Profile& profile()
{
    static Profile p;
    return p;
}
struct Timer {
    uint64_t* into;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Timer(uint64_t& total) : into(profile().on ? &total : nullptr) {}
    ~Timer()
    {
        if (into) {
            *into += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        }
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
};
void report_profile()
{
    Profile& p = profile();
    const auto now = std::chrono::steady_clock::now();
    if (!p.on || now - p.last < std::chrono::seconds(2)) {
        return;
    }
    p.last = now;
    const auto ms = [](uint64_t ns) { return static_cast<unsigned>(ns / 1000000U); };
    get_log("ts profile: %u resumes, %u ms in all - params to JS %u ms, QuickJS %u ms, params back %u ms; %u symbol "
            "lookups %u ms; %u modules loaded %u ms; %u levels begun %u ms; started in %u ms (all since start-up)",
            static_cast<unsigned>(p.resumes), ms(p.total_ns), ms(p.to_js_ns), ms(p.js_ns), ms(p.from_js_ns),
            static_cast<unsigned>(p.symbols), ms(p.symbol_ns), static_cast<unsigned>(p.loads), ms(p.load_ns),
            static_cast<unsigned>(p.begins), ms(p.begin_ns), ms(p.start_ns));
    std::vector<std::pair<uint32_t, std::pair<uint64_t, uint64_t>>> top(p.by_script.begin(), p.by_script.end());
    std::ranges::sort(top, [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
    for (size_t i = 0; i < std::min<size_t>(top.size(), 5); ++i) {
        get_log("ts profile:   %s - %u resumes, %u ms", find_checksum_name_or_unknown(top[i].first),
                static_cast<unsigned>(top[i].second.first), ms(top[i].second.second));
    }

}

Engine& engine()
{
    static Engine instance;
    return instance;
}

bool& is_on()
{
    static bool on = false;
    return on;
}

// --- values ---------------------------------------------------------------------------------------------------------

JSValue get(JSContext* c, JSValueConst object, const char* key) { return JS_GetPropertyStr(c, object, key); }

uint32_t to_u32(JSContext* c, JSValueConst value)
{
    uint32_t out = 0;
    JS_ToUint32(c, &out, value);
    return out;
}

uint32_t u32_at(JSContext* c, JSValueConst object, const char* key)
{
    const JSValue value = get(c, object, key);
    const uint32_t out = to_u32(c, value);
    JS_FreeValue(c, value);
    return out;
}

uint32_t length_of(JSContext* c, JSValueConst array) { return u32_at(c, array, "length"); }

void set(JSContext* c, JSValueConst object, const char* key, JSValue value) { JS_SetPropertyStr(c, object, key, value); }

JSValue number_array(JSContext* c, std::span<const uint32_t> words)
{
    const JSValue array = JS_NewArray(c);
    for (uint32_t i = 0; i < words.size(); ++i) {
        JS_SetPropertyUint32(c, array, i, JS_NewUint32(c, words[i]));
    }
    return array;
}

JSValue scalar(JSContext* c, uint32_t type, uint32_t bits)
{
    const JSValue object = JS_NewObject(c);
    set(c, object, "type", JS_NewUint32(c, type));
    set(c, object, "bits", JS_NewUint32(c, bits));
    return object;
}

JSValue text_value(JSContext* c, uint32_t type, const char* text)
{
    const JSValue object = JS_NewObject(c);
    set(c, object, "type", JS_NewUint32(c, type));
    const JSValue bytes = JS_NewArray(c);
    uint32_t i = 0;
    for (const char* at = text ? text : ""; *at != '\0'; ++at) {
        JS_SetPropertyUint32(c, bytes, i++, JS_NewUint32(c, static_cast<uint8_t>(*at)));
    }
    set(c, object, "bytes", bytes);
    return object;
}

JSValue coordinates_value(JSContext* c, uint32_t type, std::span<const float> xyz)
{
    std::array<uint32_t, 3> words{};
    for (size_t i = 0; i < xyz.size(); ++i) {
        words.at(i) = std::bit_cast<uint32_t>(xyz[i]);
    }
    const JSValue object = JS_NewObject(c);
    set(c, object, "type", JS_NewUint32(c, type));
    set(c, object, "coordinates", number_array(c, std::span<const uint32_t>(words.data(), xyz.size())));
    return object;
}

JSValue list_to_js(JSContext* c, Struct* structure, size_t depth);

// A deep fingerprint of a parameter list (every name, kind and value, nested ones included, mixed a 64-bit word at a
// time - it is taken at every resume, over lists holding hundreds of names): equal fingerprints - the level's view of
// it is up to date.
void mix(uint64_t& hash, uint64_t word)
{
    hash = (hash ^ word) * 0x9E3779B97F4A7C15ULL;
    hash ^= hash >> 29U;
}
void mix_text(uint64_t& hash, const char* text)
{
    const size_t length = std::strlen(text);
    size_t i = 0;
    for (; i + 8 <= length; i += 8) {
        uint64_t word = 0;
        std::memcpy(&word, text + i, 8);
        mix(hash, word);
    }
    uint64_t rest = 0;
    std::memcpy(&rest, text + i, length - i);
    mix(hash, rest);
    mix(hash, length);
}
void fingerprint_cell(uint64_t& hash, QbValueKind kind, const Cell& cell, size_t depth);
void fingerprint_list(uint64_t& hash, Struct* structure, size_t depth)
{
    for (Component* component = structure ? structure->step_to_entry(nullptr) : nullptr; component;
         component = structure->step_to_entry(component)) {
        mix(hash, component->name);
        fingerprint_cell(hash, value_kind(*component), component->value, depth + 1);
    }
    mix(hash, 0xFFFFFFFFU);
}
void fingerprint_cell(uint64_t& hash, QbValueKind kind, const Cell& cell, size_t depth)
{
    mix(hash, static_cast<uint64_t>(kind));
    if (depth > kMaxDepth) {
        return;
    }
    switch (kind) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        mix_text(hash, cell.text() ? cell.text() : "");
        break;
    case QbValueKind::kPair:
        if (const Pair* pair = cell.pair()) {
            mix(hash, std::bit_cast<uint32_t>(pair->x()));
            mix(hash, std::bit_cast<uint32_t>(pair->y()));
        }
        break;
    case QbValueKind::kVector:
        if (const Vector* vector = cell.vector()) {
            mix(hash, std::bit_cast<uint32_t>(vector->x()));
            mix(hash, std::bit_cast<uint32_t>(vector->y()));
            mix(hash, std::bit_cast<uint32_t>(vector->z()));
        }
        break;
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        fingerprint_list(hash, cell.structure(), depth);
        break;
    case QbValueKind::kArray:
    case QbValueKind::kArrayRef:
        if (Array* array = cell.array()) {
            mix(hash, static_cast<uint64_t>(array->kind_tag()));
            mix(hash, array->extent());
            for (uint32_t i = 0; i < array->extent(); ++i) {
                fingerprint_cell(hash, array->kind_tag(), array->cell_at(i), depth + 1);
            }
        }
        break;
    default:
        mix(hash, cell.word());
        break;
    }
}
uint64_t fingerprint(Struct* structure)
{
    uint64_t hash = 0xCBF29CE484222325ULL;
    fingerprint_list(hash, structure, 0);
    return hash;
}
JSValue array_to_js(JSContext* c, Array* array, size_t depth);
JSValue call_qb(const char* method, std::span<JSValue> args);

// A value kind and its cell as a value object (JS_EXCEPTION for a kind no script value has).
JSValue cell_to_js(JSContext* c, QbValueKind kind, const Cell& cell, size_t depth)
{
    if (depth > kMaxDepth) {
        return JS_ThrowRangeError(c, "QB value too deep for the TypeScript bridge");
    }
    switch (kind) {
    case QbValueKind::kInteger:
    case QbValueKind::kFloat:
    case QbValueKind::kName:
        return scalar(c, static_cast<uint32_t>(kind), cell.word());
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        return text_value(c, static_cast<uint32_t>(kind), cell.text());
    case QbValueKind::kPair: {
        const Pair* pair = cell.pair();
        const std::array<float, 2> xy{pair ? pair->x() : 0.0F, pair ? pair->y() : 0.0F};
        return coordinates_value(c, 5, xy);
    }
    case QbValueKind::kVector: {
        const Vector* vector = cell.vector();
        const std::array<float, 3> xyz{vector ? vector->x() : 0.0F, vector ? vector->y() : 0.0F,
                                       vector ? vector->z() : 0.0F};
        return coordinates_value(c, 6, xyz);
    }
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef: {
        const JSValue object = JS_NewObject(c);
        set(c, object, "type", JS_NewUint32(c, 10));
        const JSValue members = list_to_js(c, cell.structure(), depth + 1);
        if (JS_IsException(members)) {
            JS_FreeValue(c, object);
            return members;
        }
        set(c, object, "members", members);
        return object;
    }
    case QbValueKind::kArray:
    case QbValueKind::kArrayRef:
        return array_to_js(c, cell.array(), depth + 1);
    default:
        return JS_ThrowTypeError(c, "QB value kind %u can't cross to TypeScript", static_cast<unsigned>(kind));
    }
}

JSValue array_to_js(JSContext* c, Array* array, size_t depth)
{
    const JSValue object = JS_NewObject(c);
    set(c, object, "type", JS_NewUint32(c, 12));
    const QbValueKind kind = array ? array->kind_tag() : QbValueKind::kNone;
    const uint32_t count = array ? array->extent() : 0;
    set(c, object, "elementType", JS_NewUint32(c, count ? static_cast<uint32_t>(kind) : 0));
    const JSValue elements = JS_NewArray(c);
    set(c, object, "elements", elements);
    for (uint32_t i = 0; i < count; ++i) {
        const JSValue element = cell_to_js(c, kind, array->cell_at(i), depth);
        if (JS_IsException(element)) {
            JS_FreeValue(c, object);
            return element;
        }
        JS_SetPropertyUint32(c, elements, i, element);
    }
    return object;
}

// A structure's script (`script name ... endscript` in it - append_script; a TypeScript one's body names its function,
// runtime.js inline) as {type: 7, bytes}: its code, `unknown2` bytes.
JSValue script_body_to_js(JSContext* c, const Component& component)
{
    const JSValue object = JS_NewObject(c);
    set(c, object, "type", JS_NewUint32(c, static_cast<uint32_t>(QbValueKind::kScriptBody)));
    const JSValue bytes = JS_NewArray(c);
    const uint8_t* code = component.value.empty() ? nullptr : component.value.code();
    for (uint32_t i = 0; code && i < component.unknown2; ++i) {
        JS_SetPropertyUint32(c, bytes, i, JS_NewUint32(c, code[i]));
    }
    set(c, object, "bytes", bytes);
    return object;
}

// A script value's code made a structure's script component (as append_script makes one).
bool js_to_script_body(JSContext* c, JSValueConst value, Component& component, std::string& error)
{
    const JSValue bytes = get(c, value, "bytes");
    const uint32_t size = length_of(c, bytes);
    if (size == 0 || size > 0xFFFFU) {
        JS_FreeValue(c, bytes);
        error = "a script value's body is empty or too long";
        return false;
    }
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    auto* block = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::leave_heap_scope();
    const std::span<uint8_t> code(block, size);
    for (uint32_t i = 0; i < size; ++i) {
        const JSValue byte = JS_GetPropertyUint32(c, bytes, i);
        code[i] = static_cast<uint8_t>(to_u32(c, byte));
        JS_FreeValue(c, byte);
    }
    JS_FreeValue(c, bytes);
    note_code_block(code);
    set_value_kind(component, QbValueKind::kScriptBody);
    component.unknown2 = static_cast<uint16_t>(size);
    component.value.set_code(block);
    return true;
}

JSValue list_to_js(JSContext* c, Struct* structure, size_t depth)
{
    const JSValue list = JS_NewArray(c);
    uint32_t i = 0;
    for (Component* component = structure ? structure->step_to_entry(nullptr) : nullptr; component;
         component = structure->step_to_entry(component)) {
        const JSValue value = value_kind(*component) == QbValueKind::kScriptBody
                                  ? script_body_to_js(c, *component)
                                  : cell_to_js(c, value_kind(*component), component->value, depth);
        if (JS_IsException(value)) {
            JS_FreeValue(c, list);
            return value;
        }
        const JSValue field = JS_NewArray(c);
        JS_SetPropertyUint32(c, field, 0, JS_NewUint32(c, component->name));
        JS_SetPropertyUint32(c, field, 1, value);
        JS_SetPropertyUint32(c, list, i++, field);
    }
    return list;
}

bool list_to_struct(JSContext* c, JSValueConst list, Struct* dest, size_t depth, std::string& error);

std::string bytes_of(JSContext* c, JSValueConst value)
{
    const JSValue bytes = get(c, value, "bytes");
    std::string text;
    const uint32_t count = length_of(c, bytes);
    text.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const JSValue byte = JS_GetPropertyUint32(c, bytes, i);
        text.push_back(static_cast<char>(to_u32(c, byte)));
        JS_FreeValue(c, byte);
    }
    JS_FreeValue(c, bytes);
    return text;
}

std::array<float, 3> coordinates_of(JSContext* c, JSValueConst value)
{
    std::array<float, 3> out{};
    const JSValue words = get(c, value, "coordinates");
    for (uint32_t i = 0; i < std::min<uint32_t>(3, length_of(c, words)); ++i) {
        const JSValue word = JS_GetPropertyUint32(c, words, i);
        out.at(i) = std::bit_cast<float>(to_u32(c, word));
        JS_FreeValue(c, word);
    }
    JS_FreeValue(c, words);
    return out;
}

// A value object as a kind and a cell the caller takes over (false: `error` says why).
bool js_to_cell(JSContext* c, JSValueConst value, QbValueKind& kind, Cell& cell, size_t depth, std::string& error);

bool js_to_array(JSContext* c, JSValueConst value, Array*& out, size_t depth, std::string& error)
{
    const auto element_type = static_cast<QbValueKind>(u32_at(c, value, "elementType"));
    const JSValue elements = get(c, value, "elements");
    const uint32_t count = length_of(c, elements);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    out = script::new_array();
    if (count != 0) {
        out->configure_size_kind(count, element_type);
    }
    memory::leave_heap_scope();
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        const JSValue element = JS_GetPropertyUint32(c, elements, i);
        QbValueKind kind = QbValueKind::kNone;
        Cell cell;
        ok = js_to_cell(c, element, kind, cell, depth + 1, error);
        JS_FreeValue(c, element);
        if (ok && kind != element_type) {
            error = "array element of another type";
            ok = false;
        }
        if (ok) {
            out->set_cell(i, cell);
        }
    }
    JS_FreeValue(c, elements);
    return ok;
}

bool js_to_cell(JSContext* c, JSValueConst value, QbValueKind& kind, Cell& cell, size_t depth, std::string& error)
{
    if (depth > kMaxDepth) {
        error = "value too deep";
        return false;
    }
    const uint32_t type = u32_at(c, value, "type");
    switch (type) {
    case 1:
    case 2:
    case 13:
        kind = static_cast<QbValueKind>(type);
        cell.set_word(u32_at(c, value, "bits"));
        return true;
    case 3:
    case 4:
        kind = type == 3 ? QbValueKind::kString : QbValueKind::kInlineText;
        cell.set_text(spawn_text(bytes_of(c, value).c_str()));
        return true;
    case 5: {
        kind = QbValueKind::kPair;
        const std::array<float, 3> xy = coordinates_of(c, value);
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        if (!pair) {
            error = "no memory for a pair";
            return false;
        }
        pair->x() = xy[0];
        pair->y() = xy[1];
        cell.set_pair(pair);
        return true;
    }
    case 6: {
        kind = QbValueKind::kVector;
        const std::array<float, 3> xyz = coordinates_of(c, value);
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        if (!vector) {
            error = "no memory for a vector";
            return false;
        }
        vector->x() = xyz[0];
        vector->y() = xyz[1];
        vector->z() = xyz[2];
        cell.set_vector(vector);
        return true;
    }
    case 10:
    case 20: {
        kind = QbValueKind::kStruct;
        const JSValue source = type == 20 ? get(c, value, "target") : JS_DupValue(c, value);
        const JSValue members = get(c, source, "members");
        Struct* structure = script::new_struct();
        const bool ok = list_to_struct(c, members, structure, depth + 1, error);
        JS_FreeValue(c, members);
        JS_FreeValue(c, source);
        cell.set_structure(structure);
        return ok;
    }
    case 12:
    case 21: {
        kind = QbValueKind::kArray;
        const JSValue source = type == 21 ? get(c, value, "target") : JS_DupValue(c, value);
        Array* array = nullptr;
        const bool ok = js_to_array(c, source, array, depth, error);
        JS_FreeValue(c, source);
        cell.set_array(array);
        return ok;
    }
    default:
        error = "value of type " + std::to_string(type) + " can't cross from TypeScript";
        return false;
    }
}

// The list's fields appended to `dest` in order (the list is merged already: nothing replaces anything).
bool list_to_struct(JSContext* c, JSValueConst list, Struct* dest, size_t depth, std::string& error)
{
    const uint32_t count = length_of(c, list);
    for (uint32_t i = 0; i < count; ++i) {
        const JSValue field = JS_GetPropertyUint32(c, list, i);
        const JSValue name = JS_GetPropertyUint32(c, field, 0);
        const JSValue value = JS_GetPropertyUint32(c, field, 1);
        gsl::owner<Component*> component = new_component();
        component->name = to_u32(c, name);
        bool ok = false;
        if (u32_at(c, value, "type") == static_cast<uint32_t>(QbValueKind::kScriptBody)) {
            ok = js_to_script_body(c, value, *component, error);
        } else {
            QbValueKind kind = QbValueKind::kNone;
            ok = js_to_cell(c, value, kind, component->value, depth, error);
            set_value_kind(*component, kind);
        }
        JS_FreeValue(c, value);
        JS_FreeValue(c, name);
        JS_FreeValue(c, field);
        if (!ok) {
            release_component(component);
            free_component(component);
            return false;
        }
        dest->attach_module(component);
    }
    return true;
}

std::string exception_text(JSContext* c)
{
    const JSValue exception = JS_GetException(c);
    std::string text;
    if (const char* message = JS_ToCString(c, exception)) {
        text = message;
        JS_FreeCString(c, message);
    }
    if (JS_IsObject(exception)) {
        const JSValue stack = get(c, exception, "stack");
        if (const char* where = JS_IsUndefined(stack) ? nullptr : JS_ToCString(c, stack)) {
            text += " | ";
            text += where;
            JS_FreeCString(c, where);
        }
        JS_FreeValue(c, stack);
    }
    JS_FreeValue(c, exception);
    return text;
}

// --- natives (globalThis.__qbGame) ------------------------------------------------------------------------------------

JSValue throw_error(JSContext* c, const std::string& error) { return JS_ThrowTypeError(c, "%s", error.c_str()); }

// symbol(n): the global's value, a callable's descriptor, or undefined (values.js follows names standing for others).
JSValue native_symbol(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    ++profile().symbols;
    const Timer timer(profile().symbol_ns);
    const uint32_t name = to_u32(c, argv[0]);
    const SymbolTableEntry* symbol = find_symbol(name);
    if (!symbol) {
        return JS_UNDEFINED;
    }
    const QbValueKind kind = symbol->type();
    if (kind == QbValueKind::kScriptBody || kind == QbValueKind::kNativeFunc || kind == QbValueKind::kNativeMethod) {
        const JSValue callable = JS_NewObject(c);
        set(c, callable, "kind", JS_NewString(c, "callable"));
        set(c, callable, "type", JS_NewUint32(c, static_cast<uint32_t>(kind)));
        set(c, callable, "name", JS_NewUint32(c, name));
        return callable;
    }
    return cell_to_js(c, kind, symbol->value(), 0);
}

JSValue native_symbol_kind(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    const SymbolTableEntry* symbol = resolve_symbol(to_u32(c, argv[0]));
    if (symbol && symbol->type() == QbValueKind::kNativeFunc) {
        return JS_NewString(c, "function");
    }
    if (symbol && symbol->type() == QbValueKind::kNativeMethod) {
        return JS_NewString(c, "method");
    }
    return JS_UNDEFINED;
}

// callFunction(n, args): a C function in an expression (push_name): called with the arguments, the running script's
// record its context; its result, its outputs dropped.
JSValue native_call_function(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    const SymbolTableEntry* symbol = resolve_symbol(to_u32(c, argv[0]));
    if (!symbol || symbol->type() != QbValueKind::kNativeFunc) {
        return JS_NewBool(c, false);
    }
    gsl::owner<Struct*> params = script::new_struct();
    std::string error;
    if (!list_to_struct(c, argv[1], params, 0, error)) {
        params->get_clear();
        delete_struct(params);
        return throw_error(c, error);
    }
    const auto routine = symbol->value().function();
    // (the script check answers for it: script_check.h)
    const std::optional<bool> stubbed = check::command(get_running_script(), symbol->name_value(), 0, params);
    const bool result = stubbed ? *stubbed : routine(params, Context(get_running_script()));
    params->get_clear();
    delete_struct(params);
    return JS_NewBool(c, result);
}

// defineScript(n, id): script n's symbol, a body of one kTsBody token naming its definition `id` (runtime.js: each
// definition has its own, so the symbol table alone says which runs - a temporary load undone puts the older back),
// as the loader defines a .qb file's scripts.
JSValue native_define_script(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    Engine& e = engine();
    const uint32_t name = to_u32(c, argv[0]);
    const uint32_t id = to_u32(c, argv[1]);
    // 01 (the defaults' empty line), 4A <id>, 01, 24 (the end)
    const std::array<uint8_t, 8> body{0x01,
                                      token_byte(QbToken::kTsBody),
                                      static_cast<uint8_t>(id),
                                      static_cast<uint8_t>(id >> 8U),
                                      static_cast<uint8_t>(id >> 16U),
                                      static_cast<uint8_t>(id >> 24U),
                                      0x01,
                                      token_byte(QbToken::kScriptEnd)};
    SymbolTableEntry* existing = find_symbol(name);
    if (existing) {
        replace_symbol_for_definition(existing, e.loading_temporary);
    }
    SymbolTableEntry* symbol = define_script_symbol(body, checksums::Name{name}, calculate_script_checksum(Tokens(body)),
                                                    e.loading_file.c_str());
    if (!existing && e.loading_temporary) {
        remove_symbol_later(symbol);
    }
    return JS_UNDEFINED;
}

// defineGlobal(n, value): its symbol (as parse_symbol_definition makes a .qb file's).
JSValue native_define_global(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    Engine& e = engine();
    const uint32_t name = to_u32(c, argv[0]);
    gsl::owner<Component*> value = new_component();
    QbValueKind kind = QbValueKind::kNone;
    std::string error;
    if (!js_to_cell(c, argv[1], kind, value->value, 0, error)) {
        set_value_kind(*value, kind);
        release_component(value);
        free_component(value);
        return throw_error(c, error);
    }
    SymbolTableEntry* existing = find_symbol(name);
    if (existing) {
        replace_symbol_for_definition(existing, e.loading_temporary);
    }
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->set_unknown8(checksums::hash_from_text(e.loading_file.c_str()));
    symbol->set_type(kind);
    symbol->value() = value->value;
    value->value.clear();
    free_component(value);
    if (!existing && e.loading_temporary) {
        remove_symbol_later(symbol);
    }
    return JS_UNDEFINED;
}

// draw(range, stream): the game's random generator (maths::rnd, or its side stream).
JSValue native_draw(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    const auto range = static_cast<int>(to_u32(c, argv[0]));
    const int pick = to_u32(c, argv[1]) == 0 ? maths::rnd(range) : maths::roll_side_stream(range);
    return JS_NewUint32(c, static_cast<uint32_t>(pick));
}

// storedChoice(opcode, site, count): a RandomNoRepeat (0x40) / RandomPermute (0x41) choice by the game's own stored
// random state (token_reader.cpp pick_stored_random) - the pool QB's Randoms use: an entry is the running record's or
// left without an owner (a record gone), at most so many kept, the oldest dropped. QB finds an entry by its Random's
// table of jumps; a TypeScript site by a byte of its own, one per site text (module, script, site), kept for good.
JSValue native_stored_choice(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* argv)
{
    static std::unordered_map<std::string, std::unique_ptr<uint8_t>> keys;
    const uint32_t opcode = to_u32(c, argv[0]);
    std::string site;
    if (const char* text = JS_ToCString(c, argv[1])) {
        site = text;
        JS_FreeCString(c, text);
    }
    const uint32_t count = to_u32(c, argv[2]);
    std::unique_ptr<uint8_t>& key = keys[site];
    if (!key) {
        key = std::make_unique<uint8_t>(0);
    }
    const QbToken kind = opcode == 0x41U ? QbToken::kRandomPermute : QbToken::kRandomNoRepeat;
    return JS_NewUint32(c, pick_stored_random(key.get(), kind, count));
}

// frame(): the game's frame count (ts_bridge.h frame_clock).
JSValue native_frame(JSContext* c, JSValueConst /*self*/, int /*argc*/, JSValueConst* /*argv*/)
{
    return JS_NewUint32(c, frame_clock());
}

void add_native(JSContext* c, JSValueConst object, const char* name, JSCFunction* function, int arity)
{
    set(c, object, name, JS_NewCFunction(c, function, name, arity));
}

bool evaluate(const char* source, size_t size, const char* filename, std::string& error)
{
    Engine& e = engine();
    JS_UpdateStackTop(e.runtime);
    e.budget = interrupt_budget();
    const JSValue result = JS_Eval(e.context, source, size, filename, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        error = exception_text(e.context);
        return false;
    }
    JS_FreeValue(e.context, result);
    return true;
}

bool start_engine()
{
    Engine& e = engine();
    e.runtime = JS_NewRuntime();
    if (!e.runtime) {
        return false;
    }
    JS_SetMemoryLimit(e.runtime, kMemoryLimit);
    JS_SetMaxStackSize(e.runtime, kStackLimit);
    JS_SetInterruptHandler(
        e.runtime,
        [](JSRuntime* /*runtime*/, void* opaque) -> int {
            auto& self = *static_cast<Engine*>(opaque);
            if (self.budget == 0) {
                return 1;
            }
            --self.budget;
            return 0;
        },
        &e);
    e.context = JS_NewContext(e.runtime);
    if (!e.context) {
        return false;
    }
    JSContext* c = e.context;
    const JSValue global = JS_GetGlobalObject(c);
    const JSValue game = JS_NewObject(c);
    add_native(c, game, "symbol", native_symbol, 1);
    add_native(c, game, "symbolKind", native_symbol_kind, 1);
    add_native(c, game, "callFunction", native_call_function, 2);
    add_native(c, game, "defineScript", native_define_script, 2);
    add_native(c, game, "defineGlobal", native_define_global, 2);
    add_native(c, game, "draw", native_draw, 2);
    add_native(c, game, "frame", native_frame, 0);
    add_native(c, game, "storedChoice", native_stored_choice, 3);
    set(c, game, "recording", JS_NewBool(c, e.recording));
    set(c, global, "__qbGame", game);
    JS_FreeValue(c, global);
    struct Source {
        const unsigned char* bytes;
        size_t size;
        const char* name;
    };
    const std::array<Source, 5> sources{{{k_numeric_js, sizeof(k_numeric_js) - 1, "numeric.js"},
                                         {k_random_js, sizeof(k_random_js) - 1, "random.js"},
                                         {k_values_js, sizeof(k_values_js) - 1, "values.js"},
                                         {k_runtime_js, sizeof(k_runtime_js) - 1, "runtime.js"},
                                         {k_qb_ts_js, sizeof(k_qb_ts_js) - 1, "qb_ts.js"}}};
    for (const Source& source : sources) {
        std::string error;
        if (!evaluate(reinterpret_cast<const char*>(source.bytes), source.size, source.name, error)) {
            get_log("ts: %s failed to load: %s", source.name, error.c_str());
            return false;
        }
    }
    const JSValue global_again = JS_GetGlobalObject(c);
    e.qb = get(c, global_again, "qb");
    JS_FreeValue(c, global_again);
    return true;
}

// FNV-1a over a .qb file's bytes: its module's name (tools/qbjs/install_ts.ts names them the same way), so a module is
// only ever used for the very file it was translated from - an overlay's or a mod's version of a file has its own.
uint32_t file_hash(std::span<const uint8_t> bytes)
{
    uint32_t hash = 0x811C9DC5U;
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * 0x01000193U;
    }
    return hash;
}

// A cached module's first bytes: what made it - this QuickJS, this word size, from this very source.
std::string cache_header(const std::string& source)
{
    std::array<char, 96> header{};
    const auto bytes = std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size());
    const int length = std::snprintf(header.data(), header.size(), "QBTS bytecode %s %zu-bit %08x %zu\n",
                                     JS_GetVersion(), sizeof(void*) * 8, file_hash(bytes), source.size());
    return {header.data(), static_cast<size_t>(std::max(length, 0))};
}

// A module run: from its bytecode cached in `cache_path` when that was compiled from this source (QuickJS reads
// bytecode far faster than it parses - a few modules are megabytes); else compiled, cached, run.
// `text` made `path`'s whole content: written beside (.part) and moved over it, so a reader never sees half of it.
void replace_file(const std::string& path, std::string_view text)
{
    const std::string partial = path + ".part";
    const fs::Written written = fs::write_text(partial, text);
    if (!written.written || !written.closed || !fs::disk().replace(partial.c_str(), path.c_str(), false)) {
        fs::disk().remove(partial.c_str());
    }
}

bool evaluate_module(const std::string& source, const std::string& name, const std::string& cache_path,
                     bool& cached, std::string& error)
{
    Engine& e = engine();
    JSContext* c = e.context;
    JS_UpdateStackTop(e.runtime);
    e.budget = interrupt_budget();
    const std::string header = cache_header(source);
    JSValue function = JS_UNDEFINED;
    cached = false;
    if (std::string bytes; fs::read_text(cache_path, &bytes)) {
        if (bytes.size() > header.size() && bytes.starts_with(header)) {
            // (our own file, made below from a module we installed - read as trusted bytecode)
            function = JS_ReadObject(c, reinterpret_cast<const uint8_t*>(bytes.data()) + header.size(),
                                     bytes.size() - header.size(), JS_READ_OBJ_BYTECODE);
            if (JS_IsException(function)) {
                JS_FreeValue(c, JS_GetException(c));
                function = JS_UNDEFINED;
            } else {
                cached = true;
            }
        }
    }
    if (!cached) {
        function = JS_Eval(c, source.c_str(), source.size(), name.c_str(),
                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(function)) {
            error = exception_text(c);
            return false;
        }
        size_t size = 0;
        if (uint8_t* bytes = JS_WriteObject(c, &size, function, JS_WRITE_OBJ_BYTECODE)) {
            fs::disk().make_folders(cache_path.substr(0, cache_path.find_last_of('\\')).c_str());
            replace_file(cache_path, header + std::string(reinterpret_cast<const char*>(bytes), size));
            js_free(c, bytes);
        }
    }
    const JSValue result = JS_EvalFunction(c, function); // (takes the function)
    if (JS_IsException(result)) {
        error = exception_text(c);
        return false;
    }
    JS_FreeValue(c, result);
    return true;
}

// After each .qb file: its TypeScript module, when there is one.
void on_qb_loaded(const char* filename, std::span<const uint8_t> file, bool temporary)
{
    if (!is_on() || !filename) {
        return;
    }
    std::array<char, 16> name{};
    std::snprintf(name.data(), name.size(), "%08x.js", file_hash(file));
    const std::string relative = std::string("by-hash\\") + name.data();
    const std::string path = game_folder_path("thug2-decomp-ts") + "\\" + relative;
    std::string source;
    if (fs::disk().kind(path.c_str()) != fs::PathKind::kFile || !fs::read_text(path, &source)) {
        return;
    }
    Engine& e = engine();
    e.loading_file = filename;
    e.loading_temporary = temporary;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    std::snprintf(name.data(), name.size(), "%08x.qbc", file_hash(file));
    const std::string cache = game_folder_path("thug2-decomp-ts") + "\\cache\\" + name.data();
    bool cached = false;
    // (what it defines belongs to its file: let go of when the file is unloaded - on_qb_unloaded)
    std::array<JSValue, 1> owner{JS_NewUint32(e.context, checksums::hash_from_text(filename))};
    JS_FreeValue(e.context, call_qb("loadingAs", owner));
    const bool loaded = evaluate_module(source, relative, cache, cached, error);
    std::array<JSValue, 1> nobody{JS_NULL};
    JS_FreeValue(e.context, call_qb("loadingAs", nobody));
    if (loaded) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        ++profile().loads;
        profile().load_ns += static_cast<uint64_t>(ms.count()) * 1000000U;
        get_log("ts: %s - its TypeScript module loaded (%u ms%s)", filename, static_cast<unsigned>(ms.count()),
                cached ? ", compiled before" : "");
    } else {
        get_log("ts: %s - its TypeScript module failed: %s", filename, error.c_str());
    }
    e.loading_file.clear();
    e.loading_temporary = false;
}

// A .qb file unloaded: the functions its module defined let go of (its symbols are gone - a later file's definition
// of the same name has a number of its own).
void on_qb_unloaded(uint32_t file_checksum)
{
    if (!is_on()) {
        return;
    }
    std::array<JSValue, 1> owner{JS_NewUint32(engine().context, file_checksum)};
    JS_FreeValue(engine().context, call_qb("forget", owner));
}

JSValue call_qb(const char* method, std::span<JSValue> args)
{
    Engine& e = engine();
    const JSValue function = get(e.context, e.qb, method);
    JS_UpdateStackTop(e.runtime);
    e.budget = interrupt_budget();
    const JSValue result = JS_Call(e.context, function, e.qb, static_cast<int>(args.size()), args.data());
    JS_FreeValue(e.context, function);
    for (JSValue& arg : args) {
        JS_FreeValue(e.context, arg);
    }
    return result;
}

} // namespace

namespace {

std::string types_path() { return game_folder_path("thug2-decomp-ts") + "\\types-recorded.json"; }

// The recorded types written out (runtime.js recordedTypes - the earlier sessions' counts were added in at start-up).
void save_types()
{
    Engine& e = engine();
    std::array<JSValue, 0> none{};
    const JSValue text = call_qb("recordedTypes", none);
    if (const char* json = JS_IsException(text) ? nullptr : JS_ToCString(e.context, text)) {
        replace_file(types_path(), json);
        JS_FreeCString(e.context, json);
    }
    JS_FreeValue(e.context, text);
}

void save_types_now_and_then()
{
    Engine& e = engine();
    const auto now = std::chrono::steady_clock::now();
    if (!e.recording || now - e.types_saved < std::chrono::seconds(5)) {
        return;
    }
    e.types_saved = now;
    save_types();
}

} // namespace

void install()
{
    is_on() = config_int("features", "ts_scripts", 0) != 0;
    profile().on = config_int("debug", "ts_profile", 0) != 0;
    engine().recording = config_int("debug", "ts_types", 0) != 0;
    if (!is_on()) {
        return;
    }
    bool started = false;
    {
        const Timer timer(profile().start_ns);
        started = start_engine();
    }
    if (!started) {
        get_log("ts: QuickJS failed to start - TypeScript scripts are off");
        is_on() = false;
        return;
    }
    set_qb_loaded_hook(on_qb_loaded);
    set_qb_unloaded_hook(on_qb_unloaded);
    get_log("ts: TypeScript scripts on (modules in %s)", game_folder_path("thug2-decomp-ts").c_str());
    if (engine().recording) {
        // (the earlier sessions' counts, added to from here)
        if (std::string json; fs::disk().kind(types_path().c_str()) == fs::PathKind::kFile &&
                                  fs::read_text(types_path(), &json)) {
            std::array<JSValue, 1> args{JS_NewStringLen(engine().context, json.data(), json.size())};
            const JSValue loaded = call_qb("loadTypes", args);
            if (JS_IsException(loaded)) {
                get_log("ts: %s unreadable - recording types afresh: %s", types_path().c_str(),
                        exception_text(engine().context).c_str());
            }
            JS_FreeValue(engine().context, loaded);
        }
        get_log("ts: recording the types scripts see into %s", types_path().c_str());
    }
}

bool enabled() { return is_on(); }

void set_interrupt_budget(uint32_t budget) { interrupt_budget() = budget == 0 ? kInterruptBudget : budget; }

uint32_t begin(uint32_t name, uint32_t title, Struct* args, uint32_t owner, std::string& error)
{
    Engine& e = engine();
    JSContext* c = e.context;
    ++profile().begins;
    const Timer timer(profile().begin_ns);
    const uint32_t level = ++e.next_level == 0 ? ++e.next_level : e.next_level;
    const JSValue list = list_to_js(c, args, 0);
    if (JS_IsException(list)) {
        error = exception_text(c);
        return 0;
    }
    std::array<JSValue, 4> call_args{JS_NewUint32(c, level), JS_NewUint32(c, name), list, JS_NewUint32(c, owner)};
    const JSValue result = call_qb("begin", call_args);
    if (JS_IsException(result)) {
        error = exception_text(c);
        return 0;
    }
    JS_FreeValue(c, result);
    e.synced[level] = fingerprint(args);
    if (profile().on) {
        profile().level_names[level] = title;
    }
    return level;
}

Answer resume(uint32_t level, Struct* args, bool result, Struct* stmt)
{
    Engine& e = engine();
    JSContext* c = e.context;
    Answer answer;
    ++profile().resumes;
    report_profile();
    save_types_now_and_then();
    const Timer total(profile().total_ns);
    JSValue list = JS_NULL;
    const uint64_t seen = fingerprint(args);
    if (e.synced[level] != seen) {
        const Timer timer(profile().to_js_ns);
        list = list_to_js(c, args, 0);
    }
    e.synced[level] = seen;
    if (JS_IsException(list)) {
        answer.error = exception_text(c);
        return answer;
    }
    std::array<JSValue, 3> call_args{JS_NewUint32(c, level), list, JS_NewBool(c, result)};
    JSValue reply = JS_UNDEFINED;
    {
        const auto start = std::chrono::steady_clock::now();
        const Timer timer(profile().js_ns);
        reply = call_qb("resume", call_args);
        if (profile().on) {
            auto& entry = profile().by_script[profile().level_names[level]];
            ++entry.first;
            entry.second += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        }
    }
    const Timer back(profile().from_js_ns);
    if (JS_IsException(reply)) {
        answer.error = exception_text(c);
        return answer;
    }
    // The level's own changes to its parameters: the record's list rebuilt from them.
    const JSValue params = get(c, reply, "params");
    if (JS_IsArray(params)) {
        args->get_clear();
        if (!list_to_struct(c, params, args, 0, answer.error)) {
            JS_FreeValue(c, params);
            JS_FreeValue(c, reply);
            return answer;
        }
        e.synced[level] = fingerprint(args);
    }
    JS_FreeValue(c, params);
    // ... or only its assignments since its last answer, added as an assignment statement adds them.
    const JSValue assigned = get(c, reply, "assigned");
    if (JS_IsArray(assigned)) {
        if (!list_to_struct(c, assigned, args, 0, answer.error)) {
            JS_FreeValue(c, assigned);
            JS_FreeValue(c, reply);
            return answer;
        }
        e.synced[level] = fingerprint(args);
    }
    JS_FreeValue(c, assigned);
    stmt->get_clear();
    const JSValue done = get(c, reply, "done");
    answer.done = JS_ToBool(c, done) != 0;
    JS_FreeValue(c, done);
    const JSValue fields = get(c, reply, answer.done ? "returned" : "args");
    if (JS_IsArray(fields)) {
        list_to_struct(c, fields, stmt, 0, answer.error);
    }
    JS_FreeValue(c, fields);
    if (!answer.done) {
        answer.call = u32_at(c, reply, "call");
        const JSValue on = get(c, reply, "on");
        if (!JS_IsUndefined(on)) {
            answer.on = to_u32(c, on);
        }
        JS_FreeValue(c, on);
    }
    JS_FreeValue(c, reply);
    return answer;
}

void drop(uint32_t level)
{
    if (!is_on() || level == 0) {
        return;
    }
    engine().synced.erase(level);
    std::array<JSValue, 1> call_args{JS_NewUint32(engine().context, level)};
    JS_FreeValue(engine().context, call_qb("drop", call_args));
}

} // namespace script::qb::ts
