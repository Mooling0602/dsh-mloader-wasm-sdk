# dsh-mloader WASM SDK

Multi-language SDKs for WebAssembly plugins hosted by [dsh-mloader](https://github.com/Mooling0602/dsh-mloader), the multi-runtime plugin loader for DeepSeek Harness.

The WebAssembly ABI is language neutral. A plugin is a WebAssembly module that exports five functions plus `memory` and exchanges UTF-8 JSON through linear memory. Each directory in `src/` is a thin implementation of that contract for one language, and every implementation ships the same acceptance example.

## Languages

| Language | Directory | Package form | Needs WASI | Status |
| --- | --- | --- | --- | --- |
| Rust | `src/rust_sdk` | Cargo crate | no | ready |
| Zig | `src/zig_sdk` | source module | no | ready |
| Go | `src/go_sdk` | Go module | reactor | ready |
| C | not yet | source module | no | planned |

All three implementations target core ABI version 1 and pass the same conformance suite. The Go implementation is a WASI reactor, so it needs a dsh-mloader build that initializes a reactor through `_initialize` before the version check.

## Requirements

- Rust stable with the `wasm32-unknown-unknown` target for the Rust SDK.
- Zig 0.17 for the Zig SDK.
- Go 1.24 or newer for the Go SDK.
- A wasm linker. The Rust example needs `lld`.
- Node.js 20 or newer for the conformance suite.
- dsh with dsh-mloader installed and the WebAssembly backend enabled through `wasm.enabled`.

The repository ships `flake.nix`, so `nix develop` provides every tool above without changing the system.

## Development environment

```bash
nix develop
```

The shell provides `rustc`, `cargo`, `rustfmt`, `clippy`, `lld`, `go`, `zig`, `nodejs` and `wabt`, including the `wasm32-unknown-unknown` standard library.

## Conformance suite

Every example declares the same three tools, `sdk_add`, `sdk_upper` and `sdk_check`, and subscribes to `tools/execute`. One suite drives any compiled example:

```bash
./src/rust_sdk/example/build.sh
node conformance/abi.mjs --wasm src/rust_sdk/example/dist/wasm-sdk-example/wasm_sdk_example.wasm

./src/zig_sdk/example/build.sh
node conformance/abi.mjs --wasm src/zig_sdk/example/dist/wasm-sdk-example/wasm_sdk_example.wasm

./src/go_sdk/example/build.sh
node conformance/abi.mjs --wasm src/go_sdk/example/dist/wasm-sdk-example-go/wasm_sdk_example.wasm
```

The suite checks the host import boundary, the six core exports, the startup shape, the ABI version, the declaration, tool calls, error replies, malformed requests, event delivery and result buffer reuse. It builds no module of its own; it runs whatever `.wasm` path it is given.

Install at most one example plugin at a time. They share tool names, so a second installed example would collide in the tool registry.

## Per-language guides

- [Rust SDK](src/rust_sdk/README.md)
- [Zig SDK](src/zig_sdk/README.md)
- [Go SDK](src/go_sdk/README.md)

## Contract documents

The contract is owned by the loader repository. The WebAssembly specific parts are in [docs/wasm/WasmABI.md](https://github.com/Mooling0602/dsh-mloader/blob/main/docs/wasm/WasmABI.md), and the shared plugin contract is in [docs/ABI-unified.md](https://github.com/Mooling0602/dsh-mloader/blob/main/docs/ABI-unified.md).

## Repository layout

| Path | Holds |
| --- | --- |
| `conformance/abi.mjs` | language neutral ABI conformance suite |
| `src/rust_sdk` | Rust crate, build helpers and example |
| `src/zig_sdk` | Zig module, build script and example |
| `src/go_sdk` | Go module, build script and example |
| `flake.nix`, `flake.lock` | development shell and pinned nixpkgs input |
| `CONTEXT.md` | technical detail, build recipes and invariants |
| `AGENTS.md` | coding rules for AI agents and maintainers |

## Status

The two native-target SDKs, Rust and Zig, produce modules without host imports. The Go SDK produces a WASI reactor, which the loader initializes before the ABI version check. None of the three packages is published yet; consume them from this repository until a release is cut.

## License

MIT.
