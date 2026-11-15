// NOT ORIGINAL - a texture set's record (texture_set_record.h).
#include "formats/texture_set_record.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace texture_set_record {

namespace {

constexpr int kDeepest = 64;              // (nesting a record never reaches: deeper is damage)
constexpr int kLargestVersion = 1000000;  // (a version number's ceiling)
constexpr std::size_t kChecksumDigits = 8;
constexpr uint32_t kHexDigitBits = 4;
constexpr int kDecimal = 10;
constexpr int kHexLetter = 10; // ('a' / 'A' is 10)
constexpr uint32_t kUnicodeDigits = 4;
constexpr uint32_t kAsciiEnd = 0x80;

// A hex digit's value (-1: not one).
int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + kHexLetter;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + kHexLetter;
    }
    return -1;
}

// A checksum written as hex ("0a1b2c3d"; 1 - 8 digits). False: not one.
bool checksum_of(std::string_view text, uint32_t* checksum)
{
    if (text.empty() || text.size() > kChecksumDigits) {
        return false;
    }
    uint32_t value = 0;
    for (const char ch : text) {
        const int digit = hex_digit(ch);
        if (digit < 0) {
            return false;
        }
        value = (value << kHexDigitBits) | static_cast<uint32_t>(digit);
    }
    *checksum = value;
    return true;
}

// A name as the record and the game compare it: lower case, '/' as '\'.
std::string folded(std::string_view name)
{
    std::string out(name);
    std::ranges::transform(out, out.begin(), [](char ch) {
        if (ch == '/') {
            return '\\';
        }
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    });
    return out;
}

// JSON read just far enough for the record: its values checked for form, the members the game reads kept.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool record(Record* out)
    {
        const bool whole = object([&](const std::string& key) {
            if (key == "delight") {
                long long version = 0;
                if (!number(&version)) {
                    return false;
                }
                out->delight = static_cast<int>(std::clamp<long long>(version, 0, kLargestVersion));
                return true;
            }
            if (key == "files") {
                return array([&] {
                    File file;
                    if (!file_entry(&file)) {
                        return false;
                    }
                    std::ranges::sort(file.delit);
                    out->files.push_back(std::move(file));
                    return true;
                });
            }
            return skip_value();
        });
        space();
        return whole && at_ == text_.size();
    }

private:
    [[nodiscard]] char peek() const { return at_ < text_.size() ? text_.at(at_) : '\0'; }

    void space()
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') {
            ++at_;
        }
    }

    bool take(char ch)
    {
        space();
        if (at_ < text_.size() && peek() == ch) {
            ++at_;
            return true;
        }
        return false;
    }

    // A string's text (escapes resolved; a \u escape past ASCII kept as '?' - names and checksums are ASCII).
    bool string(std::string* out)
    {
        if (!take('"')) {
            return false;
        }
        out->clear();
        while (at_ < text_.size()) {
            const char ch = text_.at(at_++);
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                out->push_back(ch);
                continue;
            }
            if (at_ >= text_.size()) {
                return false;
            }
            const char escape = text_.at(at_++);
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out->push_back(escape);
                break;
            case 'b':
                out->push_back('\b');
                break;
            case 'f':
                out->push_back('\f');
                break;
            case 'n':
                out->push_back('\n');
                break;
            case 'r':
                out->push_back('\r');
                break;
            case 't':
                out->push_back('\t');
                break;
            case 'u': {
                uint32_t code = 0;
                for (uint32_t digit = 0; digit < kUnicodeDigits; ++digit) {
                    const int value = hex_digit(peek());
                    if (value < 0) {
                        return false;
                    }
                    code = (code << kHexDigitBits) | static_cast<uint32_t>(value);
                    ++at_;
                }
                out->push_back(code < kAsciiEnd ? static_cast<char>(code) : '?');
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    // A number (JSON's form); its whole part in `whole` (clamped far past any version).
    bool number(long long* whole)
    {
        space();
        const bool negative = peek() == '-';
        if (negative) {
            ++at_;
        }
        if (peek() < '0' || peek() > '9') {
            return false;
        }
        long long value = 0;
        while (peek() >= '0' && peek() <= '9') {
            value = std::min<long long>((value * kDecimal) + (peek() - '0'), kLargestVersion);
            ++at_;
        }
        if (peek() == '.') {
            ++at_;
            if (peek() < '0' || peek() > '9') {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++at_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++at_;
            if (peek() == '+' || peek() == '-') {
                ++at_;
            }
            if (peek() < '0' || peek() > '9') {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++at_;
            }
        }
        *whole = negative ? -value : value;
        return true;
    }

    bool word(std::string_view expected)
    {
        space();
        if (text_.substr(at_).starts_with(expected)) {
            at_ += expected.size();
            return true;
        }
        return false;
    }

    // An object: `member(key)` reads each member's value.
    template <class Member>
    bool object(const Member& member)
    {
        if (!take('{')) {
            return false;
        }
        if (take('}')) {
            return true;
        }
        std::string key;
        for (bool more = true; more; more = take(',')) {
            if (!string(&key) || !take(':') || !member(key)) {
                return false;
            }
        }
        return take('}');
    }

    // An array: `element()` reads each element.
    template <class Element>
    bool array(const Element& element)
    {
        if (!take('[')) {
            return false;
        }
        if (take(']')) {
            return true;
        }
        for (bool more = true; more; more = take(',')) {
            if (!element()) {
                return false;
            }
        }
        return take(']');
    }

    bool skip_value()
    {
        if (++depth_ > kDeepest) {
            return false;
        }
        space();
        bool fine = false;
        std::string ignored;
        long long number_ignored = 0;
        switch (peek()) {
        case '{':
            fine = object([&](const std::string& /*key*/) { return skip_value(); });
            break;
        case '[':
            fine = array([&] { return skip_value(); });
            break;
        case '"':
            fine = string(&ignored);
            break;
        case 't':
            fine = word("true");
            break;
        case 'f':
            fine = word("false");
            break;
        case 'n':
            fine = word("null");
            break;
        default:
            fine = number(&number_ignored);
            break;
        }
        --depth_;
        return fine;
    }

    // One of "files"' entries: its name and its de-lit textures (a checksum not written as hex is left out).
    bool file_entry(File* file)
    {
        return object([&](const std::string& key) {
            if (key == "name") {
                std::string name;
                if (!string(&name)) {
                    return false;
                }
                file->name = folded(name);
                return true;
            }
            if (key == "delit") {
                return array([&] {
                    std::string hex;
                    if (!string(&hex)) {
                        return false;
                    }
                    uint32_t checksum = 0;
                    if (checksum_of(hex, &checksum)) {
                        file->delit.push_back(checksum);
                    }
                    return true;
                });
            }
            return skip_value();
        });
    }

    std::string_view text_;
    std::size_t at_ = 0;
    int depth_ = 0;
};

} // namespace

bool read(std::string_view json, Record* record)
{
    Record read_record;
    Parser parser(json);
    if (!parser.record(&read_record)) {
        *record = {};
        return false;
    }
    *record = std::move(read_record);
    return true;
}

std::vector<uint32_t> delit_of(const Record& record, std::string_view name)
{
    const std::string wanted = folded(name);
    const auto found = std::ranges::find(record.files, wanted, &File::name);
    return found != record.files.end() ? found->delit : std::vector<uint32_t>{};
}

} // namespace texture_set_record
