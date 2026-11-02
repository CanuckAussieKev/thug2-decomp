// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/net/chat.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/checksums.h"
#include "engine/gfx/screen.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/net/thugpro_peer.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/params.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"
#include "platform/stopwatch.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/run_log.h"
#include "engine/gfx/draw_frame.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "wire/net_wire.h"
#include <gsl/pointers>
#include "engine/core/format.h"

namespace online {

namespace {

constexpr uint8_t kObserverColor = 8;
// Sender colour for messages from "Server". The receiver writes the name colour as \c<color + 2> in decimal,
// but the text renderer reads a single hex digit after \c - so only 0..7 work (the game's own observer
// colour 8 comes out as colour 1 followed by a stray "0").
constexpr uint8_t kServerColor = 7;
// What travels over the network (message 0x4C, 'L': Chat in gamenet_messages.h): the receiver (0x00532050) shows
// "\c<color+2><name>\c0 : <text>". The server relays it unchanged to every other player (0x0056A000).

// NetLink::NetMessageInfo (net_app.h): a message to be sent; its constructor (0x004549F0) sets the defaults.
using ::net::NetMessageInfo;
void init_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }

// The local player (online::PlayerRecord): its name, whether it observes, its colour (its skater's object id).
PlayerRecord* find_own_player_record(Hub* manager) { return manager->get_own_player(); }
const char* player_name(PlayerRecord* player) { return player->label_.data(); }
bool is_observer(PlayerRecord* player) { return (player->flag_bits_ >> 2) & 1; }
uint8_t team_color(PlayerRecord* player) { return static_cast<uint8_t>(player->skater_->ident_); }

// The manager's client connection (+0x08) queues a message for the server.
void send_to_server(Hub* manager, NetMessageInfo* desc)
{
    core::down_cast<net::Guest>(manager->client_app_)->send_to_host(desc);
}

// Scripts::CStruct (8 bytes, from the current script struct pool when there is one).
using ParamBag = script::Struct;

gsl::owner<ParamBag*> new_script_struct()
{
    const int current = script::g_struct_pools.current;
    memory::SlabBank* pool = core::at(script::g_struct_pools.pools, current);
    void* memory = pool ? pool->carve() : memory::alloc(8);
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void delete_script_struct(ParamBag* s)
{
    if (!s)
        return;
    s->get_clear();
    script::delete_struct(s);
}
void append_word(ParamBag* s, uint32_t name, const char* value) { s->append_word(name, value); }
void launch_script(uint32_t script, ParamBag* params)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}

constexpr uint32_t kText = 0xC4745838;           // checksum of "text"
constexpr uint32_t kShowChatScript = 0xCCB19938; // the script that adds a line to the chat display

void dup_from(char* out, size_t size, const char* in)
{
    // The original copies with no limit (and would overrun on a too-long line); stop at the field size. (Not
    // strncpy_s: a debug build fills the rest of the field with 0xFE - sent over the network.)
    const size_t length = strnlen(in, size - 1);
    std::memcpy(out, in, length);
    core::at(std::span<char>(out, size), length) = '\0';
}

} // namespace

void show_local_chat_line(const char* line)
{
    gsl::owner<ParamBag*> params = new_script_struct();
    append_word(params, kText, line);
    launch_script(kShowChatScript, params);
    delete_script_struct(params);
}

namespace {

// Shared by the normal and the named send.
void post_out(Hub* manager, const char* name, uint8_t color, const char* text)
{
    Chat packet;
    packet.color = color;
    dup_from(packet.name.data(), kNameSize, name);
    dup_from(packet.text.data(), sizeof(packet.text), text);
    std::array<uint8_t, kChatSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_chat(writer, packet); // color + name + text + '\0'

    NetMessageInfo desc;
    init_msg_desc(&desc);
    desc.ident_ = kMsgChat;
    desc.byte_count_ = static_cast<uint16_t>(writer.position());
    desc.content_ = bytes.data();
    desc.queue_ = NetMessageInfo::kLaneOrdered;
    desc.group_id_ = 8;
    // NOT ORIGINAL - THUGPRO's layout in the THUGPRO peer mode (thugpro_peer.h): our machine id first, the text cut
    // to its 127 characters.
    std::array<uint8_t, thugpro::kChatMessageSize> thugpro_bytes{};
    if (thugpro::peer_mode()) {
        desc.byte_count_ = static_cast<uint16_t>(
            thugpro::encode_chat(thugpro_bytes, thugpro::get_machine_id(), color, packet.name.data(), packet.text.data()));
        desc.content_ = thugpro_bytes.data();
        core::at(packet.text, thugpro::kChatMessageTextSize - 1) = '\0'; // (what was sent, shown below)
    }
    send_to_server(manager, &desc);

    // The server doesn't send our own line back to us, so show it here.
    std::array<char, 256> line{};
    text::format_to(line, "%s : %s", packet.name.data(), packet.text.data());
    show_local_chat_line(line.data());
}

} // namespace

Hub* fetch_hub() { return g_online_hub; }

namespace {

// The manager's keyboard task: the keys (sio::get_keyboard_chars) it acts on.
constexpr char kKeyEnter = 0x19;
constexpr char kKeyF1 = 0x1F; // (0x1B in the original - see keyboard_task)
constexpr char kKeyF2 = 0x1C;
constexpr char kKeyF3 = 0x1D;
constexpr char kKeyF4 = 0x1E;
constexpr uint32_t kId = 0x40C698AF; // "id"
constexpr uint32_t kOpenChatScript = 0x3B4548B8;
constexpr uint32_t kTauntScript = 0x2C43B5BA;
constexpr uint32_t kTauntName = 0xB53D0E0F;
uint32_t& previous_taunt_stamp()
{
    static uint32_t prev_taunt_stamp{};
    return prev_taunt_stamp;
}

// The script command ObjectExists with these parameters.
bool object_exists(script::Struct* params) { return objects::cfunc_object_exists(params, nullptr); }
void run_script_with(uint32_t script, script::Struct* params)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}

} // namespace

// 0x00532050 (online::Hub's chat handler, message 76; its data: none)
int on_chat_msg(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body_);
    std::array<char, 256> line{};
    if (thugpro::peer_mode()) {
        // NOT ORIGINAL - THUGPRO's layout (machine id first); dropped when malformed, as THUGPRO does.
        thugpro::ChatLine chat{};
        if (!thugpro::decode_chat(context->body_, context->body_size_, true, &chat))
            return 1;
        text::format_to(line, "\\c%i%s\\c0 : %s", chat.obj_id_ + 2, chat.name.data(), chat.text.data());
    } else {
        // The original sprintfs the name and the text with no bounds (a name without its NUL runs on into the text)
        // into 256 bytes on the stack; here the name stops at its 16 bytes, the text at the message's end, the line
        // at 256.
        util::ByteReader reader(message, context->body_size_);
        Chat chat;
        read_chat(reader, &chat);
        text::format_to(line, "\\c%i%s\\c0 : %s", static_cast<int8_t>(chat.color) + 2, chat.name.data(),
                        chat.text.data());
    }
    gsl::owner<ParamBag*> params = new_script_struct();
    append_word(params, kText, line.data());
    launch_script(kShowChatScript, params);
    delete_script_struct(params);
    return 1;
}

// 0x00537BD0 (online::Hub::SendChatMessage)
void send_chat_message(Hub* manager, const char* text)
{
    if (*text == '\0')
        return;
    PlayerRecord* player = find_own_player_record(manager);
    const uint8_t color = is_observer(player) ? kObserverColor : team_color(player);
    post_out(manager, player_name(player), color, text);
}

void send_named_chat_message(Hub* manager, const char* name, const char* text)
{
    if (*text == '\0')
        return;
    post_out(manager, name, kServerColor, text);
}

bool handle_chat_command(Hub* manager, const char* text)
{
    if (*text != '/')
        return false;
    // (the THUGPRO peer mode's own: /tod, /weather, /option, /proset, /thugpro - thugpro_peer.h)
    if (thugpro::handle_chat_command(text))
        return true;
    if (_stricmp(text, "/hello") == 0) {
        // \cN switches the text colour (N = one hex digit, a palette entry of the chat font; \c0 = normal).
        send_named_chat_message(manager, "Server", "\\c2Hello, \\c4World!");
        return true;
    }
    if (_stricmp(text, "/colours") == 0 || _stricmp(text, "/colors") == 0) {
        // Only shown here, not sent: each palette entry in its own colour, to pick codes from.
        show_local_chat_line("\\c11 \\c22 \\c33 \\c44 \\c55 \\c66 \\c77 \\c88 \\c99 \\cAA \\cBB \\cCC \\cDD "
                             "\\cEE \\cFF\\c0 (colour codes)");
        return true;
    }
    if (_stricmp(text, "/where") == 0) {
        // Only shown here, not sent: the level, the local skater's position and orientation, and the camera's - written
        // to thug2-decomp.log (for picking a place to show behind the main menu).
        const uint8_t level = render::current_level();
        get_log("where: level %u", level);
        auto* skater = modules::g_skate_module->get_home_rider(); // the skate module's local skater
        if (skater) {
            const maths::Vec4& p = skater->at_point_;
            const maths::Mat4& m = skater->basis_frame_;
            get_log("where: skater pos %.3f %.3f %.3f", p.x, p.y, p.z);
            for (int row = 0; row < 3; ++row)
                get_log("where: skater matrix row %d %.6f %.6f %.6f", row, core::at(m.row, row).x, core::at(m.row, row).y, core::at(m.row, row).z);
        } else {
            get_log("where: no local skater");
        }
        // The camera of the last frame drawn (choose_camera's copy, 0x00786CA8: field of view, position, rows 2 and 1).
        const render::RenderCamera& camera = render::g_camera;
        get_log("where: camera fov %.3f pos %.3f %.3f %.3f back %.6f %.6f %.6f up %.6f %.6f %.6f", camera.angle,
                std::get<0>(camera.position), std::get<1>(camera.position), std::get<2>(camera.position), std::get<0>(camera.backward), std::get<1>(camera.backward),
                std::get<2>(camera.backward), std::get<0>(camera.up), std::get<1>(camera.up), std::get<2>(camera.up));
        std::array<char, 128> line{};
        if (skater)
            text::format_to(line, "\\c4where:\\c0 level %u at %.0f %.0f %.0f - saved to the log", level,
                            skater->at_point_.x, skater->at_point_.y, skater->at_point_.z);
        else
            text::format_to(line, "\\c4where:\\c0 level %u (no skater) - saved to the log", level);
        show_local_chat_line(line.data());
        return true;
    }
    return false; // not one of ours: send it as typed
}

// 0x005334D0
void keyboard_task(tasks::Task<Hub>* /*task*/)
{
    std::array<char, 256> keys{};
    if (sio::get_keyboard_chars(keys) <= 0)
        return;
    const char key = core::at(keys, 0);
    if (key == kKeyEnter) {
        // Open the chat box - unless one of the dialogs (or the quit dialog's anchor) is up. (The parameters are
        // never freed: the original leaks them, 8 bytes a time.)
        gsl::owner<script::Struct*> params = script::spawn_param_bag();
        if (params)
            params = new (params) script::Struct;
        params->put_name_key(kId, 0x31631B98);
        if (object_exists(params))
            return;
        params->get_clear();
        params->put_name_key(kId, 0xF53D1D83);
        if (object_exists(params))
            return;
        params->get_clear();
        params->put_name_key(kId, 0x3B56E746);
        if (object_exists(params))
            return;
        params->get_clear();
        const uint32_t anchor = checksums::hash_from_text("quit_dialog_anchor");
        params->put_name_key(checksums::hash_from_text("id"), anchor);
        if (object_exists(params))
            return;
        run_script_with(kOpenChatScript, nullptr);
        sio::clear_keyboard_chars();
        return;
    }
    // F1-F4: a taunt, at most one every two seconds. F1 comes as 0x1F with PartyMod's keyboard input (0x1B is
    // Escape there): its byte at 0x00533652 - the original compares with 0x1B.
    uint32_t taunt = 0;
    switch (key) {
    case kKeyF1:
        taunt = 0x0E5FD359;
        break;
    case kKeyF2:
        taunt = 0xBEEA3518;
        break;
    case kKeyF3:
        taunt = 0x4525ADBD;
        break;
    case kKeyF4:
        taunt = 0xA36DBEE1;
        break;
    default:
        return;
    }
    if (frame_clock::get_time() - previous_taunt_stamp() <= 2000)
        return;
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->put_name_key(kTauntName, taunt);
    run_script_with(kTauntScript, params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    previous_taunt_stamp() = frame_clock::get_time();
}

} // namespace online

namespace {

constexpr uint32_t kChatTextParam = 0x61414D56; // the chat line's parameter name in the chat scripts
} // namespace

// 0x005A16B0: script function SendChatMessage <text>. Lines starting with '/' that we recognise are run as
// chat commands instead of being sent.
bool cfunc_send_chat_message(script::Struct* params, script::Script* /*script*/)
{
    const char* text = nullptr;
    script::fetch_text(params, kChatTextParam, &text);
    if (!text)
        return false;
    if (std::strlen(text) != 0 && _stricmp(text, "--EMPTY--") != 0) {
        online::Hub* manager = online::fetch_hub();
        if (!online::handle_chat_command(manager, text))
            online::send_chat_message(manager, text);
    }
    return true;
}

