#pragma once

#include "base/core/checked_index.h"
#include "base/core/raw_view.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// NOT ORIGINAL - bounds-checked little-endian readers and writers for the game's file, save and network formats.
//
// The formats are little-endian and unaligned. Each format has a plain struct of its fields and `read_<x>` /
// `write_<x>` functions built on these, so that the game's own classes never have to mirror a byte layout.
//
// Reading past the end, or writing past the capacity, fails: the value read is 0 (nothing is written), the
// position stays where it was and ok() turns false for good. Callers check ok() once after a group of reads.
namespace util {

constexpr int kBitsPerByte = 8; // the byte positions of a little-endian value
constexpr int kBitsPerShort = 16;
constexpr int kBitsPerThreeBytes = 24;

class ByteReader {
public:
    ByteReader(const void* data, size_t size) : data_(static_cast<const uint8_t*>(data)), size_(size) {}
    explicit ByteReader(std::span<const uint8_t> bytes) : data_(bytes.data()), size_(bytes.size()) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] size_t position() const { return position_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] size_t remaining() const { return size_ - position_; }
    // The next byte to be read.
    [[nodiscard]] const uint8_t* cursor() const { return rest().data(); }
    // The bytes from the next one to the end.
    [[nodiscard]] std::span<const uint8_t> rest() const { return std::span<const uint8_t>(data_, size_).subspan(position_); }

    // Move to `position` (fails beyond the end).
    bool seek(size_t position)
    {
        if (position > size_) {
            return fail();
        }
        position_ = position;
        return true;
    }
    bool skip(size_t count)
    {
        if (!has(count)) {
            return fail();
        }
        position_ += count;
        return true;
    }

    uint8_t u8()
    {
        if (!has(1)) {
            return fail_value<uint8_t>();
        }
        const uint8_t value = core::at(rest(), 0);
        ++position_;
        return value;
    }
    uint16_t u16()
    {
        if (!has(2)) {
            return fail_value<uint16_t>();
        }
        const std::span<const uint8_t> bytes = rest().first(2);
        position_ += 2;
        return static_cast<uint16_t>(core::at(bytes, 0) | core::at(bytes, 1) << kBitsPerByte);
    }
    uint32_t u32()
    {
        if (!has(4)) {
            return fail_value<uint32_t>();
        }
        const std::span<const uint8_t> bytes = rest().first(4);
        position_ += 4;
        return core::at(bytes, 0) | core::at(bytes, 1) << kBitsPerByte | core::at(bytes, 2) << kBitsPerShort |
               static_cast<uint32_t>(core::at(bytes, 3)) << kBitsPerThreeBytes;
    }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32()
    {
        const uint32_t bits = u32();
        auto value = std::bit_cast<float>(bits);
        return value;
    }

    // `count` raw bytes copied out.
    bool bytes(void* out, size_t count)
    {
        if (!has(count)) {
            return fail();
        }
        std::memcpy(out, rest().first(count).data(), count);
        position_ += count;
        return true;
    }
    // A zero-terminated string in place (the terminator read too); null if there is no terminator before the end.
    const char* c_string()
    {
        const auto* start = raw::chars(rest().data());
        const size_t length = strnlen_in_bounds();
        if (length == kNotFound) {
            fail();
            return nullptr;
        }
        position_ += length + 1;
        return start;
    }
    // A fixed-size text field (`count` bytes, zero-padded; possibly without a terminator) copied into `out`
    // (`out_size` bytes), always terminated.
    bool fixed_string(char* out, size_t out_size, size_t count)
    {
        if (!has(count) || out_size == 0) {
            return fail();
        }
        const size_t copy = count < out_size - 1 ? count : out_size - 1;
        const std::span<char> field(out, out_size);
        std::memcpy(out, rest().first(copy).data(), copy);
        core::at(field, copy) = '\0';
        core::at(field, strnlen(out, copy)) = '\0';
        position_ += count;
        return true;
    }

private:
    static constexpr size_t kNotFound = ~static_cast<size_t>(0);

    [[nodiscard]] bool has(size_t count) const { return ok_ && count <= size_ - position_; }
    bool fail()
    {
        ok_ = false;
        return false;
    }
    template <typename Value>
    Value fail_value()
    {
        ok_ = false;
        return Value{};
    }
    // The length of the text at `text`, no more than `max` (memchr stops at the first zero within them).
    static size_t strnlen(const char* text, size_t max)
    {
        const void* end = std::memchr(text, 0, max);
        return end ? static_cast<size_t>(static_cast<const char*>(end) - text) : max;
    }
    [[nodiscard]] size_t strnlen_in_bounds() const
    {
        const size_t max = size_ - position_;
        const std::span<const uint8_t> bytes = rest();
        for (size_t i = 0; i < max; ++i) {
            if (core::at(bytes, i) == 0) {
                return i;
            }
        }
        return kNotFound;
    }

    const uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
    bool ok_ = true;
};

class ByteWriter {
public:
    ByteWriter(void* data, size_t capacity) : data_(static_cast<uint8_t*>(data)), capacity_(capacity) {}
    explicit ByteWriter(std::span<uint8_t> bytes) : data_(bytes.data()), capacity_(bytes.size()) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] size_t position() const { return position_; }
    [[nodiscard]] size_t capacity() const { return capacity_; }
    [[nodiscard]] size_t remaining() const { return capacity_ - position_; }
    [[nodiscard]] uint8_t* cursor() const { return rest().data(); }
    // The room from the next byte to the end.
    [[nodiscard]] std::span<uint8_t> rest() const { return std::span<uint8_t>(data_, capacity_).subspan(position_); }

    bool seek(size_t position)
    {
        if (position > capacity_) {
            return fail();
        }
        position_ = position;
        return true;
    }

    bool u8(uint8_t value)
    {
        if (!has(1)) {
            return fail();
        }
        core::at(rest(), 0) = value;
        ++position_;
        return true;
    }
    bool u16(uint16_t value)
    {
        if (!has(2)) {
            return fail();
        }
        const std::span<uint8_t> bytes = rest().first(2);
        core::at(bytes, 0) = static_cast<uint8_t>(value);
        core::at(bytes, 1) = static_cast<uint8_t>(value >> kBitsPerByte);
        position_ += 2;
        return true;
    }
    bool u32(uint32_t value)
    {
        if (!has(4)) {
            return fail();
        }
        const std::span<uint8_t> bytes = rest().first(4);
        core::at(bytes, 0) = static_cast<uint8_t>(value);
        core::at(bytes, 1) = static_cast<uint8_t>(value >> kBitsPerByte);
        core::at(bytes, 2) = static_cast<uint8_t>(value >> kBitsPerShort);
        core::at(bytes, 3) = static_cast<uint8_t>(value >> kBitsPerThreeBytes);
        position_ += 4;
        return true;
    }
    bool i8(int8_t value) { return u8(static_cast<uint8_t>(value)); }
    bool i16(int16_t value) { return u16(static_cast<uint16_t>(value)); }
    bool i32(int32_t value) { return u32(static_cast<uint32_t>(value)); }
    bool f32(float value)
    {
        auto bits = std::bit_cast<uint32_t>(value);
        return u32(bits);
    }
    bool bytes(const void* data, size_t count)
    {
        if (!has(count)) {
            return fail();
        }
        if (count != 0U) {
            std::memcpy(rest().first(count).data(), data, count);
        }
        position_ += count;
        return true;
    }
    bool fill(uint8_t value, size_t count)
    {
        if (!has(count)) {
            return fail();
        }
        std::memset(rest().first(count).data(), value, count);
        position_ += count;
        return true;
    }
    // The text and its terminator.
    bool c_string(const char* text) { return bytes(text, std::strlen(text) + 1); }
    // A fixed-size text field: the text cut to `count` bytes, zero-padded.
    bool fixed_string(const char* text, size_t count)
    {
        if (!has(count)) {
            return fail();
        }
        const void* end = std::memchr(text, 0, count);
        const size_t length = end ? static_cast<size_t>(static_cast<const char*>(end) - text) : count;
        const std::span<uint8_t> field = rest().first(count);
        std::memcpy(field.data(), text, length);
        std::memset(field.subspan(length).data(), 0, count - length);
        position_ += count;
        return true;
    }

private:
    [[nodiscard]] bool has(size_t count) const { return ok_ && count <= capacity_ - position_; }
    bool fail()
    {
        ok_ = false;
        return false;
    }

    uint8_t* data_;
    size_t capacity_;
    size_t position_ = 0;
    bool ok_ = true;
};

} // namespace util
