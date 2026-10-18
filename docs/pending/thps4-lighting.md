# thps4-lighting - THPS4 ports drew washed out (tools/thps4_levels)

Agent "thps4-lighting". User's report: our College (thug2_college.png) looks washed out next to THPS4's own
(thps4_college.png, the same spot).

## The difference (screenshots, mean RGB of the same surfaces)

| surface            | ours            | THPS4          | ratio |
|--------------------|-----------------|----------------|-------|
| road asphalt       | 134 141 141     | 66 69 69       | 2.03  |
| brick paving       | 143 134 132     | 71 67 66       | 2.01  |
| college building   | 177 157 133     | 89 78 66       | 1.99  |
| grass hill         | 193 222 133     | 99 114 68      | 1.95  |
| sidewalk           | 160 165 177     | 81 84 89       | 1.98  |
| arch (green)       | 208 224 230     | 133 155 159    | clipped |
| tan wall           | 254 250 207     | 142 130 102    | clipped |
| sky                | 197 207 235     | 212 220 239    | ~1 (already halved) |

Everything except the sky is exactly twice as bright; colours that pass 255 clip, so the bright ones (the tan wall,
the green arch) lose their saturation (0.29 -> 0.19, 0.16 -> 0.09) and go near white. Dark surfaces keep their hue
(saturation the same) - not a fog, gamma or extra light problem, a straight x2 on the vertex colours. (The skater's
shadow, peds and the HUD differ for other reasons.)

## Cause

THPS4's PC scenes store vertex colour rgb at 255 = full and alpha at 128 = full. Every level (all 11 converted):
rgb median 122..192, max 255; alpha max 128 (College: rgb p10 86 / p50 153 / p90 229 / max 255, mean 153.5).
THUG2's shaders take 128 = full (LA: rgb p50 65, max 128; THUG1 / THAW / THPS3 ports max 128 too). The converter
halved only the skies' colours (batch 108: "THPS4 lights them at 255"), not the levels'.

## Fix

tools/thps4_levels/thps4_levels.py: halve_colors=True for every THPS4 scene (rgb >> 1, alpha kept) - the shared
tools/level_port/geometry.py already had the switch (comments updated). College after: rgb p10 43 / p50 76 / p90 114
/ max 127 (LA: 39 / 65 / 112 / 128).

Other ports: not affected - THUG1 (128 = full, max 128), THAW (max 128) and THPS3 (its converter halves 0-255 itself)
don't use the switch; their output is unchanged (geometry.py's change is comments only). Normals: only 0-29 sectors
per THPS4 level have them (THUG2's own LA has them on all), and the level lighting values from THPS4's Levels.qb are
in THUG2's range (ambient ~72, directional 75..130) - neither was the cause.

The THPS4 skate shop behind the front end's menus (menu_backdrop) is darker too, now as in THPS4.

selfcheck: 139 files, 0 failures. NOT looked at in the game yet: installed in the game folder for the user.
