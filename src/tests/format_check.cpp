// NOT ORIGINAL - offline checks of the file, save and network format readers / writers (thug2_format_check.exe).
//
//   thug2_format_check [<save file or folder>...]
//
// With no arguments only the self-contained checks run (the byte reader / writer, the wire messages' encode of a
// decode, added with them). Each save file (.SKA, .NWS, .PRK, .CAS, .CAT, .GOA, .GRF) is checked: its header read, both CRCs, and
// its summary and contents decoded into a plain tree and encoded again - byte for byte what the game wrote, by the
// same rules as the game's structure writer (engine/script/struct_buffer.cpp). Exit code 0 when everything passes.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/net/net_wire.h"
#include "engine/util/byte_stream.h"
#include "game/save_cards/save_file_format.h"
#include "game/skate/skate_net_messages.h"

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// --- the script structure buffer as a plain tree -----------------------------------------------------------------

// The buffer's types (component types, then the buffer's own integer / float forms).
constexpr uint8_t kInteger = 1, kFloat = 2, kString = 3, kLocalString = 4, kPair = 5, kVector = 6, kStruct = 0x0A,
                  kArray = 0x0C, kName = 0x0D;
constexpr uint8_t kSignedByte = 0x0E, kSignedShort = 0x0F, kUnsignedByte = 0x10, kUnsignedShort = 0x11,
                  kZeroInteger = 0x12, kZeroFloat = 0x13;

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
    float floats[3] = {};
    std::string text;
    Structure structure;
    ArrayValue array;
};

Name read_name(util::ByteReader& reader, uint8_t type)
{
    if (type & 0x80)
        return {0x80, reader.u8()};
    if (type & 0x40)
        return {0x40, reader.u16()};
    return {0, reader.u32()};
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
        value->floats[0] = reader.f32();
        value->floats[1] = reader.f32();
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
        Name name = read_name(reader, type);
        auto value = std::make_unique<Value>();
        uint8_t kind = type & 0x3F;
        switch (kind) {
        case kSignedByte:
            value->type = kInteger;
            value->bits = static_cast<uint32_t>(reader.i8());
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
    uint32_t count = reader.u16();
    for (uint32_t i = 0; i < count && reader.ok(); ++i)
        out->elements.push_back(read_element(reader, out->type));
    return reader.ok();
}

void write_name(util::ByteWriter& writer, uint8_t type, const Name& name)
{
    writer.u8(static_cast<uint8_t>(type | name.form));
    if (name.form == 0x80)
        writer.u8(static_cast<uint8_t>(name.value));
    else if (name.form == 0x40)
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
        writer.f32(value.floats[0]);
        writer.f32(value.floats[1]);
        break;
    case kVector:
        for (float f : value.floats)
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
            else if (v > 0 && v <= 0xFF)
                type = kUnsignedByte;
            else if (v > 0 && v <= 0xFFFF)
                type = kUnsignedShort;
            else if (v >= -0x80 && v <= 0x7F)
                type = kSignedByte;
            else if (v >= -0x8000 && v <= 0x7FFF)
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
            float f;
            std::memcpy(&f, &value->bits, sizeof(f));
            float magnitude = f < 0.0f ? -f : f;
            if (magnitude < 0.0001) {
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
size_t check_structure_round_trip(const uint8_t* bytes, size_t size, const std::string& what)
{
    util::ByteReader reader(bytes, size);
    Structure tree;
    bool read = read_structure(reader, &tree);
    check(read, what + ": decoded");
    if (!read)
        return 0;
    size_t used = reader.position();
    std::vector<uint8_t> again(used + 16);
    util::ByteWriter writer(again.data(), again.size());
    write_structure(writer, tree);
    check(writer.ok() && writer.position() == used && std::memcmp(again.data(), bytes, used) == 0,
          what + ": encoded again byte for byte (" + std::to_string(used) + " bytes)");
    return used;
}

// --- save files ----------------------------------------------------------------------------------------------------

bool is_save_file(const std::filesystem::path& path)
{
    static const char* const kExtensions[] = {".SKA", ".NWS", ".PRK", ".CAS", ".CAT", ".GOA", ".GRF"};
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
    std::printf("save %s\n", path.string().c_str());
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string name = path.filename().string();
    int failures_before = g_failures;

    util::ByteReader reader(file.data(), file.size());
    save_cards::SaveFileHeader header;
    check(save_cards::read_save_file_header(reader, &header), name + ": header read");
    check(header.size >= static_cast<int>(save_cards::kSaveFileHeaderSize) &&
              header.size <= static_cast<int>(file.size()),
          name + ": size in the file");
    if (g_failures != failures_before)
        return;
    auto size = static_cast<size_t>(header.size);
    check(save_cards::save_file_checksum(file.data(), size) == header.checksum, name + ": file CRC");
    check(header.summary_size <= save_cards::kMaxSummarySize, name + ": summary size");
    const uint8_t* summary = file.data() + save_cards::kSaveFileHeaderSize;
    check(checksums::generate_crc_from_buffer(summary, header.summary_size) == header.summary_crc,
          name + ": summary CRC");

    // the header written again
    uint8_t again[save_cards::kSaveFileHeaderSize];
    util::ByteWriter writer(again, sizeof(again));
    check(save_cards::write_save_file_header(writer, header) && std::memcmp(again, file.data(), sizeof(again)) == 0,
          name + ": header encoded again");

    size_t summary_used =
        check_structure_round_trip(summary, static_cast<size_t>(header.summary_size), name + " summary");
    check(summary_used == static_cast<size_t>(header.summary_size), name + ": summary ends at its size");
    std::vector<uint8_t> contents(file.begin() + static_cast<std::ptrdiff_t>(save_cards::kSaveFileHeaderSize) +
                                      header.summary_size,
                                  file.begin() + static_cast<std::ptrdiff_t>(size));
    if (path.extension() == ".NWS" || path.extension() == ".nws") {
        for (uint8_t& b : contents)
            b = static_cast<uint8_t>(b - save_cards::kSaveFileMask);
    }
    size_t contents_used = check_structure_round_trip(contents.data(), contents.size(), name + " contents");
    check(contents_used == contents.size(), name + ": contents end at the header's size");
    for (size_t i = size; i < file.size(); ++i) {
        if (file[i] != save_cards::kSaveFileMask) {
            check(false, name + ": padding");
            break;
        }
    }
}

// --- the byte reader / writer ---------------------------------------------------------------------------------------

void check_byte_stream()
{
    std::printf("byte stream\n");
    uint8_t buffer[16] = {};
    util::ByteWriter writer(buffer, 11);
    writer.u8(0x12);
    writer.u16(0x3456);
    writer.u32(0x789ABCDE);
    writer.f32(1.5f);
    check(writer.ok() && writer.position() == 11, "writer: 11 bytes");
    check(!writer.u8(0) && !writer.ok() && writer.position() == 11, "writer: past the capacity fails");
    const uint8_t expected[11] = {0x12, 0x56, 0x34, 0xDE, 0xBC, 0x9A, 0x78, 0x00, 0x00, 0xC0, 0x3F};
    check(std::memcmp(buffer, expected, 11) == 0, "writer: little-endian");
    util::ByteReader reader(buffer, 11);
    check(reader.u8() == 0x12 && reader.u16() == 0x3456 && reader.u32() == 0x789ABCDE && reader.f32() == 1.5f,
          "reader: the values back");
    check(reader.ok() && reader.remaining() == 0, "reader: at the end");
    check(reader.u32() == 0 && !reader.ok() && reader.position() == 11, "reader: past the end fails");
    const char text[] = {'a', 'b', 0, 'c', 'd'};
    util::ByteReader strings(text, sizeof(text));
    const char* first = strings.c_string();
    check(first && std::strcmp(first, "ab") == 0 && strings.position() == 3, "reader: a string");
    check(!strings.c_string() && !strings.ok(), "reader: a string without its terminator fails");
}

// --- the net layer's own messages ------------------------------------------------------------------------------------

// The stream start as the original built it: a struct on the stack, sent as its first 0x2D bytes, the piece and 3
// bytes more (zeros: our sender clears the struct).
struct ReferenceStreamStart {
    char desc[32];
    int32_t total_size;
    int32_t stream_id;
    uint32_t checksum;
    uint8_t msg_id;
    uint8_t content[0x100];
};
static_assert(sizeof(ReferenceStreamStart) == 0x130 && offsetof(ReferenceStreamStart, content) == 0x2D,
              "the wire layout of the original's struct");

void check_wire_messages()
{
    std::printf("wire messages\n");
    for (size_t piece_size : {size_t{0}, size_t{1}, size_t{100}, size_t{0x100}}) {
        std::vector<uint8_t> piece(piece_size);
        for (size_t i = 0; i < piece_size; ++i)
            piece[i] = static_cast<uint8_t>(i * 7 + 3);
        ReferenceStreamStart reference = {};
        std::snprintf(reference.desc, sizeof(reference.desc), "%s", "Appearance");
        reference.total_size = 0x1234;
        reference.stream_id = 77;
        reference.checksum = 0xDEADBEEF;
        reference.msg_id = 0x5A;
        if (piece_size)
            std::memcpy(reference.content, piece.data(), piece_size);
        size_t reference_size = piece_size + 0x30;

        net::wire::StreamStart start;
        std::snprintf(start.desc, sizeof(start.desc), "%s", "Appearance");
        start.total_size = 0x1234;
        start.stream_id = 77;
        start.checksum = 0xDEADBEEF;
        start.msg_id = 0x5A;
        uint8_t bytes[net::wire::kStreamStartMaxSize];
        util::ByteWriter writer(bytes, sizeof(bytes));
        net::wire::write_stream_start(writer, start, piece.data(), piece_size);
        std::string what = "stream start, piece " + std::to_string(piece_size);
        check(writer.ok() && writer.position() == reference_size && std::memcmp(bytes, &reference, reference_size) == 0,
              what + ": as the original's struct");

        util::ByteReader reader(bytes, writer.position());
        net::wire::StreamStart back;
        check(net::wire::read_stream_start(reader, &back) && std::strcmp(back.desc, "Appearance") == 0 &&
                  back.total_size == 0x1234 && back.stream_id == 77 && back.checksum == 0xDEADBEEF &&
                  back.msg_id == 0x5A && reader.remaining() == piece_size + 3,
              what + ": decoded");
        uint8_t again[net::wire::kStreamStartMaxSize];
        util::ByteWriter again_writer(again, sizeof(again));
        net::wire::write_stream_start(again_writer, back, reader.cursor(), piece_size);
        check(again_writer.position() == writer.position() && std::memcmp(again, bytes, writer.position()) == 0,
              what + ": encoded again");

        uint8_t piece_bytes[net::wire::kStreamPieceMaxSize];
        util::ByteWriter piece_writer(piece_bytes, sizeof(piece_bytes));
        net::wire::write_stream_piece(piece_writer, 77, piece.data(), piece_size);
        uint8_t reference_piece[4 + 0x100];
        int32_t id = 77;
        std::memcpy(reference_piece, &id, 4);
        if (piece_size)
            std::memcpy(reference_piece + 4, piece.data(), piece_size);
        check(piece_writer.position() == piece_size + 4 &&
                  std::memcmp(piece_bytes, reference_piece, piece_size + 4) == 0,
              "stream piece " + std::to_string(piece_size) + ": as the original's struct");
    }
    // (a description of 32 characters or more: the original overran its field; cut to 32 here)
    net::wire::StreamStart long_desc;
    std::memset(long_desc.desc, 'x', 32);
    uint8_t bytes[net::wire::kStreamStartMaxSize];
    util::ByteWriter writer(bytes, sizeof(bytes));
    net::wire::write_stream_start(writer, long_desc, nullptr, 0);
    check(writer.position() == 0x30 && bytes[31] == 'x' && bytes[32] == 0, "stream start: a 32-character description");

    // the sequenced wrapper: group, sequence, id
    const uint8_t sequenced[] = {0x03, 0x78, 0x56, 0x34, 0x12, 0x2A};
    util::ByteReader reader(sequenced, sizeof(sequenced));
    net::wire::SequencedHeader header;
    check(net::wire::read_sequenced_header(reader, &header) && header.group == 3 && header.sequence == 0x12345678 &&
              header.id == 0x2A,
          "sequenced header decoded");
    uint8_t again[sizeof(sequenced)];
    util::ByteWriter again_writer(again, sizeof(again));
    check(net::wire::write_sequenced_header(again_writer, header) &&
              std::memcmp(again, sequenced, sizeof(sequenced)) == 0,
          "sequenced header encoded again");
    util::ByteReader short_reader(sequenced, 5);
    check(!net::wire::read_sequenced_header(short_reader, &header), "sequenced header: too short fails");
}

// --- the skate module's messages -------------------------------------------------------------------------------------

// The scores message as the original built it (a packed struct, sent as its first (count + 2) * 5 bytes).
#pragma pack(push, 1)
struct ReferenceScores {
    uint32_t time_left;
    uint32_t cheats;
    uint8_t final;
    uint8_t count;
    struct {
        uint8_t id;
        int32_t score;
    } entries[52];
};
#pragma pack(pop)
static_assert(sizeof(ReferenceScores) == 0x10E, "the wire layout of the original's struct");

void check_skate_messages()
{
    std::printf("skate messages\n");
    for (int count : {1, 3, 8}) {
        ReferenceScores reference = {};
        reference.time_left = 95;
        reference.cheats = 0x10;
        reference.final = 1;
        reference.count = static_cast<uint8_t>(count);
        auto message = std::make_unique<skate_net::ScoresMessage>();
        message->time_left = 95;
        message->cheats = 0x10;
        message->final = true;
        message->count = count;
        for (int i = 0; i < count; ++i) {
            reference.entries[i].id = static_cast<uint8_t>(i * 3 + 1);
            reference.entries[i].score = i == 1 ? -5 : 100000 * (i + 1);
            message->entries[i].id = static_cast<int8_t>(i * 3 + 1);
            message->entries[i].score = i == 1 ? -5 : 100000 * (i + 1);
        }
        size_t reference_size = static_cast<size_t>((count + 2) * 5);
        uint8_t bytes[sizeof(ReferenceScores)];
        util::ByteWriter writer(bytes, sizeof(bytes));
        skate_net::write_scores(writer, *message);
        std::string what = "scores, " + std::to_string(count);
        check(writer.ok() && writer.position() == reference_size && std::memcmp(bytes, &reference, reference_size) == 0,
              what + ": as the original's struct");
        util::ByteReader reader(bytes, writer.position());
        auto back = std::make_unique<skate_net::ScoresMessage>();
        check(skate_net::read_scores(reader, back.get()) && back->count == count && back->final &&
                  back->time_left == 95 && back->cheats == 0x10 && reader.remaining() == 0,
              what + ": decoded");
        uint8_t again[sizeof(ReferenceScores)];
        util::ByteWriter again_writer(again, sizeof(again));
        skate_net::write_scores(again_writer, *back);
        check(again_writer.position() == reference_size && std::memcmp(again, bytes, reference_size) == 0,
              what + ": encoded again");
        util::ByteReader short_reader(bytes, reference_size - 1);
        check(!skate_net::read_scores(short_reader, back.get()) && back->count == count - 1,
              what + ": cut short keeps the whole entries");
    }
    const uint8_t steal[] = {7, 0xCC, 0xCC, 0xCC, 2, 0, 0, 0, 5, 0, 0, 0};
    util::ByteReader reader(steal, sizeof(steal));
    skate_net::GraffitiStealMessage message;
    check(skate_net::read_graffiti_steal(reader, &message) && message.run == 7 && message.victim_id == 2 &&
              message.thief_id == 5,
          "graffiti steal decoded");
}
} // namespace

int main(int argc, char** argv)
{
    check_byte_stream();
    check_wire_messages();
    check_skate_messages();
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path path(argv[i]);
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file() && is_save_file(entry.path()))
                    check_save_file(entry.path());
            }
        } else {
            check_save_file(path);
        }
    }
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
