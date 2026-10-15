# Library vs game code

From chunk 3. The linker placed object files in order, so libraries sit in contiguous blocks at the end of `.text`.

| Range | What | Functions | Size | Decompile? |
|---|---|---|---|---|
| 0x401000-0x5be670 | **Game code** (Neversoft engine + THUG2) | ~8,270 | ~1,600 KB | **Yes** |
| 0x5be670-0x5eef20 | **D3DX9 static library** (found in chunk 5: "D3DX9 Shader Assembler"; 588 of 712 functions not 16-byte aligned, unlike game code) | 712 | ~158 KB | No - link D3DX9 |
| 0x5eef20-0x5f8130 | **Game code**: Win32 platform layer | 91 | ~17 KB | **Yes** |
| 0x5f8130-0x5f8160 | Import thunks (`jmp [IAT]`, linker-generated) | 4 | - | No |
| 0x5f8160-0x622e80 | **GameSpy SDK** (gsi platform, availability check, QR2, server browsing, peer/chat, GP presence, NAT negotiation, HTTP, file transfer) | 831 | ~145 KB | No - link the SDK or reimplement against OpenSpy later |
| 0x622e80-0x622f80 | STL `std::string` throw helpers ("string too long" etc.) | 5 | <1 KB | No - comes from the STL headers |
| 0x622f80-0x640000 | **MSVC 7.1 C runtime** (Ghidra identified most of it by signature) | 2,293 | ~90 KB | No - modern CRT replaces it |
| 0x640000-0x645000 | C++ exception-unwind funclets (compiler-generated) | 100 | ~1 KB | No - compiler regenerates them |

## Notes
- The game range still contains inlined STL template code and compiler helpers mixed into game functions.
  Those come for free when the C++ is rewritten with the same containers.
- 0x5f5a00-0x5f8130 holds only 7 small functions with no strings or imports; counted as game code (it follows
  the game's Win32 platform layer: window, DirectInput, DirectSound setup around 0x5f3000-0x5f5a00).
- `0x5f8690` (called from WinMain with `"thps6pc"`) is GameSpy's availability check; "thps6pc" is THUG2's
  GameSpy game name.
- Not yet checked: smaller third-party libraries compiled into the game range (compression, image/audio codecs).
  Chunk 4 will flag these when it labels units.
