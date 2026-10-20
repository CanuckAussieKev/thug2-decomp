// NOT ORIGINAL - command-line parsing for the tools that used Python's argparse: node:util parseArgs plus argparse's
// manners - `-h` / `--help` prints the usage and exits 0, an unknown or malformed option prints the usage and the
// error to stderr and exits 2 (no stack trace).
//
//   const { values, positionals } = parseCli(USAGE, { options: {...}, allowPositionals: true });

import { type ParseArgsConfig, parseArgs } from "node:util";

export function parseCli<T extends ParseArgsConfig>(usage: string, config: T): ReturnType<typeof parseArgs<T>> {
    const args = config.args ?? process.argv.slice(2);
    if (args.includes("-h") || args.includes("--help")) {
        process.stdout.write(`${usage}\n`);
        process.exit(0);
    }
    try {
        return parseArgs(config);
    } catch (e) {
        process.stderr.write(`${usage}\nerror: ${e instanceof Error ? e.message : String(e)}\n`);
        process.exit(2);
    }
}
