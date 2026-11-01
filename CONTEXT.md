# Context

Technical detail for working on the dsh-mloader WebAssembly SDK. The user facing introduction is in [README](README.md), and the coding rules are in [AGENTS](AGENTS.md).

## What this repository is

This repository is the plugin side of the dsh-mloader WebAssembly backend. A plugin is a WebAssembly module that exports the core ABI version 1 and exchanges UTF-8 JSON with the loader worker. The loader, its manifest schema and the normative contract live in the [dsh-mloader repository](https://github.com/Mooling0602/dsh-mloader); this repository implements the module side only for several languages.

The WebAssembly ABI is language neutral, but no single library can expose it for every language. Each directory under `src/` therefore holds one thin implementation: the ABI exports, the JSON codec, the declaration builders and the argument helpers. The implementations share the contract and the conformance suite, not their code.

The contract documents are `docs/wasm/WasmABI.md` for the WebAssembly specific parts and `docs/ABI-unified.md` for the shared operation payloads, the tool object, the JSON Schema subset and the event whitelist, both in the loader repository.

## Language matrix

| Language | Directory | Module shape | Host imports | Loader requirement |
| --- | --- | --- | --- | --- |
| Rust | `src/rust_sdk` | freestanding wasm executable from a `cdylib` plugin crate | none | none beyond the core ABI |
| Zig | `src/zig_sdk` | freestanding wasm executable, no entry point | none | none beyond the core ABI |
| Go | `src/go_sdk` | WASI reactor built with `-buildmode=c-shared` | `wasi_snapshot_preview1` | the worker initializes `_initialize` before the version check |

## Repository map

| Path | Holds |
| --- | --- |
| `conformance/abi.mjs` | language neutral ABI conformance suite |
| `src/rust_sdk/Cargo.toml` | Rust crate manifest, flat layout with `[lib] path = "lib.rs"` |
| `src/rust_sdk/lib.rs` | public API, `export_plugin!` and `schema!` macros |
| `src/rust_sdk/abi.rs` | export implementation, dispatch, reply codec, buffers |
| `src/rust_sdk/declaration.rs` | `Declaration`, `Tool`, `Handler` |
| `src/rust_sdk/arg.rs`, `src/rust_sdk/error.rs` | argument accessors and the error type |
| `src/rust_sdk/example` | Rust acceptance example, build script and manifest template |
| `src/zig_sdk/sdk.zig` | Zig module |
| `src/zig_sdk/build.zig` | builds the Zig example wasm |
| `src/zig_sdk/example` | Zig acceptance example, build script and manifest template |
| `src/go_sdk/go.mod`, `src/go_sdk/mloader.go` | Go module |
| `src/go_sdk/example` | Go acceptance example, build script and manifest template |
| `flake.nix`, `flake.lock` | development shell and the pinned nixpkgs input |
| `README.md`, `README.zh.md`, `CONTEXT.md`, `AGENTS.md`, `LICENSE`, `.markdownlint.json` | repository documents |

## The contract each SDK implements

- The host calls `mloader_alloc` and writes the request into the returned buffer. The host frees the buffer with `mloader_free` after `mloader_call` returns.
- `mloader_call` reads one UTF-8 JSON object `{"op": ..., "payload": ...}` and writes a reply, then returns its byte length. The host reads the reply at `mloader_result_ptr`. The result buffer is owned by the module and is valid until the next request.
- A reply is `{"ok": true, "result": ...}` or `{"ok": false, "error": {"message": "..."}}`. A malformed request becomes a failed reply, never a trap.
- The operations are `apply`, `tool.call` and `event`. There is no `boot` and no `dispose`; initialization is the worker handshake and stopping the plugin terminates the worker.
- `apply` returns `{"tools": [...], "events": [...]}`. A tool entry is `{"name", "description", "parameters"?, "output"?, "timeoutMs"?}`. Input and result schemas are JSON text in the loader schema subset.
- The only subscribable events are the loader whitelist: `tools/execute`, `tools/pre-execute`, `tools/post-execute`, `tools/result`, `tools/change`, `agent/status`, `agent/created`, `agent/disposed`, `session/created` and `session/disposed`. A subscription is observational.
- There is no logging channel, no cleanup registration and no host callback. A module either imports nothing or only the WASI preview1 namespace, and the loader instantiates no WASI provider for a module without WASI imports.

## Rust implementation

The crate uses `std` and `serde_json`. It is not `no_std`, because a plugin can afford the standard library and the JSON dependency saves a hand written codec.

`export_plugin!(declare)` generates the five function exports in the plugin crate and calls the plugin `declare` function once through a `OnceLock`. The result buffer is a `Mutex<Vec<u8>>` and is replaced on every request. The example builds with `lto`, `opt-level = "s"`, `panic = "abort"` and symbol stripping, and produces a module with no imports.

The flat layout keeps `src/rust_sdk/src/lib.rs` from existing: the package declares `[lib] path = "lib.rs"` and the modules sit beside it.

## Zig implementation

`sdk.zig` is a module the plugin imports as `mloader`. The plugin declares a `Declaration` and writes the five exports itself, because Zig exports belong to the compiled module and the SDK cannot generate symbols in the plugin crate.

The Zig build must use `-fno-entry` and `-rdynamic`. Without `-rdynamic` the wasm linker drops the exported plugin functions and the module exports only memory. The compiled module also exports the `__stack_pointer` global; the loader ignores exports it does not require.

The SDK uses `std.heap.wasm_allocator`, a `BrkAllocator` with size class free lists, so request buffers can be released. JSON parsing and result building use an arena per request, and the reply bytes are copied into the long lived result buffer before the arena is released.

## Go implementation

A Go module cannot be a freestanding reactor: exported functions need the Go runtime, so the module is a WASI reactor built with `GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared` and `go:wasmexport` directives. A reactor traps when any export runs before `_initialize`; the loader therefore initializes the reactor before it calls `mloader_abi_version`. A loader that only handles `_start` fails this module with `wasi.start() has not been called`.

The SDK keeps one request buffer because the worker processes one request at a time. `Alloc` stores the buffer in a package global and returns its pointer. `Dispatch` compares the host pointer with the stored buffer instead of converting the integer to a pointer, which keeps `go vet` clean and keeps the read safe. The reply buffer is another package global, valid until the next request. Handler panics are recovered and reported as failed reply frames.

The request is decoded with `encoding/json`; the decoded argument map is independent of the request buffer, so the host can free the buffer after the call.

## The conformance contract

Every language example declares exactly the same declaration and behaviour:

- three tools: `sdk_add` with description `Add two numbers` and required `a` and `b`, `sdk_upper` with required `s`, and `sdk_check` with required `value`;
- one event subscription to `tools/execute`;
- `sdk_add` with `{"a": 19, "b": 23}` returns `{"sum": 42}`;
- `sdk_upper` with `{"s": "hello wasm"}` returns `{"upper": "HELLO WASM"}`;
- `sdk_check` with a negative value returns the message `value must be non-negative`;
- a missing argument produces the message `missing required argument "s"` for `sdk_upper`;
- an unknown tool produces a message that names the tool;
- an unsupported operation and invalid JSON produce failed reply frames.

`conformance/abi.mjs` performs these steps: it checks that every import is in `wasi_snapshot_preview1` or that there is none, instantiates the module with a minimal WASI provider when the module imports WASI, calls `_initialize` for a reactor or `_start` for a command module, checks the six core exports, the ABI version and the empty result buffer, then drives `apply`, `tool.call` and `event` as the worker does. It accepts one `--wasm` path and builds no module of its own, so it runs on any language output.

Changing a tool name, a description, a required list or an error message is a change to all examples and to the suite at the same time. That keeps the language implementations comparable.

## Development environment

`flake.nix` provides a dev shell with `rustc`, `cargo`, `rustfmt`, `clippy`, `lld`, `go`, `zig`, `nodejs` and `wabt`. The nixpkgs input is pinned in `flake.lock` to the revision from 2026-10-30. The shell also provides the `wasm32-unknown-unknown` standard library through the nixpkgs rustc package, so the Rust example builds without rustup.

`nix develop` opens the shell; `nix fmt` formats the Nix files with `nixfmt`.

## Build and verification recipes

```bash
nix develop

# Rust crate: unit tests, doctests, lints and the example.
cd src/rust_sdk
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
cd ../..

# Zig module, example and conformance.
cd src/zig_sdk
zig fmt --check sdk.zig build.zig example/src/plugin.zig
zig build -Doptimize=ReleaseSmall
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
cd ../..

# Go module, example and conformance.
cd src/go_sdk
gofmt -l .
go vet ./...
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example-go/wasm_sdk_example.wasm
cd ../..
```

The example builds write into ignored directories: `target/`, `.zig-cache/`, `zig-out/` and `dist/`.

## Versioning and packaging

- SDK 0.1 implements core ABI version 1. A new core ABI version is a breaking SDK release, and the loader change carries a `BREAKING CHANGE` footer first.
- The Rust crate is unpublished. It packages as a normal Cargo library with the sources, the crate README and a copy of the license.
- The Zig module is consumed as source. There is no registry package yet.
- The Go module path is `github.com/Mooling0602/dsh-mloader-wasm-sdk/src/go_sdk`. A released version needs a nested module tag such as `src/go_sdk/v0.1.0`. Until then consumers use a `replace` directive to the checkout.
- The Go implementation additionally requires the loader's WASI reactor support. The dsh-mloader working tree that this repository was developed against contains that change; it is not committed yet.

## Pitfalls

- A Zig module built with `-fno-entry` but without `-rdynamic` exports only memory. The plugin functions are dropped, and the loader reports missing exports.
- A Go reactor traps on any export before `_initialize`. Do not call an exported function from the host before the worker has initialized the reactor.
- The Go module is about 1.8 MB, the Zig example about 72 KB and the Rust example about 84 KB. Size is dominated by the runtime each language carries.
- `go vet` rejects converting the host supplied `uint32` pointer to `unsafe.Pointer`. The SDK compares the pointer instead, and a change that reintroduces the conversion breaks the vet run.
- A rustc that does not bundle a linker fails the Rust example with `linker 'lld' not found`. The dev shell provides `lld`.
- Node prints an experimental warning for `node:wasi` in the Go conformance run and in the worker. It is harmless.
- The loader worker does not call `_initialize` for a module without WASI imports. A non-WASI module has no initialization hook and must be ready when it is instantiated.
