// NOT ORIGINAL - the command lines of the tools ported from Python's argparse (docs/typescript.md: a port keeps its
// CLI): a command (one of `choices`), more positionals, `--name value` / `--name=value` options, flags, and lists
// (argparse's nargs="*": the values up to the next option). Options may be shortened to a unique prefix, as argparse
// allows. Errors go to stderr with exit code 2, as argparse's.

import { pyExit } from "./py.ts";

export type OptionKind = "string" | "flag" | "list";

export interface OptionSpec {
    kind: OptionKind;
    default?: string; // (a string option's default; a flag's is false, a list's [])
}

export interface ParsedArgs {
    command: string;
    positionals: string[];
    strings: Map<string, string | undefined>;
    flags: Map<string, boolean>;
    lists: Map<string, string[]>;
}

function fail(prog: string, usage: string, message: string): never {
    process.stderr.write(`usage: ${prog} ${usage}\n${prog}: error: ${message}\n`);
    pyExit(2);
}

export function parseConverterArgs(
    prog: string,
    choices: readonly string[],
    options: Readonly<Record<string, OptionSpec>>,
    argv: readonly string[] = process.argv.slice(2),
): ParsedArgs {
    const names = Object.keys(options);
    const usage = `[-h] ${names.map((n) => `[--${n}${options[n]!.kind === "flag" ? "" : " ..."}]`).join(" ")} {${choices.join(",")}} ...`;
    const strings = new Map<string, string | undefined>();
    const flags = new Map<string, boolean>();
    const lists = new Map<string, string[]>();
    for (const [name, spec] of Object.entries(options)) {
        if (spec.kind === "string") strings.set(name, spec.default);
        else if (spec.kind === "flag") flags.set(name, false);
        else lists.set(name, []);
    }
    const positionals: string[] = [];
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]!;
        if (arg === "-h" || arg === "--help") {
            process.stdout.write(`usage: ${prog} ${usage}\n`);
            pyExit(0);
        }
        if (!arg.startsWith("--") || arg === "--") {
            if (arg !== "--") positionals.push(arg);
            continue;
        }
        const eq = arg.indexOf("=");
        const given = arg.slice(2, eq < 0 ? undefined : eq);
        const matches = names.includes(given) ? [given] : names.filter((n) => n.startsWith(given));
        if (matches.length !== 1) {
            fail(
                prog,
                usage,
                matches.length
                    ? `ambiguous option: --${given} could match ${matches.map((m) => `--${m}`).join(", ")}`
                    : `unrecognized arguments: ${arg}`,
            );
        }
        const name = matches[0]!;
        const spec = options[name]!;
        if (spec.kind === "flag") {
            if (eq >= 0) fail(prog, usage, `argument --${name}: ignored explicit argument '${arg.slice(eq + 1)}'`);
            flags.set(name, true);
        } else if (spec.kind === "string") {
            let value: string | undefined;
            if (eq >= 0) value = arg.slice(eq + 1);
            else if (i + 1 < argv.length && !argv[i + 1]!.startsWith("-")) value = argv[++i];
            if (value === undefined) fail(prog, usage, `argument --${name}: expected one argument`);
            strings.set(name, value);
        } else {
            const values: string[] = eq >= 0 ? [arg.slice(eq + 1)] : [];
            while (i + 1 < argv.length && !argv[i + 1]!.startsWith("-")) values.push(argv[++i]!);
            lists.set(name, values);
        }
    }
    const command = positionals.shift();
    if (command === undefined) fail(prog, usage, "the following arguments are required: command");
    if (!choices.includes(command)) {
        fail(
            prog,
            usage,
            `argument command: invalid choice: '${command}' (choose from ${choices.map((c) => `'${c}'`).join(", ")})`,
        );
    }
    return { command, positionals, strings, flags, lists };
}
