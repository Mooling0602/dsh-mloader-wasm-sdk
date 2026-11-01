//! Rust SDK for WebAssembly plugins hosted by dsh-mloader.
//!
//! [dsh-mloader](https://github.com/Mooling0602/dsh-mloader) hosts plugins that
//! are not Cordis plugins. A WebAssembly plugin is a `cdylib` built for the
//! `wasm32-unknown-unknown` target. It exchanges UTF-8 JSON with the host
//! through linear memory and implements the core WebAssembly ABI version 1.
//!
//! This crate provides the six ABI exports, the request and reply codec, the
//! declaration builders for tools and event subscriptions, and argument
//! accessors that turn a malformed call into an error reply. A module built with
//! the SDK imports no WASI function and calls nothing in the host, so it runs in
//! the plugin worker the loader owns.
//!
//! # Quick start
//!
//! A plugin declares its tools and exports the ABI symbols once:
//!
//! ```ignore
//! use dsh_mloader_wasm_sdk as sdk;
//! use sdk::{json, schema, Declaration, Result, Tool, Value};
//!
//! fn add(args: &Value) -> Result<Value> {
//!     let a = sdk::arg::number(args, "a")?;
//!     let b = sdk::arg::number(args, "b")?;
//!     Ok(json!({ "sum": a + b }))
//! }
//!
//! fn declare() -> Declaration {
//!     Declaration::new().tool(
//!         Tool::new("add", "Add two numbers", add)
//!             .parameters(schema! { a: "number", b: "number" }),
//!     )
//! }
//!
//! sdk::export_plugin!(declare);
//! ```
//!
//! The `example` crate in this repository is the same plugin as a complete
//! library with a build script and a manifest.
//!
//! # Contract boundaries
//!
//! - A plugin has no logging channel, no cleanup registration and no way to
//!   call back into the host. The declaration and the JSON reply are the whole
//!   protocol.
//! - A handler panic traps the module. The host reports the call as failed and
//!   keeps the worker alive, so return [`Error`] rather than panicking.
//! - The exports and the buffer ownership rules are normative in the loader's
//!   WebAssembly ABI document:
//!   <https://github.com/Mooling0602/dsh-mloader/blob/main/docs/wasm/WasmABI.md>.
//!
//! # ABI version
//!
//! [`ABI_VERSION`] is the core ABI version the SDK implements. The host rejects
//! a module that reports any other version before its initialization code runs.

#![warn(missing_docs)]

pub mod abi;
pub mod arg;
pub mod declaration;
pub mod error;

pub use declaration::{Declaration, Handler, Tool};
pub use error::Error;
pub use serde_json::{json, Map, Value};

/// Core WebAssembly ABI version the SDK implements.
pub const ABI_VERSION: u32 = 1;

/// Result of a tool or event handler.
///
/// An `Err` value becomes the failed reply frame, and the host reports its
/// message as the failed call reason.
pub type Result<T> = std::result::Result<T, Error>;

/// Generate the five function exports of the core WebAssembly ABI.
///
/// The argument names a function that returns the plugin [`Declaration`]. The
/// SDK calls it once, on the first request, and keeps the result for later
/// calls. Invoke the macro exactly once at the crate root of the plugin crate:
///
/// ```ignore
/// fn declare() -> dsh_mloader_wasm_sdk::Declaration { ... }
///
/// dsh_mloader_wasm_sdk::export_plugin!(declare);
/// ```
///
/// The generated functions are `mloader_abi_version`, `mloader_alloc`,
/// `mloader_free`, `mloader_result_ptr` and `mloader_call`. The wasm target
/// exports the sixth required symbol, the linear memory, by itself.
#[macro_export]
macro_rules! export_plugin {
    ($declare:path) => {
        /// Report the core WebAssembly ABI version this module implements.
        #[no_mangle]
        pub extern "C" fn mloader_abi_version() -> u32 {
            $crate::ABI_VERSION
        }

        /// Allocate a request buffer for the host.
        ///
        /// The host writes the UTF-8 JSON request into the returned buffer and
        /// releases it with `mloader_free` after the call.
        #[no_mangle]
        pub extern "C" fn mloader_alloc(len: u32) -> u32 {
            $crate::abi::alloc(len)
        }

        /// Release a request buffer returned by `mloader_alloc`.
        #[no_mangle]
        pub extern "C" fn mloader_free(ptr: u32, len: u32) {
            $crate::abi::free(ptr, len)
        }

        /// Return the pointer of the current result buffer.
        ///
        /// The buffer is owned by the module and stays valid until the next
        /// request is handled.
        #[no_mangle]
        pub extern "C" fn mloader_result_ptr() -> u32 {
            $crate::abi::result_ptr()
        }

        /// Handle one request and return the length of the result buffer.
        #[no_mangle]
        pub extern "C" fn mloader_call(ptr: u32, len: u32) -> u32 {
            $crate::abi::call($declare, ptr, len)
        }
    };
}

/// Build a tool input schema from required scalar properties.
///
/// Every listed property is required, and the schema rejects unknown
/// properties. The property type is written as one of the JSON Schema type
/// names: `"object"`, `"array"`, `"string"`, `"number"`, `"integer"`,
/// `"boolean"` or `"null"`.
///
/// ```
/// use dsh_mloader_wasm_sdk::{json, schema};
///
/// let schema = schema! { a: "number", b: "number" };
/// assert_eq!(schema["type"], json!("object"));
/// assert_eq!(schema["required"], json!(["a", "b"]));
/// assert_eq!(schema["properties"]["a"]["type"], json!("number"));
/// assert_eq!(schema["additionalProperties"], json!(false));
/// ```
///
/// An optional property, an array element schema or an `enum` needs more of the
/// schema subset, so pass it to [`Tool::parameters`] as JSON instead.
#[macro_export]
macro_rules! schema {
    ( $( $name:ident : $kind:literal ),* $(,)? ) => {{
        let mut properties = $crate::Map::new();
        let mut required = ::std::vec::Vec::new();
        $(
            let mut property = $crate::Map::new();
            property.insert(
                ::std::string::String::from("type"),
                $crate::Value::String(::std::string::String::from($kind)),
            );
            let property_name = ::std::string::String::from(::std::stringify!($name));
            properties.insert(property_name.clone(), $crate::Value::Object(property));
            required.push($crate::Value::String(property_name));
        )*
        let mut schema = $crate::Map::new();
        schema.insert(
            ::std::string::String::from("type"),
            $crate::Value::String(::std::string::String::from("object")),
        );
        schema.insert(
            ::std::string::String::from("properties"),
            $crate::Value::Object(properties),
        );
        schema.insert(
            ::std::string::String::from("required"),
            $crate::Value::Array(required),
        );
        schema.insert(
            ::std::string::String::from("additionalProperties"),
            $crate::Value::Bool(false),
        );
        $crate::Value::Object(schema)
    }};
}
