// Package mloader is the WebAssembly plugin SDK for the dsh-mloader loader.
//
// A plugin imports this package, declares its tools and events in a
// Declaration, and forwards the five core ABI exports with go:wasmexport
// directives in its main package. Build it as a WASI reactor with
// -buildmode=c-shared, so the loader initializes the runtime before the first
// call.
package mloader

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"unsafe"
)

// ABIVersion is the core WebAssembly ABI version this SDK implements.
const ABIVersion uint32 = 1

// Handler serves one tool call or one event delivery.
//
// The returned value becomes the tool result. The returned error becomes a
// failed reply frame and does not stop the worker.
type Handler func(call *Call) (any, error)

// Tool is one tool the plugin declares.
type Tool struct {
	// Name is the tool name. It must match the loader pattern and be unique
	// inside the plugin.
	Name string
	// Description is the human readable tool description.
	Description string
	// Parameters is the input schema as JSON text in the loader subset.
	Parameters string
	// Output is the result schema as JSON text in the loader subset.
	Output string
	// TimeoutMS is a per-call deadline in milliseconds.
	TimeoutMS uint64
	// Handler serves the tool call.
	Handler Handler
}

// Event is one event subscription the plugin declares.
type Event struct {
	// Name is an event name from the loader whitelist.
	Name string
	// Handler observes the event.
	Handler Handler
}

// Declaration is the set of tools and events the plugin reports on apply.
type Declaration struct {
	// Tools are the declared tools.
	Tools []Tool
	// Events are the declared event subscriptions.
	Events []Event
}

// Call is one request passed to a handler.
type Call struct {
	// Tool is the tool name of a tool call. It is empty for an event.
	Tool string
	// Event is the event name of an event delivery. It is empty for a tool call.
	Event string
	// Arguments holds the decoded tool arguments, or the event argument array.
	Arguments any
}

// Number returns the named argument as a number.
func (call *Call) Number(name string) (float64, error) {
	object, err := call.object()
	if err != nil {
		return 0, err
	}
	value, ok := object[name]
	if !ok {
		return 0, fmt.Errorf("missing required argument %q", name)
	}
	number, ok := value.(float64)
	if !ok {
		return 0, fmt.Errorf("argument %q must be a number", name)
	}
	return number, nil
}

// Integer returns the named argument as an integer.
func (call *Call) Integer(name string) (int64, error) {
	number, err := call.Number(name)
	if err != nil {
		return 0, err
	}
	if number != math.Trunc(number) {
		return 0, fmt.Errorf("argument %q must be an integer", name)
	}
	return int64(number), nil
}

// String returns the named argument as a string.
func (call *Call) String(name string) (string, error) {
	object, err := call.object()
	if err != nil {
		return "", err
	}
	value, ok := object[name]
	if !ok {
		return "", fmt.Errorf("missing required argument %q", name)
	}
	text, ok := value.(string)
	if !ok {
		return "", fmt.Errorf("argument %q must be a string", name)
	}
	return text, nil
}

// Boolean returns the named argument as a boolean.
func (call *Call) Boolean(name string) (bool, error) {
	object, err := call.object()
	if err != nil {
		return false, err
	}
	value, ok := object[name]
	if !ok {
		return false, fmt.Errorf("missing required argument %q", name)
	}
	boolean, ok := value.(bool)
	if !ok {
		return false, fmt.Errorf("argument %q must be a boolean", name)
	}
	return boolean, nil
}

// Array returns the named argument as an array.
func (call *Call) Array(name string) ([]any, error) {
	object, err := call.object()
	if err != nil {
		return nil, err
	}
	value, ok := object[name]
	if !ok {
		return nil, fmt.Errorf("missing required argument %q", name)
	}
	array, ok := value.([]any)
	if !ok {
		return nil, fmt.Errorf("argument %q must be an array", name)
	}
	return array, nil
}

// object returns the decoded tool arguments as an object.
func (call *Call) object() (map[string]any, error) {
	object, ok := call.Arguments.(map[string]any)
	if !ok {
		return nil, errors.New("arguments must be an object")
	}
	return object, nil
}

// requestBuf holds the request buffer the host writes.
var requestBuf []byte

// resultBuf holds the reply buffer the host reads.
var resultBuf []byte

// Alloc allocates the request buffer and returns its pointer.
//
// It returns 0 when the size is 0. The host releases the buffer with Free.
func Alloc(size uint32) uint32 {
	if size == 0 {
		return 0
	}
	requestBuf = make([]byte, size)
	return uint32(uintptr(unsafe.Pointer(&requestBuf[0])))
}

// Free releases the request buffer.
func Free(ptr, size uint32) {
	requestBuf = nil
}

// ResultPtr returns the pointer of the current result buffer, or 0 when it is
// empty. The buffer stays valid until the next request.
func ResultPtr() uint32 {
	if len(resultBuf) == 0 {
		return 0
	}
	return uint32(uintptr(unsafe.Pointer(&resultBuf[0])))
}

// Dispatch handles one ABI request and returns the result length.
func Dispatch(declaration *Declaration, ptr, length uint32) uint32 {
	resultBuf = handle(declaration, requestBytes(ptr, length))
	return uint32(len(resultBuf))
}

// requestBytes returns the request bytes the host wrote.
//
// The host allocates the buffer with Alloc and writes into it, so the slice is
// the one Alloc created. The pointer is compared instead of converted, which
// keeps the read free of an unsafe uintptr conversion.
func requestBytes(ptr, length uint32) []byte {
	if ptr == 0 || length == 0 {
		return nil
	}
	if len(requestBuf) < int(length) {
		return nil
	}
	if uint32(uintptr(unsafe.Pointer(&requestBuf[0]))) != ptr {
		return nil
	}
	return requestBuf[:length:length]
}

// request is one decoded ABI request.
type request struct {
	Op      string          `json:"op"`
	Payload json.RawMessage `json:"payload"`
}

// handle builds the reply bytes for one request.
func handle(declaration *Declaration, requestBytes []byte) []byte {
	var parsed request
	if err := json.Unmarshal(requestBytes, &parsed); err != nil {
		return errorReply("invalid request: " + err.Error())
	}
	switch parsed.Op {
	case "apply":
		return applyReply(declaration)
	case "tool.call":
		return toolCallReply(declaration, parsed.Payload)
	case "event":
		return eventReply(declaration, parsed.Payload)
	default:
		return errorReply(fmt.Sprintf("unsupported op %q", parsed.Op))
	}
}

// toolDeclaration is one tool entry of the apply result.
type toolDeclaration struct {
	Name        string          `json:"name"`
	Description string          `json:"description"`
	Parameters  json.RawMessage `json:"parameters,omitempty"`
	Output      json.RawMessage `json:"output,omitempty"`
	TimeoutMS   uint64          `json:"timeoutMs,omitempty"`
}

// applyResult is the apply reply result.
type applyResult struct {
	Tools  []toolDeclaration `json:"tools"`
	Events []string          `json:"events"`
}

// applyReply builds the declaration object the loader validates.
func applyReply(declaration *Declaration) []byte {
	result := applyResult{
		Tools:  make([]toolDeclaration, 0, len(declaration.Tools)),
		Events: make([]string, 0, len(declaration.Events)),
	}
	for _, tool := range declaration.Tools {
		entry := toolDeclaration{Name: tool.Name, Description: tool.Description, TimeoutMS: tool.TimeoutMS}
		if tool.Parameters != "" {
			if !json.Valid([]byte(tool.Parameters)) {
				return errorReply(fmt.Sprintf("invalid parameters for tool %q", tool.Name))
			}
			entry.Parameters = json.RawMessage(tool.Parameters)
		}
		if tool.Output != "" {
			if !json.Valid([]byte(tool.Output)) {
				return errorReply(fmt.Sprintf("invalid output for tool %q", tool.Name))
			}
			entry.Output = json.RawMessage(tool.Output)
		}
		result.Tools = append(result.Tools, entry)
	}
	for _, event := range declaration.Events {
		result.Events = append(result.Events, event.Name)
	}
	return okReply(result)
}

// toolCallPayload is the payload of a tool.call request.
type toolCallPayload struct {
	Tool string          `json:"tool"`
	Args json.RawMessage `json:"args"`
}

// toolCallReply builds the reply for one tool.call request.
func toolCallReply(declaration *Declaration, payload json.RawMessage) []byte {
	var parsed toolCallPayload
	if err := json.Unmarshal(payload, &parsed); err != nil {
		return errorReply("tool.call payload: " + err.Error())
	}
	if parsed.Tool == "" {
		return errorReply("tool.call requires payload.tool")
	}
	var handler Handler
	for _, tool := range declaration.Tools {
		if tool.Name == parsed.Tool {
			handler = tool.Handler
			break
		}
	}
	if handler == nil {
		return errorReply(fmt.Sprintf("unknown tool %q", parsed.Tool))
	}
	arguments, err := decodeArguments(parsed.Args)
	if err != nil {
		return errorReply("tool.call args: " + err.Error())
	}
	call := &Call{Tool: parsed.Tool, Arguments: arguments}
	result, err := invoke(handler, call)
	if err != nil {
		return errorReply(err.Error())
	}
	return okReply(result)
}

// eventPayload is the payload of an event request.
type eventPayload struct {
	Event string          `json:"event"`
	Args  json.RawMessage `json:"args"`
}

// eventReply builds the reply for one event request.
func eventReply(declaration *Declaration, payload json.RawMessage) []byte {
	var parsed eventPayload
	if err := json.Unmarshal(payload, &parsed); err != nil {
		return errorReply("event payload: " + err.Error())
	}
	if parsed.Event == "" {
		return errorReply("event requires payload.event")
	}
	var handler Handler
	for _, event := range declaration.Events {
		if event.Name == parsed.Event {
			handler = event.Handler
			break
		}
	}
	if handler == nil {
		return okReply(nil)
	}
	arguments, err := decodeArguments(parsed.Args)
	if err != nil {
		return errorReply("event args: " + err.Error())
	}
	call := &Call{Event: parsed.Event, Arguments: arguments}
	result, err := invoke(handler, call)
	if err != nil {
		return errorReply(err.Error())
	}
	return okReply(result)
}

// decodeArguments decodes a raw JSON argument value.
func decodeArguments(raw json.RawMessage) (any, error) {
	if len(raw) == 0 {
		return map[string]any{}, nil
	}
	var value any
	if err := json.Unmarshal(raw, &value); err != nil {
		return nil, err
	}
	return value, nil
}

// invoke calls a handler and turns a panic into an error reply.
func invoke(handler Handler, call *Call) (result any, err error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			err = fmt.Errorf("handler panic: %v", recovered)
		}
	}()
	return handler(call)
}

// successReply is the successful reply frame.
type successReply struct {
	OK     bool `json:"ok"`
	Result any  `json:"result"`
}

// failureReply is the failed reply frame.
type failureReply struct {
	OK    bool `json:"ok"`
	Error struct {
		Message string `json:"message"`
	} `json:"error"`
}

// fallbackReply is used when a reply cannot be serialized.
const fallbackReply = `{"ok":false,"error":{"message":"reply serialization failed"}}`

// okReply builds a successful reply frame.
func okReply(result any) []byte {
	return marshalReply(successReply{OK: true, Result: result})
}

// errorReply builds a failed reply frame.
func errorReply(message string) []byte {
	reply := failureReply{}
	reply.Error.Message = message
	return marshalReply(reply)
}

// marshalReply serializes one reply frame.
func marshalReply(reply any) []byte {
	payload, err := json.Marshal(reply)
	if err != nil {
		return []byte(fallbackReply)
	}
	return payload
}
