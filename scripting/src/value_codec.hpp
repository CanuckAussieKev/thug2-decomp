// NOT ORIGINAL - private QuickJS codec for the rich by-value QB model.
// This header is internal to the scripting library and
// may include QuickJS; the public model (js_value.hpp) must not.
#pragma once
#include "thug2/js_value.hpp"
#include <quickjs.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace thug2::js {
// Internal error metadata survives draining the engine exception. Runtime
// marshaling treats resource failures as terminal; malformed data stays local.
class RichCodecError : public std::runtime_error {
public:
    RichCodecError(const std::string& message, bool resource) : std::runtime_error(message), resource_(resource) {}
    bool resource_failure() const noexcept { return resource_; }
private:
    bool resource_;
};
// Hard limits shared by every codec call.
struct RichLimits {
    static constexpr std::size_t max_depth = 128;
    static constexpr std::size_t max_nodes = 250000;
    static constexpr std::size_t max_fields = RichValue::max_fields;      // 4096
    static constexpr std::size_t max_elements = RichValue::max_elements;  // 250000
    static constexpr std::size_t max_string_bytes = RichValue::max_string_bytes; // 250000
    static constexpr std::size_t max_payload_bytes = 4 * 1024 * 1024;     // 4 MiB
};

// Converts a rich model into a QuickJS value object shaped like the values.js
// representation ({type,bits} / {type,bytes} / {type,coordinates} /
// {type,members} / {type,elementType,elements}).
//
// Ownership and error policy (both directions):
// - Validation/limit failures happen before or without any QuickJS state
//   change and surface as std::runtime_error; the QuickJS context is left
//   with no pending exception.
// - The total payload budget (RichLimits::max_payload_bytes) counts every
//   transported word: scalar bits, string bytes, coordinate words and
//   structure/parameter field-name CRCs, per transported occurrence.
// - If a QuickJS allocation or property access fails and leaves a pending
//   exception, encode returns JS_EXCEPTION (the pending exception stays for
//   the caller) and decode converts the pending exception into
//   RichCodecError after clearing it. Its resource_failure flag lets Runtime
//   distinguish ordinary data errors from terminal boundary resource failures.
// - Successfully encoded JSValues are owned by the caller (single reference,
//   free with JS_FreeValue); on any error path nothing leaks.
// - Encoded objects carry own data properties created via define semantics:
//   prototype setters or non-writable inherited properties cannot intercept
//   or corrupt the transported shape.
// Reference tags 20/21 are not representable in the model and are rejected
// explicitly at any depth.
class RichCodec {
public:
    explicit RichCodec(JSContext* context);
    ~RichCodec();
    RichCodec(const RichCodec&) = delete;
    RichCodec& operator=(const RichCodec&) = delete;

    // Encode a model value; shape/limit validation happens before any
    // QuickJS allocation.
    JSValue encode(const RichValue& value); // caller frees via JS_FreeValue
    // Encode an ordered parameter list as [[name,value],...].
    JSValue encode_parameters(const RichParameters& parameters);
    // Same bounded preflight as encode_parameters, without QuickJS allocation.
    // Lets the Runtime reject invalid host outputs before the marshaling phase.
    void validate_parameters(const RichParameters& parameters);
    // Decode a QuickJS value object into an owning model. Validates real
    // types/lengths/tags, rejects getter/proxy exceptions (a pending QuickJS
    // exception is never ignored) and rejects cycles. Tags 20/21 and any
    // unsupported descriptor fail explicitly.
    RichValue decode(JSValueConst value);
    // Decode [[name,value],...] into an ordered model parameter list.
    RichParameters decode_parameters(JSValueConst array);
private:
    struct Impl;
    Impl& impl_;
};
} // namespace thug2::js
