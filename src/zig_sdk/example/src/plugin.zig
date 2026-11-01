//! Acceptance example for the dsh-mloader Zig SDK.
//!
//! The plugin declares the same three tools and the same event subscription as
//! every other language SDK in this repository, so the conformance suite in
//! conformance/abi.mjs covers this module with the same assertions.

const std = @import("std");
const mloader = @import("mloader");

const add_parameters =
    \\{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false}
;

const upper_parameters =
    \\{"type":"object","properties":{"s":{"type":"string"}},"required":["s"],"additionalProperties":false}
;

const check_parameters =
    \\{"type":"object","properties":{"value":{"type":"number"}},"required":["value"],"additionalProperties":false}
;

/// Add two numbers.
fn add(call: *mloader.Call) void {
    const a = call.number("a") orelse return;
    const b = call.number("b") orelse return;
    var result: mloader.ObjectMap = .empty;
    result.put(call.arena, "sum", mloader.Value{ .float = a + b }) catch {
        call.fail("out of memory");
        return;
    };
    call.ok(mloader.Value{ .object = result });
}

/// Upper-case a string.
fn upper(call: *mloader.Call) void {
    const text = call.string("s") orelse return;
    const output = call.arena.dupe(u8, text) catch {
        call.fail("out of memory");
        return;
    };
    for (output) |*character| character.* = std.ascii.toUpper(character.*);
    var result: mloader.ObjectMap = .empty;
    result.put(call.arena, "upper", mloader.Value{ .string = output }) catch {
        call.fail("out of memory");
        return;
    };
    call.ok(mloader.Value{ .object = result });
}

/// Check that a number is not negative.
fn check(call: *mloader.Call) void {
    const value = call.number("value") orelse return;
    if (value < 0) {
        call.fail("value must be non-negative");
        return;
    }
    var result: mloader.ObjectMap = .empty;
    result.put(call.arena, "value", mloader.Value{ .float = value }) catch {
        call.fail("out of memory");
        return;
    };
    call.ok(mloader.Value{ .object = result });
}

/// Observe one host event. A subscription is observational, so the reply value
/// does not change the host execution.
fn observe(call: *mloader.Call) void {
    call.okNull();
}

/// Declarations the loader receives on apply.
const declaration = mloader.Declaration{
    .tools = &.{
        mloader.Tool{
            .name = "sdk_add",
            .description = "Add two numbers",
            .parameters = add_parameters,
            .handler = add,
        },
        mloader.Tool{
            .name = "sdk_upper",
            .description = "Upper-case a string",
            .parameters = upper_parameters,
            .handler = upper,
        },
        mloader.Tool{
            .name = "sdk_check",
            .description = "Check that a number is not negative",
            .parameters = check_parameters,
            .handler = check,
        },
    },
    .events = &.{
        mloader.Event{ .name = "tools/execute", .handler = observe },
    },
};

export fn mloader_abi_version() u32 {
    return mloader.abi_version;
}

export fn mloader_alloc(length: u32) u32 {
    return mloader.alloc(length);
}

export fn mloader_free(ptr: u32, length: u32) void {
    mloader.free(ptr, length);
}

export fn mloader_result_ptr() u32 {
    return mloader.resultPtr();
}

export fn mloader_call(ptr: u32, length: u32) u32 {
    return mloader.call(&declaration, ptr, length);
}
