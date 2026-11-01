#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

// NOT ORIGINAL - bounds-checked little-endian readers and writers for the game's file, save and network formats.
//
// The formats are little-endian and unaligned. Each format has a plain struct of its fields and `read_<x>` /
// `write_<x>` functions built on these, so that the game's own classes never have to mirror a byte layout.
//
// Reading past the end, or writing past the capacity, fails: the value read is 0 (nothing is written), the
// position stays where it was and ok() turns false for good. Callers check ok() once after a group of reads.
namespace util {

class ByteReader {
public:
    // A size not known (the game's older callers hand over a pointer only): reads are not bounded.
    static constexpr size_t kUnknownSize = ~static_cast<size_t>(0);

    ByteReader(const void* data, size_t size) : data_(static_cast<const uint8_t*>(data)), size_(size) {}

    bool ok() const { return ok_; }
    size_t position() const { return position_; }
    size_t size() const { return size_; }
    size_t remaining() const { return size_ - position_; }
    // The next byte to be read.
    const uint8_t* cursor() const { return data_ + position_; }

    // Move to `position` (fails beyond the end).
    bool seek(size_t position)
    {
        if (position > size_)
            return fail();
        position_ = position;
        return true;
    }
    bool skip(size_t count)
    {
        if (!has(count))
            return fail();
        position_ += count;
        return true;
    }

    uint8_t u8()
    {
        if (!has(1))
            return fail_value<uint8_t>();
        return data_[position_++];
    }
    uint16_t u16()
    {
        if (!has(2))
            return fail_value<uint16_t>();
        const uint8_t* p = data_ + position_;
        position_ += 2;
        return static_cast<uint16_t>(p[0] | p[1] << 8);
    }
    uint32_t u32()
    {
        if (!has(4))
            return fail_value<uint32_t>();
        const uint8_t* p = data_ + position_;
        position_ += 4;
        return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
    }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32()
    {
        uint32_t bits = u32();
        float value = std::bit_cast<float>(bits);
        return value;
    }

    // `count` raw bytes copied out.
    bool bytes(void* out, size_t count)
    {
        if (!has(count))
            return fail();
        std::memcpy(out, data_ + position_, count);
        position_ += count;
        return true;
    }
    // A zero-terminated string in place (the terminator read too); null if there is no terminator before the end.
    const char* c_string()
    {
        const auto* start = reinterpret_cast<const char*>(data_ + position_);
        size_t length = size_ == kUnknownSize ? std::strlen(start) : strnlen_in_bounds(start);
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
        if (!has(count) || out_size == 0)
            return fail();
        size_t copy = count < out_size - 1 ? count : out_size - 1;
        std::memcpy(out, data_ + position_, copy);
        out[copy] = '\0';
        out[strnlen(out, copy)] = '\0';
        position_ += count;
        return true;
    }

private:
    static constexpr size_t kNotFound = ~static_cast<size_t>(0);

    bool has(size_t count) const { return ok_ && count <= size_ - position_; }
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
    static size_t strnlen(const char* text, size_t max)
    {
        size_t length = 0;
        while (length < max && text[length])
            ++length;
        return length;
    }
    size_t strnlen_in_bounds(const char* start) const
    {
        size_t max = size_ - position_;
        for (size_t i = 0; i < max; ++i) {
            if (start[i] == '\0')
                return i;
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

    bool ok() const { return ok_; }
    size_t position() const { return position_; }
    size_t capacity() const { return capacity_; }
    size_t remaining() const { return capacity_ - position_; }
    uint8_t* cursor() const { return data_ + position_; }

    bool seek(size_t position)
    {
        if (position > capacity_)
            return fail();
        position_ = position;
        return true;
    }

    bool u8(uint8_t value)
    {
        if (!has(1))
            return fail();
        data_[position_++] = value;
        return true;
    }
    bool u16(uint16_t value)
    {
        if (!has(2))
            return fail();
        uint8_t* p = data_ + position_;
        p[0] = static_cast<uint8_t>(value);
        p[1] = static_cast<uint8_t>(value >> 8);
        position_ += 2;
        return true;
    }
    bool u32(uint32_t value)
    {
        if (!has(4))
            return fail();
        uint8_t* p = data_ + position_;
        p[0] = static_cast<uint8_t>(value);
        p[1] = static_cast<uint8_t>(value >> 8);
        p[2] = static_cast<uint8_t>(value >> 16);
        p[3] = static_cast<uint8_t>(value >> 24);
        position_ += 4;
        return true;
    }
    bool i8(int8_t value) { return u8(static_cast<uint8_t>(value)); }
    bool i16(int16_t value) { return u16(static_cast<uint16_t>(value)); }
    bool i32(int32_t value) { return u32(static_cast<uint32_t>(value)); }
    bool f32(float value)
    {
        uint32_t bits = std::bit_cast<uint32_t>(value);
        return u32(bits);
    }
    bool bytes(const void* data, size_t count)
    {
        if (!has(count))
            return fail();
        if (count)
            std::memcpy(data_ + position_, data, count);
        position_ += count;
        return true;
    }
    bool fill(uint8_t value, size_t count)
    {
        if (!has(count))
            return fail();
        std::memset(data_ + position_, value, count);
        position_ += count;
        return true;
    }
    // The text and its terminator.
    bool c_string(const char* text) { return bytes(text, std::strlen(text) + 1); }
    // A fixed-size text field: the text cut to `count` bytes, zero-padded.
    bool fixed_string(const char* text, size_t count)
    {
        if (!has(count))
            return fail();
        size_t length = 0;
        while (length < count && text[length])
            ++length;
        std::memcpy(data_ + position_, text, length);
        std::memset(data_ + position_ + length, 0, count - length);
        position_ += count;
        return true;
    }

private:
    bool has(size_t count) const { return ok_ && count <= capacity_ - position_; }
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
