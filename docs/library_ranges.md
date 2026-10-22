# Library vs game code

From chunk 3. The linker placed object files in order, so libraries sit in contiguous blocks at the end of `.text`.

| Range | What | Functions | Size | Decompile? |
|---|---|---|---|---|
| 0x401000-0x5beaae | **Game code** (Neversoft engine + THUG2; the SIO module runs to 0x5beaa0 - corrected 2026-10-16) | ~8,290 | ~1,600 KB | **Yes** |
| 0x5beaae-0x5beb1a | Import thunks (`jmp [IAT]`: Winsock, Direct3DCreate9) | 18 | - | No |
| 0x5beb1a-0x5eef20 | **D3DX9 static library** (found in chunk 5: "D3DX9 Shader Assembler"; 588 of 712 functions not 16-byte aligned, unlike game code) | 712 | ~158 KB | No - link D3DX9 |
| 0x5eef20-0x5f2e00 | **D3DX9's JPEG codec** (IJG libjpeg, part of the D3DX9 static library: every call into it comes from D3DX9, 0x5EB678-0x5EB6C3; found 2026-10-22) | ~25 (Ghidra) | ~16 KB | No - link D3DX9 |
| 0x5f2e00-0x5f6640 | **Game code**: Win32 platform layer | ~80 (Ghidra missed 9) | ~14 KB | **Yes** |
| 0x5f6640-0x5f8130 | Data in `.text` (a table of `.rdata` pointers, offsets and flags; Ghidra's "function" at 0x5F6640 is data) | - | ~7 KB | No |
| 0x5f8130-0x5f8160 | Import thunks (`jmp [IAT]`, linker-generated) | 4 | - | No |
| 0x5f8160-0x622e80 | **GameSpy SDK** (gsi platform, availability check, QR2, server browsing, peer/chat, GP presence, NAT negotiation, HTTP, file transfer) | 831 | ~145 KB | No - link the SDK or reimplement against OpenSpy later |
| 0x622e80-0x622f80 | STL `std::string` throw helpers ("string too long" etc.) | 5 | <1 KB | No - comes from the STL headers |
| 0x622f80-0x640000 | **MSVC 7.1 C runtime** (Ghidra identified most of it by signature) | 2,293 | ~90 KB | No - modern CRT replaces it |
| 0x640000-0x645000 | C++ exception-unwind funclets (compiler-generated) | 100 | ~1 KB | No - compiler regenerates them |

## Notes
- 0x401020 in the game range is the compiler's `vector constructor iterator' (runs a constructor over an array:
  `new T[n]`, arrays of objects). The compiler emits it when the game is built from source, so it needs none.
- The game range still contains inlined STL template code and compiler helpers mixed into game functions.
  Those come for free when the C++ is rewritten with the same containers.
- The platform layer ends at 0x5f6640 (three one-line setters at 0x5f6610-0x5f6630); after it, up to the import
  thunks at 0x5f8130, the bytes are a data table (checked 2026-10-22), not code.
- `0x5f8690` (called from WinMain with `"thps6pc"`) is GameSpy's availability check; "thps6pc" is THUG2's
  GameSpy game name.
- Not yet checked: smaller third-party libraries compiled into the game range (compression, image/audio codecs).
  Chunk 4 will flag these when it labels units.
