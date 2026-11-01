// Command wasm-sdk-example is the acceptance example for the Go SDK.
//
// The plugin declares the same three tools and the same event subscription as
// every other language SDK in this repository, so the conformance suite in
// conformance/abi.mjs covers it with the same assertions.
package main

import (
	"fmt"
	"strings"

	mloader "github.com/Mooling0602/dsh-mloader-wasm-sdk/src/go_sdk"
)

const addParameters = `{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false}`

const upperParameters = `{"type":"object","properties":{"s":{"type":"string"}},"required":["s"],"additionalProperties":false}`

const checkParameters = `{"type":"object","properties":{"value":{"type":"number"}},"required":["value"],"additionalProperties":false}`

// add adds two numbers.
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

// upper upper-cases a string.
func upper(call *mloader.Call) (any, error) {
	text, err := call.String("s")
	if err != nil {
		return nil, err
	}
	return map[string]any{"upper": strings.ToUpper(text)}, nil
}

// check checks that a number is not negative.
func check(call *mloader.Call) (any, error) {
	value, err := call.Number("value")
	if err != nil {
		return nil, err
	}
	if value < 0 {
		return nil, fmt.Errorf("value must be non-negative")
	}
	return map[string]any{"value": value}, nil
}

// observe observes one host event. A subscription is observational, so the
// reply value does not change the host execution.
func observe(call *mloader.Call) (any, error) {
	return nil, nil
}

// declaration is the set of tools and events the loader receives on apply.
var declaration = mloader.Declaration{
	Tools: []mloader.Tool{
		{Name: "sdk_add", Description: "Add two numbers", Parameters: addParameters, Handler: add},
		{Name: "sdk_upper", Description: "Upper-case a string", Parameters: upperParameters, Handler: upper},
		{Name: "sdk_check", Description: "Check that a number is not negative", Parameters: checkParameters, Handler: check},
	},
	Events: []mloader.Event{
		{Name: "tools/execute", Handler: observe},
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
