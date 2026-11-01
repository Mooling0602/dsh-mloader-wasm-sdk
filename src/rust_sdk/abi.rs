//! Implementation of the core WebAssembly ABI exports.
//!
//! The items in this module are public so the
//! [`export_plugin!`](crate::export_plugin) macro can name them from the plugin
//! crate. They are not part of the supported surface: use the macro and the
//! declaration builders instead.

use std::alloc::{alloc as allocate, dealloc, Layout};
use std::sync::{Mutex, OnceLock, PoisonError};

use serde_json::{json, Value};

use crate::declaration::Declaration;

/// Result buffer owned by the module.
///
/// The host reads the buffer after [`call`] returns and before the next request.
/// Every request replaces the buffer, so a pointer from an earlier call is stale
/// after another call.
static RESULT: Mutex<Vec<u8>> = Mutex::new(Vec::new());

/// Declaration of the plugin, initialized on the first request that needs it.
static DECLARATION: OnceLock<Declaration> = OnceLock::new();

/// Fallback reply used when the reply object cannot be serialized.
const SERIALIZATION_FAILED: &[u8] =
    br#"{"ok":false,"error":{"message":"reply serialization failed"}}"#;

/// A JSON null used when a payload component is absent.
const NULL: Value = Value::Null;

/// Allocate `len` bytes for the host and return their pointer.
///
/// Returns 0 when `len` is 0 or the allocation fails. The host releases the
/// buffer with [`free`] and the same length.
pub fn alloc(len: u32) -> u32 {
    if len == 0 {
        return 0;
    }
    let Ok(layout) = Layout::array::<u8>(len as usize) else {
        return 0;
    };
    // SAFETY: the layout has a non-zero size, and the pointer is released by
    // `free` with the same layout.
    unsafe { allocate(layout) as u32 }
}

/// Release a request buffer returned by [`alloc`].
///
/// A null pointer and a zero length are ignored, because the host passes back
/// the pair it received.
pub fn free(ptr: u32, len: u32) {
    if ptr == 0 || len == 0 {
        return;
    }
    let Ok(layout) = Layout::array::<u8>(len as usize) else {
        return;
    };
    // SAFETY: `ptr` was returned by `alloc` with the same length and is not used
    // after this call.
    unsafe { dealloc(ptr as *mut u8, layout) };
}

/// Return the pointer of the current result buffer, or 0 when it is empty.
pub fn result_ptr() -> u32 {
    let result = RESULT.lock().unwrap_or_else(PoisonError::into_inner);
    if result.is_empty() {
        0
    } else {
        result.as_ptr() as u32
    }
}

/// Handle one request and return the length of the result buffer.
///
/// The host writes the request into a buffer from [`alloc`] and keeps that
/// buffer valid for the duration of the call. The reply is the JSON object the
/// host reads at [`result_ptr`].
pub fn call(declare: fn() -> Declaration, ptr: u32, len: u32) -> u32 {
    let request: &[u8] = if ptr == 0 || len == 0 {
        &[]
    } else {
        // SAFETY: the host allocates the request with `alloc` and keeps it valid
        // until this function returns.
        unsafe { std::slice::from_raw_parts(ptr as *const u8, len as usize) }
    };
    store(handle(declare, request))
}

/// Return the declaration, which is built by the plugin function on first use.
fn declaration(declare: fn() -> Declaration) -> &'static Declaration {
    DECLARATION.get_or_init(declare)
}

/// Build the reply object for one request.
fn handle(declare: fn() -> Declaration, request: &[u8]) -> Value {
    let request: Value = match serde_json::from_slice(request) {
        Ok(request) => request,
        Err(error) => return error_reply(format!("invalid request: {error}")),
    };
    let Some(op) = request.get("op").and_then(Value::as_str) else {
        return error_reply("invalid request: missing op");
    };
    let payload = request.get("payload");
    match op {
        "apply" => ok_reply(declaration(declare).apply_result()),
        "tool.call" => call_tool(declare, payload),
        "event" => dispatch_event(declare, payload),
        other => error_reply(format!("unsupported op {other:?}")),
    }
}

/// Handle one `tool.call` operation.
fn call_tool(declare: fn() -> Declaration, payload: Option<&Value>) -> Value {
    let Some(tool) = payload
        .and_then(|payload| payload.get("tool"))
        .and_then(Value::as_str)
    else {
        return error_reply("tool.call requires payload.tool");
    };
    let args = payload
        .and_then(|payload| payload.get("args"))
        .unwrap_or(&NULL);
    let Some(handler) = declaration(declare).tool_handler(tool) else {
        return error_reply(format!("unknown tool {tool:?}"));
    };
    match handler(args) {
        Ok(result) => ok_reply(result),
        Err(error) => error_reply(error.message()),
    }
}

/// Handle one `event` operation.
///
/// An event without a declared handler is acknowledged with a null result, which
/// matches the loader contract for an observation that has no subscriber.
fn dispatch_event(declare: fn() -> Declaration, payload: Option<&Value>) -> Value {
    let Some(event) = payload
        .and_then(|payload| payload.get("event"))
        .and_then(Value::as_str)
    else {
        return error_reply("event requires payload.event");
    };
    let args = payload
        .and_then(|payload| payload.get("args"))
        .unwrap_or(&NULL);
    let Some(handler) = declaration(declare).event_handler(event) else {
        return ok_reply(Value::Null);
    };
    match handler(args) {
        Ok(result) => ok_reply(result),
        Err(error) => error_reply(error.message()),
    }
}

/// Build a successful reply frame.
fn ok_reply(result: Value) -> Value {
    json!({ "ok": true, "result": result })
}

/// Build a failed reply frame.
fn error_reply(message: impl Into<String>) -> Value {
    let message = message.into();
    json!({ "ok": false, "error": { "message": message } })
}

/// Serialize a reply into the result buffer and return its length.
fn store(reply: Value) -> u32 {
    let bytes = serde_json::to_vec(&reply).unwrap_or_else(|_| SERIALIZATION_FAILED.to_vec());
    let mut result = RESULT.lock().unwrap_or_else(PoisonError::into_inner);
    *result = bytes;
    result.len() as u32
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::handle;
    use crate::{arg, Declaration, Error, Result, Tool, Value};

    fn add(args: &Value) -> Result<Value> {
        let a = arg::number(args, "a")?;
        let b = arg::number(args, "b")?;
        Ok(json!({ "sum": a + b }))
    }

    fn fail(_args: &Value) -> Result<Value> {
        Err(Error::new("handler failed"))
    }

    fn declare() -> Declaration {
        Declaration::new()
            .tool(
                Tool::new("add", "Add two numbers", add)
                    .parameters(crate::schema! { a: "number", b: "number" }),
            )
            .tool(Tool::new("fail", "Always fails", fail))
            .on("tools/execute", |_args| Ok(Value::Null))
    }

    fn handle_request(request: Value) -> Value {
        let bytes = serde_json::to_vec(&request).expect("the request serializes");
        handle(declare, &bytes)
    }

    #[test]
    fn apply_declares_tools_and_events() {
        let reply = handle_request(json!({ "op": "apply", "payload": { "inject": [] } }));
        assert_eq!(reply["ok"], json!(true));
        let tools = reply["result"]["tools"]
            .as_array()
            .expect("tools is an array");
        assert_eq!(tools.len(), 2);
        assert_eq!(tools[0]["name"], json!("add"));
        assert_eq!(tools[0]["description"], json!("Add two numbers"));
        assert_eq!(tools[0]["parameters"]["required"], json!(["a", "b"]));
        assert_eq!(reply["result"]["events"], json!(["tools/execute"]));
    }

    #[test]
    fn apply_is_repeatable() {
        let first = handle_request(json!({ "op": "apply", "payload": { "inject": [] } }));
        let second = handle_request(json!({ "op": "apply", "payload": { "inject": [] } }));
        assert_eq!(first, second);
    }

    #[test]
    fn tool_call_returns_the_handler_result() {
        let reply = handle_request(
            json!({ "op": "tool.call", "payload": { "tool": "add", "args": { "a": 19, "b": 23 } } }),
        );
        assert_eq!(reply["ok"], json!(true));
        assert_eq!(reply["result"]["sum"], json!(42.0));
    }

    #[test]
    fn tool_call_reports_a_missing_argument() {
        let reply = handle_request(
            json!({ "op": "tool.call", "payload": { "tool": "add", "args": { "a": 1 } } }),
        );
        assert_eq!(reply["ok"], json!(false));
        assert_eq!(
            reply["error"]["message"],
            json!("missing required argument \"b\"")
        );
    }

    #[test]
    fn tool_call_reports_an_unknown_tool() {
        let reply =
            handle_request(json!({ "op": "tool.call", "payload": { "tool": "nope", "args": {} } }));
        assert_eq!(reply["ok"], json!(false));
        assert_eq!(reply["error"]["message"], json!("unknown tool \"nope\""));
    }

    #[test]
    fn tool_call_reports_a_handler_failure() {
        let reply =
            handle_request(json!({ "op": "tool.call", "payload": { "tool": "fail", "args": {} } }));
        assert_eq!(reply["ok"], json!(false));
        assert_eq!(reply["error"]["message"], json!("handler failed"));
    }

    #[test]
    fn event_delivery_returns_the_handler_result() {
        let reply = handle_request(
            json!({ "op": "event", "payload": { "event": "tools/execute", "args": [] } }),
        );
        assert_eq!(reply["ok"], json!(true));
        assert_eq!(reply["result"], json!(null));
    }

    #[test]
    fn event_delivery_without_a_handler_is_acknowledged() {
        let reply = handle_request(
            json!({ "op": "event", "payload": { "event": "tools/change", "args": [] } }),
        );
        assert_eq!(reply["ok"], json!(true));
        assert_eq!(reply["result"], json!(null));
    }

    #[test]
    fn an_unsupported_operation_is_rejected() {
        let reply = handle_request(json!({ "op": "boot", "payload": {} }));
        assert_eq!(reply["ok"], json!(false));
        let message = reply["error"]["message"]
            .as_str()
            .expect("the message is a string");
        assert!(message.contains("unsupported op"), "{message}");
    }

    #[test]
    fn invalid_json_is_rejected() {
        let reply = handle(declare, b"not json");
        assert_eq!(reply["ok"], json!(false));
        let message = reply["error"]["message"]
            .as_str()
            .expect("the message is a string");
        assert!(message.starts_with("invalid request"), "{message}");
    }

    #[test]
    fn a_missing_payload_is_rejected() {
        let reply = handle_request(json!({ "op": "tool.call" }));
        assert_eq!(reply["ok"], json!(false));
        assert_eq!(
            reply["error"]["message"],
            json!("tool.call requires payload.tool")
        );
    }

    #[test]
    fn schema_macro_builds_a_required_property_object() {
        let schema = crate::schema! { a: "number", b: "number" };
        assert_eq!(schema["type"], json!("object"));
        assert_eq!(schema["properties"]["a"]["type"], json!("number"));
        assert_eq!(schema["required"], json!(["a", "b"]));
        assert_eq!(schema["additionalProperties"], json!(false));
    }
}
