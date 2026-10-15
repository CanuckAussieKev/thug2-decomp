# PartyMod (ported)

Source: https://github.com/PARTYMANX/partymod-thug2, tag v1.0.4 (commit 84908a7).
Copyright (c) 2024 PARTYMANX, MIT licence - see LICENSE in this folder.

Ported into thug2-decomp so the game only needs one injected DLL. The files are kept as close to upstream as
possible so changes stay easy to compare; every local change is marked with `THUG2_DECOMP`.

Local changes:
- `main.c`: the attach work is split out as `partymod_attach()` (called from thug2-decomp's DllMain); PartyMod's
  own DllMain is only built standalone.
- `main.c`: `fastExit` calls `thug2_decomp_before_exit()` and then `ExitProcess(0)` instead of `exit(0)`: inside
  thug2-decomp's DLL, `exit` would run that DLL's C runtime shutdown while game threads are still running.
