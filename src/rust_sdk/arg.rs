//! Accessors for the arguments of one tool call.
//!
//! The host passes the arguments supplied by the model as the `args` field of
//! a `tool.call` payload. Each accessor reports a missing property and a
//! property of the wrong type as an [`Error`], so a handler can use `?` and
//! let the SDK build the failed reply frame.
//!
//! ```
//! use dsh_mloader_wasm_sdk::{arg, json, Result, Value};
//!
//! fn double(args: &Value) -> Result<Value> {
//!     let value = arg::number(args, "value")?;
//!     Ok(json!({ "double": value * 2.0 }))
//! }
//!
//! let args = json!({ "value": 21 });
//! assert_eq!(double(&args).unwrap()["double"], json!(42.0));
//! assert!(double(&json!({})).is_err());
//! ```

use serde_json::{Map, Value};

use crate::{Error, Result};

/// Return the arguments object.
///
/// The host always sends an object, so a non-object value means the request was
/// forged or the plugin was invoked outside the loader.
pub fn object(args: &Value) -> Result<&Map<String, Value>> {
    args.as_object()
        .ok_or_else(|| Error::new("arguments must be an object"))
}

/// Return the property `name` of the arguments object.
pub fn value<'a>(args: &'a Value, name: &str) -> Result<&'a Value> {
    args.get(name)
        .ok_or_else(|| Error::new(format!("missing required argument {name:?}")))
}

/// Return the property `name` as a string slice.
pub fn string<'a>(args: &'a Value, name: &str) -> Result<&'a str> {
    value(args, name)?
        .as_str()
        .ok_or_else(|| Error::new(format!("argument {name:?} must be a string")))
}

/// Return the property `name` as a number.
///
/// JSON has one numeric type, so an integer property is a number here as well.
pub fn number(args: &Value, name: &str) -> Result<f64> {
    value(args, name)?
        .as_f64()
        .ok_or_else(|| Error::new(format!("argument {name:?} must be a number")))
}

/// Return the property `name` as an integer.
///
/// A numeric value with a fraction or a value outside the signed 64 bit range
/// is an error.
pub fn integer(args: &Value, name: &str) -> Result<i64> {
    value(args, name)?
        .as_i64()
        .ok_or_else(|| Error::new(format!("argument {name:?} must be an integer")))
}

/// Return the property `name` as a boolean.
pub fn boolean(args: &Value, name: &str) -> Result<bool> {
    value(args, name)?
        .as_bool()
        .ok_or_else(|| Error::new(format!("argument {name:?} must be a boolean")))
}

/// Return the property `name` as an array of values.
pub fn array<'a>(args: &'a Value, name: &str) -> Result<&'a Vec<Value>> {
    value(args, name)?
        .as_array()
        .ok_or_else(|| Error::new(format!("argument {name:?} must be an array")))
}
