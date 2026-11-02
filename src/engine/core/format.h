#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

// NOT ORIGINAL - printf-style text without C varargs: the format is checked against the arguments' types at compile time
// (a conversion that doesn't match its argument, or a missing / extra argument, doesn't build) and the text is made by
// our own code, byte for byte what the C runtime's snprintf makes (src/tests/printf_check.cpp compares the two over every
// conversion, flag, width and precision).
//
//   text::format_to(buffer, "%s: %d", name, count);   // snprintf's contract: cut to the buffer, the full length returned
//   std::string line = text::format("%08X", value);
//
// What a conversion takes: %d %i %u %o %x %X %c - an integer, bool or enum of at most the size its length modifier
// names (none / hh / h: int, l: 32 bits as on Windows, ll / I64 / j: 64 bits, z / t: size_t / ptrdiff_t), read as that
// type the way printf reads it (uint32_t 0xFFFFFFFF as %d is -1); %f %F %e %E %g %G - a float or double; %s - a char
// pointer (null prints "(null)"), %ls a wchar_t pointer; %p - an object pointer; '*' width / precision - an int.
namespace text {

// One argument, as the conversion reads it.
using FormatArg = std::variant<int64_t, uint64_t, double, const char*, const wchar_t*, const void*>;

// The text `format` makes with `args` into `out` (at most out.size() - 1 characters and a terminator - none when `out`
// is empty); the full length it has, whether or not it fitted. The format is not checked: a conversion without an
// argument, or with one of another kind, prints as for 0 / an empty text (the original read whatever the stack held).
// For formats that come from data; a literal goes through format_to.
size_t format_runtime_to(std::span<char> out, std::string_view format, std::span<const FormatArg> args);
// The same, as a string.
std::string format_runtime(std::string_view format, std::span<const FormatArg> args);

namespace format_detail {

// What an argument is, for the compile-time check.
enum class ArgKind : uint8_t { kSigned, kUnsigned, kFloat, kText, kWideText, kPointer, kNull, kOther };
struct ArgType {
    ArgKind kind = ArgKind::kOther;
    size_t size = 0;
};

template <typename T>
using Bare = std::remove_cv_t<std::remove_pointer_t<T>>;

template <typename T>
consteval ArgType classify()
{
    using Value = std::decay_t<T>;
    if constexpr (std::is_enum_v<Value>)
        return classify<std::underlying_type_t<Value>>();
    else if constexpr (std::is_same_v<Value, bool>)
        return {ArgKind::kSigned, sizeof(int)};
    else if constexpr (std::is_integral_v<Value>)
        return {std::is_signed_v<Value> ? ArgKind::kSigned : ArgKind::kUnsigned, sizeof(Value)};
    else if constexpr (std::is_floating_point_v<Value>)
        return {ArgKind::kFloat, sizeof(Value)};
    else if constexpr (std::is_null_pointer_v<Value>)
        return {ArgKind::kNull, sizeof(void*)};
    else if constexpr (std::is_pointer_v<Value> && std::is_same_v<Bare<Value>, char>)
        return {ArgKind::kText, sizeof(void*)};
    else if constexpr (std::is_pointer_v<Value> && std::is_same_v<Bare<Value>, wchar_t>)
        return {ArgKind::kWideText, sizeof(void*)};
    else if constexpr (std::is_pointer_v<Value> && !std::is_function_v<std::remove_pointer_t<Value>>)
        return {ArgKind::kPointer, sizeof(void*)};
    else
        return {};
}

// The argument as the formatting code reads it.
template <typename T>
FormatArg make_arg(const T& value)
{
    using Value = std::decay_t<T>;
    if constexpr (std::is_enum_v<Value>) {
        return make_arg(static_cast<std::underlying_type_t<Value>>(value));
    } else if constexpr (std::is_same_v<Value, bool>) {
        return int64_t{value ? 1 : 0};
    } else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) {
        return int64_t{value};
    } else if constexpr (std::is_integral_v<Value>) {
        return uint64_t{value};
    } else if constexpr (std::is_floating_point_v<Value>) {
        return static_cast<double>(value);
    } else if constexpr (std::is_null_pointer_v<Value>) {
        return static_cast<const void*>(nullptr);
    } else if constexpr (std::is_same_v<Bare<Value>, char>) {
        return static_cast<const char*>(value);
    } else if constexpr (std::is_same_v<Bare<Value>, wchar_t>) {
        return static_cast<const wchar_t*>(value);
    } else {
        return static_cast<const void*>(value);
    }
}

// The compile-time check's verdicts: a call that reaches one of these in a constant evaluation doesn't build, and the
// compiler names the function.
void format_has_too_few_arguments();
void format_has_too_many_arguments();
void format_conversion_is_not_supported();
void format_argument_does_not_match_its_conversion();
void format_width_or_precision_argument_is_not_an_int();
void format_ends_inside_a_conversion();

// The size an integer conversion's length modifier names (0: none given - int).
constexpr size_t kIntSize = 4;
constexpr size_t kLongSize = 4; // (32 bits, as on Windows - the game's printf)
constexpr size_t kLongLongSize = 8;

constexpr bool is_integer(ArgKind kind) { return kind == ArgKind::kSigned || kind == ArgKind::kUnsigned; }

// Every conversion of `format` against the arguments' types.
template <size_t Count>
consteval void check(std::string_view format, const std::array<ArgType, Count>& types)
{
    size_t next = 0;
    auto take = [&]() -> ArgType {
        if (next >= types.size()) {
            format_has_too_few_arguments();
            return {};
        }
        return types.at(next++);
    };
    size_t i = 0;
    auto peek = [&](size_t at) { return at < format.size() ? format.at(at) : '\0'; };
    while (i < format.size()) {
        if (format.at(i) != '%') {
            ++i;
            continue;
        }
        ++i;
        if (peek(i) == '%') {
            ++i;
            continue;
        }
        while (peek(i) == '-' || peek(i) == '+' || peek(i) == ' ' || peek(i) == '#' || peek(i) == '0')
            ++i;
        if (peek(i) == '*') {
            const ArgType width = take();
            if (!is_integer(width.kind) || width.size > kIntSize)
                format_width_or_precision_argument_is_not_an_int();
            ++i;
        } else {
            while (peek(i) >= '0' && peek(i) <= '9')
                ++i;
        }
        if (peek(i) == '.') {
            ++i;
            if (peek(i) == '*') {
                const ArgType precision = take();
                if (!is_integer(precision.kind) || precision.size > kIntSize)
                    format_width_or_precision_argument_is_not_an_int();
                ++i;
            } else {
                while (peek(i) >= '0' && peek(i) <= '9')
                    ++i;
            }
        }
        // the length modifier: the integer size it names (or the wide text / long double it marks)
        size_t size = kIntSize;
        bool wide = false;
        if (peek(i) == 'h') {
            ++i;
            if (peek(i) == 'h')
                ++i;
        } else if (peek(i) == 'l') {
            ++i;
            size = kLongSize;
            wide = true;
            if (peek(i) == 'l') {
                ++i;
                size = kLongLongSize;
                wide = false;
            }
        } else if (peek(i) == 'I' && peek(i + 1) == '6' && peek(i + 2) == '4') {
            i += 3;
            size = kLongLongSize;
        } else if (peek(i) == 'j') {
            ++i;
            size = kLongLongSize;
        } else if (peek(i) == 'z') {
            ++i;
            size = sizeof(size_t);
        } else if (peek(i) == 't') {
            ++i;
            size = sizeof(std::ptrdiff_t);
        } else if (peek(i) == 'L') {
            ++i;
        }
        if (i >= format.size()) {
            format_ends_inside_a_conversion();
            return;
        }
        const char conversion = format.at(i++);
        switch (conversion) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'c': {
            const ArgType arg = take();
            if (!is_integer(arg.kind) || arg.size > size)
                format_argument_does_not_match_its_conversion();
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            if (take().kind != ArgKind::kFloat)
                format_argument_does_not_match_its_conversion();
            break;
        case 's': {
            const ArgKind kind = take().kind;
            if (kind != (wide ? ArgKind::kWideText : ArgKind::kText) && kind != ArgKind::kNull)
                format_argument_does_not_match_its_conversion();
            break;
        }
        case 'p': {
            const ArgKind kind = take().kind;
            if (kind != ArgKind::kPointer && kind != ArgKind::kText && kind != ArgKind::kWideText &&
                kind != ArgKind::kNull)
                format_argument_does_not_match_its_conversion();
            break;
        }
        default:
            format_conversion_is_not_supported();
            break;
        }
    }
    if (next != types.size())
        format_has_too_many_arguments();
}

} // namespace format_detail

// A format checked against its arguments' types when it's made (only from a constant: a literal or a constexpr text).
template <typename... Args>
class FormatString {
public:
    // (a literal)
    consteval FormatString(const char* text) : FormatString(std::string_view(text)) {}
    // (a constexpr text)
    consteval FormatString(std::string_view text) : text_(text)
    {
        constexpr std::array<format_detail::ArgType, sizeof...(Args)> kTypes{format_detail::classify<Args>()...};
        format_detail::check(text_, kTypes);
    }
    [[nodiscard]] constexpr std::string_view view() const { return text_; }

private:
    std::string_view text_;
};

// The format of a call whose arguments are `Args` (the arguments decide the types; the format only checks them).
template <typename... Args>
using FormatFor = FormatString<std::type_identity_t<Args>...>;

// snprintf: the text into `out` (cut to fit, always terminated unless `out` is empty); its full length.
template <typename... Args>
size_t format_to(std::span<char> out, FormatFor<Args...> format, Args... args)
{
    const std::array<FormatArg, sizeof...(Args)> packed{format_detail::make_arg(args)...};
    return format_runtime_to(out, format.view(), packed);
}

// The text as a string.
template <typename... Args>
std::string format(FormatFor<Args...> format, Args... args)
{
    const std::array<FormatArg, sizeof...(Args)> packed{format_detail::make_arg(args)...};
    return format_runtime(format.view(), packed);
}

// The text added to the end of `out`.
template <typename... Args>
void append_format(std::string& out, FormatFor<Args...> format, Args... args)
{
    const std::array<FormatArg, sizeof...(Args)> packed{format_detail::make_arg(args)...};
    out += format_runtime(format.view(), packed);
}

// A format that comes from data (a script's text, a text the original used as its own format): not checked when
// compiled - format_runtime_to's rules (a conversion with no argument, or one of another kind, prints as for 0 / "").
template <typename... Args>
size_t format_data_to(std::span<char> out, std::string_view format, Args... args)
{
    const std::array<FormatArg, sizeof...(Args)> packed{format_detail::make_arg(args)...};
    return format_runtime_to(out, format, packed);
}
template <typename... Args>
std::string format_data(std::string_view format, Args... args)
{
    const std::array<FormatArg, sizeof...(Args)> packed{format_detail::make_arg(args)...};
    return format_runtime(format, packed);
}

} // namespace text
