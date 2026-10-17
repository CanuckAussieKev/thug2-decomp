# NeverScript (THUG2 fork) — Known Limitations & Future Work

This fork patches NeverScript v0.9 to decompile/recompile **Tony Hawk's Underground 2
(PC)** QB scripts into bytecode the retail game loads. It is validated end-to-end
in-game (an unmodified recompile of `AU_sfx.qb` loads Australia identically to the
original; a real edit — silencing the seagull fly-up cry — works as intended).

This document tracks what is *not* yet perfect.

## 1. Verification hygiene / byte-identity
**Status: ACHIEVED for `AU_sfx.qb`; near-identical for `AU.qb`.**

`recompile(decompile(AU_sfx.qb)) == AU_sfx.qb` is now **byte-identical** (and a stable
fixpoint). The four cosmetic sources were fixed: the decompiler's header comment
(dropped — it injected a leading `0x01`), the `__register_checksums__` directive's
surrounding newlines (removed — they added stray `0x01`s before the table), and the
per-random **branch-0 newline** (now preserved via a newline right after `random {`
and an offset-base that follows it, instead of force-emitting it on every random).

`AU.qb` (478 KB) round-trips to within **1 byte** — a single newline-after-`=` (top-
level array assignment) that the compiler drops. Not yet byte-identical; harmless.
`AU_Scripts.qb` can't be measured for byte-identity yet because it fails to recompile
(see §2).

## 1a. Bulk round-trip campaign — switch / random / assignment fixes
**Status: three root-cause bugs fixed; 177 → 226 byte-identical of ~257 `.qb`.**

A round-trip sweep over every `.qb` in `qb_scripts`/`mainmenu_scripts`/`BO_scripts`/
`AU_scripts`/`TR_scripts` (decompile → recompile → `cmp`) went from **177 OK / 52
mismatch / 28 error** to **226 OK / 13 mismatch / 18 error**, with zero regressions.
Three independent root causes were fixed:

1. **Switch — spurious trailing short-break on the final case.** THUG2 omits the
   trailing `0x49` on the case abutting `endswitch` (its break is a redundant fall-
   through). The emitter wrote one after *every* case, so any switch whose last case
   had no `default` came out +3 bytes (and shifted all forward jumps crossing it).
   Now the final no-default case skips the trailing short-break and targets its intro
   offset at `endswitchPos-1` (like a `default`). ~40 files fixed (`net/*`, `skater/*`,
   `menu/*`). `BO_scripts.qb` and `chapter_info.qb` are now byte-identical.

2. **`random` — last branch over-read.** A random's last branch has no terminating
   longjump and is bounded by the random's structural end, with *no* `0x01` between it
   and the following statement. The decompiler's invocation-argument loop swallowed
   that following statement as an argument, corrupting branch sizes + longjump offsets.
   Fixed by honouring the body limit inside the argument loop. (LongJump `0x2e` target
   = `(ljPos+5) + value`; the emitter's `finalIndex - ljPos - 5` is correct once the
   AST is right.)

3. **Assignment target must be a name.** `ParseChecksum` accepts *any* token as a
   checksum name (incl. `)`), so in `((arr[idx]) = X)` — where the global `arr[idx]`
   parses as an invocation — the param loop read the inner `) = X` as a keyword-param
   `)=X` and ran past the paren. Guarded `ParseAssignment` to require an Identifier /
   RawChecksum / `<local>` name. NB a naive "checksum-then-`[` is a subscript not an
   invocation" attempt regressed array-literal arguments (`AnimEquals [a b c]`) — the
   `name [...]` bytecode is identical for subscript and call-with-array-arg, so the
   invocation parse is fine; only the runaway assignment was the bug.

Remaining mismatches/errors are other classes (a Begin/Repeat `0x00` vs While `0x20`
loop-opcode choice in the goal_* files; a handful of decompile gaps; recompiler hangs
in `cas_skater_*`). See the project notes for the running list.

## 2. Decompiler opcode / construct coverage
**Status: open — the main real gap.**

Known coverage gaps:
- **`AU_Scripts.qb` recompile — FIXED.** It used to fail ("Unrecognised token in
  body of code") on `if <function-call condition> { break }`: the condition's
  invocation greedily tried to absorb the `{ break }` body as a struct argument, and
  `ParseStruct` choked on `break`. Fixed by bailing out of struct parsing on `break`/
  `return` (control-flow, never struct content) the same way `if`/`while` already do.
  AU_Scripts now recompiles to semantically-equivalent, stable-fixpoint bytecode.
- **`scripts\game\menu\gamemenu_levelselect.qb` fails to decompile** — unhandled
  `0x0e` in a context the body walker doesn't expect (a menu script, not a level).

Broadening coverage is best done **reactively**: when a needed script won't
decompile/recompile, fix the specific opcode/construct it trips on (the same way the
level-script opcodes were added).

## 2b. `switch` round-trips byte-identically (native support)
**Status: FIXED — native `switch` implemented end-to-end.**

The decompiler emits native `switch`/`case`/`default`/`endswitch` NeverScript and the
compiler re-emits the exact `0x3C … 0x3D` switch bytecode (including the `0x49`
short-break case-intro/trailing offsets), so switch-containing files round-trip
BYTE-IDENTICALLY. The old if/elseif **lowering** was removed; it was runtime-unsafe for
complex front-end menu files.

Bytecode layout reproduced exactly:
```
0x3C <value> 0x01
  ( 0x3E 0x49<introOff> <caseValue> <body> 0x49<trailOff> )*
  ( 0x3F 0x49<defOff> <defaultBody> )?
0x3D
```
Offset formulas (LE uint16, measured FROM the `0x49` opcode position):
- `introOff = (trailShortbreakPos + 2) - introShortbreakPos`
- `trailOff = endswitchPos - trailShortbreakPos`
- `defOff   = (endswitchPos - 1) - defaultShortbreakPos`

NeverScript syntax (decompiler output / compiler input):
```
switch <value>
    case <value> { <body> }
    default { <body> }
endswitch
```
An empty case body is `{ }` (one newline = the lone `0x01` an empty case carries).
`switch`/`case`/`default`/`endswitch` are reserved words; a checksum literally named
e.g. `default` (`Anim=default`) is backtick-escaped on decompile so it round-trips.

Verified byte-identical: `mainmenu_scripts.qb`, `mainmenu_options.qb`, `gamemenu.qb`,
`cutscene.qb`, `AU_Scripts.qb`, `cheats.qb` (plus the no-switch regression set stays
byte-identical: `AU_sfx.qb`, `Sk6Ped_StateLogic.qb`, `gamemenu_pause.qb`,
`global_flags.qb`, `displayoptions.qb`, `TR_scripts.qb`).

Three pre-existing encoding bugs that the old switch-lowering had masked were also fixed
to reach byte-identity on the front-end files:
- **`(...)[i]` array access** corrupted the indexed base (parser set both `Array` and
  `Index` to the subscript node).
- **`(<x> -1)` signed-literal adjacency** — THUG2 encodes `(<x> -1)` as two adjacent
  operands `0xE <x> <int -1> 0xF` (distinct from subtraction `(<x> - 1)` →
  `0xE <x> 0xA <int 1> 0xF`). The lexer now lexes `-<digit>` (no space) as a signed
  literal; binary minus always has a trailing space in decompiler output.
- **`name =\n{…}`** dropped the newline(s) between `=` and a struct/array value
  (`0x07 0x01 0x03`); the count is now preserved.
- **LocalString (`0x1C`)** is now distinguished from String (`0x1B`) via the `%"…"`
  sigil; and name-table entries that aren't plain identifiers (texture paths like
  `models\…\…​.tex`) are emitted as quoted strings in `__register_checksums__` and
  backtick-escaped in body references.

## 2d. Decompiler: front-end / cutscene / level scripts (the "0x0e" gap)
**Status: switch-on-expression FIXED; more gaps open.**

THUG2's front-end scripts use parenthesised-expression forms the decompiler didn't handle,
blocking `mainmenu_scripts.qb`, `cutscene.qb`, `Levels.qb`, `gamemenu_options.qb`,
`gamemenu_levelselect.qb`, `gamemenu.qb`.
- **FIXED — `switch (<expr>)`:** the switch decoder only accepted a bare checksum as the switch
  value; now it falls back to `DecompileExpression` when the value is `(…)`. This unlocked
  **decompilation** of `cutscene.qb`, `Levels.qb`, `gamemenu_options.qb`. Bare-checksum switches
  are byte-identical to before (AU_Scripts etc. unchanged); full shipped corpus round-trips
  byte-identically (no regression).
- **FIXED — array subscript on an expression** `(<a>.<b>)[<c>]` (Byte_Array 0x05 as a postfix):
  `DecompileExpression` now consumes postfix `[idx]` subscripts after an atom (binds tighter than
  infix), applied in value context or when the atom was parenthesised (`(expr)[idx]` is
  unambiguously a subscript). Unlocked **decompilation** of `gamemenu_levelselect.qb`, `gamemenu.qb`.
- **FIXED — counted `Begin … Repeat <count>` loop:** `mainmenu_scripts.qb` uses a counted loop
  encoded `0x00 (Begin) <body> 0x21 (Repeat) [<count-expr>]` (e.g. `Begin { make_spin_menu_item
  {blank} } Repeat <num_blanks>`). The decompiler treated the begin `0x00` as EndOfFile and the
  Repeat `0x21` as a count-less EndWhile. Now: inside a script body a `0x00` (not the buffer's final
  byte) is a loop-begin; parse the body to `0x21`, then an optional count expression; render
  `Begin {…} Repeat <count>`. (The infinite `while {}` form, begin `0x20`, is unchanged.) Unlocked
  **decompilation** of `mainmenu_scripts.qb`. **ALL six front-end files now decompile.**
- **Compiler side — `Begin … Repeat <count>` + invocation operands in flat conditions: FIXED.**
  The compiler now lexes `Begin`/`Repeat` keywords, parses `Begin { <body> } Repeat <count>`
  (AstKind_RepeatLoop) and emits `0x00 <body> 0x21 [<count>]`; and the `and`/`or` postfix now
  propagates `allowInvocations` so flat conditions like `(<a> = 0 and GotParam up)` parse.
  **`mainmenu_scripts.qb` now round-trips (decompile→recompile, fixpoint stable, ~1 byte off the
  original) — it's moddable.** `Levels.qb`/`gamemenu_options.qb` recompile (via §2b switch lowering).
- **`cutscene.qb` `if ! <obj>:<method>` — FIXED.** `pruneStructIfInvoked` now also handles
  `LogicalNot(ColonExpression(.., Invocation))`, so `if ! Skater:IsSkaterOnVehicle { body }` no
  longer eats the body. cutscene.qb round-trips (fixpoint stable).
- **Front-end recompile status: 5 of 6 round-trip** — mainmenu_scripts, cutscene, Levels,
  gamemenu_options, gamemenu. **OPEN — `gamemenu_levelselect.qb`: pre-existing compiler
  INFINITE-LOOP** on an invocation arg of the form `cmd key=(<localref> [<arr>] .member)` —
  i.e. `(subscript-then-dot)` as a parenthesised value, parsed with `allowInvocations=false`
  (also reproduces on the pre-session binary, so not a regression). `ParseExpression` spins with
  zero progress on this construct. Deferred (level-select menu, not feature-critical). Needs a
  0-progress fix in the expression parser's paren/array-access/dot interaction.
- **(superseded note) `if ! <obj>:<method>`** (negated colon-expression condition, e.g.
  `if ! Skater:IsSkaterOnVehicle`). `if skater:walking` and `<y> = skater:walking` both compile,
  but `! skater:walking` does not (and negated *invocations* like `! GotParam down` DO). The
  `ParseLogicalNot` → `ParseExpression(…, true)` path mis-handles a colon-expression operand;
  mechanism still unclear (the same call works from `ParseIfStatement`). Blocks recompiling
  cutscene.qb (needed for the Skip-Cutscenes toggle). Workaround used in TR: rewrite
  `if ! a:b { X }` → `if a:b {} else { X }`.
- **Note:** decompiling ≠ recompilable. `cutscene.qb` decompiles but RECOMPILE fails (a compiler
  "Unrecognised token" gap); `Levels.qb`/`gamemenu_options.qb` recompile only via the §2b
  switch→if-chain lowering (semantically-equivalent, not byte-identical, needs in-game validation).

## 2c. Chained parenthesised expressions `(A op B op C ...)` — FIXED
**Status: FIXED (byte-identical).**

The compiler used to parse only a *single* operator inside parentheses: `(A = B)`
worked, but `(A = 0 or B = <c>)` failed ("Unrecognised token in body of code") and
`(0.8 * 250.0 / <width>)` failed ("Incomplete assignment"). `handleBinaryOperator`
parsed one operator, then required `)`; anything else aborted. This blocked common
goal-NPC and UI scripts (e.g. `scripts\game\ped\Sk6Ped_StateLogic.qb`,
`scripts\game\menu\soundoptions.qb`).

THUG2 stores a parenthesised expression as one flat infix token stream between a single
`0xE`/`0xF` pair (operands and operator bytes inline, no nested parens). The fix parses
the whole chain into a new `AstKind_FlatExpression` (ordered operands + operator kinds)
and emits it verbatim. Because `ParseExpression` already grabs `or`/`and` as a
right-associative postfix, the parser **flattens that logical spine** back into the
flat lists so mixed comparison/logical chains reassemble correctly. A single operator
still yields the original binary node (byte-identical to before — zero regression);
two or more yield the flat node. Operator→byte map: `=`0x7, `<`0x12, `<=`0x13, `>`0x14,
`>=`0x15, `-`0xA `+`0xB `/`0xC `*`0xD, `or`0x32, `and`0x33 (`FlatOperatorByte`).

Validated: `(A = 0 or B = <c>)` and `(0.8 * 250.0 / <width>)` compile **byte-identical**
to the originals; the full `Sk6Ped_StateLogic.qb` now round-trips **byte-identically**
(previously failed to recompile); AU_sfx/menu round-trips unchanged. Files: `compiler/
ast.go` (`AstKind_FlatExpression`), `compiler/parser.go` (chaining + spine flatten),
`compiler/output.go` (`FlatOperatorByte` + emitter).

## 3. In-game validation scope
**Status: open.**

Only **Australia (AU)** is validated in-game. Other levels almost certainly work (same
opcode set) but are untested end-to-end.

## 4. Adding brand-new symbols in a mod
**Status: open / design constraint.**

THUG2 appends a trailing symbol/name table. We reproduce the **original's exact order**
verbatim (via the `__register_checksums__` directive) — byte-faithful for round-trips
and value-mods. But for a mod that introduces a **brand-new** symbol not in the
original table, we cannot compute its correct slot, because the order is a Neversoft
hash-bucket dump whose hash we have not reproduced (no simple modulo/mask matches).
Note: it is also not confirmed whether table order matters at runtime at all — the
one data point suggesting it did was later traced to an unrelated broken save file.

## 5. Engine is out of scope
The game **engine** (`THUG2.exe`: rendering, physics, the QB interpreter, low-level
systems) is compiled C++ and can only be disassembled, never recovered as source.
Only the **script layer** (`.qb` files) is decompilable to NeverScript.

## Pre-existing repo issues (untouched, do not affect the `ns` binary)
- `compiler/tests` has two `main`s (won't `go test`).
- The unused `newcompiler` package has a `Sprintf` build error.
- The consistency test requires `roq.exe`.
