// NOT ORIGINAL - rich by-value QB data model for the optional QuickJS bridge.
// owning immutable acyclic containers and byte strings.
// Reference tags 20/21 are deliberately not representable here; the codec
// rejects them explicitly. This header must stay independent of QuickJS.
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thug2::js {
class RichValue;
namespace detail {
// Shared immutable payload node; internal to RichValue.
struct RichValueNode {
    std::uint32_t type = 0;
    std::uint32_t bits = 0;          // types 1/2/13
    std::uint32_t element_type = 0;  // type 12
    std::vector<std::uint8_t> bytes; // types 3/4
    std::vector<std::uint32_t> coordinates; // types 5/6
    std::vector<std::uint32_t> member_names;   // type 10, parallel to member_values
    std::vector<RichValue> member_values;     // type 10
    std::vector<RichValue> elements;          // type 12
};
} // namespace detail
class RichValue;
using RichValueNode = detail::RichValueNode;

// Immutable recursive by-value model of a QB value. Nodes are shared and
// never mutated after construction, so a decoded model survives the JS
// boundary without dangling pointers. Cycles cannot be constructed through
// the public factories: containers are built from already-complete values.
class RichValue {
public:
    using Bytes = std::vector<std::uint8_t>;   // raw string bytes, no NUL
    using Words = std::vector<std::uint32_t>; // raw float32 words

    // Factories validate their payload and throw std::runtime_error on any
    // violation (limits match the codec contract).
    static RichValue integer(std::uint32_t bits) { return scalar(1, bits); }
    static RichValue floating(std::uint32_t bits) { return scalar(2, bits); }
    static RichValue name(std::uint32_t bits) { return scalar(13, bits); }
    static RichValue string(Bytes bytes, bool local = false) {
        if (bytes.size() > max_string_bytes)
            throw std::runtime_error("rich value: string byte limit");
        for (std::uint8_t c : bytes)
            if (c == 0) throw std::runtime_error("rich value: embedded NUL in string payload");
        auto node = std::make_shared<Node>();
        node->type = local ? 4u : 3u;
        node->bytes = std::move(bytes);
        return RichValue(std::move(node));
    }
    static RichValue pair(std::uint32_t x, std::uint32_t y) {
        return coordinates(5, {x, y});
    }
    static RichValue vector(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return coordinates(6, {x, y, z});
    }
    static RichValue structure(std::vector<std::uint32_t> names, std::vector<RichValue> values) {
        if (names.size() != values.size())
            throw std::runtime_error("rich value: structure member mismatch");
        if (names.size() > max_fields)
            throw std::runtime_error("rich value: structure field limit");
        auto node = std::make_shared<Node>();
        node->type = 10u;
        node->member_names = std::move(names);
        node->member_values = std::move(values);
        return RichValue(std::move(node));
    }
    static RichValue array(std::uint32_t element_type, std::vector<RichValue> elements) {
        if (element_type != 0 && element_type != 1 && element_type != 2 && element_type != 3 &&
            element_type != 4 && element_type != 5 && element_type != 6 && element_type != 10 &&
            element_type != 12 && element_type != 13)
            throw std::runtime_error("rich value: invalid array element type");
        if (elements.size() > max_elements)
            throw std::runtime_error("rich value: array element limit");
        if ((element_type == 0) != elements.empty())
            throw std::runtime_error("rich value: empty array type mismatch");
        for (const RichValue& child : elements)
            if (child.type() != element_type)
                throw std::runtime_error("rich value: array element type mismatch");
        auto node = std::make_shared<Node>();
        node->type = 12u;
        node->element_type = element_type;
        node->elements = std::move(elements);
        return RichValue(std::move(node));
    }

    // Transported QB type tag: 1, 2, 3, 4, 5, 6, 10, 12 or 13.
    std::uint32_t type() const { return data().type; }
    // Raw bits for types 1/2/13. No float64 round-trip, no Name/Integer mixing.
    std::uint32_t bits() const {
        require_type(1, "bits");
        return data().bits;
    }
    // Byte payload for types 3/4; canonical NUL-prefix bytes without a NUL.
    const Bytes& bytes() const {
        require_type(3, "bytes");
        return data().bytes;
    }
    // Raw float32 words for types 5/6 (2 or 3 entries).
    const Words& coordinates() const {
        require_type(5, "coordinates");
        return data().coordinates;
    }
    // Ordered member list for type 10; duplicates and order preserved as-is.
    const std::vector<std::uint32_t>& member_names() const {
        require_type(10, "member names");
        return data().member_names;
    }
    const std::vector<RichValue>& member_values() const {
        require_type(10, "member values");
        return data().member_values;
    }
    // Element type for type 12; 0 iff the array is empty.
    std::uint32_t element_type() const {
        require_type(12, "element type");
        return data().element_type;
    }
    // Homogeneous elements for type 12.
    const std::vector<RichValue>& elements() const {
        require_type(12, "elements");
        return data().elements;
    }

    // Structural equality: same type and same raw payload. Container identity
    // is not tracked in the by-value model.
    bool operator==(const RichValue& other) const {
        const Node& a = data();
        const Node& b = other.data();
        if (a.type != b.type) return false;
        switch (a.type) {
        case 1: case 2: case 13: return a.bits == b.bits;
        case 3: case 4: return a.bytes == b.bytes;
        case 5: case 6: return a.coordinates == b.coordinates;
        case 10:
            return a.member_names == b.member_names && a.member_values == b.member_values;
        case 12:
            return a.element_type == b.element_type && a.elements == b.elements;
        default: return false;
        }
    }
    bool operator!=(const RichValue& other) const { return !(*this == other); }

    // Hard model limits shared with the codec.
    static constexpr std::size_t max_fields = 4096;
    static constexpr std::size_t max_elements = 250000;
    static constexpr std::size_t max_string_bytes = 250000;
private:
    using Node = detail::RichValueNode;
    static RichValue scalar(std::uint32_t type, std::uint32_t bits) {
        auto node = std::make_shared<Node>();
        node->type = type;
        node->bits = bits;
        return RichValue(std::move(node));
    }
    static RichValue coordinates(std::uint32_t type, Words words) {
        auto node = std::make_shared<Node>();
        node->type = type;
        node->coordinates = std::move(words);
        return RichValue(std::move(node));
    }
    explicit RichValue(std::shared_ptr<const Node> node) : node_(std::move(node)) {}
    const Node& data() const {
        if (!node_) throw std::runtime_error("rich value: empty value");
        return *node_;
    }
    void require_type(std::uint32_t expected, const char* /*accessor*/) const {
        const std::uint32_t actual = data().type;
        // Accessor families: bits covers 1/2/13, bytes 3/4, coordinates 5/6.
        const bool ok = (expected == 1 && (actual == 1 || actual == 2 || actual == 13)) ||
                        (expected == 3 && (actual == 3 || actual == 4)) ||
                        (expected == 5 && (actual == 5 || actual == 6)) ||
                        (expected == 10 && actual == 10) || (expected == 12 && actual == 12);
        if (!ok) throw std::runtime_error("rich value: wrong type for accessor");
    }
    std::shared_ptr<const Node> node_;
};

// Ordered parameter field: uint32 CRC name plus the value. Order and
// duplicates are preserved exactly; no semantic merge at this level.
struct RichField {
    std::uint32_t name;
    RichValue value;
    bool operator==(const RichField& other) const { return name==other.name && value==other.value; }
};
using RichParameters = std::vector<RichField>;

} // namespace thug2::js
