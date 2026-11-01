#!/usr/bin/env bash
# Build the Go acceptance example into a complete plugin directory.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
sdk=$(cd -- "$here/.." && pwd -P)
out=${1:-"$here/dist"}

mkdir -p "$out/wasm-sdk-example-go"
(cd "$sdk" && GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared -o "$out/wasm-sdk-example-go/wasm_sdk_example.wasm" ./example)
sed 's|@ENTRY@|wasm_sdk_example.wasm|g' "$here/mloader.plugin.json.in" > "$out/wasm-sdk-example-go/mloader.plugin.json"
printf 'built %s\n' "$out/wasm-sdk-example-go"
