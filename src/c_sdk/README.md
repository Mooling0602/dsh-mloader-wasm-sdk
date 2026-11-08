# C SDK

C11 implementation of the dsh-mloader WebAssembly core ABI version 1. This SDK is the C language implementation in the [multi-language SDK repository](../..).

A C module is compiled together with the SDK as a WASI reactor. The SDK owns the five ABI exports, the JSON request scanner, the reply writer, the declaration object and the scalar argument accessors. The plugin defines one symbol, `ml_plugin_definition`, and its handlers.

## Requirements

- A C11 compiler with a `wasm32-wasip1` sysroot. `zig cc` from Zig 0.17 provides both, and the repository dev shell provides Zig.
- dsh with dsh-mloader installed and WASI reactor initialization support.
- Node.js 20 or newer only for the conformance suite.

The SDK has no third-party dependency. The JSON support covers what the ABI needs and the plugin writes tool results as JSON text.

## Quick start

Include `mloader.h`, define the handlers, the tool table and the plugin definition, then compile your source together with `mloader.c`:

```bash
zig cc -target wasm32-wasi -mexec-model=reactor -O2 -Wall -Wextra \
    -I path/to/dsh-mloader-wasm-sdk/src/c_sdk \
    -o my_plugin.wasm main.c path/to/dsh-mloader-wasm-sdk/src/c_sdk/mloader.c
```

A minimal `main.c`:

```c
#include "mloader.h"

#include <stdio.h>

static void add(ml_call *call) {
    double a = 0;
    double b = 0;
    if (!ml_number(call, "a", &a)) return;
    if (!ml_number(call, "b", &b)) return;
    char result[128];
    snprintf(result, sizeof(result), "{"sum":%g}", a + b);
    ml_reply_ok(call, result);
}

static const char add_parameters[] =
    "{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"
    ""required":["a","b"],"additionalProperties":false}";

static const ml_tool tools[] = {
    {"add", "Add two numbers", add_parameters, NULL, 0, add},
};

const ml_plugin ml_plugin_definition = {
    tools,
    sizeof(tools) / sizeof(tools[0]),
    NULL,
    0,
};
```

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

- The plugin defines `const ml_plugin ml_plugin_definition` with the tool table, the event table and their counts. The SDK references that symbol and owns the ABI exports.
- A handler has the type `void (*)(ml_call *call)` and calls exactly one of `ml_reply_ok` with a result JSON text or `ml_reply_fail` with a message.
- `ml_number`, `ml_integer`, `ml_boolean` and `ml_string` read one argument. A missing argument or a wrong type records the failure message and returns 0, so the handler returns without a second reply and the SDK sends the recorded error frame. `ml_string` decodes JSON escapes into the caller buffer and does not NUL terminate.
- `ml_json_escape` turns a string into a JSON string body for a result the handler builds. `ml_call_tool` and `ml_call_event` return the tool or event name of the current call.
- `parameters` and `output` are JSON text in the loader schema subset. The SDK validates them at apply time and reports an invalid declaration as a failed reply.
- Event handlers reply like tool handlers, but the loader ignores the result because a subscription is observational.

## What the module produces

The module is a WASI reactor that exports `memory`, `_initialize` and the five ABI functions. The used libc features resolve statically, so the module imports no host function, but the reactor hook is still required and the loader calls `_initialize` before the version check. The example module is about 1.1 MB, dominated by wasi-libc.

## Building and testing this module

```bash
cd src/c_sdk
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example-c/wasm_sdk_example.wasm
```

The SDK has no unit test target; the conformance suite is the acceptance test for the C implementation, because the ABI is exercised through the compiled module. The example build uses `-Wall -Wextra` and stays warning free.

## Example

`example/main.c` is a complete plugin with the three conformance tools and its manifest template. `example/build.sh` builds the reactor and assembles `example/dist/wasm-sdk-example-c`.

## License

MIT.
