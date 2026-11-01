#include "mloader.h"

#include <stdio.h>
#include <string.h>

static const char add_parameters[] =
    "{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"number\"},\"b\":{\"type\":\"number\"}},"
    "\"required\":[\"a\",\"b\"],\"additionalProperties\":false}";

static const char upper_parameters[] =
    "{\"type\":\"object\",\"properties\":{\"s\":{\"type\":\"string\"}},"
    "\"required\":[\"s\"],\"additionalProperties\":false}";

static const char check_parameters[] =
    "{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"number\"}},"
    "\"required\":[\"value\"],\"additionalProperties\":false}";

static void add(ml_call *call) {
    double a = 0;
    double b = 0;
    if (!ml_number(call, "a", &a)) return;
    if (!ml_number(call, "b", &b)) return;
    char result[128];
    int written = snprintf(result, sizeof(result), "{\"sum\":%g}", a + b);
    if (written <= 0 || (size_t)written >= sizeof(result)) {
        ml_reply_fail(call, "result too large");
        return;
    }
    ml_reply_ok(call, result);
}

static void upper(ml_call *call) {
    char text[512];
    size_t length = 0;
    if (!ml_string(call, "s", text, sizeof(text), &length)) return;
    for (size_t index = 0; index < length; index++) {
        if (text[index] >= 'a' && text[index] <= 'z') {
            text[index] = (char)(text[index] - 'a' + 'A');
        }
    }
    char escaped[1024];
    if (ml_json_escape(text, length, escaped, sizeof(escaped)) == (size_t)-1) {
        ml_reply_fail(call, "input too long");
        return;
    }
    char result[1200];
    int written = snprintf(result, sizeof(result), "{\"upper\":\"%s\"}", escaped);
    if (written <= 0 || (size_t)written >= sizeof(result)) {
        ml_reply_fail(call, "result too large");
        return;
    }
    ml_reply_ok(call, result);
}

static void check_value(ml_call *call) {
    double value = 0;
    if (!ml_number(call, "value", &value)) return;
    if (value < 0) {
        ml_reply_fail(call, "value must be non-negative");
        return;
    }
    char result[128];
    int written = snprintf(result, sizeof(result), "{\"value\":%g}", value);
    if (written <= 0 || (size_t)written >= sizeof(result)) {
        ml_reply_fail(call, "result too large");
        return;
    }
    ml_reply_ok(call, result);
}

static void observe(ml_call *call) {
    ml_reply_ok(call, "null");
}

static const ml_tool tools[] = {
    {"sdk_add", "Add two numbers", add_parameters, NULL, 0, add},
    {"sdk_upper", "Upper-case a string", upper_parameters, NULL, 0, upper},
    {"sdk_check", "Check that a number is not negative", check_parameters, NULL, 0, check_value},
};

static const ml_event events[] = {
    {"tools/execute", observe},
};

const ml_plugin ml_plugin_definition = {
    tools,
    sizeof(tools) / sizeof(tools[0]),
    events,
    sizeof(events) / sizeof(events[0]),
};
