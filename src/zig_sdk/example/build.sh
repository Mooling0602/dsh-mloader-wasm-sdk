#!/usr/bin/env bash
# Build the Zig acceptance example into a complete plugin directory.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
sdk=$(cd -- "$here/.." && pwd -P)
out=${1:-"$here/dist"}

cd "$sdk"
zig build -Doptimize=ReleaseSmall

mkdir -p "$out/wasm-sdk-example"
cp -- "$sdk/zig-out/bin/wasm_sdk_example.wasm" "$out/wasm-sdk-example/wasm_sdk_example.wasm"
sed 's|@ENTRY@|wasm_sdk_example.wasm|g' "$here/mloader.plugin.json.in" > "$out/wasm-sdk-example/mloader.plugin.json"
printf 'built %s\n' "$out/wasm-sdk-example"
