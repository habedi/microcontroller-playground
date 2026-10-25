const std = @import("std");
const microzig = @import("microzig");
const MicroBuild = microzig.MicroBuild(.{ .rp2xxx = true });

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const mb = MicroBuild.init(b, b.dependency("microzig", .{})) orelse return;
    const firmware = mb.add_firmware(.{
        .name = "gpio-blink",
        .target = mb.ports.rp2xxx.boards.raspberrypi.pico2_arm,
        .optimize = optimize,
        .root_source_file = b.path("experiments/gpio-blink/src/main.zig"),
    });
    mb.install_firmware(firmware, .{});
    mb.install_firmware(firmware, .{ .format = .elf });
}
