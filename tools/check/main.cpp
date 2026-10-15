// thug2_check: the install-time checks, without starting the game.
//
// Loads THUG2PM.exe's image as plain data (nothing in it runs) and points the code walker at it, then checks
// every registered replacement's argument size against the original's RET n, exactly as InstallReplacements
// does when the game starts.
//
//   thug2_check [<path to THUG2PM.exe>]      (default C:\Games\THUG2\THUG2PM.exe)
//
// Exit code 0 when everything matches, 1 if a replacement's argument size differs, 2 if the exe can't be
// loaded.
#include <windows.h>

#include <cstdio>
#include <vector>

#include "hook/hook.h"

namespace {

bool LoadImageAsData(const char* path)
{
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        std::printf("can't open %s\n", path);
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(file);

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::printf("%s isn't an exe\n", path);
        return false;
    }
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != 0x00400000) {
        std::printf("%s isn't the 32-bit game exe (image base 0x400000)\n", path);
        return false;
    }
    DWORD image_size = nt->OptionalHeader.SizeOfImage;
    auto* image =
        static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) {
        std::printf("couldn't allocate %lu bytes for the image\n", image_size);
        return false;
    }
    SetCodeReadOffset(reinterpret_cast<intptr_t>(image) - 0x00400000);
    std::memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        DWORD size = section->SizeOfRawData < section->Misc.VirtualSize || section->Misc.VirtualSize == 0
                         ? section->SizeOfRawData
                         : section->Misc.VirtualSize;
        if (section->PointerToRawData + size > bytes.size())
            size = static_cast<DWORD>(bytes.size() - section->PointerToRawData);
        std::memcpy(image + section->VirtualAddress, bytes.data() + section->PointerToRawData, size);
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "C:\\Games\\THUG2\\THUG2PM.exe";
    if (!LoadImageAsData(path))
        return 2;

    int checked = 0, unchecked = 0, mismatched = 0;
    for (const ArgumentSizeCheck& check : CheckArgumentSizes()) {
        const Replacement& r = check.replacement;
        if (check.Mismatch()) {
            ++mismatched;
            std::printf("MISMATCH 0x%08X %s: the original pops %d bytes of arguments, ours %d\n", r.address, r.name,
                        check.original_pop, r.callee_pop);
        } else if (r.callee_pop >= 0 && check.original_pop >= 0) {
            ++checked;
        } else {
            ++unchecked;
            std::printf("not checked 0x%08X %s (%s)\n", r.address, r.name,
                        r.callee_pop < 0 ? "type not checkable: REPLACE_UNCHECKED stub" : "original's RET not found");
        }
    }
    std::printf("%d replacements: %d match the original's argument size, %d not checked, %d MISMATCHED\n",
                checked + unchecked + mismatched, checked, unchecked, mismatched);
    return mismatched ? 1 : 0;
}
