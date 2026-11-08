# Rust SDK

Rust implementation of the dsh-mloader WebAssembly core ABI version 1. This crate is the Rust language implementation in the [multi-language SDK repository](../..).

## Requirements

- Rust stable with the `wasm32-unknown-unknown` target. `rust-toolchain.toml` installs it for rustup users.
- A wasm linker. A rustc installation that does not bundle one needs `lld`.
- dsh with dsh-mloader installed.

`nix develop` at the repository root provides the toolchain, the wasm target and `lld`.

## Quick start

Create a library crate and depend on the SDK:

```toml
[package]
name = "my-wasm-plugin"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
dsh-mloader-wasm-sdk = { path = "../dsh-mloader-wasm-sdk/src/rust_sdk" }
```

The crate is not published yet, so depend on the repository path until a release is cut. Declare the tools and generate the ABI exports in `src/lib.rs`:

```rust
use dsh_mloader_wasm_sdk as sdk;
use sdk::{json, schema, Declaration, Result, Tool, Value};

fn add(args: &Value) -> Result<Value> {
    let a = sdk::arg::number(args, "a")?;
    let b = sdk::arg::number(args, "b")?;
    Ok(json!({ "sum": a + b }))
}

fn declare() -> Declaration {
    Declaration::new().tool(
        Tool::new("add", "Add two numbers", add)
            .parameters(schema! { a: "number", b: "number" }),
    )
}

sdk::export_plugin!(declare);
```

Build the module:

```bash
cargo build --release --target wasm32-unknown-unknown
```

Write a manifest beside the module and point `entry` at it:

```json
{
  "id": "my-wasm-plugin",
  "name": "My WASM Plugin",
  "version": "0.1.0",
  "backend": "wasm",
  "entry": "my_wasm_plugin.wasm"
}
```

Place the directory under `<profile>/mloader/plugins/` and restart dsh.

## The API

- `export_plugin!(declare)` generates `mloader_abi_version`, `mloader_alloc`, `mloader_free`, `mloader_result_ptr` and `mloader_call`. The `declare` function returns the declaration the loader receives on apply.
- `Declaration` collects tools and event subscriptions. `Tool::new` takes the name, the description and a handler. `Tool::parameters` sets the input schema, `Tool::output` sets the result schema and `Tool::timeout_ms` sets a per-call deadline.
- `schema! { a: "number", b: "string" }` builds the input schema for a tool whose arguments are required scalars and whose properties are closed. Use `serde_json::json!` for optional properties and nested schemas.
- `sdk::arg` reads one argument and reports a missing property or a wrong type as an `Error`, which becomes a failed reply frame. The accessors are `object`, `value`, `string`, `number`, `integer`, `boolean` and `array`.
- A handler returns `Result<Value>`. The `Ok` value is the tool result. The `Err` message is the failed call reason. A panic traps the module and also fails the call, so prefer `Err`.
- `Declaration::on` subscribes to one event from the loader whitelist. A subscription is observational, so the handler return value is ignored.

## What the crate produces

The compiled module imports no host function, so it needs no WASI provider. It exports `memory` and the five ABI functions, and it reports core ABI version 1. The example module is about 84 KB with the release profile of the example crate.

## Building and testing this crate

```bash
cd src/rust_sdk
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
```

The crate has unit tests for the request dispatch and doctests for the public API. The example builds the same plugin the conformance suite drives.

## Example

`example/` is a complete plugin with the three conformance tools, its manifest template and its build script. Build it with `./example/build.sh`; the output is a complete plugin directory under `example/dist/wasm-sdk-example`.

## License

MIT.
