const std = @import("std");

const tinyFS = @cImport({
    @cInclude("libDisk.c");
    @cInclude("libTinyFS.c");
});

const DATASIZE = tinyFS.TFS_BLOCK__FILE_SIZE_DATA;
const BLOCKSIZE = tinyFS.BLOCKSIZE;

fn assert(a: bool, comptime fmt: []const u8, vars: anytype) void {
    if (a) {
        return;
    }
    std.debug.panic(fmt, vars);
}

fn assert_eq(a: anytype, b: @TypeOf(a), comptime fmt: []const u8, vars: anytype) void {
    if (a == b) {
        return;
    }
    std.debug.panic("assert_eq failed: {any} != {any} : " ++ fmt, .{ a, b } ++ vars);
}

test "diskTest.c" {
    const run = false;
    if (!run) {
        return error.SkipZigTest;
    }
    defer for (0..5) |i| {
        var buf: ["diskX.dsk".len]u8 = undefined;
        const disk_name = std.fmt.bufPrint(&buf, "disk{d}.dsk", .{i}) catch unreachable;
        std.fs.cwd().deleteFile(disk_name) catch {};
    };

    const diskTest = @cImport({
        @cInclude("tests/diskTest.c");
    });
    const ok = diskTest.run();
    try std.testing.expectEqual(errno_from(ok), .SUCCESS);
}

test "tfsTest.c" {
    const run = false;
    if (!run) {
        return error.SkipZigTest;
    }
    defer {
        // var buf: ["diskX.dsk".len]u8 = undefined;
        // const disk_name = std.fmt.bufPrint(&buf, "disk{d}.dsk", .{i}) catch unreachable;
        const disk_name = tinyFS.DEFAULT_DISK_NAME;
        std.fs.cwd().deleteFile(disk_name) catch {};
    }

    const tfsTest = @cImport({
        @cInclude("tests/tfsTest.c");
    });
    const ok = tfsTest.run_tfstest();
    try std.testing.expectEqual(errno_from(ok), .SUCCESS);
}

fn block_byte(block: []u8, block_index: usize, byte_index: usize) u8 {
    return block[block_index * 256 + byte_index];
}

test "LIBDISK: fail-on-write-oob" {
    var test_fs_file: [*c]const u8 = "/tmp/write_oob.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};
    const fd = tinyFS.openDisk(@constCast(test_fs_file), 3 * tinyFS.BLOCKSIZE);
    defer assert_eq(tinyFS.closeDisk(fd), 0, "closeDisk failed\n", .{});
    const bytes: [256]u8 = undefined;
    assert_eq(errno_from(tinyFS.writeBlock(fd, 4, @constCast(@ptrCast((&bytes).ptr)))), .RANGE, "writeBlock failed\n", .{});
}

test "mkfs" {
    var test_fs_file: [*c]const u8 = "/tmp/mkfs.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), 1024) == 0, "tfs_mkfs failed\n", .{});

    const file = try std.fs.cwd().openFileZ(test_fs_file, .{});
    const stat = try file.stat();
    assert(stat.size == 1024, "File size not 1024 is {d}\n", .{stat.size});

    var contents: [1024]u8 = undefined;
    const read_bytes = try file.read(&contents);
    assert(read_bytes == 1024, "read_bytes.len == 1024\n", .{});
    const blocks_count: isize = 4;

    assert(block_byte(&contents, 0, 0) == 1, "Superblock not set\n", .{});

    for (0..blocks_count) |block_index| {
        assert(
            block_byte(&contents, block_index, 1) == 0x44,
            "Block {d} magic not set to 0x44\n",
            .{block_index},
        );
        assert(
            std.mem.eql(
                u8,
                contents[block_index * 256 ..][4..256],
                std.mem.zeroes([256]u8)[4..256],
            ),
            "Block not zeroed\n",
            .{},
        );
    }
}

test "mkfs-non-256-len" {
    var test_fs_file: [*c]const u8 = "/tmp/mkfs.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), 1025) == 0, "tfs_mkfs failed\n", .{});

    const file = try std.fs.cwd().openFileZ(test_fs_file, .{});
    defer file.close();
    const stat = try file.stat();
    assert_eq(stat.size, 1025, "File size not {d}\n", .{1025});

    var contents: [1024]u8 = undefined;
    const read_bytes = try file.read(&contents);
    assert(read_bytes == 1024, "read_bytes.len == 1024\n", .{});
    const blocks_count: isize = 4;

    assert(block_byte(&contents, 0, 0) == 1, "Superblock not set\n", .{});

    for (0..blocks_count) |block_index| {
        assert(
            block_byte(&contents, block_index, 1) == 0x44,
            "Block {d} magic not set to 0x44\n",
            .{block_index},
        );
        assert(
            std.mem.eql(
                u8,
                contents[block_index * 256 ..][4..256],
                std.mem.zeroes([256]u8)[4..256],
            ),
            "Block not zeroed\n",
            .{},
        );
    }
}

test "mkfs-sub-256-nBytes" {
    var test_fs_file: [*c]const u8 = "/tmp/mkfs.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), 255) < 0, "tfs_mkfs failed\n", .{});
}

test "mkfs-0-nBytes" {
    var test_fs_file: [*c]const u8 = "/tmp/mkfs.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), 1 * BLOCKSIZE) == 0, "tfs_mkfs failed\n", .{});

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), 0) == 0, "tfs_mkfs failed\n", .{});

    const file = try std.fs.cwd().openFileZ(test_fs_file, .{});
    defer file.close();
    const stat = try file.stat();
    assert(stat.size == 256, "File size not 0 is {d}\n", .{stat.size});
}

test "mkfs-neg-nBytes" {
    var test_fs_file: [*c]const u8 = "/tmp/mkfs.tfs";
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), @as(i32, @intCast(-1))) < 0, "tfs_mkfs failed\n", .{});
}

fn mkfs(comptime name: []const u8, comptime nBytes: u64) ![5 + name.len:0]u8 {
    const test_file_name: *const [5 + name.len:0]u8 = "/tmp/" ++ name;
    var test_fs_file: [*c]const u8 = test_file_name.ptr;
    std.fs.deleteFileAbsoluteZ(test_fs_file) catch {};

    const num_bytes = comptime (nBytes - @mod(nBytes, 256));

    assert(tinyFS.tfs_mkfs(@constCast(test_fs_file), nBytes) == 0, "tfs_mkfs failed\n", .{});

    const file = try std.fs.cwd().openFileZ(test_fs_file, .{});
    defer file.close();
    const stat = try file.stat();
    assert_eq(stat.size, num_bytes, "File size not {d} is {d}\n", .{ num_bytes, stat.size });

    var contents: [num_bytes]u8 = undefined;
    const read_bytes = try file.read(&contents);
    assert(read_bytes == num_bytes, "read_bytes.len == {d}\n", .{num_bytes});
    const blocks_count: isize = @divFloor(nBytes, 256);

    assert(block_byte(&contents, 0, 0) == 1, "Superblock not set\n", .{});

    for (0..blocks_count) |block_index| {
        assert(
            block_byte(&contents, block_index, 1) == 0x44,
            "Block {d} magic not set to 0x44\n",
            .{block_index},
        );
        assert(
            std.mem.eql(
                u8,
                contents[block_index * 256 ..][4..256],
                std.mem.zeroes([256]u8)[4..256],
            ),
            "Block not zeroed\n",
            .{},
        );
    }

    return test_file_name.*;
}

const alloc = std.testing.allocator;
const errno = std.os.errno;

test "mount+dismount" {
    var fs_file = try mkfs("mount.tfs", 1024);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

fn errno_from(i: i32) std.os.linux.E {
    if (i >= 0)
        return .SUCCESS;
    return @enumFromInt(-i);
}

test "mount-twice-fail" {
    var fs_file = try mkfs("mount.tfs", 1024);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});
    assert(tinyFS.tfs_meta.mounted, "tfs meta is mounted\n", .{});
    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .BUSY, "tfs_mount failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

fn read_file(fd: c_int, comptime size: usize) ![size]u8 {
    var data: [size]u8 = std.mem.zeroes([size]u8);
    for (0..size) |read_count| {
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, data[read_count..].ptr)), .SUCCESS, "tfs_readByte failed on byte {d}\n read {any}\n", .{ read_count, data[0..read_count] });
    }
    return data;
}

test "open-file" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }
    assert_eq(tinyFS.tfs_free_block_count(), 8, "tfs_free_block_count failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

test "files" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }

    var multi_block_data: [DATASIZE * 4]u8 = undefined;
    @memset(&multi_block_data, 0x42);

    // tinyFS.hexdump_all_blocks();
    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 4, "tfs_free_block_count failed\n", .{});

    const fd_2 = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd_2), .SUCCESS, "tfs_openFile failed\n", .{});
    var read_data = try read_file(fd_2, multi_block_data.len);
    assert(std.mem.eql(u8, &multi_block_data, &read_data), "read_data == multi_block_data\n", .{});

    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

test "delete then write" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }

    var multi_block_data: [DATASIZE * 4]u8 = undefined;
    @memset(&multi_block_data, 0x42);

    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 4, "tfs_free_block_count failed\n", .{});

    assert_eq(errno_from(tinyFS.tfs_deleteFile(fd)), .SUCCESS, "tfs_deleteFile failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});

    const fd_2 = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd_2), .SUCCESS, "tfs_openFile failed\n", .{});
    var read_data = try read_file(fd_2, multi_block_data.len);
    assert(std.mem.eql(u8, &multi_block_data, &read_data), "read_data == multi_block_data\n", .{});

    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

test "smol files" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }

    var multi_block_data: [DATASIZE - 24]u8 = undefined;
    @memset(&multi_block_data, 0x42);

    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 7, "tfs_free_block_count failed\n", .{});

    const fd_2 = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd_2), .SUCCESS, "tfs_openFile failed\n", .{});
    var read_data = try read_file(fd_2, multi_block_data.len);
    assert(std.mem.eql(u8, &multi_block_data, &read_data), "read_data == multi_block_data\n", .{});

    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

test "multiple smol files" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    var file_2_name: [*c]u8 = @constCast("file2");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }

    var sub_block_data: [DATASIZE - 24]u8 = undefined;
    @memset(&sub_block_data, 0x42);

    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &sub_block_data, @intCast(sub_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 7, "tfs_free_block_count failed\n", .{});

    const fd_file_2 = tinyFS.tfs_openFile(file_2_name);
    assert_eq(errno_from(tinyFS.tfs_writeFile(fd_file_2, &sub_block_data, @intCast(sub_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 5, "tfs_free_block_count failed\n", .{});
    tinyFS.hexdump_all_blocks();
    assert_eq(errno_from(tinyFS.tfs_deleteFile(fd_file_2)), .SUCCESS, "tfs_deleteFile failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_closeFile(fd_file_2)), .SUCCESS, "tfs_closeFile failed\n", .{});
    // tinyFS.hexdump_all_blocks();
    assert_eq(tinyFS.tfs_free_block_count(), 7, "tfs_free_block_count failed\n", .{});

    const fd_2 = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd_2), .SUCCESS, "tfs_openFile failed\n", .{});
    var read_data = try read_file(fd_2, sub_block_data.len);
    assert(std.mem.eql(u8, &sub_block_data, &read_data), "read_data == multi_block_data\n", .{});

    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}

test "time" {
    if (true) {
        return error.SkipZigTest;
    }
    const t = tinyFS.time_t;

    std.debug.print("{}\n", .{t});
}

test "stat" {
    var fs_file = try mkfs("mount.tfs", tinyFS.BLOCKSIZE * 10);
    var fs_file_ptr: [*:0]u8 = &fs_file;

    assert_eq(errno_from(tinyFS.tfs_mount(fs_file_ptr)), .SUCCESS, "tfs_mount failed\n", .{});

    var file_name: [*c]u8 = @constCast("file1");
    const fd = tinyFS.tfs_openFile(file_name);
    assert_eq(errno_from(fd), .SUCCESS, "tfs_openFile failed\n", .{});
    {
        var data: u8 = 0;
        assert_eq(errno_from(tinyFS.tfs_readByte(fd, &data)), .RANGE, "tfs_readbyte succeeded when file has no size", .{});
    }

    var multi_block_data: [DATASIZE * 4]u8 = undefined;
    @memset(&multi_block_data, 0x42);

    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});
    assert_eq(tinyFS.tfs_free_block_count(), 4, "tfs_free_block_count failed\n", .{});

    assert_eq(errno_from(tinyFS.tfs_deleteFile(fd)), .SUCCESS, "tfs_deleteFile failed\n", .{});
    assert_eq(errno_from(tinyFS.tfs_writeFile(fd, &multi_block_data, @intCast(multi_block_data.len))), .SUCCESS, "tfs_writeFile failed\n", .{});

    var read_data = try read_file(fd, multi_block_data.len);
    assert(std.mem.eql(u8, &multi_block_data, &read_data), "read_data == multi_block_data\n", .{});

    tinyFS.hexdump_all_blocks();
    const stat_info = tinyFS.tfs_readFileInfo(fd);
    assert_eq(errno_from(stat_info.err), .SUCCESS, "stat failed\n", .{});
    // const name = std.mem.span(@as([*c]const u8, @ptrCast(stat_info.name[0..].ptr)));
    // std.debug.print("{s} -> {s}\n", .{ stat_info.name, name });
    // assert(std.mem.eql(u8, "file1", name), "name not equal got {s}\n", .{name});

    assert_eq(stat_info.size, multi_block_data.len, "size not equal got {d}\n", .{stat_info.size});

    assert_eq(errno_from(tinyFS.tfs_unmount()), .SUCCESS, "tfs_unmount failed\n", .{});
}
