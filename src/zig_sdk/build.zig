const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .wasm32,
        .os_tag = .freestanding,
    });
    const optimize = b.standardOptimizeOption(.{});

    const sdk = b.createModule(.{
        .root_source_file = b.path("sdk.zig"),
        .target = target,
        .optimize = optimize,
    });
    const plugin = b.createModule(.{
        .root_source_file = b.path("example/src/plugin.zig"),
        .target = target,
        .optimize = optimize,
    });
    plugin.addImport("mloader", sdk);

    const wasm = b.addExecutable(.{
        .name = "wasm_sdk_example",
        .root_module = plugin,
    });
    wasm.entry = .disabled;
    wasm.rdynamic = true;
    b.installArtifact(wasm);
}
