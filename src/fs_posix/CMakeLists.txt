# The macOS (arm64) port's file backend (src/fs_posix): the operating system's files by path over POSIX - opened, read,
# written, sized and closed, streams, folders made and listed, files removed and moved - as the game calls them. It
# implements fs::Disk (engine/file/disk.h) in place of src/fs_win32's Win32 calls; app/ installs it (app/main.cpp,
# THUG2_PORTABLE). It includes the engine and the layers below it (tools/layers.ts checks it).
add_library(thug2_fs_posix STATIC
    posix_disk.cpp
)
target_link_libraries(thug2_fs_posix PUBLIC thug2_engine)
