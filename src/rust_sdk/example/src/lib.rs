//! Acceptance example for the dsh-mloader WebAssembly SDK.
//!
//! The module declares three tools and one event subscription through
//! [`Declaration`], and `export_plugin!` generates the five function exports of
//! the core ABI. Build it with `example/build.sh`; the `tests/abi.mjs` suite
//! drives the compiled module through the ABI.

use dsh_mloader_wasm_sdk as sdk;
use sdk::{json, schema, Declaration, Error, Result, Tool, Value};

/// Add two numbers.
fn add(args: &Value) -> Result<Value> {
    let a = sdk::arg::number(args, "a")?;
    let b = sdk::arg::number(args, "b")?;
    Ok(json!({ "sum": a + b }))
}

/// Upper-case a string.
fn upper(args: &Value) -> Result<Value> {
    let text = sdk::arg::string(args, "s")?;
    Ok(json!({ "upper": text.to_uppercase() }))
}

/// Check that one number is not negative.
///
/// The failure path is part of the example: the host reports the message as the
/// failed tool call.
fn check(args: &Value) -> Result<Value> {
    let value = sdk::arg::number(args, "value")?;
    if value < 0.0 {
        return Err(Error::new("value must be non-negative"));
    }
    Ok(json!({ "value": value }))
}

/// Declarations the SDK reports to the host on apply.
fn declare() -> Declaration {
    Declaration::new()
        .tool(
            Tool::new("sdk_add", "Add two numbers", add)
                .parameters(schema! { a: "number", b: "number" }),
        )
        .tool(
            Tool::new("sdk_upper", "Upper-case a string", upper)
                .parameters(schema! { s: "string" }),
        )
        .tool(
            Tool::new("sdk_check", "Check that a number is not negative", check)
                .parameters(schema! { value: "number" }),
        )
        .on("tools/execute", |_args| Ok(Value::Null))
}

sdk::export_plugin!(declare);
