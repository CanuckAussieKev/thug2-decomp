// NOT ORIGINAL - programs the game starts and watches (child_process.h).

#include "platform/child_process.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwctype>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/no_destroy.h"

namespace sys::child_process {

namespace {

constexpr DWORD kReadSize = 4096;
constexpr DWORD kStoppedExitCode = 1;
constexpr std::size_t kMessageSize = 256;

// A Windows handle closed with its owner.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    ~Handle() { close(); }
    [[nodiscard]] HANDLE get() const { return handle_; }
    void close()
    {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_ = nullptr;
};

// A child: its process, its job, the pipe its output comes through and the thread reading it.
struct Child {
    Handle process;
    Handle job;
    Handle output_pipe;
    std::thread reader;
    std::mutex lock;
    std::string output; // (read, not yet taken)
    bool read_all = false;
};

struct Children {
    std::map<int, std::unique_ptr<Child>> running;
    int last = 0;
};

Children& children()
{
    static core::NoDestroy<Children> state;
    return state.get();
}

Child* find(int child)
{
    auto it = children().running.find(child);
    return it != children().running.end() ? it->second.get() : nullptr;
}

std::wstring widened(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string windows_error(const char* what)
{
    std::array<char, kMessageSize> message{};
    const DWORD error = GetLastError();
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                                        message.data(), static_cast<DWORD>(message.size()), nullptr);
    std::string text(message.data(), length);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return std::string(what) + ": " + (text.empty() ? "error " + std::to_string(error) : text);
}

bool same_name(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() && std::ranges::equal(a, b, [](wchar_t x, wchar_t y) { return std::towupper(x) == std::towupper(y); });
}

// The game's environment with `added`'s variables put in (a name already there replaced), as CreateProcess takes it:
// "name=value" strings one after another, each with its terminator, and one more after the last.
std::wstring environment_block(const std::vector<std::pair<std::string, std::string>>& added)
{
    std::vector<std::wstring> entries;
    if (wchar_t* strings = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = strings; *entry != L'\0';) {
            const std::wstring_view text(entry);
            entries.emplace_back(text);
            entry = std::next(entry, static_cast<std::ptrdiff_t>(text.size() + 1));
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [name, value] : added) {
        const std::wstring wide_name = widened(name);
        // (a drive's "=C:=..." entries start with '=': the name is what comes before the next one)
        std::erase_if(entries, [&wide_name](const std::wstring& entry) {
            const std::size_t equals = entry.find(L'=', 1);
            return equals != std::wstring::npos && same_name(std::wstring_view(entry).substr(0, equals), wide_name);
        });
        entries.push_back(wide_name + L"=" + widened(value));
    }
    std::ranges::sort(entries, [](const std::wstring& a, const std::wstring& b) {
        return std::ranges::lexicographical_compare(a, b, [](wchar_t x, wchar_t y) { return std::towupper(x) < std::towupper(y); });
    });
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void read_output(Child* child)
{
    std::array<char, kReadSize> buffer{};
    DWORD got = 0;
    while (ReadFile(child->output_pipe.get(), buffer.data(), kReadSize, &got, nullptr) != 0 && got > 0) {
        const std::scoped_lock hold(child->lock);
        child->output.append(buffer.data(), got);
    }
    const std::scoped_lock hold(child->lock);
    child->read_all = true;
}

} // namespace

std::wstring quoted(const std::wstring& argument)
{
    // (as the C runtime reads a command line back: backslashes are literal except before a quote, where 2n + 1 of
    // them make n and a literal quote)
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring text = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            text.append((backslashes * 2) + 1, L'\\');
        } else {
            text.append(backslashes, L'\\');
        }
        backslashes = 0;
        text.push_back(character);
    }
    text.append(backslashes * 2, L'\\');
    text.push_back(L'"');
    return text;
}

std::wstring command_line(const Command& command)
{
    std::wstring line = quoted(widened(command.program));
    for (const std::string& argument : command.arguments) {
        line.push_back(L' ');
        line += quoted(widened(argument));
    }
    return line;
}

int start(const Command& command, std::string* why)
{
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (CreatePipe(&read_end, &write_end, &inherit, 0) == 0) {
        *why = windows_error("CreatePipe");
        return 0;
    }
    Handle output(read_end);
    Handle child_output(write_end);
    SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0);
    const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
    if (input.get() == nullptr) {
        *why = windows_error("NUL");
        return 0;
    }

    // Only the pipe and NUL handed down (not every inheritable handle of the game's).
    std::array<HANDLE, 2> handed = {child_output.get(), input.get()};
    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    std::vector<std::byte> list_memory(list_size);
    void* list = list_memory.data(); // (Windows' opaque list)
    auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list);
    if (InitializeProcThreadAttributeList(attributes, 1, 0, &list_size) == 0 ||
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, static_cast<void*>(handed.data()), sizeof(handed), nullptr,
                                  nullptr) == 0) {
        *why = windows_error("the handle list");
        return 0;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = child_output.get();
    startup.StartupInfo.hStdError = child_output.get();
    startup.lpAttributeList = attributes;

    std::wstring line = command_line(command);
    std::wstring environment = environment_block(command.environment);
    const std::wstring program = widened(command.program);
    const std::wstring folder = widened(command.folder);
    PROCESS_INFORMATION info{};
    const BOOL made = CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                         EXTENDED_STARTUPINFO_PRESENT,
                                     environment.data(), folder.empty() ? nullptr : folder.c_str(), &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attributes);
    if (made == 0) {
        *why = windows_error(command.program.c_str());
        return 0;
    }
    Handle process(info.hProcess);
    const Handle thread(info.hThread);

    // Its job: the tree it starts ends with it (stop), or with the game (the job's last handle closed).
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job.get() == nullptr ||
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0 ||
        AssignProcessToJobObject(job.get(), process.get()) == 0) {
        *why = windows_error("the job object");
        TerminateProcess(process.get(), kStoppedExitCode);
        return 0;
    }
    ResumeThread(thread.get());
    child_output.close(); // (the child has its own: the pipe ends when it and its children are done)

    auto child = std::make_unique<Child>();
    child->process = std::move(process);
    child->job = std::move(job);
    child->output_pipe = std::move(output);
    Child* reading = child.get();
    child->reader = std::thread([reading] { read_output(reading); });
    const int number = ++children().last;
    children().running.emplace(number, std::move(child));
    return number;
}

std::string take_output(int child)
{
    Child* found = find(child);
    if (found == nullptr) {
        return {};
    }
    const std::scoped_lock hold(found->lock);
    return std::exchange(found->output, {});
}

bool finished(int child, int* exit_code)
{
    Child* found = find(child);
    if (found == nullptr) {
        *exit_code = -1;
        return true;
    }
    if (WaitForSingleObject(found->process.get(), 0) != WAIT_OBJECT_0) {
        return false;
    }
    {
        const std::scoped_lock hold(found->lock);
        if (!found->read_all) {
            return false;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(found->process.get(), &code);
    *exit_code = static_cast<int>(code);
    return true;
}

void stop(int child)
{
    if (const Child* found = find(child)) {
        TerminateJobObject(found->job.get(), kStoppedExitCode);
    }
}

void release(int child)
{
    auto it = children().running.find(child);
    if (it == children().running.end()) {
        return;
    }
    Child& found = *it->second;
    TerminateJobObject(found.job.get(), kStoppedExitCode);
    if (found.reader.joinable()) {
        // (the job ended: the pipe's writers are gone and the read ends - and a read still waiting is cancelled)
        CancelSynchronousIo(found.reader.native_handle());
        found.reader.join();
    }
    children().running.erase(it);
}

} // namespace sys::child_process
