# Zig SDK

Zig module for the dsh-mloader WebAssembly core ABI version 1. This module is the Zig language implementation in the [multi-language SDK repository](../..).

## Requirements

- Zig 0.17.
- dsh with dsh-mloader installed.

`nix develop` at the repository root provides Zig.

## Quick start

Wire the SDK into your `build.zig` as a module:

```zig
const sdk = b.createModule(.{
    .root_source_file = b.path("path/to/dsh-mloader-wasm-sdk/src/zig_sdk/sdk.zig"),
    .target = target,
    .optimize = optimize,
});
const plugin = b.createModule(.{
    .root_source_file = b.path("src/plugin.zig"),
    .target = target,
    .optimize = optimize,
});
plugin.addImport("mloader", sdk);

const wasm = b.addExecutable(.{ .name = "my_plugin", .root_module = plugin });
wasm.entry = .disabled;
wasm.rdynamic = true;
```

Declare the tools, the handlers and the five ABI exports in `src/plugin.zig`:

```zig
const mloader = @import("mloader");

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

const parameters =
    \\{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false}
;

const declaration = mloader.Declaration{
    .tools = &.{
        mloader.Tool{
            .name = "add",
            .description = "Add two numbers",
            .parameters = parameters,
            .handler = add,
        },
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
```

Build it as a freestanding wasm executable with no entry point:

```bash
zig build
```

The equivalent command line is `zig build-exe -fno-entry -rdynamic -target wasm32-freestanding -O ReleaseSmall src/plugin.zig`.

Write a manifest beside the module and point `entry` at it:

```json
{
  "id": "my-wasm-plugin",
  "name": "My WASM Plugin",
  "version": "0.1.0",
  "backend": "wasm",
  "entry": "my_plugin.wasm"
}
```

Place the directory under `<profile>/mloader/plugins/` and restart dsh.

## The API

- `Declaration` collects `Tool` and `Event` entries. Each `Tool` has a name, a description, optional `parameters` and `output` JSON text, an optional per-call `timeout_ms` and a handler.
- A handler has the type `*const fn (call: *Call) void`. It reads arguments through `Call.number`, `Call.integer`, `Call.string`, `Call.boolean` and `Call.array`, and replies with `Call.ok`, `Call.okNull` or `Call.fail`. A failed argument accessor records the message and returns null, so a handler can use `orelse return`.
- `Call.arena` is the allocator for the current request. Build result objects with `mloader.ObjectMap` and `mloader.Array`, and store JSON text that the loader validates.
- `Declaration.events` entries subscribe to one event from the loader whitelist. A subscription is observational, so the handler result is ignored.
- `alloc`, `free`, `resultPtr` and `call` implement the buffer and request paths. Exports are written in the plugin crate so the final wasm module owns the symbols.

## What the module produces

The compiled module imports no host function, so it needs no WASI provider. It exports `memory` and the five ABI functions, plus the `__stack_pointer` global that the Zig linker adds. The extra global is harmless; the loader checks that the required exports exist and ignores the rest. The example module is about 72 KB.

## Building and testing this module

```bash
cd src/zig_sdk
zig fmt --check sdk.zig build.zig example/src/plugin.zig
zig build -Doptimize=ReleaseSmall
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
```

The SDK has no unit test target; the conformance suite is the acceptance test for the Zig implementation, because the ABI is exercised through the compiled module.

## Example

`example/` is a complete plugin with the three conformance tools and its manifest template. `example/build.sh` builds the module and assembles `example/dist/wasm-sdk-example`.

## License

MIT.
