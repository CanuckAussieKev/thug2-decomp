// NOT ORIGINAL - format-2 translation IR. No runtime references or PC pointers.
export type ScalarLiteral = { type: 1 | 2 | 13; bits: number };
export type Literal = ScalarLiteral | { type: 3 | 4; bytes: number[] } | { type: 5 | 6; coordinates: number[] };
export type Expr =
    | { kind: "literal"; offset: number; value: Literal }
    | { kind: "expressionResult"; offset: number; value: Expr }
    | { kind: "parameter"; offset: number; name: number; mode: "value" | "expression" }
    // A name in an expression: a global's value - or, when it names a C function, the function called there
    // (push_name), with the arguments that follow it (functionCall) or none (global); its result 0 / 1.
    | { kind: "global"; offset: number; name: number }
    | { kind: "functionCall"; offset: number; name: number; fields: Field[] }
    // <...>: in an expression a live reference to the parameters; as a value (copy) a structure copied from them
    // (parse_value / add_parameter's kAllArguments).
    | { kind: "allParameters"; offset: number; copy?: true }
    | { kind: "binary"; offset: number; op: number; left: Expr; right: Expr }
    | { kind: "randomRange"; offset: number; a: number; b: number; second: boolean }
    // twice: drawn twice, the first pick dropped - it follows a bare name in a field list, whose look-ahead for '='
    // (parse_arguments / parse_structure) resolves it once before it is read
    | { kind: "randomChoice"; offset: number; opcode: number; weights: number[]; choices: Expr[]; twice?: true }
    | { kind: "structure"; offset: number; fields: Field[] }
    | { kind: "array"; offset: number; items: Expr[] }
    // A script as a value (a structure's `script name ... endscript`: the CAS tables' part scripts) - the field is
    // named by `name`; the C++ runs it (ModelAssembler::launch_script).
    | { kind: "inlineScript"; offset: number; name: number; defaults: Field[]; body: Statement[] };
// A Random among argument runs (resolve_random_jumps between parse_arguments' fields): one branch's fields used.
export type RandomFields = {
    kind: "randomFields";
    offset: number;
    opcode: number;
    weights: number[];
    branches: Field[][];
    twice?: true; // (as randomChoice's)
};
export type Field =
    | { name: number; value: Expr; offset: number; flattenStructure?: true }
    | { spread: true; offset: number }
    | RandomFields;
// via: the command's name is read from a parameter (`<script> args`; 0 for `<...> args`: the parameters' unnamed
// name) when the statement runs (read_name -> read_hash_value); name is then 0.
// on: `object:Command` - the object's name, written (name) or read from a parameter like via (via).
export type NameRef = { name: number } | { via: number };
// byExpr: `(expression) args` - the expression's result is called when it is a name, else only its truth taken.
export type Call = {
    kind: "call";
    offset: number;
    name: number;
    fields: Field[];
    via?: number;
    on?: NameRef;
    byExpr?: Expr;
    // (expression):Command - the object's name computed (onExpr)
    onExpr?: Expr;
};
export type SwitchLabel = { offset: number; fields: Field[] };
export type Assign = { kind: "assign"; offset: number; name: number; value: Expr };
export type Statement =
    | Call
    | Assign
    | { kind: "return"; offset: number; fields: Field[] }
    // Preserve the complete repeat argument list: GetInteger(0) is applied
    // after typed argument merging, not to the first syntactic argument.
    | { kind: "loop"; offset: number; body: Statement[]; repeatOffset: number; repeatFields: Field[] }
    | { kind: "break"; offset: number; target: number }
    // A Random among statements (resolve_random_jumps before a statement): one branch's statements run.
    | { kind: "randomBlock"; offset: number; opcode: number; weights: number[]; branches: Statement[][] }
    | {
          kind: "switch";
          offset: number;
          fields: Field[];
          cases: { labels: SwitchLabel[]; body: Statement[] }[];
          fallback?: Statement[];
      }
    // negate: `if NOT ...` (0x39) - the statement's result inverted (run_conditional).
    // condition: an assignment there (`if x = 1`) runs and counts as true.
    | { kind: "if"; offset: number; condition: Call | Expr | Assign; negate?: true; yes: Statement[]; no: Statement[] };
export type Definition =
    | { kind: "script"; offset: number; name: number; defaults: Field[]; body: Statement[] }
    | { kind: "global"; offset: number; name: number; value: Expr };
// names: the module's checksum-name records (0x2B), checksum -> spelling; the game
// ignores them, the readable emitter uses them.
export type ModuleIR = { format: 2; source: string; definitions: Definition[]; names?: Record<number, string> };
