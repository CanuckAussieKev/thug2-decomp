# TEST ONLY (under the game lock): the installed overlay's SKATE NOW loads the named level instead (free skate).
# python -I tools/game/boot_level.py thps3_foun C:\Games\THUG2-1
# (make_menu_level.py --game <folder> puts the real SKATE NOW back)
import os, subprocess, sys, tempfile
NS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "out", "neverscript", "ns.exe")
level, game = sys.argv[1], sys.argv[2]
F = os.path.join(game, "thug2-decomp-data", "front_end", "levels", "mainmenu", "mainmenu_scripts.qb")
src = os.path.join(tempfile.gettempdir(), "mainmenu_scripts.q")
r = subprocess.run([NS, "-d", F, "-o", src], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
text = open(src, encoding="utf-8", errors="surrogateescape").read()
start = text.index("script main_menu_skate_now {")
end = text.index("\n}", start) + 2
new = ("script main_menu_skate_now {\n    EnableSun \n    SetGameType freeskate \n    SetCurrentGameType \n"
       "    level_select_change_level level=Load_%s \n}" % level)
text = text[:start] + new + text[end:]
open(src, "w", encoding="utf-8", errors="surrogateescape").write(text)
r = subprocess.run([NS, "-c", src, "-o", F, "-targetGame", "thug2"], capture_output=True, text=True)
print(r.returncode, r.stdout[-300:], r.stderr[-300:])
