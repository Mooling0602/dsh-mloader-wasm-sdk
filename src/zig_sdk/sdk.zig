//! WebAssembly plugin SDK for dsh-mloader.
//!
//! A plugin imports this file as a module named mloader, declares its tools and
//! events in a Declaration, and forwards the five core ABI exports:
//!
//!   export fn mloader_abi_version() u32 { return mloader.abi_version; }
//!   export fn mloader_alloc(len: u32) u32 { return mloader.alloc(len); }
//!   export fn mloader_free(ptr: u32, len: u32) void { mloader.free(ptr, len); }
//!   export fn mloader_result_ptr() u32 { return mloader.resultPtr(); }
//!   export fn mloader_call(ptr: u32, len: u32) u32 {
//!       return mloader.call(&declaration, ptr, len);
//!   }
//!
//! A plugin writes one Declaration and imports this file in its build. The
//! module imports no WASI function, so it runs in the loader worker without a
//! host import.

const std = @import("std");

/// Core WebAssembly ABI version this SDK implements.
pub const abi_version: u32 = 1;

const allocator = std.heap.wasm_allocator;

/// Handler invoked for one tool call or event delivery.
///
/// Read arguments through the Call, and reply with Call.ok, Call.okNull or
/// Call.fail. A handler that replies with neither becomes a failed call.
pub const Handler = *const fn (call: *Call) void;

/// One tool the plugin declares.
pub const Tool = struct {
    /// Tool name. It must match the loader pattern and be unique in the plugin.
    name: []const u8,
    /// Human readable tool description.
    description: []const u8,
    /// Input schema as JSON text in the loader schema subset.
    parameters: ?[]const u8 = null,
    /// Result schema as JSON text in the loader schema subset.
    output: ?[]const u8 = null,
    /// Per-call deadline in milliseconds.
    timeout_ms: ?u64 = null,
    /// Handler that serves the tool call.
    handler: Handler,
};

/// One event subscription the plugin declares.
pub const Event = struct {
    /// Event name from the loader whitelist.
    name: []const u8,
    /// Handler that observes the event.
    handler: Handler,
};

/// Tools and event subscriptions the plugin reports on apply.
pub const Declaration = struct {
    tools: []const Tool = &.{},
    events: []const Event = &.{},
};

/// A JSON value, re-exported so a plugin does not import std for it.
pub const Value = std.json.Value;

/// A JSON object map, re-exported for building tool results.
pub const ObjectMap = std.json.ObjectMap;

/// A JSON array, re-exported for building tool results.
pub const Array = std.json.Array;

/// One request, passed to a handler.
pub const Call = struct {
    /// Arguments of the call: the tool arguments or the event argument array.
    args: Value,
    /// Allocator for the current request. Memory is released after the reply is
    /// serialized.
    arena: std.mem.Allocator,
    /// Result the handler produced.
    result: ?Value = null,
    /// Failure message the handler produced.
    error_message: ?[]const u8 = null,

    /// Reply with a JSON result.
    pub fn ok(self: *Call, value: Value) void {
        self.result = value;
    }

    /// Reply with a null result.
    pub fn okNull(self: *Call) void {
        self.result = .null;
    }

    /// Reply with a failure message.
    pub fn fail(self: *Call, message: []const u8) void {
        if (self.error_message == null) self.error_message = message;
    }

    /// Reply with a formatted failure message.
    pub fn failFmt(self: *Call, comptime format: []const u8, args: anytype) void {
        if (self.error_message != null) return;
        self.error_message = std.fmt.allocPrint(self.arena, format, args) catch "out of memory";
    }

    /// Return the named argument.
    ///
    /// A missing argument or a non-object argument value records a failure
    /// message and returns null, so a handler can use orelse return.
    pub fn get(self: *Call, name: []const u8) ?Value {
        if (self.error_message != null) return null;
        const object = switch (self.args) {
            .object => |object| object,
            else => {
                self.fail("arguments must be an object");
                return null;
            },
        };
        return object.get(name) orelse {
            self.failFmt("missing required argument \"{s}\"", .{name});
            return null;
        };
    }

    /// Return the named argument as a string.
    pub fn string(self: *Call, name: []const u8) ?[]const u8 {
        const value = self.get(name) orelse return null;
        return switch (value) {
            .string => |text| text,
            else => {
                self.failFmt("argument \"{s}\" must be a string", .{name});
                return null;
            },
        };
    }

    /// Return the named argument as a number.
    pub fn number(self: *Call, name: []const u8) ?f64 {
        const value = self.get(name) orelse return null;
        switch (value) {
            .integer => |value_integer| return @floatFromInt(value_integer),
            .float => |float| return float,
            .number_string => |text| {
                return std.fmt.parseFloat(f64, text) catch {
                    self.failFmt("argument \"{s}\" must be a number", .{name});
                    return null;
                };
            },
            else => {
                self.failFmt("argument \"{s}\" must be a number", .{name});
                return null;
            },
        }
    }

    /// Return the named argument as an integer.
    pub fn integer(self: *Call, name: []const u8) ?i64 {
        const value = self.get(name) orelse return null;
        return switch (value) {
            .integer => |value_integer| value_integer,
            else => {
                self.failFmt("argument \"{s}\" must be an integer", .{name});
                return null;
            },
        };
    }

    /// Return the named argument as a boolean.
    pub fn boolean(self: *Call, name: []const u8) ?bool {
        const value = self.get(name) orelse return null;
        return switch (value) {
            .bool => |value_boolean| value_boolean,
            else => {
                self.failFmt("argument \"{s}\" must be a boolean", .{name});
                return null;
            },
        };
    }

    /// Return the named argument as an array.
    pub fn array(self: *Call, name: []const u8) ?Array {
        const value = self.get(name) orelse return null;
        return switch (value) {
            .array => |items| items,
            else => {
                self.failFmt("argument \"{s}\" must be an array", .{name});
                return null;
            },
        };
    }
};

/// Result buffer owned by the module and valid until the next request.
var result_buffer: []u8 = &.{};
var result_owned: bool = false;

/// Fallback reply used when the reply cannot be serialized.
var fallback_reply = "{\"ok\":false,\"error\":{\"message\":\"reply serialization failed\"}}".*;

/// Replace the result buffer, releasing the previous one when it was owned.
fn setResult(bytes: []u8, owned: bool) void {
    if (result_owned) allocator.free(result_buffer);
    result_buffer = bytes;
    result_owned = owned;
}

/// Allocate a request buffer for the host and return its pointer.
///
/// Returns 0 when the length is 0 or the allocation fails.
pub fn alloc(length: u32) u32 {
    if (length == 0) return 0;
    const buffer = allocator.alloc(u8, length) catch return 0;
    return @intCast(@intFromPtr(buffer.ptr));
}

/// Release a request buffer returned by alloc.
pub fn free(ptr: u32, length: u32) void {
    if (ptr == 0 or length == 0) return;
    const buffer: [*]u8 = @ptrFromInt(ptr);
    allocator.free(buffer[0..length]);
}

/// Return the pointer of the current result buffer, or 0 when it is empty.
pub fn resultPtr() u32 {
    if (result_buffer.len == 0) return 0;
    return @intCast(@intFromPtr(result_buffer.ptr));
}

/// Handle one request and return the length of the result buffer.
pub fn call(declaration: *const Declaration, ptr: u32, length: u32) u32 {
    var arena_state = std.heap.ArenaAllocator.init(allocator);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const request: []const u8 = if (ptr == 0 or length == 0) &.{} else blk: {
        const bytes: [*]const u8 = @ptrFromInt(ptr);
        break :blk bytes[0..length];
    };

    const reply = handle(declaration, arena, request) catch |err| errorReplyFmt(
        arena,
        "internal error: {s}",
        .{@errorName(err)},
    ) catch {
        setResult(fallback_reply[0..], false);
        return @intCast(fallback_reply.len);
    };

    const owned = allocator.dupe(u8, reply) catch {
        setResult(fallback_reply[0..], false);
        return @intCast(fallback_reply.len);
    };
    setResult(owned, true);
    return @intCast(owned.len);
}

/// Build the reply bytes for one request.
fn handle(
    declaration: *const Declaration,
    arena: std.mem.Allocator,
    request: []const u8,
) error{OutOfMemory}![]u8 {
    const parsed = std.json.parseFromSliceLeaky(Value, arena, request, .{}) catch |err| {
        return errorReplyFmt(arena, "invalid request: {s}", .{@errorName(err)});
    };
    const op = stringField(objectField(parsed, "op")) orelse
        return errorReply(arena, "invalid request: missing op");
    const payload = objectField(parsed, "payload") orelse Value{ .null = {} };

    if (std.mem.eql(u8, op, "apply")) {
        const result = declarationValue(declaration, arena) catch |err| {
            return errorReplyFmt(arena, "invalid declaration: {s}", .{@errorName(err)});
        };
        return okReply(arena, result);
    }
    if (std.mem.eql(u8, op, "tool.call")) {
        return handleToolCall(declaration, arena, payload);
    }
    if (std.mem.eql(u8, op, "event")) {
        return handleEvent(declaration, arena, payload);
    }
    return errorReplyFmt(arena, "unsupported op \"{s}\"", .{op});
}

/// Handle one tool.call operation.
fn handleToolCall(
    declaration: *const Declaration,
    arena: std.mem.Allocator,
    payload: Value,
) error{OutOfMemory}![]u8 {
    const name = stringField(objectField(payload, "tool")) orelse
        return errorReply(arena, "tool.call requires payload.tool");
    const args = objectField(payload, "args") orelse Value{ .null = {} };
    const tool = findTool(declaration, name) orelse
        return errorReplyFmt(arena, "unknown tool \"{s}\"", .{name});

    var request = Call{ .args = args, .arena = arena };
    tool.handler(&request);
    if (request.error_message) |message| return errorReply(arena, message);
    const result = request.result orelse
        return errorReply(arena, "handler returned no result");
    return okReply(arena, result);
}

/// Handle one event operation.
fn handleEvent(
    declaration: *const Declaration,
    arena: std.mem.Allocator,
    payload: Value,
) error{OutOfMemory}![]u8 {
    const name = stringField(objectField(payload, "event")) orelse
        return errorReply(arena, "event requires payload.event");
    const args = objectField(payload, "args") orelse Value{ .null = {} };
    if (findEvent(declaration, name)) |event| {
        var request = Call{ .args = args, .arena = arena };
        event.handler(&request);
        if (request.error_message) |message| return errorReply(arena, message);
        return okReply(arena, request.result orelse Value{ .null = {} });
    }
    return okReply(arena, Value{ .null = {} });
}

/// Return the tool with the given name.
fn findTool(declaration: *const Declaration, name: []const u8) ?Tool {
    for (declaration.tools) |tool| {
        if (std.mem.eql(u8, tool.name, name)) return tool;
    }
    return null;
}

/// Return the event subscription with the given name.
fn findEvent(declaration: *const Declaration, name: []const u8) ?Event {
    for (declaration.events) |event| {
        if (std.mem.eql(u8, event.name, name)) return event;
    }
    return null;
}

/// Build the declaration object the loader validates.
fn declarationValue(
    declaration: *const Declaration,
    arena: std.mem.Allocator,
) !Value {
    var tools = std.json.Array.init(arena);
    for (declaration.tools) |tool| {
        var object: ObjectMap = .empty;
        try object.put(arena, "name", Value{ .string = tool.name });
        try object.put(arena, "description", Value{ .string = tool.description });
        if (tool.parameters) |text| {
            const schema = try std.json.parseFromSliceLeaky(Value, arena, text, .{});
            try object.put(arena, "parameters", schema);
        }
        if (tool.output) |text| {
            const schema = try std.json.parseFromSliceLeaky(Value, arena, text, .{});
            try object.put(arena, "output", schema);
        }
        if (tool.timeout_ms) |timeout_ms| {
            try object.put(arena, "timeoutMs", Value{ .integer = @intCast(timeout_ms) });
        }
        try tools.append(Value{ .object = object });
    }

    var events = std.json.Array.init(arena);
    for (declaration.events) |event| {
        try events.append(Value{ .string = event.name });
    }

    var result: ObjectMap = .empty;
    try result.put(arena, "tools", Value{ .array = tools });
    try result.put(arena, "events", Value{ .array = events });
    return Value{ .object = result };
}

/// The success reply frame.
const OkReply = struct {
    ok: bool = true,
    result: Value,
};

/// The failed reply frame.
const ErrorReply = struct {
    ok: bool = false,
    @"error": struct {
        message: []const u8,
    },
};

/// Build a successful reply frame.
fn okReply(arena: std.mem.Allocator, result: Value) error{OutOfMemory}![]u8 {
    return std.json.Stringify.valueAlloc(arena, OkReply{ .result = result }, .{});
}

/// Build a failed reply frame.
fn errorReply(arena: std.mem.Allocator, message: []const u8) error{OutOfMemory}![]u8 {
    return std.json.Stringify.valueAlloc(arena, ErrorReply{ .@"error" = .{ .message = message } }, .{});
}

/// Build a failed reply frame with a formatted message.
fn errorReplyFmt(
    arena: std.mem.Allocator,
    comptime format: []const u8,
    args: anytype,
) error{OutOfMemory}![]u8 {
    const message = try std.fmt.allocPrint(arena, format, args);
    return errorReply(arena, message);
}

/// Return the named field of a JSON object.
fn objectField(value: Value, name: []const u8) ?Value {
    return switch (value) {
        .object => |object| object.get(name),
        else => null,
    };
}

/// Return a JSON string field.
fn stringField(value: ?Value) ?[]const u8 {
    const actual = value orelse return null;
    return switch (actual) {
        .string => |text| text,
        else => null,
    };
}
