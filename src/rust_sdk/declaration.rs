//! Tool and event declarations a plugin reports to the host.

use serde_json::{Map, Value};

use crate::Result;

/// Handler invoked for one tool call or event delivery.
///
/// The value is the `args` field of the request payload. A tool handler returns
/// the JSON value the host reports as the tool result. The host ignores an event
/// handler result, because a subscription is observational.
pub type Handler = fn(&Value) -> Result<Value>;

/// One tool a plugin declares.
///
/// The host validates the declaration before it registers the tool. A name must
/// match `^[A-Za-z0-9_-]{1,64}$`, a description must be non-empty and a name
/// may not repeat inside one plugin. The loader document holds the exact
/// contract.
#[derive(Clone, Debug)]
pub struct Tool {
    name: &'static str,
    description: &'static str,
    parameters: Option<Value>,
    output: Option<Value>,
    timeout_ms: Option<u64>,
    handler: Handler,
}

impl Tool {
    /// Create a tool with a name, a description and a handler.
    pub fn new(name: &'static str, description: &'static str, handler: Handler) -> Self {
        Self {
            name,
            description,
            parameters: None,
            output: None,
            timeout_ms: None,
            handler,
        }
    }

    /// Set the input schema of the tool.
    ///
    /// The host default is an empty object schema that rejects every property.
    /// Use the [`schema!`](crate::schema) macro when every argument is a required
    /// scalar, or pass the JSON Schema subset the loader accepts directly.
    pub fn parameters(mut self, schema: Value) -> Self {
        self.parameters = Some(schema);
        self
    }

    /// Set the result schema of the tool.
    ///
    /// The host default is an object schema that accepts additional properties.
    /// Declare a schema when the handler returns a value that is not an object,
    /// such as a string or a number.
    pub fn output(mut self, schema: Value) -> Self {
        self.output = Some(schema);
        self
    }

    /// Set a per-call deadline in milliseconds.
    ///
    /// The deadline does not replace the manifest `timeouts.callMs` value, and
    /// the loader applies both. Prefer the manifest value when one deadline
    /// covers every tool of the plugin.
    pub fn timeout_ms(mut self, timeout_ms: u64) -> Self {
        self.timeout_ms = Some(timeout_ms);
        self
    }

    /// Return the declaration object the host validates.
    pub(crate) fn declaration(&self) -> Value {
        let mut object = Map::new();
        object.insert("name".to_string(), Value::String(self.name.to_string()));
        object.insert(
            "description".to_string(),
            Value::String(self.description.to_string()),
        );
        if let Some(parameters) = &self.parameters {
            object.insert("parameters".to_string(), parameters.clone());
        }
        if let Some(output) = &self.output {
            object.insert("output".to_string(), output.clone());
        }
        if let Some(timeout_ms) = self.timeout_ms {
            object.insert("timeoutMs".to_string(), Value::from(timeout_ms));
        }
        Value::Object(object)
    }

    /// Return the handler of this tool.
    pub(crate) fn handler(&self) -> Handler {
        self.handler
    }
}

/// One event subscription a plugin declares.
#[derive(Clone, Debug)]
struct Event {
    name: &'static str,
    handler: Handler,
}

/// Tools and event subscriptions a plugin declares.
///
/// The SDK reports the declaration in the `apply` reply. The host performs the
/// real registrations on its own context and revokes them when the plugin
/// unloads.
#[derive(Clone, Debug, Default)]
pub struct Declaration {
    tools: Vec<Tool>,
    events: Vec<Event>,
}

impl Declaration {
    /// Create a declaration with no tools and no events.
    pub fn new() -> Self {
        Self::default()
    }

    /// Add one tool.
    ///
    /// A duplicate tool name is a loader error, so the plugin fails to apply.
    pub fn tool(mut self, tool: Tool) -> Self {
        self.tools.push(tool);
        self
    }

    /// Subscribe to one host event.
    ///
    /// Only the events in the loader whitelist can be subscribed:
    /// `tools/execute`, `tools/pre-execute`, `tools/post-execute`,
    /// `tools/result`, `tools/change`, `agent/status`, `agent/created`,
    /// `agent/disposed`, `session/created` and `session/disposed`. A
    /// subscription is observational, so the handler return value does not
    /// change the host execution. An event handler must return promptly and must
    /// not wait for host work.
    pub fn on(mut self, event: &'static str, handler: Handler) -> Self {
        self.events.push(Event {
            name: event,
            handler,
        });
        self
    }

    /// Build the `result` object of the `apply` reply.
    pub(crate) fn apply_result(&self) -> Value {
        let tools = Value::Array(self.tools.iter().map(Tool::declaration).collect());
        let events = Value::Array(
            self.events
                .iter()
                .map(|event| Value::String(event.name.to_string()))
                .collect(),
        );
        let mut result = Map::new();
        result.insert("tools".to_string(), tools);
        result.insert("events".to_string(), events);
        Value::Object(result)
    }

    /// Return the handler declared for a tool name.
    pub(crate) fn tool_handler(&self, name: &str) -> Option<Handler> {
        self.tools
            .iter()
            .find(|tool| tool.name == name)
            .map(Tool::handler)
    }

    /// Return the handler declared for an event name.
    pub(crate) fn event_handler(&self, name: &str) -> Option<Handler> {
        self.events
            .iter()
            .find(|event| event.name == name)
            .map(|event| event.handler)
    }
}
