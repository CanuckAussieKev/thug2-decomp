// NOT ORIGINAL - the folders a THUG2 PC install has that the game expects to find, which the Microsoft installer makes
// and no disc image carries (they are empty, so no file in the MSI's cabinets holds them). The retail MSI's
// `CreateFolder` table names them: `save` (next to the game folder: the profiles, the career and the parks are written
// into it - `Save\%s` in game/save_cards/memcard.cpp) and `Data\missions`, `Data\worlds` under the data folder.
//
// Windows gets them from the installer. A game folder assembled from the discs' files (docs/design/macos-port.md) does not, and the
// game then reports "Error while trying to save" because it cannot create a file in a folder that isn't there. Making
// them is what an install did, so the game's own behaviour is unchanged.

#include <string>

#include "engine/file/disk.h"
#include "platform/posix/windows.h"

namespace port {

void install_layout()
{
    const char* const folders[] = {"save", "Data\\missions", "Data\\worlds"};
    for (const char* folder : folders) {
        if (fs::disk().kind(folder) != fs::PathKind::kFolder) {
            fs::disk().make_folders(folder);
        }
    }
}

} // namespace port
