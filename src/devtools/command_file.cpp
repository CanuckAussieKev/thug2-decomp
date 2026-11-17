// NOT ORIGINAL - the automation channel's command file read as it grows (command_file.h).
#include "devtools/command_file.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/file/disk.h"
#include "engine/file/disk_files.h"

namespace frontend::automation {

namespace {
constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
} // namespace

std::optional<CommandRead> CommandFile::next()
{
    if (waiting_) {
        return std::nullopt;
    }
    waiting_ = true;
    return CommandRead{.from = position_ - tail_.size(),
                       .position = position_,
                       .still_size = still_size_,
                       .still_time = still_time_,
                       .still_known = still_known_};
}

void CommandFile::restart()
{
    position_ = 0;
    tail_.clear();
    partial_.clear();
    still_looks_ = 0;
    still_known_ = false;
}

void CommandFile::add_line(std::string line, std::vector<std::string>* lines)
{
    if (line.starts_with(kByteOrderMark)) {
        line.erase(0, kByteOrderMark.size());
    }
    lines->push_back(std::move(line));
}

bool CommandFile::take(const CommandBytes& found, std::vector<std::string>* lines)
{
    waiting_ = false;
    if (!found.seen) {
        // (no file: nothing to take - one made later starts from its beginning)
        const bool had = position_ != 0 || !partial_.empty();
        restart();
        return had;
    }
    if (found.unchanged) {
        // A last line without an end of line, once the file has stood still a while.
        if (!partial_.empty() && ++still_looks_ >= kStillLooks) {
            add_line(std::exchange(partial_, std::string()), lines);
            still_looks_ = 0;
        }
        return false;
    }
    if (!found.opened) {
        still_known_ = false; // (look again next time)
        return false;
    }
    const uint64_t from = position_ - tail_.size();
    const bool rewritten = found.size < position_ || found.from != from || found.data.size() < tail_.size() ||
                           found.data.compare(0, tail_.size(), tail_) != 0;
    if (rewritten) {
        restart(); // (the next look reads it from the start)
        return true;
    }
    const std::string_view fresh = std::string_view(found.data).substr(tail_.size());
    position_ += fresh.size();
    tail_ += fresh;
    if (tail_.size() > kTailBytes) {
        tail_.erase(0, tail_.size() - kTailBytes);
    }
    // (all of it taken: the file as it stands is the still one; a look cut at kMostBytes reads on)
    still_known_ = position_ == found.size;
    still_size_ = found.size;
    still_time_ = found.time;
    if (!fresh.empty()) {
        still_looks_ = 0;
    }
    partial_ += fresh;
    std::size_t end = 0;
    while ((end = partial_.find('\n')) != std::string::npos) {
        std::string line = partial_.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        partial_.erase(0, end + 1);
        add_line(std::move(line), lines);
    }
    return false;
}

void CommandFile::skip_to(uint64_t size, uint64_t time)
{
    restart();
    position_ = size;
    still_size_ = size;
    still_time_ = time;
    still_known_ = true;
}

CommandBytes read_command_file(const std::string& path, const CommandRead& read)
{
    CommandBytes found;
    fs::FileStat info;
    if (!fs::disk().stat(path.c_str(), &info) || info.folder) {
        return found;
    }
    found.seen = true;
    found.size = info.size;
    found.time = info.write_time;
    found.from = read.from;
    if (read.still_known && found.size == read.still_size && found.time == read.still_time) {
        found.unchanged = true;
        return found;
    }
    if (found.size < read.from) {
        found.opened = true; // (shorter than the position: rewritten - take() starts again)
        return found;
    }
    const fs::OwnedFile file(fs::disk().open(path.c_str(), {.access = fs::Access::kRead,
                                                            .sharing = fs::Sharing::kReadWriteDelete,
                                                            .disposition = fs::Disposition::kOpenExisting,
                                                            .hint = fs::Hint::kNormal}));
    if (!file) {
        return found; // (held without sharing for a moment by its writer: try again)
    }
    const uint64_t wanted = std::min(found.size - read.from, CommandFile::kMostBytes);
    found.data.resize(static_cast<std::size_t>(wanted));
    uint32_t got = 0;
    found.opened = fs::disk().seek_to(file.get(), read.from) &&
                   (wanted == 0 || fs::disk().read(file.get(), found.data.data(), static_cast<uint32_t>(wanted), &got, nullptr));
    found.data.resize(found.opened ? got : 0);
    return found;
}

} // namespace frontend::automation
