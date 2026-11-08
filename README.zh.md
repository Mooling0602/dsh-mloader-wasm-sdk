# dsh-mloader WASM SDK

[English](README.md) | 简体中文

[DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness) 的插件加载器 [dsh-mloader](https://github.com/Mooling0602/dsh-mloader) 所用的多语言 WebAssembly 插件 SDK。

WebAssembly ABI 是语言中立的。插件是一个 WebAssembly 模块，导出五个函数加 `memory`，通过线性内存交换 UTF-8 JSON。`src/` 下的每个目录都是这套契约在一种语言里的薄封装，所有实现都附带同一份验收示例。

[![dsh-mloader 徽章](https://img.shields.io/badge/powered_by-dsh--mloader-4D6BFE?style=flat-square&logo=deepseek&logoColor=white)](https://github.com/Mooling0602/dsh-mloader)
[![MIT 协议徽章](https://img.shields.io/github/license/Mooling0602/dsh-mloader-wasm-sdk?style=flat-square)](https://github.com/Mooling0602/dsh-mloader-wasm-sdk/blob/main/LICENSE)

## 语言

| 语言 | 目录 | 分发形态 | 启动形态 | 状态 |
| --- | --- | --- | --- | --- |
| Rust | `src/rust_sdk` | Cargo crate | 普通 | 可用 |
| Zig | `src/zig_sdk` | 源码模块 | 普通 | 可用 |
| Go | `src/go_sdk` | Go module | WASI reactor | 可用 |
| C | `src/c_sdk` | 源码文件 | reactor | 可用 |

四种实现都面向核心 ABI 版本 1，并共用同一套一致性套件。Go 与 C 模块导出 `_initialize`，需要 dsh-mloader 在版本检查之前初始化 reactor。Go 模块导入 WASI preview1 命名空间，C 模块静态链接 wasi-libc 且不导入任何函数。

## 环境要求

- Rust stable，并安装 `wasm32-unknown-unknown` 目标，用于 Rust SDK。
- Zig 0.17，用于 Zig SDK。同一工具链通过 `zig cc` 与 wasi-libc 编译 C SDK。
- Go 1.24 或更新版本，用于 Go SDK。
- 一个 wasm 链接器。Rust 示例需要 `lld`。
- Node.js 20 或更新版本，用于一致性套件。
- 已安装 dsh 与 dsh-mloader。

仓库附带 `flake.nix`，因此 `nix develop` 会在不修改系统的前提下提供以上全部工具。

## 开发环境

```bash
nix develop
```

该 shell 提供 `rustc`、`cargo`、`rustfmt`、`clippy`、`lld`、`go`、`zig`、`nodejs` 与 `wabt`，包含 `wasm32-unknown-unknown` 标准库。

## 一致性套件

每个示例都声明同样的三个工具 `sdk_add`、`sdk_upper`、`sdk_check`，并订阅 `tools/execute`。同一套件可以驱动任意编译产物：

```bash
./src/rust_sdk/example/build.sh
node conformance/abi.mjs --wasm src/rust_sdk/example/dist/wasm-sdk-example/wasm_sdk_example.wasm

./src/zig_sdk/example/build.sh
node conformance/abi.mjs --wasm src/zig_sdk/example/dist/wasm-sdk-example/wasm_sdk_example.wasm

./src/go_sdk/example/build.sh
node conformance/abi.mjs --wasm src/go_sdk/example/dist/wasm-sdk-example-go/wasm_sdk_example.wasm

./src/c_sdk/example/build.sh
node conformance/abi.mjs --wasm src/c_sdk/example/dist/wasm-sdk-example-c/wasm_sdk_example.wasm
```

套件检查宿主导入边界、六个核心导出、启动形态、ABI 版本、声明对象、工具调用、错误回复、畸形请求、事件投递与结果缓冲区复用。它不构建任何模块，只运行传入的 `.wasm` 路径。

同一时间最多安装一个示例插件。它们共用工具名，安装第二个会在工具注册表里冲突。

## 各语言指南

- [Rust SDK](src/rust_sdk/README.md)
- [Zig SDK](src/zig_sdk/README.md)
- [Go SDK](src/go_sdk/README.md)
- [C SDK](src/c_sdk/README.md)

## 契约文档

契约由加载器仓库维护。WebAssembly 相关部分见 [docs/wasm/WasmABI.md](https://github.com/Mooling0602/dsh-mloader/blob/main/docs/wasm/WasmABI.md)，共享插件契约见 [docs/ABI-unified.md](https://github.com/Mooling0602/dsh-mloader/blob/main/docs/ABI-unified.md)，插件包布局见 [docs/wasm/Package.md](https://github.com/Mooling0602/dsh-mloader/blob/main/docs/wasm/Package.md)。

## 仓库布局

| 路径 | 内容 |
| --- | --- |
| `conformance/abi.mjs` | 语言中立的一致性套件 |
| `src/rust_sdk` | Rust crate、构建脚本与示例 |
| `src/zig_sdk` | Zig 模块、构建脚本与示例 |
| `src/go_sdk` | Go module、构建脚本与示例 |
| `src/c_sdk` | C 源码、构建脚本与示例 |
| `flake.nix`、`flake.lock` | 开发 shell 与固定的 nixpkgs 输入 |
| `CONTEXT.md` | 技术细节、构建配方与不变量 |
| `AGENTS.md` | 面向 AI agent 与维护者的编码规则 |

## 状态

Rust、Zig 与 C 产出没有宿主导入的模块。Go 产出导入 preview1 命名空间的 WASI reactor。C 与 Go 导出 `_initialize`，加载器会在 ABI 版本检查之前调用它。所有包都尚未发布；在发布之前请从本仓库引用。

## 许可证

MIT。见 [LICENSE](LICENSE)。
