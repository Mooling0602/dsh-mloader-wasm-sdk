#!/usr/bin/env bash
# Build the SDK acceptance example into a complete plugin directory.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
out=${1:-"$here/dist"}
target=${CARGO_TARGET_DIR:-"$here/target"}

cargo build --release --target wasm32-unknown-unknown --manifest-path "$here/Cargo.toml"

artifact="$target/wasm32-unknown-unknown/release/wasm_sdk_example.wasm"
if [[ ! -f "$artifact" ]]; then
    printf 'error: expected artifact not found: %s\n' "$artifact" >&2
    exit 1
fi

mkdir -p "$out/wasm-sdk-example"
cp -- "$artifact" "$out/wasm-sdk-example/wasm_sdk_example.wasm"
sed 's|@ENTRY@|wasm_sdk_example.wasm|g' "$here/mloader.plugin.json.in" \
    > "$out/wasm-sdk-example/mloader.plugin.json"
printf 'built %s\n' "$out/wasm-sdk-example"
