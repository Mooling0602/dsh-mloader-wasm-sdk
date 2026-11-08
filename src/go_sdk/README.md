# Go SDK

Go module for the dsh-mloader WebAssembly core ABI version 1. This module is the Go language implementation in the [multi-language SDK repository](../..).

A Go module that exports wasm functions is a WASI reactor, not a command. The loader must initialize the reactor through `_initialize` before it calls the ABI version export. A loader that only starts command modules reports `wasi.start() has not been called` for a Go plugin.

## Requirements

- Go 1.24 or newer. The module uses `-buildmode=c-shared` for `wasip1` and the `go:wasmexport` directive.
- dsh with dsh-mloader installed and WASI reactor initialization support.
- Node.js 20 or newer only for the conformance suite.

`nix develop` at the repository root provides Go.

## Quick start

Create a module and depend on the SDK. The module is not published yet, so use a replace directive:

```goto
module my-plugin

go 1.24.0

require github.com/Mooling0602/dsh-mloader-wasm-sdk/src/go_sdk v0.0.0

replace github.com/Mooling0602/dsh-mloader-wasm-sdk/src/go_sdk => /path/to/dsh-mloader-wasm-sdk/src/go_sdk
```

Declare the tools and the five ABI exports in `main.go`:

```go
package main

import mloader "github.com/Mooling0602/dsh-mloader-wasm-sdk/src/go_sdk"

const addParameters = `{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false}`

func add(call *mloader.Call) (any, error) {
	a, err := call.Number("a")
	if err != nil {
		return nil, err
	}
	b, err := call.Number("b")
	if err != nil {
		return nil, err
	}
	return map[string]any{"sum": a + b}, nil
}

var declaration = mloader.Declaration{
	Tools: []mloader.Tool{
		{Name: "add", Description: "Add two numbers", Parameters: addParameters, Handler: add},
	},
}

//go:wasmexport mloader_abi_version
func mloaderABIVersion() uint32 { return mloader.ABIVersion }

//go:wasmexport mloader_alloc
func mloaderAlloc(size uint32) uint32 { return mloader.Alloc(size) }

//go:wasmexport mloader_free
func mloaderFree(ptr, size uint32) { mloader.Free(ptr, size) }

//go:wasmexport mloader_result_ptr
func mloaderResultPtr() uint32 { return mloader.ResultPtr() }

//go:wasmexport mloader_call
func mloaderCall(ptr, length uint32) uint32 { return mloader.Dispatch(&declaration, ptr, length) }

func main() {}
```

Build the reactor:

```bash
GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared -o my_plugin.wasm .
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

- `Declaration` collects `Tool` and `Event` entries. `Tool.Parameters` and `Tool.Output` are JSON text that the SDK validates at apply time, and `Tool.TimeoutMS` is a per-call deadline.
- A handler has the type `func(*Call) (any, error)`. The returned value becomes the tool result. The returned error becomes a failed reply frame. A panic is recovered and reported as a failed reply frame too.
- `Call.Number`, `Call.Integer`, `Call.String`, `Call.Boolean` and `Call.Array` decode one argument and return an error whose message names the argument.
- `Dispatch` handles one request, `Alloc` and `Free` own the request buffer, and `ResultPtr` exposes the reply buffer.
- `ABIVersion` is the core ABI version the SDK implements. It is 1.

## What the module produces

The compiled module imports `wasi_snapshot_preview1` and exports `_initialize`, `memory` and the five ABI functions. The loader initializes the reactor before the ABI version check, then drives it with the normal request path. The example module is about 1.8 MB, because it carries the Go runtime.

The worker processes one request at a time, so the SDK keeps one request buffer and one result buffer. The request buffer stays referenced by the module until the host frees it, and the result buffer stays valid until the next request.

## Building and testing this module

```bash
cd src/go_sdk
gofmt -l .
go vet ./...
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example-go/wasm_sdk_example.wasm
```

`go vet` must stay clean. The SDK avoids a uintptr to pointer conversion by comparing the request pointer that `Alloc` returned instead of converting the host value.

The SDK has no unit test target; the conformance suite is the acceptance test for the Go implementation, because the ABI is exercised through the compiled module.

## Example

`example/main.go` is a complete plugin with the three conformance tools and its manifest template. `example/build.sh` builds the reactor and assembles `example/dist/wasm-sdk-example-go`.

## License

MIT.
