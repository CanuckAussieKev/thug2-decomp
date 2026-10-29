# QB to JavaScript values and call contract

This is the integration contract for issues #1, #2, #3 and #4 in this fork.
Version 1 describes the implemented scalar milestone at `6591353`.
Version 2 selects the interfaces for the next implementation; those interfaces
are **not implemented by this document**. Existing format-1 output continues to
use the scalar runtime until the integrator enables the version-2 path.

The baseline is upstream `a9b4780`. Behavior references are
[struct.h](../src/game/script/struct.h),
[struct.cpp](../src/game/script/struct.cpp),
[parse.cpp](../src/game/script/parse.cpp),
[eval.h](../src/game/script/eval.h),
[eval.cpp](../src/game/script/eval.cpp),
[load.cpp](../src/game/script/load.cpp) and
[script-runtime.md](script-runtime.md). This is a selected port interface, not
an official Neversoft specification or a declaration of complete compatibility.

## Version 1 remains supported

Implemented values are `{type: 1 | 2 | 13, bits: uint32}`. Parameters are ordered
arrays of `[nameChecksum, value]`. An absent value is `undefined`, not zero,
`null`, false or a type-0 component. Checksums and raw bits are integral JS
Numbers in `[0, 4294967295]`; signed integer interpretation is `bits | 0`.

The existing `qb.module({format:1, source, scripts})`, `qb.define(name, fn)`,
generator signature `(args, root)`, `qb.call`, `qb.frame`, `qb.step`,
`qb.merge`, `qb.assign`, `qb.fields`, `qb.get` and `qb.truth` retain their
current signatures. Calls yield through `yield*`; C++ callbacks return
`{condition, outputs, frames?}`. An unknown command fails explicitly.

Current scalar `qb.get` does not implement included-global lookup. Current
`qb.truth` rejects Name expressions because ExecuteCommand dispatches them.
Version-2 support must not silently change these existing module requirements.

## Version 2 value representation

The new standalone module exports `globalThis.qbValues` with `apiVersion: 2`.
Payloads are explicit and data-only; no PC address, C++ pointer, decoded JS
Number standing for a float32 payload, or implicit Unicode conversion is used.

| QB type | Selected runtime shape | Copy and identity |
| --- | --- | --- |
| 1 Integer | `{type:1, bits:u32}` | Copy bits; interpret signed only for the relevant operation |
| 2 Float | `{type:2, bits:u32}` | Copy float32 bits, including negative zero and NaN payloads |
| 3 String | `{type:3, bytes:number[]}` | Copy byte sequence |
| 4 LocalString | `{type:4, bytes:number[]}` | Same payload, different type |
| 5 Pair | `{type:5, coordinates:[u32,u32]}` | Copy coordinate bits |
| 6 Vector | `{type:6, coordinates:[u32,u32,u32]}` | Copy coordinate bits |
| 10 Structure | `{type:10, members:Parameters}` | Mutable container identity; ordinary component copy is deep |
| 12 Array | `{type:12, elementType:u32, elements:Value[]}` | Mutable container identity; ordinary component copy is deep |
| 13 Name | `{type:13, bits:u32}` | Raw checksum unless the operation explicitly resolves it |
| 20 Structure reference | `{type:20, target:StructureValue}` | New wrapper may share the same live target |
| 21 Array reference | `{type:21, target:ArrayValue}` | Same rule as type 20 |

Reference types 20/21 are runtime-only, produced by expression/reference
operations. They are not serialized as literals in the IR, and are not native
addresses. Their target is a strong JS reference, so a suspended generator keeps
the target alive. Freeing a parameter frame does not invalidate an independently
retained reference. Reproducing dangling-pointer reads is outside the contract.

Type 0 is reserved for an empty component/untyped array metadata, not a public
missing-value object. Types 7/8/9 are script/function symbols in the dispatcher,
not transportable data values. Unknown types, opaque pointers and null pointer
array slots fail until separately implemented and tested.

Strings store unsigned bytes before the first NUL, without a terminator. A QB
reader still advances by the entire encoded token length, including bytes after
the first NUL; unterminated/truncated payloads produce a bounded error. Display
decoding is separate. JS strings may be used for diagnostics and module labels,
not as an implicit replacement for game string bytes.

Factories copy supplied lists, check payload bounds and create a new identity
for each new container. An Array's populated elements match `elementType`;
Integer elements in a Float array are converted to float32. An empty parsed
array uses `elementType:0`. Nonnumeric mixed input is not made into a JS-style
heterogeneous array. ParseArray's sizing/population and random draws belong to
the translator's construction path, not to a one-pass array factory.

## Ownership and parameter operations

`Parameters` stays an ordered list rather than an object or Map. Multiple
components with the same checksum can coexist when their types differ.

`copy(value, "value", ctx)` follows CopyComponent(reference=0): owning
structures/arrays are copied recursively, strings and coordinates are copied,
scalars keep their bits, and existing type-20/21 references keep their target.
`copy(value, "reference", ctx)` follows CopyComponent(reference=1): owning
structures/arrays become references to the original target; other value types
follow their component-copy rules. Owning cycles in a deep copy fail with a
cycle/depth error; traversal through reference targets is bounded separately.

`merge(a,b,ctx)` returns a new parameter list with component copies, as
AppendStructure does. For each incoming field, remove earlier fields with the
same name when their types match, or when both types are Integer/Float.
For unnamed Name fields (`name == 0`), matching types replace only if their
checksum payloads also match. Append the incoming copied field at the end.
The originals remain unchanged. `append(dst,src,ctx)` applies this behavior
in place and preserves the identity of `dst` and its owning Structure.

For example, the successive fields
`[K,Integer(1)], [K,Name(9)], [K,Float(2.0)]` leave Name(9) followed by
Float(2.0). The Name did not disappear. Unnamed Name(10), Name(11), Name(10)
leave Name(11), Name(10). Missing fields are omitted; an explicit Integer(0)
is retained. Host-specific defaults are supplied by the host, not the lookup.

A normal parameter read uses FindLastComponent followed by a value copy.
An expression parameter read takes the found component, resolves a Name in
that context and applies MakeReference. Thus a normal call receives its own
owning container copy, whereas an explicit expression reference can observe
mutations to the same target across a wait. A deep copy has a different target.
Passing an already referenced component does not magically make it owning.

Version 2 must test those distinct paths. The archived immutable value model
is a source of test cases, not the authority for identity or mutability.

## Symbols and lookup rules

Every operation receives an explicit context:

```ts
type Site = {source:string, script:number, offset:number};
type CallableSymbol = {kind:"callable", type:7|8|9, name:number};
type Context = {
    site: Site;
    owner: number; // Nonzero task ID; 0 during module loading.
    symbol: (checksum:number) => Value | CallableSymbol | undefined;
    charge: (work:number) => void;
    limits: {depth:number, nodes:number};
};
```

`symbol` returns the current stored value/descriptor without following aliases
or copying. It must not execute a script. Callable descriptors are registry
metadata, not data Values or PC addresses. Runtime-owned registration provides it;
the values module has no hidden global symbol table. Proposed `qb.context(site)`
supplies a context and charges the current task or bounded module-load budget.
Its charge callback captures that budget's owner, so a retained context cannot
charge a different task merely because another generator has since run.

`lookupGlobal(name,ctx)` implements LookUpSymbol alias following. A missing
root is `undefined`. An alias whose target is missing returns the last Name
value, as the original does. An alias cycle or excessive depth raises an error.
`resolve(value,ctx)` leaves non-Names unchanged and uses that lookup for Names,
retaining the original Name when no root exists or the symbol is callable, as
ResolveNameComponent does. Operations such as equality that instead interpret
callable symbol payloads in the original must reject that case until separately
implemented; they must not accidentally compare the preserved checksum instead.
Resolution occurs only where
the source operation requests it. Plain names in checksum arrays, GetChecksum
queries and a structure member's selector remain raw checksums.

`lookupTyped(params,name,type,ctx)` follows CStruct::Search: retain the last
correct match, convert Integer to Float when Float is requested, resolve a
matching Name as a global candidate, and traverse included global structures
only through unnamed Name components on the nonmatching-name path. A later
wrong-type field does not erase a prior match. An exact Name-type request keeps
its raw Name; resolution is for a Name standing for another requested type.
There is no blanket reference
tag normalization in this getter.

`readChecksum(params,name,ctx)` implements the specialized GetChecksum:
matching Name fields are raw. For an unnamed request, skip a Name that resolves
to a global Structure; do not expand that component as a matching value.
Nonmatching unnamed structure inclusions recurse. `readString` first performs
the entire String search and only if absent performs LocalString search; it
does not just pick the last of either kind. `contains` follows FindComponent's
presence/unnamed-flag behavior, not JS truthiness or last-value lookup.

Normal module loading follows source definition order. Version 2 permits
ordinary later definitions to replace earlier definitions when no tasks are
retained; aliases remain Names until a lookup requires them. Load-time
expressions use symbols available at that point. Hot reload, temporary symbol
stacks and unload while tasks run are deferred. Format-1 duplicate rejection
remains in place. Loading is not transactional; discard a runtime on load failure.

## Proposed values helpers

All listed helpers are version-2 work for #2; names and argument order are
reserved here. Validation/type failures carry `ctx.site` where a context exists.

| Helper | Result and policy |
| --- | --- |
| `integer(bits)`, `float(bits)`, `name(bits)` | Validate u32 and produce a distinct scalar type |
| `string(bytes,local=false)`, `pair(coords)`, `vector(coords)` | New copied payload, preserving byte/bit representation |
| `structure(fields,ctx)`, `array(elementType,elements,ctx)` | New owning container; copied, checked contents |
| `copy(value,mode,ctx)` | Component copy; mode is `"value"` or `"reference"` |
| `merge(a,b,ctx)`, `append(dst,src,ctx)` | Pure merge or in-place append with typed replacement |
| `param(params,name,mode,ctx)` | FindLastComponent; mode is `"value"` or `"expression"`; absent returns undefined |
| `lookupGlobal(name,ctx)`, `resolve(value,ctx)` | Explicit alias lookup and Name resolution |
| `global(name,ctx)` | Expression name: resolved data, owning containers made references; absent root yields Name(name); callable descriptors reject until expression calls are implemented |
| `lookupTyped(params,name,type,ctx)` | Last matching typed value, with source-specific promotion/inclusion rules |
| `readChecksum(params,name,ctx)`, `readString(params,name,ctx)` | Specialized getters described above; absent returns undefined |
| `contains(params,name,ctx)` | Presence boolean, including unnamed flags and included structures |
| `binary(op,a,b,ctx)` | Operation identified by original EOperator numeric token; unsupported combinations fail |
| `statementTruth(value,ctx)` | Number's raw word is nonzero; Name requests dispatch and currently fails; valid strings/coordinates/containers have a nonnull payload and are true |

`binary` covers array indexing as op 5 and member selection/dot product as op 8.
A type-21 array produces reference container elements; a type-12 array produces
copies. A type-20 structure's selected member is copied in reference mode.
LocalString array indexing remains unsupported until source-compatible behavior
is explicitly implemented. Bounds apply even when an original low-level getter
would ignore a singleton index. Arithmetic delegates to eval.cpp behavior, not
to a generated JS operator.

Operator IDs are 5 index, 7 equality, 8 dot/member, 10 subtract, 11 add,
12 divide, 13 multiply/cross product, 18 less, 20 greater, 50 OR, 51 AND.
Integer operations retain uint32 results; floating operations round at the
source's float32 stores. Numerical precision cases require independent tests;
Number arithmetic alone is not an x87 compatibility claim. GetBool is different
from statementTruth: it keeps an Integer's word, maps nonzero Float to 1 and
other types to 0. AND/OR evaluate both inputs and combine GetBool results
bitwise; they are not JS short-circuit operators.

Equality resolves Names as ComponentsEqual does. Numbers compare numerically;
String and LocalString require matching types and bytewise C-string equality;
coordinates compare component float values. Owning type-10/12 values compare
the same container identity, not structural contents. Reference tags 20/21
take ComponentsEqual's default-false path, even for the same target. Structure
less/greater use subset rules; strings compare lengths. Implemented subsets
must state omissions rather than substituting deep equality or JS `===`.

## Proposed translator IR and emitted calls

Keep format-1 IR unchanged. Version-2 IR uses a module
`{format:2, source, definitions:Definition[]}`. Definitions preserve binary
source order and are either existing script records with `kind:"script"`, or
`{kind:"global", name, value:Expr, offset}`. All runtime values in IR are literal
data; no live reference/identity is serialized. Every statement has its original
byte offset. Inline values may also retain offsets for finer diagnostics.

Expression variants are the existing `literal` and `parameter`, plus:

```ts
{kind:"parameter", name, mode:"value" | "expression"}
{kind:"global", name}
{kind:"allParameters"}
{kind:"binary", op, left:Expr, right:Expr}
{kind:"structure", fields:Field[]}
{kind:"array", items:Expr[]}
```

A literal Name is always raw. The parser emits `global` where ParseExpression
would look up a Name; after-dot selectors stay literal Names. An array-literal
node denotes QB's parsing/construction operation, not a promise that arbitrary
expressions are allowed as original literal-array elements. Random/control-flow
inside arrays needs a future representation preserving both passes. Until then,
reject such arrays, never flatten them into a single random draw.

Keep existing call/assignment/return/if statement forms. Added forms are
`{kind:"loop",offset,body,repeatOffset,repeatFields:Field[]}` for begin/repeat and
`{kind:"break",offset,target}` where target is the innermost local begin offset. Preserve
the complete repeat argument list: the runtime parses it on the first repeat visit,
then gets the unnamed Integer after typed argument merging. A missing Integer
means unbounded repetition; zero and negative counts retain uint32 countdown
semantics. On break, the repeat arguments are parsed without caller parameters
and discarded. Emission must preserve that distinction rather than drop them.
The initial reader supports at most ten lexically nested loops and rejects breaks
outside a local loop; the original's shared cross-call loop stack needs additional
runtime support. A switch is
`{kind:"switch",offset,fields:Field[],cases:{labels:{offset,fields:Field[]}[],body:Statement[]}[],fallback?:Statement[]}`.
Switch and case selectors are the first component after argument parsing/typed
merging, not necessarily the first syntactic value. An empty selector remains
representable. Labels in each group preserve source order: after a match, later
grouped labels still parse their arguments even though equality is not tested.
An absent fallback means no default; an empty fallback is a present empty default.
The initial reader requires structured compiled cases and exits targeting the
matching endswitch; arbitrary statements before the first case are unsupported.
The reader validates original branches, repeat boundaries and grouped-case
targets before constructing these nodes. Unsupported control flow stays an error.

Version-2 generated code starts with a required feature manifest:

```js
qb.module({format:2, source:"example.qb", scripts:[], requires:{values:2}});
// Only add requires.random = 1 for code that actually uses qbRandom.
```

The integrator extends `qb.module` to check exact supported format/helper API
versions before registrations run. A missing helper or unsupported version
fails immediately. `qb.defineGlobal(name,value,site)` is a proposed coordinator
hook; definitions call it in original order. `qb.define(name,fn)` keeps its
signature, with format-2 replacement policy during the load phase only.

Examples of reserved emitted calls, after those helpers are integrated:

```js
const ctx = qb.context({source:"example.qb", script:100, offset:42});
const x = qbValues.param(p, 200, "value", ctx);        // Copy for a plain argument.
const ref = qbValues.param(p, 201, "expression", ctx); // Expression borrow.
const item = qbValues.binary(5, ref, qbValues.integer(0), ctx);
const args = x === undefined ? [] : qbValues.merge([], [[300, x]], ctx);
yield* qb.call(400, args, p, ctx.site);
```

The argument builder omits missing values before passing fields into merge;
an absent expression operand errors. A separate example of typed identity:
`copy(structureValue,"value",ctx)` makes a new container; reference-mode copying
points at the original. Appending a new field through that live target is seen
through all references, but not by the previous deep copy.

`qb.call` remains the sole generator dispatch path. It merges explicit callee
return fields, isolates owning locals, and returns true for a QB-call condition
after that callee completes. A host can return either condition. Call-site
exceptions keep original source/CRC/offset. Member dispatch, expression host
calls, interrupts and event-driven continuations are deferred dispatcher work.

## Proposed random module

Issue #4 owns `globalThis.qbRandom`, `apiVersion:1`, with
`create(seedBits=0)` returning one engine-wide RNG instance, shared across tasks.
The coordinator-owned `qb.random()` accessor returns that instance; it is
created once per Runtime, not once per start/update. Generated random calls
use `ctx.owner` and `ctx.charge`. Stored choices during module loading (owner 0)
remain unsupported until their source behavior is tested separately.
The following methods reserve the interface; stream is the integer 0 or 1:

| Method | Result |
| --- | --- |
| `raw(stream=0)` | Advance only that stream; return its unsigned 16-bit sample |
| `scale(sample,rangeBits)` | Pure original signed-wrap Scale; return uint32 result bits |
| `draw(rangeBits,stream=0)` | raw plus scale; return uint32 bits, not a JS negative integer |
| `state(stream=0)` | Copied `{add,mul,value}` uint32 state, not a mutable internal object |
| `seed(seedBits)` | Reinitialize both streams; retain stored-choice history |
| `range(aFloatBits,bFloatBits,second=false,charge)` | `{type:1|2,bits:u32}`; endpoints are float32 bits |
| `choose(opcode,site,weights,owner,charge)` | Zero-based index for 0x2f/0x37/0x40/0x41 |
| `forget(owner)` | Mark this owner's stored records reusable with owner 0; retain their history |
| `clearStored()` | Explicit new reset of stored history for teardown/tests |

Owner is a nonzero uint32 task identity, never a PC pointer; 0 is reserved for
orphaned reusable records. The key uses script checksum, original byte offset,
opcode and choice count, plus owner/reusable state. Source is diagnostic.
The coordinator invokes forget at task destruction/cancellation, as original
script destruction does. Stream draws and work charge happen in source order.
No hidden Math.random or per-task stream reseeding is allowed.

Weights retain uint16 bits; unsafe negative weights/zero totals fail explicitly.
Stored history is capped at 100 records, oldest insertion evicted, not LRU.
Permute accepts the source-safe 2..66 choice range. Retry, initial-index,
reuse, rounding and stream-consumption quirks require original-function vectors.
Array construction calls this same instance during both sizing and population.
The module does not itself schedule generators or choose a JS array literal's
inference/population paths. NaN/Inf and unsupported unsafe cases fail explicitly.

## C++ boundary and limits

The existing `thug2::js::Value {type,bits}` transport remains **scalar only**.
Do not pack JS object identities or byte buffers into `bits` or change that C++
layout in tasks #2/#3/#4. Rich JS values stay within QuickJS until a separate
coordinator-owned transport change defines containers, references, ownership,
resource accounting and failure cleanup on both sides. Rich values reaching the
current host boundary must error. Runtime snapshots currently have that same
scalar-only limit; an end-to-end rich-value task requires explicit bridge work.

Contexts enforce depth 128 and at most 250,000 visited value nodes per operation,
with shared task/load work accounting and cycle guards. Parameter lists keep
the existing 4,096-field cap. A helper reports `syntax`, `unsupported`, `type`,
`missing`, `cycle`, `arithmetic` or `limit` with a Site; C++ snapshots may continue
exposing only the rendered message until the coordinator adds typed diagnostics.
Source offsets and limits are explicit new safety behavior, not original ABI.

Keep existing 4 MiB inputs/source, 100,000 IR nodes, 64 conditional-block depth,
32 call depth and 256 retained-task limits. JS memory/interrupt caps still apply
and cannot preempt an arbitrary C++ callback. Unsafe pointer reads, allocation
overruns, unbounded alias loops and uninitialized results are not reproduced.

## File ownership and integration order

| Helper or integration surface | Owner | Files | Required check |
| --- | --- | --- | --- |
| Contract | #1 coordinator | `docs/qb-js-contract.md` | Source review and representative examples |
| All proposed `qbValues` APIs | #2 | `scripting/values.js`, `scripting/tests/values.test.js` | QuickJS self-test plus scalar regressions |
| Reader, IR, emitter | #3 | `tools/qbjs/translate.ts`, `fixtures.ts`, `translate.test.ts`, new `reader.ts`, `ir.ts`, `emit.ts` as needed | Type/style, truncations, offsets, deterministic output |
| `qbRandom` and its instance methods | #4 | `scripting/random.js`, `scripting/tests/random.test.js`, `tools/verify_js_random.py` as needed | Original vectors, both streams after each operation |
| Existing scalar transport and embedding safety | #5 | `scripting/src/runtime.cpp`, `include/thug2/js_runtime.hpp`, CMake/workflow, `tests/runtime_tests.cpp` | Windows/Linux, JS ownership/error/limit checks |
| New module wiring, context, globals, format-2 dispatch, `qb.random()`, rich bridge | #6 coordinator | `scripting/runtime.js`, CMake/header generation and future bridge changes | Integrated QuickJS and genuine QB helper |

The #5 owner controls CMake/embedding while that task runs. Tasks #2/#4 deliver
standalone JS modules/self-tests without editing those files; self-tests can be
concatenated with generated scalar fixtures and passed to `qb-js-tests` locally.
The coordinator wires modules after the overlapping platform work is accepted.
For #3, reader/IR checks can finish before runtime integration; generated rich
code is not claimed executable until its required helpers/context are wired.

The order is contract, independent values/translator/RNG/platform commits,
then coordinator wiring, richer transport when needed, and one real QB helper.
Current frame waits and ordinary calls remain regressions throughout. Object
handles, member/event dispatch, interrupts, spawned-script traversal and renderer
integration require later contracts and tests. A missing capability is reported,
not replaced with a success stub.
