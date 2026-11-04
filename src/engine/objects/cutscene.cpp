// Cutscenes: the first pieces of CinematicTable and CinematicEntry (0x004638B0-0x004644A0). See cutscene.h.

#include "engine/objects/cutscene.h"

#include "base/core/str.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstring>
#include <array>

#include "base/core/checksums.h"
#include "frontend/front_end.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"
#include "engine/sys/stopwatch.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"

namespace objects {

namespace {
constexpr std::size_t kNameBufferBytes = 256;
constexpr std::size_t kTextBytes = 0x200;

constexpr uint32_t kSubtitleScript = 0x625985F9; // run with each subtitle's structure (name unknown)
constexpr uint32_t kTime = 0x906B67BA;           // a subtitle's start (name unknown)
constexpr uint32_t kDuration = 0x79A07F3F;       // and how long it shows (name unknown)
constexpr uint32_t kNoSkipping = 0xA2107A0E;     // a global integer: cutscenes can't be skipped (name unknown)
constexpr uint32_t kSlot12Global = 0xA2A4F02F;   // CinematicEntry::update_option_state's global (name unknown)

// The skip buttons (the controller's makes).
constexpr uint32_t kButtonX = 0x40;
constexpr uint32_t kButtonStart = 0x800;

std::array<char, kNameBufferBytes>& name_buffer()
{
    static std::array<char, kNameBufferBytes> cutscene_name_buffer{};
    return cutscene_name_buffer;
}

} // namespace

// 0x004638B0
const char* cutscene_name_from_path(const char* path)
{
    auto& name = name_buffer();
    const std::string_view text(path);
    if (const size_t slash = text.find_last_of("\\/"); slash != std::string_view::npos) {
        // (copied forwards a byte at a time, as the original)
        text::copy_unbounded(name.data(), std::string(text.substr(slash + 1)).c_str());
    }
    if (char* extension = std::strstr(name.data(), ".")) {
        *extension = '\0';
    }
    return name.data();
}

// 0x00463910
uint32_t CinematicTable::hash_of_name(const char* suffix, int number) const
{
    if (number == -1) {
        return checksums::fold_text_into_checksum(label_, suffix);
    }
    std::array<char, kTextBytes> text{};
    text::format_to(text, "%s%d\n", suffix, number);
    return checksums::fold_text_into_checksum(label_, text.data());
}

// 0x004639A0
int CinematicTable::is_finished() const { return clock_ >= end_time_ ? 1 : 0; }

// 0x004639C0
void CinematicTable::update_vblanks()
{
    vblanks_ += frame_clock::fetch_retrace_count() - prev_retraces_;
    prev_retraces_ = frame_clock::fetch_retrace_count();
}

// 0x00463A10
void CinematicTable::set_subtitles(script::Array* subtitles)
{
    subtitles_ = subtitles;
    next_subtitle_ = 0;
    auto* first = subtitles->struct_at(0);
    first->fetch_float(kTime, &next_subtitle_time_, 1);
}

// 0x00464120
void CinematicTable::check_skip()
{
    if (script::fetch_int_value(kNoSkipping) != 0 || !can_skip_ || clock_ == 0.0F) {
        return;
    }
    const modules::FrontEndInputHandler* handler = core::at(modules::front_end_instance()->pad_listeners(), 0).get();
    auto* device = handler->dev();
    if (!device->is_attached() || (device->is_usable() == 0U)) {
        return;
    }
    if ((handler->control_data()->makes() & (kButtonX | kButtonStart)) != 0U) {
        clock_ = end_time_;
        skipped_ = true;
    }
}

// 0x004644A0
void CinematicTable::update_subtitles()
{
    // (comparisons as the original's: a NaN time never shows a subtitle)
    if (!subtitles_ || !(next_subtitle_time_ >= 0.0F) || !(clock_ >= next_subtitle_time_)) {
        return;
    }
    auto* subtitle = subtitles_->struct_at(next_subtitle_);
    subtitle_duration_ = 0.0F;
    subtitle->fetch_float(kDuration, &subtitle_duration_, 0);
    script::launch_script(kSubtitleScript, subtitle, nullptr, false, nullptr);
    subtitle_time_ = next_subtitle_time_;
    ++next_subtitle_;
    next_subtitle_time_ = -1.0F;
    if (next_subtitle_ < subtitles_->extent()) {
        auto* next = subtitles_->struct_at(next_subtitle_);
        // (a subtitle without a time leaves the local as it was: the original's holds this object's address)
        float time = 0.0F;
        const CinematicTable* self = this;
        std::memcpy(&time, static_cast<const void*>(&self), sizeof(time));
        next->fetch_float(kTime, &time, 1);
        next_subtitle_time_ = time;
    }
}

// 0x00463A50
void CinematicEntry::push_option_state() { set_unused_option(static_cast<uint32_t>(!flag_5d_)); }

// 0x00463A60
void CinematicEntry::update_option_state()
{
    if (flag_5d_) {
        if (unknown60_ == -1) {
            set_unused_option(0U);
        }
    } else if (unknown60_ == -1) {
        set_unused_option(1U);
        unknown60_ = script::fetch_int_value(kSlot12Global);
    }
}

// 0x00464580
bool CinematicEntry::can_be_skipped() { return content_ ? content_->can_skip() : false; }

} // namespace objects
