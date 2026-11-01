#!/usr/bin/env bash
# Build the C acceptance example into a complete plugin directory.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
sdk=$(cd -- "$here/.." && pwd -P)
out=${1:-"$here/dist"}

mkdir -p "$out/wasm-sdk-example-c"
zig cc -target wasm32-wasi -mexec-model=reactor -O2 -Wall -Wextra \
    -I"$sdk" \
    -Wl,-z,stack-size=262144 \
    -o "$out/wasm-sdk-example-c/wasm_sdk_example.wasm" \
    "$here/main.c" "$sdk/mloader.c"
sed 's|@ENTRY@|wasm_sdk_example.wasm|g' "$here/mloader.plugin.json.in" > "$out/wasm-sdk-example-c/mloader.plugin.json"
printf 'built %s\n' "$out/wasm-sdk-example-c"
