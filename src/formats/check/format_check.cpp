// NOT ORIGINAL - offline checks of the file, save and network format readers / writers (thug2_format_check.exe).
//
//   thug2_format_check [<save file or folder> | <Data folder>...]
//
// A Data folder (one with a pre\ folder) has its archives and data files checked (format_check_files.cpp).
// With no arguments only the self-contained checks run (the byte reader / writer, the wire messages' encode of a
// decode, added with them). Each save file (.SKA, .NWS, .PRK, .CAS, .CAT, .GOA, .GRF) is checked: its header read, both CRCs, and
// its summary and contents decoded into a plain tree and encoded again - byte for byte what the game wrote, by the
// same rules as the game's structure writer (engine/script/struct_buffer.cpp). Exit code 0 when everything passes.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <iterator>
#include <span>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "wire/gamenet_messages.h"
#include "wire/net_wire.h"
#include "formats/byte_stream.h"
#include "formats/park_map_format.h"
#include "formats/save_file_format.h"
#include "wire/skate_net_messages.h"
#include "formats/check/format_check_files.h"
#include "formats/check/format_check_net.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace {

int& failures()
{
    static int failures{0};
    return failures;
}

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++failures();
        text::print("  FAIL %s\n", what.c_str());
    }
}

// --- the script structure buffer as a plain tree -----------------------------------------------------------------

// The buffer's types (component types, then the buffer's own integer / float forms).
constexpr uint8_t kInteger = 1, kFloat = 2, kString = 3, kLocalString = 4, kPair = 5, kVector = 6, kStruct = 0x0A,
                  kArray = 0x0C, kName = 0x0D;
constexpr uint8_t kSignedByte = 0x0E, kSignedShort = 0x0F, kUnsignedByte = 0x10, kUnsignedShort = 0x11,
                  kZeroInteger = 0x12, kZeroFloat = 0x13;

// Test fixtures of the structure buffer's reference reader / writer.
constexpr uint8_t kShortNameForm = 0x80;  // the name is an index into the short names
constexpr uint8_t kMediumNameForm = 0x40; // the name is an index into the medium names
// The ranges that choose an integer's smallest form (a negative form's lowest value is minus its magnitude).
constexpr int kUnsignedByteMax = 0xFF;
constexpr int kUnsignedShortMax = 0xFFFF;
constexpr int kSignedByteMagnitude = 0x80;
constexpr int kSignedByteMax = 0x7F;
constexpr int kSignedShortMagnitude = 0x8000;
constexpr int kSignedShortMax = 0x7FFF;
constexpr double kTinyFloatLimit = 0.0001; // a float of smaller magnitude is written as kZeroFloat
constexpr std::size_t kReencodeSlackBytes = 16; // room past the original's size for an encode that comes out longer
struct Name {
    uint8_t form = 0; // 0x80: index into the short names, 0x40: into the medium names, 0: the checksum
    uint32_t value = 0;
};

struct Value;
struct Structure {
    std::vector<std::pair<Name, std::unique_ptr<Value>>> components;
};
struct ArrayValue {
    uint8_t type = 0;
    std::vector<std::unique_ptr<Value>> elements;
};
struct Value {
    uint8_t type = 0; // the component type (the integer / float forms folded into kInteger / kFloat)
    uint32_t bits = 0;
    std::array<float, 3> floats = {};
    std::string text;
    Structure structure;
    ArrayValue array;
};

Name read_name(util::ByteReader& reader, uint8_t type)
{
    if (type & kShortNameForm)
        return {.form = kShortNameForm, .value = reader.u8()};
    if (type & kMediumNameForm)
        return {.form = kMediumNameForm, .value = reader.u16()};
    return {.form = 0, .value = reader.u32()};
}

bool read_structure(util::ByteReader& reader, Structure* out);
bool read_array(util::ByteReader& reader, ArrayValue* out);

std::unique_ptr<Value> read_element(util::ByteReader& reader, uint8_t type)
{
    auto value = std::make_unique<Value>();
    value->type = type;
    switch (type) {
    case kInteger:
    case kName:
    case kFloat:
        value->bits = reader.u32();
        break;
    case kString:
    case kLocalString: {
        const char* text = reader.c_string();
        value->text = text ? text : "";
        break;
    }
    case kPair:
        core::at(value->floats, 0) = reader.f32();
        core::at(value->floats, 1) = reader.f32();
        break;
    case kVector:
        for (float& f : value->floats)
            f = reader.f32();
        break;
    case kStruct:
        read_structure(reader, &value->structure);
        break;
    case kArray:
        read_array(reader, &value->array);
        break;
    default:
        break;
    }
    return value;
}

bool read_structure(util::ByteReader& reader, Structure* out)
{
    for (uint8_t type = reader.u8(); type != 0 && reader.ok(); type = reader.u8()) {
        const Name name = read_name(reader, type);
        auto value = std::make_unique<Value>();
        const uint8_t kind = type & 0x3F;
        switch (kind) {
        case kSignedByte:
            value->type = kInteger;
            value->bits = static_cast<uint32_t>(int{reader.i8()});
            break;
        case kSignedShort:
            value->type = kInteger;
            value->bits = static_cast<uint32_t>(reader.i16());
            break;
        case kUnsignedByte:
            value->type = kInteger;
            value->bits = reader.u8();
            break;
        case kUnsignedShort:
            value->type = kInteger;
            value->bits = reader.u16();
            break;
        case kZeroInteger:
            value->type = kInteger;
            break;
        case kZeroFloat:
            value->type = kFloat;
            break;
        default:
            value = read_element(reader, kind);
            break;
        }
        out->components.emplace_back(name, std::move(value));
    }
    return reader.ok();
}

bool read_array(util::ByteReader& reader, ArrayValue* out)
{
    out->type = reader.u8();
    const uint32_t count = reader.u16();
    for (uint32_t i = 0; i < count && reader.ok(); ++i)
        out->elements.push_back(read_element(reader, out->type));
    return reader.ok();
}

void write_name(util::ByteWriter& writer, uint8_t type, const Name& name)
{
    writer.u8(static_cast<uint8_t>(type | name.form));
    if (name.form == kShortNameForm)
        writer.u8(static_cast<uint8_t>(name.value));
    else if (name.form == kMediumNameForm)
        writer.u16(static_cast<uint16_t>(name.value));
    else
        writer.u32(name.value);
}

void write_structure(util::ByteWriter& writer, const Structure& structure);
void write_array(util::ByteWriter& writer, const ArrayValue& array);

void write_element(util::ByteWriter& writer, const Value& value)
{
    switch (value.type) {
    case kInteger:
    case kName:
    case kFloat:
        writer.u32(value.bits);
        break;
    case kString:
    case kLocalString:
        writer.c_string(value.text.c_str());
        break;
    case kPair:
        writer.f32(core::at(value.floats, 0));
        writer.f32(core::at(value.floats, 1));
        break;
    case kVector:
        for (const float f : value.floats)
            writer.f32(f);
        break;
    case kStruct:
        write_structure(writer, value.structure);
        break;
    case kArray:
        write_array(writer, value.array);
        break;
    default:
        break;
    }
}

// The game's writer's rules: an integer in its smallest form, a tiny float as kZeroFloat.
void write_structure(util::ByteWriter& writer, const Structure& structure)
{
    for (const auto& [name, value] : structure.components) {
        if (value->type == kInteger) {
            auto v = static_cast<int32_t>(value->bits);
            uint8_t type = kInteger;
            if (v == 0)
                type = kZeroInteger;
            else if (v > 0 && v <= kUnsignedByteMax)
                type = kUnsignedByte;
            else if (v > 0 && v <= kUnsignedShortMax)
                type = kUnsignedShort;
            else if (v >= -kSignedByteMagnitude && v <= kSignedByteMax)
                type = kSignedByte;
            else if (v >= -kSignedShortMagnitude && v <= kSignedShortMax)
                type = kSignedShort;
            write_name(writer, type, name);
            if (type == kInteger)
                writer.i32(v);
            else if (type == kSignedByte || type == kUnsignedByte)
                writer.u8(static_cast<uint8_t>(v));
            else if (type == kSignedShort || type == kUnsignedShort)
                writer.u16(static_cast<uint16_t>(v));
            continue;
        }
        if (value->type == kFloat) {
            float f = 0.0f;
            std::memcpy(&f, &value->bits, sizeof(f));
            const float magnitude = f < 0.0f ? -f : f;
            if (magnitude < kTinyFloatLimit) {
                write_name(writer, kZeroFloat, name);
                continue;
            }
        }
        write_name(writer, value->type, name);
        write_element(writer, *value);
    }
    writer.u8(0);
}

void write_array(util::ByteWriter& writer, const ArrayValue& array)
{
    writer.u8(array.type);
    writer.u16(static_cast<uint16_t>(array.elements.size()));
    for (const auto& element : array.elements)
        write_element(writer, *element);
}

// A structure decoded from `bytes` and encoded again: byte for byte the same, and its size returned (0 on failure).
size_t check_structure_round_trip(const uint8_t* bytes, size_t size, const std::string& what,
                                  Structure* decoded = nullptr)
{
    util::ByteReader reader(bytes, size);
    Structure local;
    Structure& tree = decoded ? *decoded : local;
    const bool read = read_structure(reader, &tree);
    check(read, what + ": decoded");
    if (!read)
        return 0;
    const size_t used = reader.position();
    std::vector<uint8_t> again(used + kReencodeSlackBytes);
    util::ByteWriter writer(again.data(), again.size());
    write_structure(writer, tree);
    check(writer.ok() && writer.position() == used && std::memcmp(again.data(), bytes, used) == 0,
          what + ": encoded again byte for byte (" + std::to_string(used) + " bytes)");
    return used;
}

// --- park maps -------------------------------------------------------------------------------------------------------

constexpr uint32_t kParkEditorMap = 0x337C5289; // "Park_editor_map"

void find_park_maps(const Structure& structure, std::vector<const ArrayValue*>* found)
{
    for (const auto& [name, value] : structure.components) {
        if (name.form == 0 && name.value == kParkEditorMap && value->type == kArray)
            found->push_back(&value->array);
        else if (value->type == kStruct)
            find_park_maps(value->structure, found);
    }
}

// The park map checks: a fill byte that no conversion writes, and a word count shorter than the map's.
constexpr int kUnwrittenFill = 0xEE;
constexpr int kShortWordCount = 10;
constexpr int kShortMapBytes = 40; // kShortWordCount words
// A park map (an integer array of its words): its checksum, and the header and records decoded and encoded again -
// byte for byte, zeros after them.
void check_park_map(const ArrayValue& array, const std::string& what)
{
    using namespace park_editing;
    text::print("  park map in %s\n", what.c_str());
    check(array.type == kInteger && array.elements.size() * 4 == kParkMapBytes, what + ": park map size");
    if (array.elements.size() * 4 != kParkMapBytes)
        return;
    std::vector<uint8_t> map(kParkMapBytes);
    util::ByteWriter words(map.data(), map.size());
    for (const auto& element : array.elements)
        words.u32(element->bits);
    // the game's conversions between the map and its words
    std::vector<uint32_t> word_values(kParkMapWords);
    for (uint32_t i = 0; i < kParkMapWords; ++i)
        core::at(word_values, i) = core::at(array.elements, i)->bits;
    std::vector<uint8_t> from_words(kParkMapBytes, kUnwrittenFill);
    park_map_from_words(std::span<uint8_t, kParkMapBytes>(from_words), word_values);
    std::vector<uint32_t> back_to_words(kParkMapWords);
    park_map_to_words(std::span<const uint8_t, kParkMapBytes>(from_words), back_to_words);
    check(from_words == map && back_to_words == word_values, what + ": park map to / from its words");
    std::vector<uint8_t> short_map(kParkMapBytes, kUnwrittenFill);
    park_map_from_words(std::span<uint8_t, kParkMapBytes>(short_map), std::span<const uint32_t>(word_values).first(kShortWordCount));
    check(std::memcmp(short_map.data(), map.data(), kShortMapBytes) == 0 && core::at(short_map, kShortMapBytes) == 0 && short_map.back() == 0,
          what + ": park map from too few words: the rest zeroed");
    util::ByteReader reader(map.data(), map.size());
    ParkMapHeader header;
    check(read_park_map_header(reader, &header), what + ": park map header");
    check(park_map_checksum(map) == header.checksum, what + ": park map CRC");
    std::vector<uint8_t> again(kParkMapBytes);
    util::ByteWriter writer(again.data(), again.size());
    write_park_map_header(writer, header);
    check(std::memcmp(again.data(), map.data(), kParkMapHeaderSize) == 0, what + ": park map header encoded again");
    std::memcpy(std::span(again).subspan(kParkMapHeaderSize).data(), std::span(map).subspan(kParkMapHeaderSize).data(),
                kParkMapFloorCells);
    reader.seek(kParkMapRecordsStart);
    writer.seek(kParkMapRecordsStart);
    const bool current = header.version == kParkMapVersion;
    for (int i = 0; std::cmp_less(i, header.piece_count); ++i) {
        ParkMapPiece piece;
        read_park_map_piece(reader, &piece);
        write_park_map_piece(writer, piece);
    }
    for (int i = 0; std::cmp_less(i, header.gap_count); ++i) {
        ParkMapGap gap;
        read_park_map_gap(reader, &gap, current);
        if (current) {
            write_park_map_gap(writer, gap);
        } else {
            // (no writer for the old layout: its bytes copied)
            writer.bytes(std::span(map).subspan(writer.position()).data(), kOldParkMapGapSize);
        }
    }
    check(reader.ok() && writer.ok() && reader.position() == header.extent && writer.position() == header.extent,
          what + ": park map records end at its size (" + std::to_string(header.piece_count) + " pieces, " +
              std::to_string(header.gap_count) + " gaps)");
    check(std::memcmp(again.data(), map.data(), kParkMapBytes) == 0, what + ": park map encoded again byte for byte");
}

void check_park_maps_in(const Structure& structure, const std::string& what)
{
    std::vector<const ArrayValue*> maps;
    find_park_maps(structure, &maps);
    for (const ArrayValue* map : maps)
        check_park_map(*map, what);
}

// A bare structure file (Data/CustomParks/custom<n>.prk): decoded and encoded again, its park map checked.
void check_structure_file(const std::filesystem::path& path, const std::vector<uint8_t>& file)
{
    text::print("structure file %s\n", path.string().c_str());
    const std::string name = path.filename().string();
    Structure tree;
    const size_t used = check_structure_round_trip(file.data(), file.size(), name, &tree);
    check(used == file.size(), name + ": the structure is the whole file");
    check_park_maps_in(tree, name);
}

// --- save files ----------------------------------------------------------------------------------------------------

bool is_save_file(const std::filesystem::path& path)
{
    static const auto kExtensions = std::to_array<const char*>({".SKA", ".NWS", ".PRK", ".CAS", ".CAT", ".GOA", ".GRF"});
    std::string extension = path.extension().string();
    for (char& c : extension)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const char* known : kExtensions) {
        if (extension == known)
            return true;
    }
    return false;
}

void check_save_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string name = path.filename().string();

    util::ByteReader reader(file.data(), file.size());
    save_cards::SaveFileHeader header;
    const bool save = save_cards::read_save_file_header(reader, &header) &&
                      header.size >= ints::as_signed(save_cards::kSaveFileHeaderSize) &&
                      std::cmp_less_equal(header.size, file.size()) && header.summary_size >= 0 &&
                      header.summary_size <= save_cards::kMaxSummarySize;
    if (!save) { // (not a save: a bare structure, as the custom parks)
        check_structure_file(path, file);
        return;
    }
    text::print("save %s\n", path.string().c_str());
    auto size = static_cast<size_t>(header.size);
    check(save_cards::save_file_checksum(file.data(), size) == header.checksum, name + ": file CRC");
    check(header.summary_size <= save_cards::kMaxSummarySize, name + ": summary size");
    const uint8_t* summary = std::span(file).subspan(save_cards::kSaveFileHeaderSize).data();
    check(checksums::generate_crc_from_buffer(summary, header.summary_size) == header.summary_crc,
          name + ": summary CRC");

    // the header written again
    std::array<uint8_t, save_cards::kSaveFileHeaderSize> again{};
    util::ByteWriter writer(again.data(), sizeof(again));
    check(save_cards::write_save_file_header(writer, header) && std::memcmp(again.data(), file.data(), sizeof(again)) == 0,
          name + ": header encoded again");

    const size_t summary_used =
        check_structure_round_trip(summary, static_cast<size_t>(header.summary_size), name + " summary");
    check(summary_used == ints::as_unsigned(header.summary_size), name + ": summary ends at its size");
    std::vector<uint8_t> contents(file.begin() + static_cast<std::ptrdiff_t>(save_cards::kSaveFileHeaderSize) +
                                      header.summary_size,
                                  file.begin() + static_cast<std::ptrdiff_t>(size));
    if (path.extension() == ".NWS" || path.extension() == ".nws") {
        for (uint8_t& b : contents)
            b = static_cast<uint8_t>(b - save_cards::kSaveFileMask);
    }
    Structure tree;
    const size_t contents_used =
        check_structure_round_trip(contents.data(), contents.size(), name + " contents", &tree);
    check_park_maps_in(tree, name);
    check(contents_used == contents.size(), name + ": contents end at the header's size");
    for (size_t i = size; i < file.size(); ++i) {
        if (core::at(file, i) != save_cards::kSaveFileMask) {
            check(false, name + ": padding");
            break;
        }
    }
}

// --- the byte reader / writer ---------------------------------------------------------------------------------------

// check_byte_stream: one byte, one 16-bit and one 32-bit integer and one float, written in that order.
constexpr std::size_t kByteStreamScratchSize = 16;
constexpr std::size_t kByteStreamSampleBytes = 11; // 1 + 2 + 4 + 4
constexpr int kByteSample = 0x12;
constexpr int kShortSample = 0x3456;
constexpr uint32_t kWordSample = 0x789ABCDE; // every byte differs: the byte order shows
constexpr float kFloatSample = 1.5f;
void check_byte_stream()
{
    text::print("byte stream\n");
    std::array<uint8_t, kByteStreamScratchSize> buffer = {};
    util::ByteWriter writer(buffer.data(), kByteStreamSampleBytes);
    writer.u8(kByteSample);
    writer.u16(kShortSample);
    writer.u32(kWordSample);
    writer.f32(kFloatSample);
    check(writer.ok() && writer.position() == kByteStreamSampleBytes, "writer: 11 bytes");
    check(!writer.u8(0) && !writer.ok() && writer.position() == kByteStreamSampleBytes, "writer: past the capacity fails");
    const std::array<uint8_t, 11> expected = {{0x12, 0x56, 0x34, 0xDE, 0xBC, 0x9A, 0x78, 0x00, 0x00, 0xC0, 0x3F}};
    check(std::memcmp(buffer.data(), expected.data(), kByteStreamSampleBytes) == 0, "writer: little-endian");
    util::ByteReader reader(buffer.data(), kByteStreamSampleBytes);
    check(reader.u8() == kByteSample && reader.u16() == kShortSample && reader.u32() == kWordSample && reader.f32() == kFloatSample,
          "reader: the values back");
    check(reader.ok() && reader.remaining() == 0, "reader: at the end");
    check(reader.u32() == 0 && !reader.ok() && reader.position() == kByteStreamSampleBytes, "reader: past the end fails");
    const auto text = std::to_array<char>({'a', 'b', 0, 'c', 'd'});
    util::ByteReader strings(text.data(), sizeof(text));
    const char* first = strings.c_string();
    check(first && std::strcmp(first, "ab") == 0 && strings.position() == 3, "reader: a string");
    check(!strings.c_string() && !strings.ok(), "reader: a string without its terminator fails");
}

// --- the net layer's own messages ------------------------------------------------------------------------------------

// The stream start's reference layout and the test values of check_wire_messages.
constexpr std::size_t kStreamDescSize = 32;
constexpr std::size_t kStreamPieceCapacity = 0x100;
constexpr std::size_t kReferenceStreamStartBytes = 0x130;
constexpr std::size_t kReferenceStreamStartContentOffset = 0x2D;
constexpr int kPieceFillStride = 7;
constexpr int kStreamTotalSizeSample = 0x1234;
constexpr int kStreamIdSample = 77;
constexpr uint32_t kStreamChecksumSample = 0xDEADBEEF;
constexpr int kStreamMessageIdSample = 0x5A;
constexpr int kLongDescMessageBytes = 0x30;
constexpr int kLongDescLastIndex = 31;
constexpr int kLongDescEndIndex = 32;
constexpr uint32_t kSequenceSample = 0x12345678;
constexpr int kSequencedIdSample = 0x2A;
constexpr std::size_t kTruncatedHeaderBytes = 5; // fewer than the sequenced header needs
// The stream start as the original built it: a struct on the stack, sent as its first 0x2D bytes, the piece and 3
// bytes more (zeros: our sender clears the struct).
struct ReferenceStreamStart {
    std::array<char, kStreamDescSize> desc;
    int32_t total_size;
    int32_t stream_id;
    uint32_t checksum;
    uint8_t msg_id;
    std::array<uint8_t, kStreamPieceCapacity> content;
};
static_assert(sizeof(ReferenceStreamStart) == kReferenceStreamStartBytes && offsetof(ReferenceStreamStart, content) == kReferenceStreamStartContentOffset,
              "the wire layout of the original's struct");

void check_wire_messages()
{
    text::print("wire messages\n");
    for (const size_t piece_size : {size_t{0}, size_t{1}, size_t{100}, size_t{0x100}}) {
        std::vector<uint8_t> piece(piece_size);
        for (size_t i = 0; i < piece_size; ++i)
            core::at(piece, i) = static_cast<uint8_t>(i * kPieceFillStride + 3);
        ReferenceStreamStart reference = {};
        text::format_to(reference.desc, "%s", "Appearance");
        reference.total_size = kStreamTotalSizeSample;
        reference.stream_id = kStreamIdSample;
        reference.checksum = kStreamChecksumSample;
        reference.msg_id = kStreamMessageIdSample;
        if (piece_size)
            std::memcpy(reference.content.data(), piece.data(), piece_size);
        const size_t reference_size = piece_size + 0x30;

        net::wire::StreamStart start;
        text::format_to(start.desc, "%s", "Appearance");
        start.total_size = kStreamTotalSizeSample;
        start.stream_id = kStreamIdSample;
        start.checksum = kStreamChecksumSample;
        start.msg_id = kStreamMessageIdSample;
        std::array<uint8_t, net::wire::kStreamStartMaxSize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        net::wire::write_stream_start(writer, start, piece.data(), piece_size);
        const std::string what = "stream start, piece " + std::to_string(piece_size);
        check(writer.ok() && writer.position() == reference_size && std::memcmp(bytes.data(), &reference, reference_size) == 0,
              what + ": as the original's struct");

        util::ByteReader reader(bytes.data(), writer.position());
        net::wire::StreamStart back;
        check(net::wire::read_stream_start(reader, &back) && std::strcmp(back.desc.data(), "Appearance") == 0 &&
                  back.total_size == kStreamTotalSizeSample && back.stream_id == kStreamIdSample && back.checksum == kStreamChecksumSample &&
                  back.msg_id == kStreamMessageIdSample && reader.remaining() == piece_size + 3,
              what + ": decoded");
        std::array<uint8_t, net::wire::kStreamStartMaxSize> again{};
        util::ByteWriter again_writer(again.data(), sizeof(again));
        net::wire::write_stream_start(again_writer, back, reader.cursor(), piece_size);
        check(again_writer.position() == writer.position() && std::memcmp(again.data(), bytes.data(), writer.position()) == 0,
              what + ": encoded again");

        std::array<uint8_t, net::wire::kStreamPieceMaxSize> piece_bytes{};
        util::ByteWriter piece_writer(piece_bytes.data(), sizeof(piece_bytes));
        net::wire::write_stream_piece(piece_writer, kStreamIdSample, piece.data(), piece_size);
        std::array<uint8_t, 4 + kStreamPieceCapacity> reference_piece{};
        int32_t id = kStreamIdSample;
        std::memcpy(reference_piece.data(), &id, 4);
        if (piece_size)
            std::memcpy(std::span(reference_piece).subspan(4).data(), piece.data(), piece_size);
        check(piece_writer.position() == piece_size + 4 &&
                  std::memcmp(piece_bytes.data(), reference_piece.data(), piece_size + 4) == 0,
              "stream piece " + std::to_string(piece_size) + ": as the original's struct");
    }
    // (a description of 32 characters or more: the original overran its field; cut to 32 here)
    net::wire::StreamStart long_desc;
    std::memset(long_desc.desc.data(), 'x', kStreamDescSize);
    std::array<uint8_t, net::wire::kStreamStartMaxSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    net::wire::write_stream_start(writer, long_desc, nullptr, 0);
    check(writer.position() == kLongDescMessageBytes && core::at(bytes, kLongDescLastIndex) == 'x' && core::at(bytes, kLongDescEndIndex) == 0, "stream start: a 32-character description");

    // the sequenced wrapper: group, sequence, id
    const auto sequenced = std::to_array<uint8_t>({0x03, 0x78, 0x56, 0x34, 0x12, 0x2A});
    util::ByteReader reader(sequenced.data(), sizeof(sequenced));
    net::wire::SequencedHeader header;
    check(net::wire::read_sequenced_header(reader, &header) && header.group == 3 && header.sequence == kSequenceSample &&
              header.id == kSequencedIdSample,
          "sequenced header decoded");
    std::array<uint8_t, sizeof(sequenced)> again{};
    util::ByteWriter again_writer(again.data(), sizeof(again));
    check(net::wire::write_sequenced_header(again_writer, header) &&
              std::memcmp(again.data(), sequenced.data(), sizeof(sequenced)) == 0,
          "sequenced header encoded again");
    util::ByteReader short_reader(sequenced.data(), kTruncatedHeaderBytes);
    check(!net::wire::read_sequenced_header(short_reader, &header), "sequenced header: too short fails");
}

// --- the skate module's messages -------------------------------------------------------------------------------------

// The skate module's and online messages' reference layouts (the original's structs) and test values.
constexpr std::size_t kScoreEntryCount = 52;
constexpr std::size_t kReferenceScoresBytes = 0x10E;
constexpr int kScoreEntryWireBytes = 5; // an id byte and a 32-bit score
constexpr int kScoresTimeLeftSample = 95;
constexpr int kScoresCheatsSample = 0x10;
constexpr int kNegativeScoreSample = 5; // the second entry is minus this
constexpr int kScoreStep = 100000;
constexpr std::size_t kStartPointCount = 8;
constexpr int kStartPointFillCount = 8;
constexpr std::size_t kReferenceSessionBriefBytes = 0x50;
constexpr std::size_t kReferenceSessionBriefStartPointsOffset = 0x28;
constexpr std::size_t kReferenceSessionBriefVariantOffset = 0x4C;
constexpr std::size_t kReferenceStartGameBytes = 0x48;
constexpr std::size_t kReferenceStartGameStopAtZeroOffset = 0x14;
constexpr std::size_t kReferenceStartGameCrownStartSlotOffset = 0x24;
constexpr std::size_t kReferenceStartGameStartPointsOffset = 0x28;
constexpr std::size_t kServerNameSize = 16;
constexpr std::size_t kServerLevelSize = 32;
constexpr std::size_t kServerModeSize = 32;
constexpr std::size_t kServerPlayerNameSize = 16;
constexpr std::size_t kServerPlayerSlots = 8;
constexpr std::size_t kReferenceServerInfoBytes = 0xE0;
constexpr std::size_t kReferenceServerInfoTimeOffset = 0x5C;
constexpr int kServerNameOffset = 8; // where the name begins in the encoded server info
constexpr int kMaxPlayersSample = 8;
constexpr int kServerTimeSample = 123456;
// A session brief and a start-game message filled with values that differ field by field.
constexpr int kGameIdSample = 200;
constexpr int kProOptionBitsSample = 0x45;
constexpr int kBriefTimeLeftSample = 299;
constexpr uint32_t kGameTypeSample = 0x1C471C60;
constexpr uint32_t kLevelChecksumSample = 0xDEAD1234;
constexpr uint32_t kBriefGoalsSample = 0x11223344;
constexpr int kTimeLimitSample = 300;
constexpr int kBriefTargetScoreSample = 1000000;
constexpr int kBriefStartPointStride = 5;
constexpr int kStartGameTimeLimitSample = 120;
constexpr int kStartGameTargetScoreSample = 50000;
constexpr int kStartGameIdSample = 77;
constexpr uint32_t kStartGameGoalsSample = 0x55667788;
constexpr int kStartGameStartPointBase = 7;
// The scores message as the original built it (a packed struct, sent as its first (count + 2) * 5 bytes).
#pragma pack(push, 1)
struct ReferenceScores {
    uint32_t time_left;
    uint32_t cheats;
    uint8_t final;
    uint8_t count;
    struct Entry {
        uint8_t id;
        int32_t score;
    };
    std::array<Entry, kScoreEntryCount> entries;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceScores) == kReferenceScoresBytes, "the wire layout of the original's struct");

// The session messages as the original built them (structs on the stack, sent whole; padding zeroed here).
struct ReferenceSessionBrief {
    uint8_t crown_start_slot;
    uint8_t not_lan;
    uint8_t game_id;
    uint8_t pro_option_bits;
    uint8_t max_players;
    std::array<uint8_t, 3> pad5;
    uint32_t time_left, game_type, level, teams, goals, team_graffiti_mode, time_limit, target_score;
    std::array<uint32_t, kStartPointCount> start_points;
    uint32_t round;
    uint8_t variant;
    std::array<uint8_t, 3> pad4d;
};
static_assert(sizeof(ReferenceSessionBrief) == kReferenceSessionBriefBytes && offsetof(ReferenceSessionBrief, start_points) == kReferenceSessionBriefStartPointsOffset &&
              offsetof(ReferenceSessionBrief, variant) == kReferenceSessionBriefVariantOffset);
struct ReferenceStartGame {
    uint32_t game_type, time_limit, target_score, fireball_level, unused10;
    int8_t stop_at_zero, reset_scores;
    uint8_t game_id, unused17;
    uint32_t teams, goals, team_graffiti_mode;
    int8_t crown_start_slot;
    std::array<uint8_t, 3> unused25;
    std::array<uint32_t, kStartPointCount> start_points;
};
static_assert(sizeof(ReferenceStartGame) == kReferenceStartGameBytes && offsetof(ReferenceStartGame, stop_at_zero) == kReferenceStartGameStopAtZeroOffset &&
              offsetof(ReferenceStartGame, crown_start_slot) == kReferenceStartGameCrownStartSlotOffset &&
              offsetof(ReferenceStartGame, start_points) == kReferenceStartGameStartPointsOffset);

#pragma pack(push, 1)
struct ReferenceServerInfo {
    int8_t num_players, max_players, num_observers, max_observers;
    uint8_t password, in_game, host_mode, logged_in;
    std::array<char, kServerNameSize> name;
    std::array<char, kServerLevelSize> level;
    std::array<char, kServerModeSize> mode;
    uint8_t skill_level;
    std::array<uint8_t, 3> pad59;
    uint32_t time;
    std::array<std::array<char, kServerPlayerNameSize>, kServerPlayerSlots> players;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceServerInfo) == kReferenceServerInfoBytes && offsetof(ReferenceServerInfo, time) == kReferenceServerInfoTimeOffset);

void check_server_info()
{
    ReferenceServerInfo reference = {};
    online::ServerInfo info;
    reference.num_players = info.num_players = 3;
    reference.max_players = info.max_players = kMaxPlayersSample;
    reference.num_observers = info.num_observers = 1;
    reference.max_observers = info.max_observers = 2;
    reference.password = 1;
    info.password = true;
    reference.host_mode = info.host_mode = 2;
    text::format_to(reference.name, "%s", "Kevin's park");
    text::format_to(info.name, "%s", "Kevin's park");
    text::format_to(reference.level, "%s", "Barcelona");
    text::format_to(info.level, "%s", "Barcelona");
    text::format_to(reference.mode, "%s", "Trick Attack");
    text::format_to(info.mode, "%s", "Trick Attack");
    reference.skill_level = info.skill_level = 4;
    reference.time = info.time = kServerTimeSample;
    const std::array<const char*, 3> names = {{"one", "fifteen_chars__", "x"}};
    for (int i = 0; i < 3; ++i) {
        text::format_to(core::at(reference.players, i), "%s", core::at(names, i));
        text::format_to(core::at(info.players, i), "%s", core::at(names, i));
    }
    std::array<uint8_t, kReferenceServerInfoBytes> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    online::write_server_info(writer, info);
    check(writer.ok() && writer.position() == sizeof(bytes) && std::memcmp(bytes.data(), &reference, sizeof(bytes)) == 0,
          "server info: as the original's struct");
    util::ByteReader reader(bytes.data(), sizeof(bytes));
    auto back = std::make_unique<online::ServerInfo>();
    online::read_server_info(reader, back.get());
    std::array<uint8_t, kReferenceServerInfoBytes> again{};
    util::ByteWriter again_writer(again.data(), sizeof(again));
    online::write_server_info(again_writer, *back);
    check(reader.ok() && std::memcmp(again.data(), bytes.data(), sizeof(bytes)) == 0 &&
              std::strcmp(core::at(back->players, 1).data(), "fifteen_chars__") == 0,
          "server info: decoded and encoded again");
    // a name filling its field without a terminator: cut there when read
    std::memset(std::span(bytes).subspan(kServerNameOffset).data(), 'n', kServerNameSize);
    util::ByteReader unterminated(bytes.data(), sizeof(bytes));
    online::read_server_info(unterminated, back.get());
    check(std::strlen(back->name.data()) == kServerNameSize && std::strcmp(back->level.data(), "Barcelona") == 0,
          "server info: an unterminated name stops at its field");
}

void check_session_messages()
{
    check_server_info();
    ReferenceSessionBrief brief_ref = {};
    online::SessionBrief brief;
    brief_ref.crown_start_slot = brief.crown_start_slot = 3;
    brief_ref.not_lan = 1;
    brief.not_lan = true;
    brief_ref.game_id = brief.game_id = kGameIdSample;
    brief_ref.pro_option_bits = brief.pro_option_bits = kProOptionBitsSample;
    brief_ref.max_players = brief.max_players = kMaxPlayersSample;
    brief_ref.time_left = brief.time_left = kBriefTimeLeftSample;
    brief_ref.game_type = brief.game_type = kGameTypeSample;
    brief_ref.level = brief.level = kLevelChecksumSample;
    brief_ref.teams = brief.teams = 2;
    brief_ref.goals = brief.goals = kBriefGoalsSample;
    brief_ref.team_graffiti_mode = brief.team_graffiti_mode = 1;
    brief_ref.time_limit = brief.time_limit = kTimeLimitSample;
    brief_ref.target_score = brief.target_score = kBriefTargetScoreSample;
    for (int i = 0; i < kStartPointFillCount; ++i)
        core::at(brief_ref.start_points, i) = core::at(brief.start_points, i) = static_cast<uint32_t>(i * kBriefStartPointStride + 1);
    brief_ref.round = brief.round = 4;
    brief_ref.variant = brief.variant = 2;
    std::array<uint8_t, kReferenceSessionBriefBytes> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    online::write_session_brief(writer, brief);
    check(writer.ok() && writer.position() == sizeof(brief_ref) && std::memcmp(bytes.data(), &brief_ref, sizeof(bytes)) == 0,
          "session brief: as the original's struct");
    util::ByteReader reader(bytes.data(), sizeof(bytes));
    online::SessionBrief brief_back;
    online::read_session_brief(reader, &brief_back);
    std::array<uint8_t, kReferenceSessionBriefBytes> again{};
    util::ByteWriter again_writer(again.data(), sizeof(again));
    online::write_session_brief(again_writer, brief_back);
    check(reader.ok() && reader.remaining() == 0 && std::memcmp(again.data(), bytes.data(), sizeof(bytes)) == 0,
          "session brief: decoded and encoded again");
    uint32_t mode = 0;
    std::memcpy(&mode, std::span(bytes).subspan(online::kSessionBriefGameTypeAt).data(), 4);
    check(mode == brief.game_type, "session brief: the guards' game type offset");

    ReferenceStartGame start_ref = {};
    online::StartGame start;
    start_ref.game_type = start.game_type = kGameTypeSample;
    start_ref.time_limit = start.time_limit = kStartGameTimeLimitSample;
    start_ref.target_score = start.target_score = kStartGameTargetScoreSample;
    start_ref.fireball_level = start.fireball_level = 3;
    start_ref.stop_at_zero = start.stop_at_zero = 1;
    start_ref.reset_scores = start.reset_scores = 0;
    start_ref.game_id = start.game_id = kStartGameIdSample;
    start_ref.teams = start.teams = 4;
    start_ref.goals = start.goals = kStartGameGoalsSample;
    start_ref.team_graffiti_mode = start.team_graffiti_mode = 0;
    start_ref.crown_start_slot = start.crown_start_slot = -1;
    for (int i = 0; i < kStartPointFillCount; ++i)
        core::at(start_ref.start_points, i) = core::at(start.start_points, i) = static_cast<uint32_t>(kStartGameStartPointBase - i);
    std::array<uint8_t, kReferenceStartGameBytes> start_bytes{};
    util::ByteWriter start_writer(start_bytes.data(), sizeof(start_bytes));
    online::write_start_game(start_writer, start);
    check(start_writer.ok() && start_writer.position() == sizeof(start_ref) &&
              std::memcmp(start_bytes.data(), &start_ref, sizeof(start_bytes)) == 0,
          "start game: as the original's struct");
    util::ByteReader start_reader(start_bytes.data(), sizeof(start_bytes));
    online::StartGame start_back;
    online::read_start_game(start_reader, &start_back);
    std::array<uint8_t, kReferenceStartGameBytes> start_again{};
    util::ByteWriter start_again_writer(start_again.data(), sizeof(start_again));
    online::write_start_game(start_again_writer, start_back);
    check(start_reader.ok() && std::memcmp(start_again.data(), start_bytes.data(), sizeof(start_bytes)) == 0 &&
              start_back.crown_start_slot == -1,
          "start game: decoded and encoded again");
}

void check_skate_messages()
{
    text::print("skate messages\n");
    for (const int count : {1, 3, 8}) {
        ReferenceScores reference = {};
        reference.time_left = kScoresTimeLeftSample;
        reference.cheats = kScoresCheatsSample;
        reference.final = 1;
        reference.count = static_cast<uint8_t>(count);
        auto message = std::make_unique<skate_net::ScoresMessage>();
        message->time_left = kScoresTimeLeftSample;
        message->cheats = kScoresCheatsSample;
        message->final = true;
        message->count = count;
        for (int i = 0; i < count; ++i) {
            core::at(reference.entries, i).id = static_cast<uint8_t>(i * 3 + 1);
            core::at(reference.entries, i).score = i == 1 ? -kNegativeScoreSample : kScoreStep * (i + 1);
            core::at(message->entries, i).id = static_cast<int8_t>(i * 3 + 1);
            core::at(message->entries, i).score = i == 1 ? -kNegativeScoreSample : kScoreStep * (i + 1);
        }
        auto reference_size = static_cast<size_t>((count + 2) * kScoreEntryWireBytes);
        std::array<uint8_t, sizeof(ReferenceScores)> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        skate_net::write_scores(writer, *message);
        const std::string what = "scores, " + std::to_string(count);
        check(writer.ok() && writer.position() == reference_size && std::memcmp(bytes.data(), &reference, reference_size) == 0,
              what + ": as the original's struct");
        util::ByteReader reader(bytes.data(), writer.position());
        auto back = std::make_unique<skate_net::ScoresMessage>();
        check(skate_net::read_scores(reader, back.get()) && back->count == count && back->final &&
                  back->time_left == kScoresTimeLeftSample && back->cheats == kScoresCheatsSample && reader.remaining() == 0,
              what + ": decoded");
        std::array<uint8_t, sizeof(ReferenceScores)> again{};
        util::ByteWriter again_writer(again.data(), sizeof(again));
        skate_net::write_scores(again_writer, *back);
        check(again_writer.position() == reference_size && std::memcmp(again.data(), bytes.data(), reference_size) == 0,
              what + ": encoded again");
        util::ByteReader short_reader(bytes.data(), reference_size - 1);
        check(!skate_net::read_scores(short_reader, back.get()) && back->count == count - 1,
              what + ": cut short keeps the whole entries");
    }
    check_session_messages();
}
} // namespace

namespace {
int run_checks(int argc, char** argv)
{
    check_byte_stream();
    check_wire_messages();
    check_skate_messages();
    failures() += format_check_net::run();
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path(core::at(std::span(argv, static_cast<size_t>(argc)), i));
        if (std::filesystem::is_directory(path / "pre")) { // a Data folder: its archives and files
            failures() += format_check_files::run(path);
        } else if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file() && is_save_file(entry.path()))
                    check_save_file(entry.path());
            }
        } else {
            check_save_file(path);
        }
    }
    if (failures())
        text::print("%d FAILED\n", failures());
    else
        text::print("all passed\n");
    return failures() ? 1 : 0;
}
} // namespace

int main(int argc, char** argv)
{
    // (a stream or locale failure while reading the files: reported, not left to std::terminate)
    try {
        return run_checks(argc, argv);
    } catch (const std::exception& error) {
        text::print("format check stopped: %s\n", error.what());
        return 2;
    }
}
