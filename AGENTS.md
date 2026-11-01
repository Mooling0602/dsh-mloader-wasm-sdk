# AGENTS.md

Rules for AI coding agents working in this repository. Follow them exactly. When a rule and your own judgement disagree, the rule wins.

## 1. Git: commits and pushes always need the user's approval

- **Never create a commit without asking first.** Ask for approval every time, even if you have committed in this same session before.
- **Commit messages must be reviewed by the user before the commit exists.** Draft the message, show it, and wait for approval.
- **Never push without asking first.** Ask every time.
- Do not run remote operations, including publishing to any package registry, without the user's explicit approval in the current session.

## 2. Commit messages: Conventional Commits

Follow [Conventional Commits v1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):

```plaintext
<type>[optional scope][!]: <description>

[optional body]

[optional footer(s)]
```

- **Types:** `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `chore`, `style`, `revert`.
- **Scope** is optional but preferred. Use the area touched, such as `sdk`, `rust`, `zig`, `go`, `c`, `conformance`, `flake`, `docs`, `test` or `deps`.
- **Description** is lowercase, imperative and carries no trailing period. Describe the change, not the file.
- **Body** explains why the change was made. It is mandatory for a bug fix and for any change to the ABI contract or the reply frames.
- **Do not wrap the message by hand.** The subject and each body paragraph are one complete line, with no character limit.
- **Footers:** use `BREAKING CHANGE: <description>` or a `!` after the type or scope for any change to the core ABI, the export names, the request and reply frames, the declaration object or the buffer ownership rules.
- **One logical change per commit.** A change that touches one language implementation stays separate from a change that touches the conformance contract, unless the contract change requires the implementation change.

## 3. Language style

### Rust

- Every public item carries a `///` doc comment, the crate carries a `//!` block, and every module carries a `//!` block.
- A doc comment states the contract: units, ordering, ownership, failure conditions and the invariant the caller must uphold. It does not restate the signature.
- Keep the crate dependency light. `serde_json` is the only runtime dependency; add another only with a reason.

### Zig

- Every public declaration carries a `///` doc comment.
- Free functions that implement ABI paths are documented with their buffer or reply contract. Name the allocation lifetime when a function returns a slice.
- `zig fmt` is the formatter. A change that leaves a file unformatted fails verification.

### Go

- Every exported identifier carries a doc comment that begins with its name, following the standard Go convention.
- Package documentation is in the package comment of `mloader.go`.
- `gofmt` and `go vet` are part of verification. Keep both clean, including the unsafe pointer rule.

### C

- Every declaration in the public header carries a `/* */` comment that states the contract, including buffer lifetime and failure conditions.
- Internal helpers use the `static` keyword and the `ml_` prefix. C11 is the language baseline.
- The example build uses `-Wall -Wextra` and stays warning free. The SDK has no third-party dependency, so do not add one for JSON.

### All languages

- Comments are rare. Keep one when it records something the code cannot express, such as a `SAFETY` reason, an allocation invariant or a protocol ordering constraint. Place it next to the code it constrains and state the reason.
- Do not leave commented out code, changelog comments or decorative separators behind.

## 4. Documentation layers

| Document | Audience | Holds |
| --- | --- | --- |
| `AGENTS.md` | agents, maintainers | coding rules and conventions |
| `CONTEXT.md` | agents, maintainers | technical detail: ABI mapping, buffer ownership, per-language notes, toolchain notes, verification recipes |
| `README.md` and `README.zh.md` | plugin authors | what the SDKs are and how to pick and use one |
| `src/<language>_sdk/README.md` | plugin authors of one language | quick start and API for that implementation |

One fact has one home. Link to it instead of repeating it, except in `CONTEXT.md`, which exists so that an agent can load the working model from one file. A change to behaviour, to the ABI or to an invariant updates the owning document in the same change.

## 5. Describe the new state, delete the old one

Write the present tense and describe only how the system works now. Do not write what the code used to do, and do not add version history to reference material. Delete superseded code, stale documentation and the test that asserts the old contract in the same change. Do not leave a shim or a deprecated alias behind.

## 6. Prose style

- Write in English. The only exception is `README.zh.md`, which exists for translation.
- Do not use the em-dash or use dashes as connectors. Use a comma, a colon, a semicolon, a full stop or parentheses.
- Write each paragraph as one complete text line. Do not wrap at a column. A heading, a table row, a list item, a code block and the blank line between blocks each stay on their own line.
- Write complete sentences, keep the terminology consistent with the code, and prefer the plain word.
- Every Markdown file follows these rules.

## 7. The conformance contract is shared

- The examples in all language directories declare the same tools and the same events, and they answer the same calls with the same results.
- `conformance/abi.mjs` is the single acceptance test for the ABI across languages. Do not fork it per language, and do not add language specific branches.
- A change to one shared tool name, description, required list, error message or result shape is a change to every language example and to the suite in the same commit.

## 8. Working with dsh

- Never restart, kill or interrupt a running dsh service.
- When a change needs the running service to take effect, stop at the handoff. State what changed, give the exact command to run and the exact result to look for, then ask the user to restart and report back. Treat the user's report as the verification.
- A local integration probe that mounts dsh-mloader from a checkout is allowed because it is a separate Node process, not a replacement dsh service. It must clean up the temporary files it creates.
- The loader is a separate repository. A change to its worker, its documentation or its tests also needs the user's approval, and it is committed and reviewed there, not here.

## 9. Verification before handoff

```bash
nix develop

# Rust
cd src/rust_sdk
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
cd ../..

# Zig
cd src/zig_sdk
zig fmt --check sdk.zig build.zig example/src/plugin.zig
zig build -Doptimize=ReleaseSmall
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example/wasm_sdk_example.wasm
cd ../..

# Go
cd src/go_sdk
gofmt -l .
go vet ./...
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example-go/wasm_sdk_example.wasm
cd ../..

# C
cd src/c_sdk
./example/build.sh
node ../../conformance/abi.mjs --wasm example/dist/wasm-sdk-example-c/wasm_sdk_example.wasm
cd ../..
```

Never commit build artifacts. `target/`, `.zig-cache/`, `zig-out/` and `dist/` are ignored, and a temporary probe script is deleted after use.

## 10. ABI discipline

- The export names, the request and reply frames, the declaration object and the buffer ownership rules are an external contract shared with the dsh-mloader loader. Do not change one side alone.
- A change to the contract lands in the loader first, carries a `BREAKING CHANGE` footer there, bumps the core ABI version, and then this repository follows in a new release.
- When the loader and an SDK disagree, the loader repository is authoritative. Cross check the WebAssembly ABI document before changing an export or a frame.

## 11. The development shell

- Keep `flake.nix` and `flake.lock` working. `nix develop` must provide every tool the verification recipes use.
- The shell provides the toolchain without changing the system. Do not suggest system package installation when the shell already provides the tool.
