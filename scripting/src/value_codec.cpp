// NOT ORIGINAL - private QuickJS codec for the rich by-value QB model.
// See value_codec.hpp for the ownership contract.
#include "value_codec.hpp"
#include <quickjs.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace thug2::js {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("rich codec: " + message); }

bool is_scalar_type(std::uint32_t type) { return type == 1 || type == 2 || type == 13; }
bool is_string_type(std::uint32_t type) { return type == 3 || type == 4; }
bool is_coord_type(std::uint32_t type) { return type == 5 || type == 6; }
bool is_element_type(std::uint32_t type) {
    // 0 is the canonical element type of an empty array (see RichValue::array).
    return type == 0 || type == 1 || type == 2 || type == 3 || type == 4 || type == 5 ||
           type == 6 || type == 10 || type == 12 || type == 13;
}

// Shared per-call budget. Every public call starts with a fresh budget.
// Encode charges it entirely during preflight, before any QuickJS
// allocation; decode (untrusted input) charges it while walking.
struct Budget {
    std::size_t depth = 0;
    std::size_t nodes = 0;
    std::size_t payload = 0;
    void node() {
        if (nodes >= RichLimits::max_nodes) fail("node limit");
        ++nodes;
    }
    void enter() {
        if (depth >= RichLimits::max_depth) fail("depth limit");
        ++depth;
    }
    void exit() { --depth; }
    void payload_bytes(std::size_t bytes) {
        if (bytes > RichLimits::max_payload_bytes || payload > RichLimits::max_payload_bytes - bytes)
            fail("payload byte limit");
        payload += bytes;
    }
};

// RAII guard that frees a JSValue on scope exit unless released.
class JSGuard {
public:
    explicit JSGuard(JSContext* ctx) : ctx_(ctx) {}
    JSGuard(const JSGuard&) = delete;
    JSGuard& operator=(const JSGuard&) = delete;
    ~JSGuard() { JS_FreeValue(ctx_, value_); }
    JSValue release() { return std::exchange(value_, JS_UNDEFINED); }
    JSValueConst slot() const { return value_; }
    void hold(JSValue v) { value_ = v; }
private:
    JSContext* ctx_;
    JSValue value_ = JS_UNDEFINED;
};

// Scoped recursion depth guard.
class DepthGuard {
public:
    explicit DepthGuard(Budget& b) : b_(b) { b_.enter(); }
    ~DepthGuard() { b_.exit(); }
private:
    Budget& b_;
};
} // namespace

struct RichCodec::Impl {
    JSContext* context;
    Budget budget;

    explicit Impl(JSContext* ctx) : context(ctx) {}

    // --- shared helpers -----------------------------------------------------
    // Converts a JS number to uint32 without float64 round-trip surprises:
    // only exact integers in [0, 2^32) are accepted. Missing properties
    // (undefined) and fractional values are rejected instead of coercing.
    bool to_uint32(JSValueConst v, std::uint32_t& out) {
        if (!JS_IsNumber(v)) return false;
        double d = 0.0;
        if (JS_ToFloat64(context, &d, v) < 0) return false; // pending exception stays set
        if (!(d >= 0.0 && d <= 4294967295.0) || std::floor(d) != d) return false;
        out = static_cast<std::uint32_t>(d);
        return true;
    }
    std::uint32_t array_length(JSValueConst array) {
        JSValue len = JS_GetPropertyStr(context, array, "length");
        if (JS_IsException(len)) rethrow_pending("length read threw");
        JSGuard g(context);
        g.hold(len);
        std::uint32_t out = 0;
        if (!to_uint32(len, out)) rethrow_pending("expected uint32 length");
        return out;
    }
    JSValue element(JSValueConst array, std::uint32_t index) {
        JSValue v = JS_GetPropertyUint32(context, array, index);
        if (JS_IsException(v)) rethrow_pending("element read threw");
        return v;
    }
    // quickjs-ng accepts real arrays only; proxy payloads reject explicitly.
    bool is_array(JSValueConst v) {
        const int r = JS_IsArray(v);
        if (r < 0) rethrow_pending("array check threw");
        return r == 1;
    }
    // Drops a pending QuickJS exception without running any user code.
    void drain_pending() {
        if (JS_HasException(context)) {
            JSValue exc = JS_GetException(context);
            JS_FreeValue(context, exc);
        }
    }
    // Never hand out JS_EXCEPTION without a pending engine exception: some
    // QuickJS allocation paths (e.g. JS_NewAtom) fail without setting one.
    void ensure_pending(const char* message) {
        if (!JS_HasException(context)) JS_ThrowInternalError(context, "%s", message);
    }
    // Clears a pending QuickJS exception and converts it to std::runtime_error.
    // Diagnostics never run user code: the message is read through
    // JS_GetOwnProperty, which exposes accessors instead of invoking them,
    // and non-Error values are skipped entirely.
    [[noreturn]] void rethrow_pending(const char* what) {
        const bool pending = JS_HasException(context);
        JSValue exc = JS_GetException(context); // clears the pending exception
        JSGuard g(context);
        g.hold(exc);
        std::string message(what);
        bool resource = pending && JS_IsNull(exc);
        if (JS_IsError(exc)) {
            JSAtom atom = JS_NewAtom(context, "message");
            if (atom != JS_ATOM_NULL) {
                JSPropertyDescriptor desc;
                const int found = JS_GetOwnProperty(context, &desc, exc, atom);
                JS_FreeAtom(context, atom);
                if (found > 0) {
                    JSGuard gv(context);
                    gv.hold(desc.value);
                    JSGuard gg(context);
                    gg.hold(desc.getter);
                    JSGuard gs(context);
                    gs.hold(desc.setter);
                    if (!(desc.flags & JS_PROP_GETSET) && JS_IsString(desc.value)) {
                        const char* utf8 = JS_ToCString(context, desc.value);
                        if (utf8) {
                            struct CString {
                                JSContext* ctx; const char* text;
                                ~CString() { JS_FreeCString(ctx, text); }
                            } owned{context, utf8};
                            message += ": ";
                            message += utf8;
                            resource = message.find("out of memory") != std::string::npos ||
                                       message.find("stack overflow") != std::string::npos ||
                                       message.find("interrupted") != std::string::npos;
                        } else {
                            resource = true;
                        }
                    }
                }
                if (found < 0) resource = true;
                // found == 0: no message property. found == -1: the descriptor
                // read itself failed and set a new pending exception; the
                // public wrappers drain it after this throw propagates.
            } else resource = true;
        }
        throw RichCodecError("rich codec: " + message, resource);
    }
    void check_no_pending() {
        if (JS_HasException(context)) rethrow_pending("unexpected pending QuickJS exception");
    }

    // --- preflight: model limits, before any QuickJS state ------------------
    // Walks the immutable model and charges the full budget so that the
    // encode walk itself can only fail through QuickJS allocations.
    // Payload accounting is total: every transported word counts,
    // including scalar bits and structure/parameter field-name CRCs.
    static constexpr std::size_t word_bytes = sizeof(std::uint32_t);
    void preflight(const RichValue& value) {
        budget.node();
        const std::uint32_t type = value.type();
        if (is_scalar_type(type)) {
            budget.payload_bytes(word_bytes); // transported bits word
            return;
        }
        if (is_string_type(type)) {
            budget.payload_bytes(value.bytes().size());
            return;
        }
        if (is_coord_type(type)) {
            budget.payload_bytes(value.coordinates().size() * word_bytes);
            return;
        }
        DepthGuard guard(budget);
        if (type == 10) {
            if (value.member_names().size() > RichLimits::max_fields) fail("field limit");
            budget.payload_bytes(value.member_names().size() * word_bytes); // CRC names
            for (const RichValue& v : value.member_values()) preflight(v);
            return;
        }
        if (value.elements().size() > RichLimits::max_elements) fail("element limit");
        for (const RichValue& v : value.elements()) preflight(v);
    }

    // --- encode: model -> QuickJS --------------------------------------------
    // The model is immutable and preflighted, so every failure below is a
    // QuickJS allocation failure: return JS_EXCEPTION and leave the pending
    // exception in place (ensure_pending backfills one if QuickJS did not).
    // All consuming calls (JS_DefinePropertyValueStr/Uint32)
    // free their value on both success and failure, so values are handed over
    // exactly once and never freed twice.
    // Define semantics (not assignment): the property is created as an own
    // data property regardless of prototype contents, so inherited setters
    // or non-writable inherited properties cannot intercept transport writes.
    bool set_u32(JSValueConst object, const char* key, std::uint32_t x) {
        JSValue v = JS_NewUint32(context, x);
        if (JS_IsException(v)) return false;
        return JS_DefinePropertyValueStr(context, object, key, v, JS_PROP_C_W_E) >= 0; // consumes v
    }
    bool set_value(JSValueConst object, const char* key, JSValue v) {
        return JS_DefinePropertyValueStr(context, object, key, v, JS_PROP_C_W_E) >= 0; // consumes v
    }
    bool push_u32(JSValueConst array, std::uint32_t index, std::uint32_t x) {
        JSValue v = JS_NewUint32(context, x);
        if (JS_IsException(v)) return false;
        return JS_DefinePropertyValueUint32(context, array, index, v, JS_PROP_C_W_E) >= 0; // consumes v
    }
    bool push_value(JSValueConst array, std::uint32_t index, JSValue v) {
        return JS_DefinePropertyValueUint32(context, array, index, v, JS_PROP_C_W_E) >= 0; // consumes v
    }

    JSValue encode(const RichValue& value) {
        const std::uint32_t type = value.type();
        if (is_scalar_type(type)) return encode_scalar(type, value.bits());
        if (is_string_type(type)) return encode_string(type, value.bytes());
        if (is_coord_type(type)) return encode_coords(type, value.coordinates());
        if (type == 10) return encode_structure(value);
        return encode_array(value);
    }
    JSValue encode_scalar(std::uint32_t type, std::uint32_t bits) {
        JSValue out = JS_NewObject(context);
        if (JS_IsException(out)) return out;
        JSGuard go(context);
        go.hold(out);
        if (!set_u32(out, "type", type) || !set_u32(out, "bits", bits)) return JS_EXCEPTION;
        return go.release();
    }
    JSValue encode_string(std::uint32_t type, const RichValue::Bytes& bytes) {
        JSValue array = JS_NewArray(context);
        if (JS_IsException(array)) return array;
        JSGuard ga(context);
        ga.hold(array);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (!push_u32(array, static_cast<std::uint32_t>(i), bytes[i])) return JS_EXCEPTION;
        JSValue out = JS_NewObject(context);
        if (JS_IsException(out)) return out;
        JSGuard go(context);
        go.hold(out);
        if (!set_u32(out, "type", type) || !set_value(out, "bytes", ga.release())) return JS_EXCEPTION;
        return go.release();
    }
    JSValue encode_coords(std::uint32_t type, const RichValue::Words& words) {
        JSValue array = JS_NewArray(context);
        if (JS_IsException(array)) return array;
        JSGuard ga(context);
        ga.hold(array);
        for (std::size_t i = 0; i < words.size(); ++i)
            if (!push_u32(array, static_cast<std::uint32_t>(i), words[i])) return JS_EXCEPTION;
        JSValue out = JS_NewObject(context);
        if (JS_IsException(out)) return out;
        JSGuard go(context);
        go.hold(out);
        if (!set_u32(out, "type", type) || !set_value(out, "coordinates", ga.release())) return JS_EXCEPTION;
        return go.release();
    }
    JSValue encode_structure(const RichValue& value) {
        JSValue members = JS_NewArray(context);
        if (JS_IsException(members)) return members;
        JSGuard gm(context);
        gm.hold(members);
        const auto& names = value.member_names();
        const auto& values = value.member_values();
        for (std::size_t i = 0; i < names.size(); ++i) {
            JSValue field = encode_field(names[i], values[i]);
            if (JS_IsException(field)) return JS_EXCEPTION;
            if (!push_value(members, static_cast<std::uint32_t>(i), field)) return JS_EXCEPTION;
        }
        JSValue out = JS_NewObject(context);
        if (JS_IsException(out)) return out;
        JSGuard go(context);
        go.hold(out);
        if (!set_u32(out, "type", 10) || !set_value(out, "members", gm.release())) return JS_EXCEPTION;
        return go.release();
    }
    JSValue encode_array(const RichValue& value) {
        JSValue elements = JS_NewArray(context);
        if (JS_IsException(elements)) return elements;
        JSGuard ge(context);
        ge.hold(elements);
        const auto& items = value.elements();
        for (std::size_t i = 0; i < items.size(); ++i) {
            JSValue item = encode(items[i]);
            if (JS_IsException(item)) return JS_EXCEPTION;
            if (!push_value(elements, static_cast<std::uint32_t>(i), item)) return JS_EXCEPTION;
        }
        JSValue out = JS_NewObject(context);
        if (JS_IsException(out)) return out;
        JSGuard go(context);
        go.hold(out);
        if (!set_u32(out, "type", 12) || !set_u32(out, "elementType", value.element_type()) ||
            !set_value(out, "elements", ge.release())) {
            return JS_EXCEPTION;
        }
        return go.release();
    }
    JSValue encode_field(std::uint32_t name, const RichValue& value) {
        JSValue field = JS_NewArray(context);
        if (JS_IsException(field)) return field;
        JSGuard g(context);
        g.hold(field);
        if (!push_u32(field, 0, name)) return JS_EXCEPTION;
        JSValue item = encode(value);
        if (JS_IsException(item)) return JS_EXCEPTION;
        if (!push_value(field, 1, item)) return JS_EXCEPTION; // consumed either way
        return g.release();
    }

    // --- decode: QuickJS -> model -------------------------------------------
    // All decode helpers throw std::runtime_error; a pending QuickJS
    // exception is always converted and cleared via rethrow_pending, and the
    // public wrappers drain anything left over on failure paths.
    std::uint32_t type_tag(JSValueConst object) {
        JSValue raw = JS_GetPropertyStr(context, object, "type");
        if (JS_IsException(raw)) rethrow_pending("type read threw");
        JSGuard g(context);
        g.hold(raw);
        std::uint32_t type = 0;
        if (!to_uint32(raw, type)) rethrow_pending("expected uint32 'type'");
        if (type == 20 || type == 21)
            fail("reference tags 20/21 are not supported by the by-value codec");
        if (!is_scalar_type(type) && !is_string_type(type) && !is_coord_type(type) &&
            type != 10 && type != 12)
            fail("unsupported value type " + std::to_string(type));
        return type;
    }
    std::uint32_t uint32_property(JSValueConst object, const char* key) {
        JSValue raw = JS_GetPropertyStr(context, object, key);
        if (JS_IsException(raw)) rethrow_pending((std::string(key) + " read threw").c_str());
        JSGuard g(context);
        g.hold(raw);
        std::uint32_t out = 0;
        if (!to_uint32(raw, out)) {
            std::string message = std::string("expected uint32 '") + key + "'";
            rethrow_pending(message.c_str());
        }
        return out;
    }
    RichValue decode(JSValueConst value, std::unordered_set<const void*>& path) {
        budget.node();
        if (!JS_IsObject(value)) fail("expected a QB value object");
        const void* identity = JS_VALUE_GET_PTR(value);
        if (!path.insert(identity).second) fail("cyclic value traversal");
        struct Exit {
            std::unordered_set<const void*>& path;
            const void* id;
            ~Exit() { path.erase(id); }
        } exit{path, identity};
        const std::uint32_t type = type_tag(value);
        if (is_scalar_type(type)) {
            const std::uint32_t bits = uint32_property(value, "bits");
            budget.payload_bytes(word_bytes); // transported bits word
            return type == 1 ? RichValue::integer(bits)
                 : type == 2 ? RichValue::floating(bits)
                             : RichValue::name(bits);
        }
        if (is_string_type(type)) {
            JSValue raw = JS_GetPropertyStr(context, value, "bytes");
            if (JS_IsException(raw)) rethrow_pending("bytes read threw");
            JSGuard g(context);
            g.hold(raw);
            if (!is_array(g.slot())) fail("invalid string shape");
            const std::uint32_t length = array_length(g.slot());
            if (length > RichLimits::max_string_bytes) fail("string byte limit");
            RichValue::Bytes bytes(length);
            for (std::uint32_t i = 0; i < length; ++i) {
                JSGuard gi(context);
                gi.hold(element(g.slot(), i));
                std::uint32_t byte = 0;
                if (!to_uint32(gi.slot(), byte)) rethrow_pending("invalid string byte");
                if (byte > 255) fail("invalid string byte");
                bytes[i] = static_cast<std::uint8_t>(byte);
            }
            budget.payload_bytes(bytes.size());
            return RichValue::string(std::move(bytes), type == 4);
        }
        if (is_coord_type(type)) {
            JSValue raw = JS_GetPropertyStr(context, value, "coordinates");
            if (JS_IsException(raw)) rethrow_pending("coordinates read threw");
            JSGuard g(context);
            g.hold(raw);
            if (!is_array(g.slot())) fail("invalid coordinate shape");
            const std::uint32_t length = array_length(g.slot());
            if (length != (type == 5 ? 2u : 3u)) fail("invalid coordinate count");
            RichValue::Words words(length);
            for (std::uint32_t i = 0; i < length; ++i) {
                JSGuard gi(context);
                gi.hold(element(g.slot(), i));
                std::uint32_t word = 0;
                if (!to_uint32(gi.slot(), word)) rethrow_pending("invalid coordinate word");
                words[i] = word;
            }
            budget.payload_bytes(words.size() * sizeof(std::uint32_t));
            return type == 5 ? RichValue::pair(words[0], words[1])
                             : RichValue::vector(words[0], words[1], words[2]);
        }
        DepthGuard guard(budget);
        if (type == 10) {
            JSValue raw = JS_GetPropertyStr(context, value, "members");
            if (JS_IsException(raw)) rethrow_pending("members read threw");
            JSGuard g(context);
            g.hold(raw);
            if (!is_array(g.slot())) fail("expected members array");
            const std::uint32_t length = array_length(g.slot());
            if (length > RichLimits::max_fields) fail("field limit");
            budget.payload_bytes(length * word_bytes); // CRC names
            std::vector<std::uint32_t> names(length);
            std::vector<RichValue> values;
            values.reserve(length);
            for (std::uint32_t i = 0; i < length; ++i) {
                JSGuard gi(context);
                gi.hold(element(g.slot(), i));
                if (!is_array(gi.slot())) fail("invalid parameter field");
                const std::uint32_t field_length = array_length(gi.slot());
                if (field_length != 2) fail("invalid parameter field");
                JSGuard gn(context);
                gn.hold(element(gi.slot(), 0));
                std::uint32_t name = 0;
                if (!to_uint32(gn.slot(), name)) rethrow_pending("expected uint32 field name");
                JSGuard gv(context);
                gv.hold(element(gi.slot(), 1));
                names[i] = name;
                values.push_back(decode(gv.slot(), path));
            }
            return RichValue::structure(std::move(names), std::move(values));
        }
        // type 12
        const std::uint32_t element_type = uint32_property(value, "elementType");
        if (!is_element_type(element_type)) fail("invalid array element type");
        JSValue raw = JS_GetPropertyStr(context, value, "elements");
        if (JS_IsException(raw)) rethrow_pending("elements read threw");
        JSGuard g(context);
        g.hold(raw);
        if (!is_array(g.slot())) fail("invalid array shape");
        const std::uint32_t length = array_length(g.slot());
        if (length > RichLimits::max_elements) fail("element limit");
        if ((element_type == 0) != (length == 0)) fail("empty array type mismatch");
        std::vector<RichValue> elements;
        elements.reserve(length);
        for (std::uint32_t i = 0; i < length; ++i) {
            JSGuard gi(context);
            gi.hold(element(g.slot(), i));
            RichValue child = decode(gi.slot(), path);
            if (child.type() != element_type) fail("array element type mismatch");
            elements.push_back(std::move(child));
        }
        return RichValue::array(element_type, std::move(elements));
    }
    RichParameters decode_parameters(JSValueConst array, std::unordered_set<const void*>& path) {
        if (!is_array(array)) fail("expected parameters array");
        const std::uint32_t length = array_length(array);
        if (length > RichLimits::max_fields) fail("field limit");
        budget.payload_bytes(length * word_bytes); // CRC names
        RichParameters out;
        out.reserve(length);
        for (std::uint32_t i = 0; i < length; ++i) {
            JSGuard g(context);
            g.hold(element(array, i));
            if (!is_array(g.slot())) fail("invalid parameter field");
            const std::uint32_t field_length = array_length(g.slot());
            if (field_length != 2) fail("invalid parameter field");
            JSGuard gn(context);
            gn.hold(element(g.slot(), 0));
            std::uint32_t name = 0;
            if (!to_uint32(gn.slot(), name)) rethrow_pending("expected uint32 field name");
            JSGuard gv(context);
            gv.hold(element(g.slot(), 1));
            out.push_back(RichField{name, decode(gv.slot(), path)});
        }
        return out;
    }
};

RichCodec::RichCodec(JSContext* context) : impl_(*new Impl(context)) {}
RichCodec::~RichCodec() { delete &impl_; }

// Failure contract (see value_codec.hpp):
// - model/limit violations throw std::runtime_error before any QuickJS state
//   is touched (preflight); std::bad_alloc is never swallowed;
// - a QuickJS allocation failure returns JS_EXCEPTION and a pending engine
//   exception is always set;
// - decode always leaves the context without a pending exception.

JSValue RichCodec::encode(const RichValue& value) {
    impl_.budget = Budget{};
    impl_.preflight(value);
    JSValue out = impl_.encode(value);
    if (JS_IsException(out)) impl_.ensure_pending("quickjs allocation failed during encode");
    return out;
}

JSValue RichCodec::encode_parameters(const RichParameters& parameters) {
    validate_parameters(parameters);
    JSValue array = JS_NewArray(impl_.context);
    if (JS_IsException(array)) {
        impl_.ensure_pending("quickjs allocation failed during encode");
        return array;
    }
    JSGuard g(impl_.context);
    g.hold(array);
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        JSValue field = impl_.encode_field(parameters[i].name, parameters[i].value);
        if (JS_IsException(field) ||
            JS_DefinePropertyValueUint32(impl_.context, array, static_cast<std::uint32_t>(i),
                                          field, JS_PROP_C_W_E) < 0) {
            // field was consumed by the define call if it ran; the sentinel
            // case carries no reference. Never free it here.
            impl_.ensure_pending("quickjs allocation failed during encode");
            return JS_EXCEPTION;
        }
    }
    return g.release();
}

void RichCodec::validate_parameters(const RichParameters& parameters) {
    if (parameters.size() > RichLimits::max_fields)
        throw std::runtime_error("rich codec: field limit");
    impl_.budget = Budget{};
    // Charge the outer field-name CRCs first, then the field values, so the
    // parameter list and its decoded form account the same total payload
    // (decode_parameters charges length * word_bytes for the same names).
    impl_.budget.payload_bytes(parameters.size() * Impl::word_bytes);
    for (const RichField& field : parameters) impl_.preflight(field.value);
}

RichValue RichCodec::decode(JSValueConst value) {
    impl_.budget = Budget{};
    std::unordered_set<const void*> path;
    try {
        RichValue out = impl_.decode(value, path);
        impl_.check_no_pending();
        return out;
    } catch (...) {
        // Every failure path leaves a reusable context: any pending QuickJS
        // exception (including ones raised while diagnosing) is dropped here
        // without running user code. std::bad_alloc still propagates.
        impl_.drain_pending();
        throw;
    }
}

RichParameters RichCodec::decode_parameters(JSValueConst array) {
    impl_.budget = Budget{};
    std::unordered_set<const void*> path;
    try {
        RichParameters out = impl_.decode_parameters(array, path);
        impl_.check_no_pending();
        return out;
    } catch (...) {
        impl_.drain_pending();
        throw;
    }
}
} // namespace thug2::js
