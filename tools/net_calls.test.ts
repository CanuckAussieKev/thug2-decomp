// NOT ORIGINAL - where Winsock may be seen (docs/architecture.md "The network transport seam"): only the Winsock
// network backend (src/net_winsock) includes Winsock's headers, calls its functions and links ws2_32, and only app/
// installs it. The engine's network library, the online session and WinMain reach sockets and address lookups through
// net::Transport (engine/net/transport.h). The GameSpy SDK keeps its own sockets: its glue (platform/gamespy.cpp)
// names Winsock's types where the SDK's functions take them (and its check, src/tests/gamespy_check.cpp, runs fake
// servers on the loopback), and the SDK's CMake (cmake/gamespy_sdk.cmake) links ws2_32 for the SDK.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const SRC = join(ROOT, "src");

// A source file's text without its comments (a "// (inet_addr)" note isn't a call).
function code(text: string): string {
    return text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
}

// The files under src/ (relative to src/, / separators) named by `wanted` whose code matches `pattern`.
function using(pattern: RegExp, wanted: RegExp = /\.(cpp|h)$/): string[] {
    const found: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (wanted.test(entry.name) && pattern.test(code(readFileSync(path, "utf8"))))
                found.push(relative(SRC, path).replaceAll("\\", "/"));
        }
    };
    walk(SRC);
    return found.sort();
}

const BACKEND = "net_winsock/";
// The GameSpy SDK's glue: Winsock's types where the SDK's functions take them (the game's socket handed to the SDK,
// the SDK's addresses handed back).
const SDK_GLUE = "platform/gamespy.cpp";
// The glue's check: the fake servers it runs the SDK against, on the loopback addresses (tests/gamespy_check.cpp).
const SDK_GLUE_CHECK = "tests/gamespy_check.cpp";

const WINSOCK_HEADERS = /^\s*#\s*include\s*<(winsock2?|ws2tcpip|ws2def|mswsock|ws2ipdef)\.h>/im;
// Winsock's functions and the types and constants of its C interface.
const WINSOCK_CALLS = new RegExp(
    String.raw`\b(WSA[A-Z]\w*|closesocket|ioctlsocket|sendto|recvfrom|recv|setsockopt|getsockopt|getsockname|` +
        "getpeername|gethostbyname|gethostbyaddr|gethostname|inet_addr|inet_ntoa|inet_pton|inet_ntop|" +
        String.raw`getaddrinfo|freeaddrinfo|htons|ntohs|htonl|ntohl)\s*\(|` +
        String.raw`\b(SOCKET|sockaddr_in|sockaddr|hostent|in_addr|INVALID_SOCKET|SOCKET_ERROR|INADDR_ANY|INADDR_NONE|` +
        String.raw`AF_INET|SOCK_DGRAM|SOCK_STREAM|IPPROTO_UDP|FIONBIO|SOL_SOCKET|SO_BROADCAST)\b`,
);

test("only net_winsock/ includes Winsock's headers", () => {
    const users = using(WINSOCK_HEADERS);
    assert.deepEqual(
        users.filter((name) => !name.startsWith(BACKEND)),
        [],
    );
    assert.ok(users.includes("net_winsock/winsock_transport.cpp"), "net_winsock/winsock_transport.cpp includes them");
});

test("only net_winsock/ (and the GameSpy SDK's glue, for the SDK's types) uses Winsock", () => {
    const users = using(WINSOCK_CALLS);
    assert.deepEqual(
        users.filter((name) => !name.startsWith(BACKEND) && name !== SDK_GLUE && name !== SDK_GLUE_CHECK),
        [],
    );
    assert.ok(users.includes("net_winsock/winsock_transport.cpp"), "net_winsock/winsock_transport.cpp makes the calls");
});

test("only net_winsock/ links ws2_32 (the GameSpy SDK links it for itself)", () => {
    // (a CMake comment may name it: only a link counts)
    const linkers = using(/\btarget_link_libraries\([^)]*\bws2_32\b/, /^CMakeLists\.txt$/);
    assert.deepEqual(linkers, ["net_winsock/CMakeLists.txt"]);
    assert.match(
        readFileSync(join(ROOT, "cmake", "gamespy_sdk.cmake"), "utf8"),
        /\btarget_link_libraries\(gamespy_sdk[^)]*\bws2_32\b/,
    );
});

test("only app/ installs a transport and picks Winsock's", () => {
    const pickers = using(/^\s*#\s*include\s*"net_winsock\/winsock_transport\.h"/m);
    assert.deepEqual(
        pickers.filter((name) => !name.startsWith("app/") && name !== "net_winsock/winsock_transport.cpp"),
        [],
    );
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the transport");
    const installers = using(/\binstall_transport\s*\(/);
    assert.deepEqual(
        installers.filter((name) => name !== "engine/net/transport.h" && name !== "engine/net/transport.cpp"),
        ["app/main.cpp"],
    );
});
