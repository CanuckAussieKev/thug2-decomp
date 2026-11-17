#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// NOT ORIGINAL - the test-automation channel's command file read as it grows (devtools/automation.h: tools append lines
// to thug2-decomp-commands.txt while the game runs). CommandFile is pure (no file system): the game's disk worker makes
// its looks (read_command_file, devtools/automation.cpp poll_file) and thug2_automation_file_check drives the same two
// against a file being appended to.
//
// One look at a time: next() gives the read to make only when no look is waiting for its answer, and take() applies
// the answer before the next look is asked for - so every look starts where the last one's bytes ended (the channel
// once asked again from the old position while a look was in flight: the same lines twice, then the read position past
// the file's real end - appended commands came out cut, "t tod day" for "console set tod day").
//   - The position is the bytes taken so far; each look also reads back the last few of them (kTailBytes) and they must
//     be what was taken: a file rewritten in place (a tool writing it anew, even to the same or a greater size) is read
//     from its start again, as one that got shorter is.
//   - Lines end at \n (a \r before it dropped: CRLF); a last line without its end waits - it is taken once the file has
//     stood still for kStillLooks looks (a writer that never ends its last line). A UTF-8 byte order mark at a line's
//     start is dropped (PowerShell's Out-File writes one).
//   - A look reads at most kMostBytes; the rest comes on the next look.
namespace frontend::automation {

struct CommandRead {
    uint64_t from = 0;     // read from here: the position less the tail kept to check
    uint64_t position = 0; // the bytes taken so far
    // the file as last seen whole (nothing new to take): the worker may skip reading when it still is
    uint64_t still_size = 0;
    uint64_t still_time = 0;
    bool still_known = false;
};

struct CommandBytes {
    bool seen = false;      // the file exists
    bool opened = false;    // it was read (else: try again)
    bool unchanged = false; // its size and time are the still ones given (nothing read)
    uint64_t size = 0;
    uint64_t time = 0;
    uint64_t from = 0;      // where `data` starts (the request's from)
    std::string data;
};

class CommandFile {
public:
    static constexpr uint64_t kTailBytes = 16;
    static constexpr uint64_t kMostBytes = 1ULL << 20;
    static constexpr int kStillLooks = 30;

    // The next look to make - none while one is waiting for take().
    [[nodiscard]] std::optional<CommandRead> next();
    // A look's answer: the lines it completed appended to `lines`. Returns true when the file was found rewritten (or
    // shorter): the lines taken before are the old file's - the caller starts its count again.
    bool take(const CommandBytes& found, std::vector<std::string>* lines);
    // Start after what the file holds now (a file older than the run isn't run): `size` bytes taken, unchecked.
    void skip_to(uint64_t size, uint64_t time);

    [[nodiscard]] uint64_t position() const { return position_; }
    [[nodiscard]] const std::string& partial() const { return partial_; }

private:
    void restart();
    void add_line(std::string line, std::vector<std::string>* lines);

    bool waiting_ = false;
    uint64_t position_ = 0;
    std::string tail_; // the last bytes taken (at most kTailBytes)
    std::string partial_;
    int still_looks_ = 0;
    uint64_t still_size_ = 0;
    uint64_t still_time_ = 0;
    bool still_known_ = false;
};

// Makes the look `read` asks for at the file at `path` (on the game's disk worker; on the check's reading thread): its
// size and time, and - unless they are the still ones - the bytes from read.from to its end (at most kMostBytes), the
// file opened shared with its writers - through the game's file seam (fs::disk(), engine/file/disk.h).
[[nodiscard]] CommandBytes read_command_file(const std::string& path, const CommandRead& read);

} // namespace frontend::automation
