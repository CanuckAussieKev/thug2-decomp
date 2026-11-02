#pragma once

#include <cstddef>
#include <span>
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

} // namespace core
