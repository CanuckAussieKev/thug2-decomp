// NOT ORIGINAL - thug2_automation_file_check.exe: the test-automation channel's command file read while it is being
// appended to (devtools/command_file.h - the reader the game's automation channel uses, its looks made the game's way:
// one at a time, each answered a "frame" later on another thread).
//   - Simulated files (a string, its write time a counter): lines appended in pieces cut anywhere - mid-line, mid-CRLF,
//     a byte order mark first - each taken once, whole, in order; a file rewritten shorter, rewritten to the same
//     size with other text, and rewritten longer all read again from their start; a last line without its end taken
//     once the file has stood still; a file of several megabytes read across looks of at most kMostBytes.
//   - A real file: a writer thread appends 3,000 commands (each append opened, written, closed - as tools/game's
//     Add-GameCommands does - its text cut at random places, CRLF and LF mixed) while the reader makes its looks with
//     read_command_file on a worker thread, each applied before the next is asked for; every command must arrive once,
//     whole and in order. Then the file is rewritten while being read: the reader starts again and takes the new text.
//   thug2_automation_file_check [--seed N]
// Exit code: 0 when every check passed, 1 when one failed.

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "devtools/command_file.h"
#include "engine/file/disk.h"
#include "fs_win32/win32_disk.h"
#include "tests/no_error_dialogs.h"

namespace {

using frontend::automation::CommandBytes;
using frontend::automation::CommandFile;
using frontend::automation::CommandRead;

int g_checked = 0;
int g_failed = 0;

void expect(bool ok, const std::string& what)
{
    ++g_checked;
    if (!ok) {
        ++g_failed;
    }
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
}

// A simulated file: its text and a write time that moves on with every change.
struct FakeFile {
    std::string text;
    uint64_t time = 1;
    void append(const std::string& more)
    {
        text += more;
        ++time;
    }
    void rewrite(std::string all)
    {
        text = std::move(all);
        ++time;
    }
    // read_command_file's rules on the string
    [[nodiscard]] CommandBytes look(const CommandRead& read) const
    {
        CommandBytes found;
        found.seen = true;
        found.size = text.size();
        found.time = time;
        found.from = read.from;
        if (read.still_known && found.size == read.still_size && found.time == read.still_time) {
            found.unchanged = true;
            return found;
        }
        found.opened = true;
        if (found.size >= read.from) {
            found.data = text.substr(static_cast<std::size_t>(read.from),
                                     static_cast<std::size_t>(std::min<uint64_t>(found.size - read.from, CommandFile::kMostBytes)));
        }
        return found;
    }
};

// The game's loop on a simulated file: each frame the look asked for a frame before is answered (from the file as it
// stands now) and only then the next one asked. `restarts` counts the rewrites the reader reported.
struct Driver {
    CommandFile reader;
    std::optional<CommandRead> asked;
    std::vector<std::string> lines;
    int restarts = 0;
    void frame(const FakeFile& file)
    {
        if (asked) {
            if (reader.take(file.look(*asked), &lines)) {
                ++restarts;
                lines.clear();
            }
        }
        asked = reader.next();
    }
    void frames(const FakeFile& file, int count)
    {
        for (int i = 0; i < count; ++i) {
            frame(file);
        }
    }
};

std::string command(int i) { return "console set tod day " + std::to_string(i) + " wait 200"; }

void check_pieces(std::mt19937& random)
{
    FakeFile file;
    Driver driver;
    std::string pending = "\xEF\xBB\xBF";
    constexpr int kCommands = 500;
    for (int i = 0; i < kCommands; ++i) {
        pending += command(i) + ((i % 3) == 0 ? "\r\n" : "\n");
    }
    // appended in pieces of 1 - 40 bytes, a frame (or none, or two) between them
    std::size_t at = 0;
    while (at < pending.size()) {
        const std::size_t piece = std::min<std::size_t>(pending.size() - at, 1 + (random() % 40));
        file.append(pending.substr(at, piece));
        at += piece;
        driver.frames(file, static_cast<int>(random() % 3));
    }
    driver.frames(file, 4);
    bool all = driver.lines.size() == kCommands;
    for (std::size_t i = 0; all && i < driver.lines.size(); ++i) {
        all = driver.lines[i] == command(static_cast<int>(i));
    }
    expect(all && driver.restarts == 0,
           "appended in pieces cut anywhere (a BOM first, CRLF and LF): " + std::to_string(driver.lines.size()) +
               " lines of " + std::to_string(kCommands) + ", each whole and in order");
}

void check_rewrites()
{
    // shorter
    {
        FakeFile file;
        Driver driver;
        file.append("one\ntwo\nthree\n");
        driver.frames(file, 3);
        file.rewrite("four\n");
        driver.frames(file, 3);
        expect(driver.restarts == 1 && driver.lines == std::vector<std::string>{"four"},
               "rewritten shorter: read again from its start");
    }
    // the same size, other text
    {
        FakeFile file;
        Driver driver;
        file.append("aaaa\nbbbb\n");
        driver.frames(file, 3);
        file.rewrite("cccc\ndddd\n");
        driver.frames(file, 3);
        expect(driver.restarts == 1 && driver.lines == std::vector<std::string>({"cccc", "dddd"}),
               "rewritten to the same size with other text: read again from its start");
    }
    // longer, other text
    {
        FakeFile file;
        Driver driver;
        file.append("first one\n");
        driver.frames(file, 3);
        file.rewrite("a new file\nwith two lines\n");
        driver.frames(file, 3);
        expect(driver.restarts == 1 && driver.lines == std::vector<std::string>({"a new file", "with two lines"}),
               "rewritten longer with other text: read again from its start");
    }
}

void check_last_line()
{
    FakeFile file;
    Driver driver;
    file.append("done\nno end");
    driver.frames(file, CommandFile::kStillLooks / 2);
    const bool waits = driver.lines == std::vector<std::string>{"done"};
    driver.frames(file, CommandFile::kStillLooks * 2);
    expect(waits && driver.lines == std::vector<std::string>({"done", "no end"}),
           "a last line without its end: kept while the file may still grow, taken once it has stood still");
}

void check_large()
{
    FakeFile file;
    Driver driver;
    std::string all;
    constexpr int kCommands = 100000; // (about 3.5 MB)
    for (int i = 0; i < kCommands; ++i) {
        all += command(i) + "\n";
    }
    file.append(all);
    driver.frames(file, 10);
    bool same = driver.lines.size() == kCommands;
    for (std::size_t i = 0; same && i < driver.lines.size(); i += 997) {
        same = driver.lines[i] == command(static_cast<int>(i));
    }
    expect(same && driver.lines.back() == command(kCommands - 1),
           "a file of " + std::to_string(all.size() / 1024) + " KB read in looks of at most " +
               std::to_string(CommandFile::kMostBytes / 1024) + " KB: " + std::to_string(driver.lines.size()) + " lines");
}

// Appends `text` as a tool does: opened for appending (shared), written, closed.
bool append_file(const std::string& path, const std::string& text)
{
    for (int attempt = 0; attempt < 200; ++attempt) {
        const HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        DWORD written = 0;
        const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE &&
                        written == text.size();
        CloseHandle(file);
        return ok;
    }
    return false;
}

// The game's loop on a real file: the looks made on a worker thread, each answered before the next is asked for;
// returns the lines once `done` is set and the file has been taken to its end (and its last line has had time).
struct RealReader {
    std::string path;
    CommandFile reader;
    std::vector<std::string> lines;
    int restarts = 0;
    void run(const std::atomic<bool>& done, std::size_t want_lines)
    {
        std::optional<CommandBytes> answer;
        std::thread worker;
        std::atomic<bool> answered{false};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            if (worker.joinable() && answered) {
                worker.join();
                answered = false;
                if (reader.take(*answer, &lines)) {
                    ++restarts;
                    lines.clear();
                }
            }
            if (!worker.joinable()) {
                if (done && lines.size() >= want_lines) {
                    break;
                }
                if (const std::optional<CommandRead> read = reader.next()) {
                    worker = std::thread([this, read = *read, &answer, &answered] {
                        answer = frontend::automation::read_command_file(path, read);
                        answered = true;
                    });
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500)); // (a frame, shortened)
        }
        if (worker.joinable()) {
            worker.join();
        }
    }
};

void check_real_file(std::mt19937& random)
{
    char folder[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, folder);
    const std::string path = std::string(folder) + "thug2_automation_file_check_" + std::to_string(GetCurrentProcessId()) + ".txt";
    DeleteFileA(path.c_str());
    constexpr int kCommands = 3000;
    std::string all;
    for (int i = 0; i < kCommands; ++i) {
        all += command(i) + ((i % 5) == 0 ? "\r\n" : "\n");
    }
    // the writer: pieces cut at random places (1 - 300 bytes), a pause now and then
    std::vector<std::size_t> cuts;
    for (std::size_t at = 0; at < all.size();) {
        at = std::min(all.size(), at + 1 + (random() % 300));
        cuts.push_back(at);
    }
    std::atomic<bool> done{false};
    bool wrote = true;
    std::thread writer([&] {
        std::size_t from = 0;
        for (std::size_t i = 0; i < cuts.size(); ++i) {
            wrote = wrote && append_file(path, all.substr(from, cuts[i] - from));
            from = cuts[i];
            if ((i % 7) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        }
        done = true;
    });
    RealReader reading;
    reading.path = path;
    reading.run(done, kCommands);
    writer.join();
    bool all_there = reading.lines.size() == kCommands;
    int first_wrong = -1;
    for (std::size_t i = 0; i < reading.lines.size() && i < kCommands; ++i) {
        if (reading.lines[i] != command(static_cast<int>(i))) {
            all_there = false;
            first_wrong = static_cast<int>(i);
            break;
        }
    }
    expect(wrote && all_there && reading.restarts == 0,
           "a real file appended to while read (" + std::to_string(cuts.size()) + " appends): " +
               std::to_string(reading.lines.size()) + " lines of " + std::to_string(kCommands) +
               (first_wrong >= 0 ? ", the first wrong: " + std::to_string(first_wrong) + " '" + reading.lines[first_wrong] + "'"
                                 : std::string(", each whole and in order")));
    // rewritten while being read: the reader starts again
    const std::string fresh = "waitfor ready\nchoose menu_options\n";
    {
        const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        wrote = file != INVALID_HANDLE_VALUE && WriteFile(file, fresh.data(), static_cast<DWORD>(fresh.size()), &written, nullptr) != FALSE;
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
    reading.lines.clear();
    reading.restarts = 0;
    reading.run(done, 2);
    expect(wrote && reading.restarts == 1 && reading.lines == std::vector<std::string>({"waitfor ready", "choose menu_options"}),
           "the real file rewritten: read again from its start (" + std::to_string(reading.lines.size()) + " lines)");
    DeleteFileA(path.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    fs::install_disk(fs::win32::disk()); // (the reader's looks go through the game's file seam)
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    uint32_t seed = 0x5EEDU;
    for (std::size_t i = 1; i + 1 < arguments.size(); ++i) {
        if (std::string(arguments[i]) == "--seed") {
            seed = static_cast<uint32_t>(std::strtoul(arguments[i + 1], nullptr, 0));
        }
    }
    std::printf("thug2_automation_file_check (seed %u):\n", seed);
    std::mt19937 random(seed);
    check_pieces(random);
    check_rewrites();
    check_last_line();
    check_large();
    check_real_file(random);
    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
