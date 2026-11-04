// NOT ORIGINAL - the file system: the ordered sources and the calls down them (file_system.h).

#include "engine/file/file_system.h"

#include <cstdint>
#include <span>

#include "base/core/checked_index.h"

namespace fs {

void FileSystem::add(Source& source)
{
    if (count_ < kMaxSources) {
        core::at(sources_, count_++) = &source;
    }
}

namespace {

// The first source's answer that handles the call (`ask(source)`), else the type's default.
template <typename T, typename Ask>
T first_handled(std::span<Source* const> sources, Ask ask)
{
    for (Source* source : sources) {
        const Answer<T> answer = ask(*source);
        if (answer.handled) {
            return answer.value;
        }
    }
    return T{};
}

} // namespace

bool FileSystem::exists(const char* name)
{
    return first_handled<bool>(std::span(sources_).first(count_), [&](Source& source) { return source.exists(name); });
}

Handle FileSystem::open(const char* name, const char* mode)
{
    return first_handled<Handle>(std::span(sources_).first(count_),
                                 [&](Source& source) { return source.open(name, mode); });
}

int FileSystem::close(Handle file)
{
    return first_handled<int>(std::span(sources_).first(count_), [&](Source& source) { return source.close(file); });
}

int FileSystem::read(void* buffer, int size, int count, Handle file)
{
    return first_handled<int>(std::span(sources_).first(count_),
                              [&](Source& source) { return source.read(buffer, size, count, file); });
}

int FileSystem::write(const void* buffer, int size, int count, Handle file)
{
    return first_handled<int>(std::span(sources_).first(count_),
                              [&](Source& source) { return source.write(buffer, size, count, file); });
}

uint32_t FileSystem::seek(Handle file, SeekTo seek)
{
    return first_handled<uint32_t>(std::span(sources_).first(count_),
                                   [&](Source& source) { return source.seek(file, seek); });
}

uint32_t FileSystem::size(Handle file)
{
    return first_handled<uint32_t>(std::span(sources_).first(count_), [&](Source& source) { return source.size(file); });
}

bool FileSystem::loose_override(const char* name, std::span<char, kLoosePathSize> path)
{
    for (Source* source : std::span(sources_).first(count_)) {
        if (source->in_memory()) {
            return false;
        }
        if (source->loose_copy(name, path)) {
            return true;
        }
    }
    return false;
}

bool FileSystem::loose_path(const char* name, std::span<char, kLoosePathSize> path)
{
    for (Source* source : std::span(sources_).first(count_)) {
        if (source->loose_copy(name, path)) {
            return true;
        }
    }
    return false;
}

FileSystem& file_system()
{
    static FileSystem system; // (constant-initialised: no guard, nothing to destroy)
    return system;
}

} // namespace fs
