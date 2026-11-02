#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <gsl/assert>
#include <gsl/util>

// NOT ORIGINAL - a position in a run of elements (a span and an index into it) standing where the original walked a
// pointer: `*cursor`, `cursor[k]`, `++cursor`, `cursor += n` and `cursor + n` mean what they meant on the pointer, each
// element read or written is checked against the run's extent (core::at's check: an access outside the run ends the
// game with a report), and the position may be one past the last element, as a pointer may. Hot loops keep their shape
// (contiguous elements, no copies): the cursor is the span, the index and nothing else.
namespace core {

template <typename T>
class Cursor {
public:
    Cursor() = default;
    explicit Cursor(std::span<T> run, std::ptrdiff_t position = 0) : run_(run), position_(position) {}
    // A cursor over mutable elements stands for one over const elements.
    template <typename U>
        requires(std::is_same_v<std::remove_const_t<T>, U> && std::is_const_v<T>)
    Cursor(const Cursor<U>& other) : run_(other.run()), position_(other.position())
    {
    }

    [[nodiscard]] T& operator*() const { return gsl::at(run_, position_); }
    [[nodiscard]] T* operator->() const { return &gsl::at(run_, position_); }
    [[nodiscard]] T& operator[](std::ptrdiff_t offset) const { return gsl::at(run_, position_ + offset); }

    Cursor& operator++()
    {
        ++position_;
        return *this;
    }
    Cursor operator++(int)
    {
        Cursor before = *this;
        ++position_;
        return before;
    }
    Cursor& operator--()
    {
        --position_;
        return *this;
    }
    Cursor& operator+=(std::ptrdiff_t count)
    {
        position_ += count;
        return *this;
    }
    Cursor& operator-=(std::ptrdiff_t count)
    {
        position_ -= count;
        return *this;
    }
    [[nodiscard]] Cursor operator+(std::ptrdiff_t count) const { return Cursor(run_, position_ + count); }
    [[nodiscard]] Cursor operator-(std::ptrdiff_t count) const { return Cursor(run_, position_ - count); }
    // Unsigned counts and offsets (sizes, lengths read from data) step the same way.
    template <std::unsigned_integral U>
    [[nodiscard]] T& operator[](U offset) const
    {
        return (*this)[static_cast<std::ptrdiff_t>(offset)];
    }
    template <std::unsigned_integral U>
    Cursor& operator+=(U count)
    {
        return *this += static_cast<std::ptrdiff_t>(count);
    }
    template <std::unsigned_integral U>
    Cursor& operator-=(U count)
    {
        return *this -= static_cast<std::ptrdiff_t>(count);
    }
    template <std::unsigned_integral U>
    [[nodiscard]] Cursor operator+(U count) const
    {
        return *this + static_cast<std::ptrdiff_t>(count);
    }
    template <std::unsigned_integral U>
    [[nodiscard]] Cursor operator-(U count) const
    {
        return *this - static_cast<std::ptrdiff_t>(count);
    }
    [[nodiscard]] std::ptrdiff_t operator-(const Cursor& other) const { return position_ - other.position_; }
    [[nodiscard]] bool operator==(const Cursor& other) const { return position_ == other.position_; }
    [[nodiscard]] auto operator<=>(const Cursor& other) const { return position_ <=> other.position_; }

    [[nodiscard]] std::span<T> run() const { return run_; }
    [[nodiscard]] std::ptrdiff_t position() const { return position_; }
    // The elements from the position to the run's end.
    [[nodiscard]] std::span<T> rest() const { return run_.subspan(static_cast<std::size_t>(position_)); }
    [[nodiscard]] std::size_t remaining() const { return run_.size() - static_cast<std::size_t>(position_); }
    // The element's address at the position (the run's end at its end) - for what keeps a plain pointer.
    [[nodiscard]] T* address() const { return rest().data(); }
    // The cursor at `place` in the same run (checked to be in it, its end included).
    [[nodiscard]] Cursor moved_to(const T* place) const
    {
        const std::ptrdiff_t position = place - run_.data();
        Expects(position >= 0 && static_cast<std::size_t>(position) <= run_.size());
        return Cursor(run_, position);
    }

private:
    std::span<T> run_;
    std::ptrdiff_t position_ = 0;
};

// NOT ORIGINAL - a position in a NUL-terminated text (the run is the text and its terminator): what the string walks of
// the text drawing and the front end do with a `const char*`. `*cursor` is the character there, the terminator's 0 at the
// end and also past it (the original read whatever followed the terminator when an escape's skip stepped over it; the
// walks stop at the terminator instead), `++cursor` and `cursor += n` step on.
class CStringCursor {
public:
    CStringCursor() = default;
    explicit CStringCursor(const char* text) : run_(text, std::char_traits<char>::length(text) + 1) {}

    [[nodiscard]] char operator*() const
    {
        return position_ < run_.size() ? gsl::at(run_, static_cast<std::ptrdiff_t>(position_)) : '\0';
    }
    // The character `offset` on (0 past the terminator).
    [[nodiscard]] char operator[](std::size_t offset) const
    {
        return position_ + offset < run_.size() ? gsl::at(run_, static_cast<std::ptrdiff_t>(position_ + offset)) : '\0';
    }
    CStringCursor& operator++()
    {
        ++position_;
        return *this;
    }
    CStringCursor operator++(int)
    {
        CStringCursor before = *this;
        ++position_;
        return before;
    }
    CStringCursor& operator+=(std::size_t count)
    {
        position_ += count;
        return *this;
    }
    // The text from the position (the terminator alone at and past the end).
    [[nodiscard]] std::string_view rest() const
    {
        if (position_ + 1 >= run_.size())
            return {};
        const std::span<const char> text = run_.subspan(position_, run_.size() - 1 - position_);
        return {text.data(), text.size()};
    }
    [[nodiscard]] std::size_t position() const { return position_; }

private:
    std::span<const char> run_;
    std::size_t position_ = 0;
};

// The elements of a null-terminated array of pointers (a list that ends at the first null), as a run: the terminator is
// left out. The array has no extent of its own, so the run's search is bounded by the address space only.
template <typename T>
[[nodiscard]] std::span<T* const> null_terminated(T* const* list)
{
    const std::span<T* const> all(list, static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T*));
    std::size_t count = 0;
    while (gsl::at(all, static_cast<std::ptrdiff_t>(count)))
        ++count;
    return all.first(count);
}

} // namespace core
