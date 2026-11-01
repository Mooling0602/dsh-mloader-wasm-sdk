#include "mloader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML_ERROR_SIZE 256
#define ML_NUMBER_SIZE 64

struct ml_call {
    const char *tool;
    const char *event;
    const char *args;
    size_t args_length;
    char error[ML_ERROR_SIZE];
    int replied;
};

static char *ml_result_buffer;
static size_t ml_result_capacity;
static size_t ml_result_length;
static int ml_result_failed;

static int ml_result_reserve(size_t additional) {
    if (ml_result_failed) return 0;
    size_t needed = ml_result_length + additional;
    if (needed <= ml_result_capacity) return 1;
    size_t capacity = ml_result_capacity == 0 ? 1024 : ml_result_capacity;
    while (capacity < needed) capacity *= 2;
    char *buffer = realloc(ml_result_buffer, capacity);
    if (buffer == NULL) {
        ml_result_failed = 1;
        return 0;
    }
    ml_result_buffer = buffer;
    ml_result_capacity = capacity;
    return 1;
}

static void ml_result_append(const char *text, size_t length) {
    if (!ml_result_reserve(length)) return;
    memcpy(ml_result_buffer + ml_result_length, text, length);
    ml_result_length += length;
}

#define ML_APPEND_LITERAL(text) ml_result_append((text), sizeof(text) - 1)

static void ml_set_error(ml_call *call, const char *format, ...) {
    if (call->error[0] != '\0') return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(call->error, sizeof(call->error), format, arguments);
    va_end(arguments);
}

static const char *ml_skip_ws(const char *position, const char *end) {
    while (position < end) {
        char character = *position;
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') break;
        position++;
    }
    return position;
}

static const char *ml_skip_string(const char *position, const char *end) {
    if (position >= end || *position != '"') return NULL;
    position++;
    while (position < end) {
        unsigned char character = (unsigned char)*position;
        if (character == '"') return position + 1;
        if (character == '\\') {
            position += 2;
            continue;
        }
        if (character < 0x20) return NULL;
        position++;
    }
    return NULL;
}

static int ml_hex4(const char *position, unsigned *code) {
    unsigned value = 0;
    for (int index = 0; index < 4; index++) {
        char digit = position[index];
        value <<= 4;
        if (digit >= '0' && digit <= '9') {
            value |= (unsigned)(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            value |= (unsigned)(digit - 'a' + 10);
        } else if (digit >= 'A' && digit <= 'F') {
            value |= (unsigned)(digit - 'A' + 10);
        } else {
            return 0;
        }
    }
    *code = value;
    return 1;
}

static int ml_encode_utf8(unsigned code, char *destination, size_t capacity, size_t *used) {
    if (code < 0x80) {
        if (*used + 1 > capacity) return 0;
        destination[(*used)++] = (char)code;
    } else if (code < 0x800) {
        if (*used + 2 > capacity) return 0;
        destination[(*used)++] = (char)(0xC0 | (code >> 6));
        destination[(*used)++] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        if (*used + 3 > capacity) return 0;
        destination[(*used)++] = (char)(0xE0 | (code >> 12));
        destination[(*used)++] = (char)(0x80 | ((code >> 6) & 0x3F));
        destination[(*used)++] = (char)(0x80 | (code & 0x3F));
    } else {
        if (*used + 4 > capacity) return 0;
        destination[(*used)++] = (char)(0xF0 | (code >> 18));
        destination[(*used)++] = (char)(0x80 | ((code >> 12) & 0x3F));
        destination[(*used)++] = (char)(0x80 | ((code >> 6) & 0x3F));
        destination[(*used)++] = (char)(0x80 | (code & 0x3F));
    }
    return 1;
}

static int ml_decode_string(const char *start, const char *end, char *destination, size_t capacity, size_t *length) {
    if (start >= end || *start != '"') return 0;
    if (end - start < 2 || *(end - 1) != '"') return 0;
    const char *position = start + 1;
    const char *content_end = end - 1;
    size_t used = 0;
    while (position < content_end) {
        unsigned char character = (unsigned char)*position++;
        if (character != '\\') {
            if (used >= capacity) return 0;
            destination[used++] = (char)character;
            continue;
        }
        if (position >= content_end) return 0;
        char escape = *position++;
        if (escape == '"' || escape == '\\' || escape == '/') {
            character = (unsigned char)escape;
        } else if (escape == 'b') {
            character = '\b';
        } else if (escape == 'f') {
            character = '\f';
        } else if (escape == 'n') {
            character = '\n';
        } else if (escape == 'r') {
            character = '\r';
        } else if (escape == 't') {
            character = '\t';
        } else if (escape == 'u') {
            unsigned code = 0;
            if (content_end - position < 4) return 0;
            if (!ml_hex4(position, &code)) return 0;
            position += 4;
            if (code >= 0xD800 && code <= 0xDBFF) {
                unsigned low = 0;
                if (content_end - position < 6) return 0;
                if (position[0] != '\\' || position[1] != 'u') return 0;
                if (!ml_hex4(position + 2, &low)) return 0;
                position += 6;
                if (low < 0xDC00 || low > 0xDFFF) return 0;
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                return 0;
            }
            if (!ml_encode_utf8(code, destination, capacity, &used)) return 0;
            continue;
        } else {
            return 0;
        }
        if (used >= capacity) return 0;
        destination[used++] = (char)character;
    }
    *length = used;
    return 1;
}

static const char *ml_skip_value(const char *position, const char *end, int depth) {
    if (depth > 64) return NULL;
    position = ml_skip_ws(position, end);
    if (position >= end) return NULL;
    char character = *position;
    if (character == '"') return ml_skip_string(position, end);
    if (character == '{') {
        position++;
        for (;;) {
            position = ml_skip_ws(position, end);
            if (position >= end) return NULL;
            if (*position == '}') return position + 1;
            const char *key_end = ml_skip_string(position, end);
            if (key_end == NULL) return NULL;
            position = ml_skip_ws(key_end, end);
            if (position >= end || *position != ':') return NULL;
            position++;
            position = ml_skip_value(position, end, depth + 1);
            if (position == NULL) return NULL;
            position = ml_skip_ws(position, end);
            if (position >= end) return NULL;
            if (*position == ',') {
                position++;
                continue;
            }
            if (*position == '}') return position + 1;
            return NULL;
        }
    }
    if (character == '[') {
        position++;
        for (;;) {
            position = ml_skip_ws(position, end);
            if (position >= end) return NULL;
            if (*position == ']') return position + 1;
            position = ml_skip_value(position, end, depth + 1);
            if (position == NULL) return NULL;
            position = ml_skip_ws(position, end);
            if (position >= end) return NULL;
            if (*position == ',') {
                position++;
                continue;
            }
            if (*position == ']') return position + 1;
            return NULL;
        }
    }
    if (end - position >= 4 && memcmp(position, "true", 4) == 0) return position + 4;
    if (end - position >= 5 && memcmp(position, "false", 5) == 0) return position + 5;
    if (end - position >= 4 && memcmp(position, "null", 4) == 0) return position + 4;
    if (character == '-' || (character >= '0' && character <= '9')) {
        const char *cursor = position + 1;
        while (cursor < end) {
            char digit = *cursor;
            if ((digit >= '0' && digit <= '9') || digit == '.' || digit == 'e' || digit == 'E' || digit == '+' || digit == '-') {
                cursor++;
                continue;
            }
            break;
        }
        return cursor;
    }
    return NULL;
}

static int ml_object_member(const char *object, size_t length, const char *name, const char **value, size_t *value_length) {
    const char *position = object;
    const char *end = object + length;
    position = ml_skip_ws(position, end);
    if (position >= end || *position != '{') return 0;
    position++;
    for (;;) {
        position = ml_skip_ws(position, end);
        if (position >= end) return 0;
        if (*position == '}') return 0;
        const char *key_end = ml_skip_string(position, end);
        if (key_end == NULL) return 0;
        char key[ML_ERROR_SIZE];
        size_t key_length = 0;
        if (!ml_decode_string(position, key_end, key, sizeof(key) - 1, &key_length)) return 0;
        key[key_length] = '\0';
        position = ml_skip_ws(key_end, end);
        if (position >= end || *position != ':') return 0;
        position++;
        const char *value_start = ml_skip_ws(position, end);
        const char *value_end = ml_skip_value(value_start, end, 0);
        if (value_end == NULL) return 0;
        if (strcmp(key, name) == 0) {
            *value = value_start;
            *value_length = (size_t)(value_end - value_start);
            return 1;
        }
        position = ml_skip_ws(value_end, end);
        if (position >= end) return 0;
        if (*position == ',') {
            position++;
            continue;
        }
        return 0;
    }
}

static int ml_member_string(const char *object, size_t length, const char *name, char *destination, size_t capacity, size_t *value_length) {
    const char *value = NULL;
    size_t raw_length = 0;
    if (!ml_object_member(object, length, name, &value, &raw_length)) return 0;
    if (raw_length == 0 || *value != '"') return 0;
    return ml_decode_string(value, value + raw_length, destination, capacity, value_length);
}

static int ml_decode_number(const char *value, size_t length, double *number) {
    if (length == 0 || length >= ML_NUMBER_SIZE) return 0;
    char buffer[ML_NUMBER_SIZE];
    memcpy(buffer, value, length);
    buffer[length] = '\0';
    char *parse_end = NULL;
    double parsed = strtod(buffer, &parse_end);
    if (parse_end == buffer || parse_end != buffer + length) return 0;
    *number = parsed;
    return 1;
}

static int ml_json_valid(const char *text) {
    size_t length = strlen(text);
    const char *end = ml_skip_value(text, text + length, 0);
    if (end == NULL) return 0;
    end = ml_skip_ws(end, text + length);
    return end == text + length;
}

static int ml_argument(ml_call *call, const char *name, const char **value, size_t *length) {
    if (call->error[0] != '\0') return 0;
    if (!ml_object_member(call->args, call->args_length, name, value, length)) {
        ml_set_error(call, "missing required argument \"%s\"", name);
        return 0;
    }
    return 1;
}

int ml_number(ml_call *call, const char *name, double *out) {
    const char *value = NULL;
    size_t length = 0;
    if (!ml_argument(call, name, &value, &length)) return 0;
    if (!ml_decode_number(value, length, out)) {
        ml_set_error(call, "argument \"%s\" must be a number", name);
        return 0;
    }
    return 1;
}

int ml_integer(ml_call *call, const char *name, long long *out) {
    double number = 0;
    if (!ml_number(call, name, &number)) return 0;
    long long integer = (long long)number;
    if ((double)integer != number) {
        ml_set_error(call, "argument \"%s\" must be an integer", name);
        return 0;
    }
    *out = integer;
    return 1;
}

int ml_boolean(ml_call *call, const char *name, int *out) {
    const char *value = NULL;
    size_t length = 0;
    if (!ml_argument(call, name, &value, &length)) return 0;
    if (length == 4 && memcmp(value, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (length == 5 && memcmp(value, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    ml_set_error(call, "argument \"%s\" must be a boolean", name);
    return 0;
}

int ml_string(ml_call *call, const char *name, char *out, size_t capacity, size_t *length) {
    const char *value = NULL;
    size_t raw_length = 0;
    if (!ml_argument(call, name, &value, &raw_length)) return 0;
    if (raw_length == 0 || *value != '"') {
        ml_set_error(call, "argument \"%s\" must be a string", name);
        return 0;
    }
    if (!ml_decode_string(value, value + raw_length, out, capacity, length)) {
        ml_set_error(call, "argument \"%s\" is too long", name);
        return 0;
    }
    return 1;
}

static size_t ml_escape_char(unsigned char character, char *encoded) {
    if (character == '"' || character == '\\') {
        encoded[0] = '\\';
        encoded[1] = (char)character;
        return 2;
    }
    if (character == '\b') {
        encoded[0] = '\\';
        encoded[1] = 'b';
        return 2;
    }
    if (character == '\f') {
        encoded[0] = '\\';
        encoded[1] = 'f';
        return 2;
    }
    if (character == '\n') {
        encoded[0] = '\\';
        encoded[1] = 'n';
        return 2;
    }
    if (character == '\r') {
        encoded[0] = '\\';
        encoded[1] = 'r';
        return 2;
    }
    if (character == '\t') {
        encoded[0] = '\\';
        encoded[1] = 't';
        return 2;
    }
    if (character < 0x20) {
        int written = snprintf(encoded, 8, "\\u%04x", (unsigned)character);
        return written > 0 ? (size_t)written : 0;
    }
    encoded[0] = (char)character;
    return 1;
}

size_t ml_json_escape(const char *source, size_t length, char *destination, size_t capacity) {
    size_t used = 0;
    for (size_t index = 0; index < length; index++) {
        char encoded[8];
        size_t encoded_length = ml_escape_char((unsigned char)source[index], encoded);
        if (encoded_length == 0) return (size_t)-1;
        if (used + encoded_length + 1 > capacity) return (size_t)-1;
        memcpy(destination + used, encoded, encoded_length);
        used += encoded_length;
    }
    destination[used] = '\0';
    return used + 1;
}

static void ml_append_json_string(const char *text, size_t length) {
    ML_APPEND_LITERAL("\"");
    for (size_t index = 0; index < length; index++) {
        char encoded[8];
        size_t encoded_length = ml_escape_char((unsigned char)text[index], encoded);
        if (encoded_length == 0) continue;
        ml_result_append(encoded, encoded_length);
    }
    ML_APPEND_LITERAL("\"");
}

static void ml_append_error_frame(const char *message) {
    ML_APPEND_LITERAL("{\"ok\":false,\"error\":{\"message\":");
    ml_append_json_string(message, strlen(message));
    ML_APPEND_LITERAL("}}");
}

void ml_reply_ok(ml_call *call, const char *result_json) {
    if (call->replied) return;
    call->replied = 1;
    ML_APPEND_LITERAL("{\"ok\":true,\"result\":");
    ml_result_append(result_json, strlen(result_json));
    ML_APPEND_LITERAL("}");
}

void ml_reply_fail(ml_call *call, const char *message) {
    if (call->replied) return;
    call->replied = 1;
    ml_append_error_frame(message);
}

const char *ml_call_tool(const ml_call *call) {
    return call->tool;
}

const char *ml_call_event(const ml_call *call) {
    return call->event;
}

static const char *ml_validate_declaration(const ml_plugin *plugin) {
    for (size_t index = 0; index < plugin->tool_count; index++) {
        const ml_tool *tool = &plugin->tools[index];
        if (tool->name == NULL || tool->description == NULL || tool->handler == NULL) {
            return "a tool needs a name, a description and a handler";
        }
        if (tool->parameters != NULL && !ml_json_valid(tool->parameters)) {
            return "a tool parameters schema is not valid JSON";
        }
        if (tool->output != NULL && !ml_json_valid(tool->output)) {
            return "a tool output schema is not valid JSON";
        }
    }
    for (size_t index = 0; index < plugin->event_count; index++) {
        if (plugin->events[index].name == NULL || plugin->events[index].handler == NULL) {
            return "an event needs a name and a handler";
        }
    }
    return NULL;
}

static void ml_append_apply_frame(const ml_plugin *plugin) {
    ML_APPEND_LITERAL("{\"ok\":true,\"result\":{\"tools\":[");
    for (size_t index = 0; index < plugin->tool_count; index++) {
        const ml_tool *tool = &plugin->tools[index];
        if (index > 0) ML_APPEND_LITERAL(",");
        ML_APPEND_LITERAL("{\"name\":");
        ml_append_json_string(tool->name, strlen(tool->name));
        ML_APPEND_LITERAL(",\"description\":");
        ml_append_json_string(tool->description, strlen(tool->description));
        if (tool->parameters != NULL) {
            ML_APPEND_LITERAL(",\"parameters\":");
            ml_result_append(tool->parameters, strlen(tool->parameters));
        }
        if (tool->output != NULL) {
            ML_APPEND_LITERAL(",\"output\":");
            ml_result_append(tool->output, strlen(tool->output));
        }
        if (tool->timeout_ms > 0) {
            char buffer[32];
            int written = snprintf(buffer, sizeof(buffer), ",\"timeoutMs\":%lu", tool->timeout_ms);
            if (written > 0) ml_result_append(buffer, (size_t)written);
        }
        ML_APPEND_LITERAL("}");
    }
    ML_APPEND_LITERAL("],\"events\":[");
    for (size_t index = 0; index < plugin->event_count; index++) {
        if (index > 0) ML_APPEND_LITERAL(",");
        ml_append_json_string(plugin->events[index].name, strlen(plugin->events[index].name));
    }
    ML_APPEND_LITERAL("]}}");
}

static void ml_dispatch_tool_call(const char *payload, size_t payload_length) {
    char tool_name[64];
    size_t tool_length = 0;
    if (!ml_member_string(payload, payload_length, "tool", tool_name, sizeof(tool_name) - 1, &tool_length)) {
        ml_append_error_frame("tool.call requires payload.tool");
        return;
    }
    tool_name[tool_length] = '\0';
    const ml_tool *tool = NULL;
    for (size_t index = 0; index < ml_plugin_definition.tool_count; index++) {
        if (strcmp(ml_plugin_definition.tools[index].name, tool_name) == 0) {
            tool = &ml_plugin_definition.tools[index];
            break;
        }
    }
    if (tool == NULL) {
        char message[128];
        snprintf(message, sizeof(message), "unknown tool \"%s\"", tool_name);
        ml_append_error_frame(message);
        return;
    }
    const char *arguments = NULL;
    size_t arguments_length = 0;
    if (!ml_object_member(payload, payload_length, "args", &arguments, &arguments_length)) {
        static const char empty_object[] = "{}";
        arguments = empty_object;
        arguments_length = sizeof(empty_object) - 1;
    }
    ml_call call;
    memset(&call, 0, sizeof(call));
    call.tool = tool->name;
    call.args = arguments;
    call.args_length = arguments_length;
    tool->handler(&call);
    if (call.replied) return;
    if (call.error[0] != '\0') {
        ml_append_error_frame(call.error);
        return;
    }
    ml_append_error_frame("handler returned no result");
}

static void ml_dispatch_event(const char *payload, size_t payload_length) {
    char event_name[64];
    size_t event_length = 0;
    if (!ml_member_string(payload, payload_length, "event", event_name, sizeof(event_name) - 1, &event_length)) {
        ml_append_error_frame("event requires payload.event");
        return;
    }
    event_name[event_length] = '\0';
    const ml_event *event = NULL;
    for (size_t index = 0; index < ml_plugin_definition.event_count; index++) {
        if (strcmp(ml_plugin_definition.events[index].name, event_name) == 0) {
            event = &ml_plugin_definition.events[index];
            break;
        }
    }
    if (event == NULL) {
        ML_APPEND_LITERAL("{\"ok\":true,\"result\":null}");
        return;
    }
    const char *arguments = NULL;
    size_t arguments_length = 0;
    if (!ml_object_member(payload, payload_length, "args", &arguments, &arguments_length)) {
        static const char empty_array[] = "[]";
        arguments = empty_array;
        arguments_length = sizeof(empty_array) - 1;
    }
    ml_call call;
    memset(&call, 0, sizeof(call));
    call.event = event->name;
    call.args = arguments;
    call.args_length = arguments_length;
    event->handler(&call);
    if (call.replied) return;
    if (call.error[0] != '\0') {
        ml_append_error_frame(call.error);
        return;
    }
    ML_APPEND_LITERAL("{\"ok\":true,\"result\":null}");
}

static void ml_dispatch(const char *request, size_t request_length) {
    char operation[32];
    size_t operation_length = 0;
    if (!ml_member_string(request, request_length, "op", operation, sizeof(operation) - 1, &operation_length)) {
        ml_append_error_frame("invalid request: missing op");
        return;
    }
    operation[operation_length] = '\0';
    if (strcmp(operation, "apply") == 0) {
        const char *invalid = ml_validate_declaration(&ml_plugin_definition);
        if (invalid != NULL) {
            char message[ML_ERROR_SIZE];
            snprintf(message, sizeof(message), "invalid declaration: %s", invalid);
            ml_append_error_frame(message);
            return;
        }
        ml_append_apply_frame(&ml_plugin_definition);
        return;
    }
    const char *payload = NULL;
    size_t payload_length = 0;
    if (!ml_object_member(request, request_length, "payload", &payload, &payload_length)) {
        ml_append_error_frame("invalid request: missing payload");
        return;
    }
    if (strcmp(operation, "tool.call") == 0) {
        ml_dispatch_tool_call(payload, payload_length);
        return;
    }
    if (strcmp(operation, "event") == 0) {
        ml_dispatch_event(payload, payload_length);
        return;
    }
    char message[128];
    snprintf(message, sizeof(message), "unsupported op \"%s\"", operation);
    ml_append_error_frame(message);
}

__attribute__((export_name("mloader_abi_version"))) uint32_t mloader_abi_version(void) {
    return ML_ABI_VERSION;
}

__attribute__((export_name("mloader_alloc"))) uint32_t mloader_alloc(uint32_t length) {
    if (length == 0) return 0;
    void *buffer = malloc(length);
    return (uint32_t)(uintptr_t)buffer;
}

__attribute__((export_name("mloader_free"))) void mloader_free(uint32_t pointer, uint32_t length) {
    (void)length;
    free((void *)(uintptr_t)pointer);
}

__attribute__((export_name("mloader_result_ptr"))) uint32_t mloader_result_ptr(void) {
    if (ml_result_length == 0 || ml_result_buffer == NULL) return 0;
    return (uint32_t)(uintptr_t)ml_result_buffer;
}

__attribute__((export_name("mloader_call"))) uint32_t mloader_call(uint32_t pointer, uint32_t length) {
    ml_result_length = 0;
    ml_result_failed = 0;
    const char *request = (const char *)(uintptr_t)pointer;
    if (request == NULL || length == 0) {
        ml_append_error_frame("invalid request: empty request");
        return (uint32_t)ml_result_length;
    }
    ml_dispatch(request, (size_t)length);
    if (ml_result_failed) return 0;
    return (uint32_t)ml_result_length;
}
