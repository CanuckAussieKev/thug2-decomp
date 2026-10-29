// NOT ORIGINAL - format-2 translation IR. No runtime references or PC pointers.
export type ScalarLiteral = { type: 1 | 2 | 13; bits: number };
export type Literal = ScalarLiteral | { type: 3 | 4; bytes: number[] } | { type: 5 | 6; coordinates: number[] };
export type Expr =
    | { kind: "literal"; offset: number; value: Literal }
    | { kind: "parameter"; offset: number; name: number; mode: "value" | "expression" }
    | { kind: "global"; offset: number; name: number }
    | { kind: "allParameters"; offset: number }
    | { kind: "binary"; offset: number; op: number; left: Expr; right: Expr }
    | { kind: "structure"; offset: number; fields: Field[] }
    | { kind: "array"; offset: number; items: Expr[] };
export type Field = { name: number; value: Expr; offset: number } | { spread: true; offset: number };
export type Call = { kind: "call"; offset: number; name: number; fields: Field[] };
export type SwitchLabel = { offset: number; fields: Field[] };
export type Statement =
    | Call
    | { kind: "assign"; offset: number; name: number; value: Expr }
    | { kind: "return"; offset: number; fields: Field[] }
    // Preserve the complete repeat argument list: GetInteger(0) is applied
    // after typed argument merging, not to the first syntactic argument.
    | { kind: "loop"; offset: number; body: Statement[]; repeatOffset: number; repeatFields: Field[] }
    | { kind: "break"; offset: number; target: number }
    | {
          kind: "switch";
          offset: number;
          fields: Field[];
          cases: { labels: SwitchLabel[]; body: Statement[] }[];
          fallback?: Statement[];
      }
    | { kind: "if"; offset: number; condition: Call | Expr; yes: Statement[]; no: Statement[] };
export type Definition =
    | { kind: "script"; offset: number; name: number; defaults: Field[]; body: Statement[] }
    | { kind: "global"; offset: number; name: number; value: Expr };
export type ModuleIR = { format: 2; source: string; definitions: Definition[] };
