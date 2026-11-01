/*
 * dsh-mloader WebAssembly plugin SDK for C.
 *
 * A plugin compiles this file together with its own source and defines the
 * ml_plugin_definition symbol. The SDK owns the five ABI exports, the JSON
 * request and reply codec, the tool and event declaration object and the
 * argument accessors. Build the result as a WASI reactor with
 * -mexec-model=reactor, so the loader calls _initialize before the version
 * check.
 */
#ifndef MLOADER_H
#define MLOADER_H

#include <stddef.h>
#include <stdint.h>

/* Core WebAssembly ABI version this SDK implements. */
#define ML_ABI_VERSION 1U

/* One request passed to a handler. */
typedef struct ml_call ml_call;

/* Handler invoked for one tool call or event delivery. */
typedef void (*ml_handler)(ml_call *call);

/* One tool the plugin declares. */
typedef struct ml_tool {
    const char *name;
    const char *description;
    /* Input schema as JSON text, or NULL for the loader default. */
    const char *parameters;
    /* Result schema as JSON text, or NULL for the loader default. */
    const char *output;
    /* Per-call deadline in milliseconds, or 0 for none. */
    unsigned long timeout_ms;
    ml_handler handler;
} ml_tool;

/* One event subscription the plugin declares. */
typedef struct ml_event {
    const char *name;
    ml_handler handler;
} ml_event;

/* Tools and event subscriptions the plugin reports on apply. */
typedef struct ml_plugin {
    const ml_tool *tools;
    size_t tool_count;
    const ml_event *events;
    size_t event_count;
} ml_plugin;

/* The plugin defines this symbol. */
extern const ml_plugin ml_plugin_definition;

/* Read one argument. Return 1 on success; record a handler error otherwise. */
int ml_number(ml_call *call, const char *name, double *out);
int ml_integer(ml_call *call, const char *name, long long *out);
int ml_boolean(ml_call *call, const char *name, int *out);

/*
 * Read one string argument and decode JSON escapes into out. The result is not
 * NUL terminated. Return 1 on success; record a handler error otherwise.
 */
int ml_string(ml_call *call, const char *name, char *out, size_t capacity, size_t *length);

/* Reply helpers. A handler calls exactly one of them. */
void ml_reply_ok(ml_call *call, const char *result_json);
void ml_reply_fail(ml_call *call, const char *message);

/*
 * Escape source into a JSON string body. Return the number of bytes written
 * including the trailing NUL, or (size_t)-1 when the buffer is too small.
 */
size_t ml_json_escape(const char *source, size_t length, char *destination, size_t capacity);

/* Return the tool name of a tool call, or NULL for an event delivery. */
const char *ml_call_tool(const ml_call *call);

/* Return the event name of an event delivery, or NULL for a tool call. */
const char *ml_call_event(const ml_call *call);

#endif
