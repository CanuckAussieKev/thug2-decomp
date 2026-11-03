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
#include "engine/gfx/renderer.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace {
constexpr unsigned kStructBytes = 8;
constexpr uint32_t kTauntRepeatDelay = 2000; // ms

// Names (the checksums of their text).
constexpr uint32_t kCurrentMenuAnchor = 0xF53D1D83u; // current_menu_anchor
constexpr uint32_t kDialogBoxAnchor = 0x3B56E746u; // dialog_box_anchor
constexpr uint32_t kGetSomeString = 0x4525ADBDu; // get_some_string
constexpr uint32_t kKeyboardAnchor = 0x31631B98u; // keyboard_anchor
constexpr uint32_t kNoWayString = 0xA36DBEE1u; // no_way_string
constexpr uint32_t kPropsString = 0x0E5FD359u; // props_string
constexpr uint32_t kYourDaddyString = 0xBEEA3518u; // your_daddy_string

} // namespace

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

const char* player_name(PlayerRecord* player) { return player->label().data(); }
bool is_observer(PlayerRecord* player) { return (player->flag_bits() >> 2) & 1; }
uint8_t team_color(PlayerRecord* player) { return static_cast<uint8_t>(player->skater()->ident()); }


// Scripts::CStruct (8 bytes, from the current script struct pool when there is one).
using ParamBag = script::Struct;

gsl::owner<ParamBag*> new_script_struct()
{
    const int current = script::struct_pools().current();
    memory::SlabBank* pool = core::at(script::struct_pools().pools(), current);
    void* memory = pool ? pool->carve() : memory::alloc(kStructBytes);
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
    desc.set_ident(MessageId::kChat);
    desc.set_byte_count(static_cast<uint16_t>(writer.position()));
    desc.set_content(bytes.data());
    desc.set_queue(NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    // NOT ORIGINAL - THUGPRO's layout in the THUGPRO peer mode (thugpro_peer.h): our machine id first, the text cut
    // to its 127 characters.
    std::array<uint8_t, thugpro::kChatMessageSize> thugpro_bytes{};
    if (thugpro::peer_mode()) {
        desc.set_byte_count(static_cast<uint16_t>(
            thugpro::encode_chat(thugpro_bytes, {.machine_id_ = thugpro::get_machine_id(), .obj_id_ = color}, packet.name.data(), packet.text.data())));
        desc.set_content(thugpro_bytes.data());
        core::at(packet.text, thugpro::kChatMessageTextSize - 1) = '\0'; // (what was sent, shown below)
    }
    core::down_cast<net::Guest>(manager->client_app())->send_to_host(&desc);

    // The server doesn't send our own line back to us, so show it here.
    constexpr size_t kLineSize = 256;
    std::array<char, kLineSize> line{};
    text::format_to(line, "%s : %s", packet.name.data(), packet.text.data());
    show_local_chat_line(line.data());
}

} // namespace

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
    const char* message = net::wire::text_of(context->body());
    constexpr size_t kLineSize = 256;
    std::array<char, kLineSize> line{};
    if (thugpro::peer_mode()) {
        // NOT ORIGINAL - THUGPRO's layout (machine id first); dropped when malformed, as THUGPRO does.
        thugpro::ChatLine chat{};
        if (!thugpro::decode_chat(context->body(), context->body_size(), true, &chat))
            return 1;
        text::format_to(line, "\\c%i%s\\c0 : %s", chat.obj_id_ + 2, chat.name.data(), chat.text.data());
    } else {
        // The original sprintfs the name and the text with no bounds (a name without its NUL runs on into the text)
        // into 256 bytes on the stack; here the name stops at its 16 bytes, the text at the message's end, the line
        // at 256.
        util::ByteReader reader(message, context->body_size());
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
    PlayerRecord* player = manager->get_own_player();
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
        auto* skater = modules::Skate::sole()->get_home_rider(); // the skate module's local skater
        if (skater) {
            const maths::Vec4& p = skater->at_point();
            const maths::Mat4& m = skater->basis_frame();
            get_log("where: skater pos %.3f %.3f %.3f", p.x, p.y, p.z);
            for (int row = 0; row < 3; ++row)
                get_log("where: skater matrix row %d %.6f %.6f %.6f", row, core::at(m.row, row).x, core::at(m.row, row).y, core::at(m.row, row).z);
        } else {
            get_log("where: no local skater");
        }
        // The camera of the last frame drawn (choose_camera's copy, 0x00786CA8: field of view, position, rows 2 and 1).
        const render::RenderCamera& camera = render::renderer().render_camera();
        get_log("where: camera fov %.3f pos %.3f %.3f %.3f back %.6f %.6f %.6f up %.6f %.6f %.6f", camera.angle,
                std::get<0>(camera.position), std::get<1>(camera.position), std::get<2>(camera.position), std::get<0>(camera.backward), std::get<1>(camera.backward),
                std::get<2>(camera.backward), std::get<0>(camera.up), std::get<1>(camera.up), std::get<2>(camera.up));
        constexpr size_t kLineSize = 128;
        std::array<char, kLineSize> line{};
        if (skater)
            text::format_to(line, "\\c4where:\\c0 level %u at %.0f %.0f %.0f - saved to the log", level,
                            skater->at_point().x, skater->at_point().y, skater->at_point().z);
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
    constexpr size_t kKeysSize = 256;
    std::array<char, kKeysSize> keys{};
    if (sio::get_keyboard_chars(keys) <= 0)
        return;
    const char key = core::at(keys, 0);
    if (key == kKeyEnter) {
        // Open the chat box - unless one of the dialogs (or the quit dialog's anchor) is up. (The parameters are
        // never freed: the original leaks them, 8 bytes a time.)
        gsl::owner<script::Struct*> params = script::spawn_param_bag();
        if (params)
            params = new (params) script::Struct;
        params->put_name_key(checksums::Name{kId}, kKeyboardAnchor);
        if (object_exists(params))
            return;
        params->get_clear();
        params->put_name_key(checksums::Name{kId}, kCurrentMenuAnchor);
        if (object_exists(params))
            return;
        params->get_clear();
        params->put_name_key(checksums::Name{kId}, kDialogBoxAnchor);
        if (object_exists(params))
            return;
        params->get_clear();
        const uint32_t anchor = checksums::hash_from_text("quit_dialog_anchor");
        params->put_name_key(checksums::Name{checksums::hash_from_text("id")}, anchor);
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
        taunt = kPropsString;
        break;
    case kKeyF2:
        taunt = kYourDaddyString;
        break;
    case kKeyF3:
        taunt = kGetSomeString;
        break;
    case kKeyF4:
        taunt = kNoWayString;
        break;
    default:
        return;
    }
    if (frame_clock::get_time() - previous_taunt_stamp() <= kTauntRepeatDelay)
        return;
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->put_name_key(checksums::Name{kTauntName}, taunt);
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
        online::Hub* manager = online::net_game_state().online_hub();
        if (!online::handle_chat_command(manager, text))
            online::send_chat_message(manager, text);
    }
    return true;
}

