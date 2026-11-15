# The Win32 file backend (src/fs_win32): the operating system's files by path - opened, read, written, sized and
# closed, streams, folders made and listed, files removed and moved - as the game called them. It implements fs::Disk
# (engine/file/disk.h); app/ installs it (app/main.cpp). It includes the engine and the layers below it
# (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without Win32's files.
# NOT ORIGINAL - a portable build takes port/posix_disk.cpp (the same fs::Disk over open / pread / opendir) in place of
# Win32's calls. The layer's name stays: it is the file layer the exe installs.
add_library(thug2_fs_win32 STATIC win32_disk.cpp)
# NOT ORIGINAL - a portable build compiles port/posix_disk.cpp instead (Win32's calls have no counterpart here).
if(THUG2_PORTABLE)
    set_source_files_properties(win32_disk.cpp PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(thug2_fs_win32 PRIVATE ${PROJECT_SOURCE_DIR}/port/posix_disk.cpp)
endif()
target_link_libraries(thug2_fs_win32 PUBLIC thug2_engine)
