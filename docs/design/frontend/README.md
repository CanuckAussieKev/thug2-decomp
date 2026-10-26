# Front-end redesign: chosen direction "B+C" (zine layout, kinetic selection)

Chosen by the user on 2026-10-25 from the mock-ups at claude.ai/artifact/C8v9KXxf4YtJ64YpRGniKD (private canvas).
These files are the mock-ups' source (Design Component HTML: markup + a small JS class). Reference for the in-game build:

- HMain.dc.html, HLevels.dc.html - THE CHOSEN STYLE (main menu, level select).
- BMain / BLevels - the zine base it comes from; C2Levels - the kinetic ideas (per-game colours, selected card tilt).
- Career, Create, Online, Options, Pause - first-pass (direction A) screens: their CONTENT and structure (options = the
  ini keys) carry over; restyle them in the B+C look.

Style: ground #0A0A0A, paper text #F2EFE8, muted #B9B5AC / #8E8A82, rules #3A3A38, house accent safety yellow #F2E205;
per-game colours THUG2 #A48BFF, THUG #2FD38A, THPS4 #FF6B3D, THPS3 #3DB7FF, THPS2 #FF4F9A, THAW #F2C230, P8 #5FE0D0,
Misc #C9CBD0. Type: Anton (big condensed names), IBM Plex Mono (labels, numbers, hints), IBM Plex Sans (body). Layout:
left column of numbered items in huge type, right side full-bleed art (loading screens) with the big name and a mono
kicker. Kinetic selection: only the selected line moves - translateX(24px) rotate(-3deg) with a slanted colour cut
(parallelogram) behind it, text turns black. Motion (not in the mock-ups): selection snaps in ~120 ms; screen changes
are one diagonal wipe in the game's colour; confirming a level slices the big name along the cut and the loading
screen comes up through the gap; art pans 2-3% over 10 s.
