# The Win32 file backend (src/fs_win32): the operating system's files by path - opened, read, written, sized and
# closed, streams, folders made and listed, files removed and moved - as the game called them. It implements fs::Disk
# (engine/file/disk.h); app/ installs it (app/main.cpp). It includes the engine and the layers below it
# (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without Win32's files.
add_library(thug2_fs_win32 STATIC
    win32_disk.cpp
)
target_link_libraries(thug2_fs_win32 PUBLIC thug2_engine)
