#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "neb_test.h"
#include "../libTinyFS.h"

char* test_fs_file = "/tmp/mkfs.tfs";

#define block_byte(block, block_index, byte_index) block[(block_index) * (BLOCKSIZE) + (byte_index)]

int run() {
    remove(test_fs_file);

    assert(tfs_mkfs(test_fs_file, 1024) == 0, "tfs_mkfs failed\n");

    int fd = open(test_fs_file, O_RDWR, S_IRUSR);
    assert(fd >= 0, "tfs_mkfs failed to create file\n");

    char zeroes[256] = {0};

    char contents[1024];
    read(fd, contents, 1024);

    int blocks_count = 4; 

    int block_index = 0;

    assert(block_byte(contents, 0, 0) == 1, "Superblock not set\n");

    for (; block_index < blocks_count; block_index++) {
        assert(block_byte(contents, block_index, 1) == 0x44, "Block %d magic not set to 0x44\n", block_index);
        assert(memcmp((void*)(&contents[block_index] + 4), zeroes, 252) == 0, "Block not zeroed\n");
    }

    return 0;
}
