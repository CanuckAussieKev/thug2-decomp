# Imports

Generated from `data/derived/functions.json` (chunk 2). "Callers" = game functions that call the DLL directly.
Most Direct3D/DirectSound/DirectInput use goes through COM interfaces (vtable calls), so those DLLs show only
their creation function here.

| DLL | APIs used | Callers | Caller address range | What it tells us |
|---|---|---|---|---|
| KERNEL32 | 109 | 221 | 0x401c30–0x636851 | Spread everywhere; mostly CRT (heap, files, locale, threads) |
| WS2_32 | 17 | 115 | 0x4548f0–0x622c90 | Networking: sockets, DNS |
| WSOCK32 | 11 | 44 | 0x5f8170–0x622c90 | More networking (select/listen/accept) — likely GameSpy SDK |
| USER32 | 19 | 9 | 0x44eef0–0x5f5860 | Window creation, message loop, MessageBox |
| BINKW32 | 14 | 9 | 0x44fcc0–0x453470 | Movie playback — the Bink player code is in this range |
| ADVAPI32 | 7 | 9 | 0x445dd0–0x5f8530 | Registry (settings / install path) |
| WINMM | 2 | 3 | 0x4b6f10–0x4d89c0 | Multimedia timers |
| DSOUND | 2 | 2 | 0x4d7c80–0x5f59d0 | DirectSound 8 device creation |
| D3D9 | 1 | 1 | 0x4d8650 | `Direct3DCreate9` — renderer init (D3D9 device pointer at 0x7d69e0) |
| GDI32 | 1 | 1 | 0x5d01e5 | `DeleteObject` only |
| DINPUT8 | 1 | 1 | 0x5f44f0 | `DirectInput8Create` — input init (PartyMod skips this and uses SDL2) |

## Proxy DLL candidates (for loading our hook DLL)

The game statically imports both of these, so Windows loads them before the game starts:

- **`dinput8.dll`**: a system DLL, but Windows checks the game folder first, so a `dinput8.dll` placed next to
  the exe gets loaded instead. Our proxy forwards `DirectInput8Create` to the real one in System32. This is a
  common modding technique; the old THUG2 widescreen mod used it (PartyMod's README says to remove that mod).
  Only 1 export to forward.
- **`binkw32.dll`**: already ships in the game folder. Would mean renaming the original and forwarding 14 exports.

Preferred: **`dinput8.dll`**: one export, no game files renamed.
