#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "libDisk.h"
#include "libTinyFS.h"
#include "TinyFS_errno.h"

#define TFS_BLOCK_MAGIC 0x44

#define TFS_OPEN_FILES_MAX 65535
#define TFS_FILE_SIZE_MAX 65535
#define TFS_FILE_NAME_LEN_MAX 8

#define TFS_BLOCK_TYPE_SUPER 1
#define TFS_BLOCK_TYPE_INODE 2
#define TFS_BLOCK_TYPE__DATA 3
#define TFS_BLOCK_TYPE__FREE 4

#define TFS_BLOCK_SUPER_INDEX 0

#define TFS_BLOCK__FILE_SIZE_DATA 252
#define TFS_BLOCK_INODE_SIZE_SIZE 2
#define TFS_BLOCK_INODE_SIZE_NAME 9
#define TFS_BLOCK_INODE_SIZE_TIME 8

#define TFS_BLOCK_EVERY_POS__TYPE 0
#define TFS_BLOCK_EVERY_POS_MAGIC 1
#define TFS_BLOCK_EVERY_POS__ADDR 2
#define TFS_BLOCK__FILE_POS__DATA 4
#define TFS_BLOCK_INODE_POS__SIZE 4
#define TFS_BLOCK_INODE_POS__NAME 6
#define TFS_BLOCK_INODE_POS_MTIME (TFS_BLOCK_INODE_POS__NAME + TFS_BLOCK_INODE_SIZE_NAME)
#define TFS_BLOCK_INODE_POS_ATIME (TFS_BLOCK_INODE_POS_MTIME + TFS_BLOCK_INODE_SIZE_TIME)
#define TFS_BLOCK_INODE_POS_CTIME (TFS_BLOCK_INODE_POS_ATIME + TFS_BLOCK_INODE_SIZE_TIME)


#ifndef FAIL_MACRO
#define FAIL_MACRO
#define fail(err) do {\
    errno = -err; \
    return err; \
    } while (0)
#define fail_if(err) if (err < 0) fail(err)
#endif

#ifndef DBG_MACRO
#define DBG_MACRO
#ifdef DEBUG
#define dbg(...) do { \
    fprintf(stderr, "%s:%d : ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    } while (0)
#else
#define dbg(...) (void)0
#endif
#endif
#define panic(fmt, ...) do { \
    fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, (fmt), ##__VA_ARGS__); \
    exit(1); \
    } while(0)

#define panic_prefix(prefix, prefix_fmt, prefix_arg, fmt, ...) do { \
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, (prefix)); \
    fprintf(stderr, prefix_fmt, prefix_arg); \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    int len = strlen(fmt); \
    if (len != 0 && fmt[len - 1] != '\n') \
        fprintf(stderr, "\n"); \
    exit(1); \
    } while(0)

#define assert(cond, fmt, ...) do { \
    if (!(cond)) { \
        panic_prefix("Assertion failed: ",  "%s\n", #cond, fmt, ##__VA_ARGS__); \
    } \
    } while(0)

typedef uint16_t addr_t;

enum tstamp {
    TSTAMP_CREATE,
    TSTAMP_ACCESS,
    TSTAMP_MODIFY,
};

struct tfs_stat {
    int err;
    uint16_t size;
    char name[TFS_FILE_NAME_LEN_MAX + 1];
    time_t ctime;
    time_t atime;
    time_t mtime;
};

void tfs_write_addr(char* block, addr_t addr);
addr_t tfs_read_addr(char* block);
void tfs_write_size(char* block, addr_t addr);
addr_t tfs_read_size(char* block);
void hexdump_block(char* block);
void hexdump_all_blocks();
void tfs_write_tstamp(char* block, enum tstamp tstamp, time_t t);
void tfs_write_tstamp_now(char* block, enum tstamp tstamp);
uint64_t tfs_read_tstamp(char* block, enum tstamp tstamp);
void tfs_read_tstamp_into(char* block, enum tstamp tstamp, uint64_t* t);

static struct {
    bool mounted;
    int disk;
} tfs_meta;

struct tfs_file_ptr {
    addr_t block_num;
    uint8_t byte_index;
};
struct tfs_openfile {
    bool live;
    uint16_t size;
    struct tfs_file_ptr ptr;
    int inode_index;
    char name[TFS_FILE_NAME_LEN_MAX + 1];
};
static struct tfs_openfile tfs_openfile_table[TFS_OPEN_FILES_MAX] = {0};


/* Makes a blank TinyFS file system of size nBytes on the unix file specified by ‘filename’. This function should use the emulated disk library to open the specified unix file, and upon success, format the file to be a mountable disk. This includes initializing all data to 0x00, setting magic numbers, initializing and writing the superblock and inodes, etc. Must return a specified success/error code. */
int tfs_mkfs(char *filename, int nBytes) {
    int disk = openDisk(filename, nBytes);
    fail_if(disk);

    int block_count = (nBytes - (nBytes % BLOCKSIZE)) / BLOCKSIZE;

    char block_default[BLOCKSIZE] = {0};
    block_default[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE__FREE;
    block_default[TFS_BLOCK_EVERY_POS_MAGIC] = TFS_BLOCK_MAGIC;

    int block_index = 0;
    for (block_index = 0; block_index < block_count; block_index++) {
        addr_t next_block_index = 0;
        if (block_index + 1 != block_count)
            next_block_index = block_index + 1;
        tfs_write_addr(block_default, next_block_index);
        fail_if(writeBlock(disk, block_index, block_default));
    }

    char block_super[BLOCKSIZE] = {0};
    block_super[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE_SUPER;
    block_super[TFS_BLOCK_EVERY_POS_MAGIC] = TFS_BLOCK_MAGIC;

    addr_t first_free_block_index = 0;
    if (block_count > 1)
        first_free_block_index = 1;
    tfs_write_addr(block_super, first_free_block_index);

    fail_if(writeBlock(disk, TFS_BLOCK_SUPER_INDEX, block_super));

    fail_if(closeDisk(disk));

    return TFS_OK;
}

 
/* tfs_mount(char *diskname) "mounts" a TinyFS file system located within ‘diskname’.As part of the mount operation, tfs_mount should verify the file system is the correct type. In tinyFS, only one file system may be mounted at a time. Use tfs_unmount to cleanly unmount the currently mounted file system. Must return a specified success/error code. */
int tfs_mount(char *diskname) {
    if (tfs_meta.mounted == true)
        fail(TFS_ERR_ALREADY_MOUNTED);
    int disk = openDisk(diskname, 0);
    fail_if(disk);
    char block_super[BLOCKSIZE] = {0};
    fail_if(readBlock(disk, TFS_BLOCK_SUPER_INDEX, block_super));
    if (block_super[TFS_BLOCK_EVERY_POS__TYPE] != TFS_BLOCK_TYPE_SUPER)
        return TFS_ERR_NO_DISK;
    if (block_super[TFS_BLOCK_EVERY_POS_MAGIC] != TFS_BLOCK_MAGIC)
        return TFS_ERR_INVALID;
    tfs_meta.mounted = true;
    tfs_meta.disk = disk;
    return TFS_OK;
}

/*  tfs_unmount(void) "unmounts" the currently mounted file system */
int tfs_unmount(void) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;
    fail_if(closeDisk(tfs_meta.disk));
    tfs_meta.mounted = false;
    return TFS_OK;
}
 
/* Creates or Opens a file for reading and writing on the currently mounted file system. Creates a dynamic resource table entry for the file, and returns a file descriptor (integer) that can be used to reference this entry while the filesystem is mounted. */
fileDescriptor tfs_openFile(char *name) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;

    if (name == NULL)
        return TFS_ERR_INVALID;
            
    int name_len = strlen(name);
    if (name_len > TFS_FILE_NAME_LEN_MAX)
        return TFS_ERR_INVALID;

    int i;
    bool found = false;
    for (i = 0; i < TFS_OPEN_FILES_MAX; i++) {
        if (!tfs_openfile_table[i].live) {
            found = true;
            break;
        }
    }
    if (!found)
        return TFS_ERR_TOO_MANY_FILES;

    fileDescriptor FD = i;

    struct tfs_openfile* file_meta = &tfs_openfile_table[i];

    // find existing file
    char block_tmp[BLOCKSIZE];
    int block_index = 0;
    for (block_index = 0; readBlock(tfs_meta.disk, block_index, block_tmp) >= 0; block_index++) {
        if (block_tmp[TFS_BLOCK_EVERY_POS__TYPE] != TFS_BLOCK_TYPE_INODE) {
            continue;
        }
        // printf("comparing existing %s to new %s\n", &block_tmp[TFS_BLOCK_INODE_POS__NAME], name);
        if (memcmp(&block_tmp[TFS_BLOCK_INODE_POS__NAME], name, name_len) != 0) {
            continue;
        }
        char* block_inode = block_tmp;
        file_meta->inode_index = block_index;
        file_meta->live = true;
        file_meta->size = tfs_read_size(block_inode);
        if (file_meta->size == 0) {
            file_meta->ptr.block_num = block_index;
        } else {
            file_meta->ptr.block_num = tfs_read_addr(block_inode);
        }
        // printf("found file %s\n", name);
        // printf("inode index = %d\n block index = %d\n", file_meta->inode_index, file_meta->ptr.block_num);
        file_meta->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA;
        memcpy(file_meta->name, name, name_len);
        return FD;
    }

    // no file found - create file
    // printf("creating file %s\n", name);
    char block_super[BLOCKSIZE];
    char block_inode[BLOCKSIZE];

    fail_if(readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));

    addr_t inode_index = tfs_read_addr(block_super);
    if (inode_index == 0)
        return TFS_ERR_NO_FREE_BLOCKS;

    fail_if(readBlock(tfs_meta.disk, inode_index, block_inode));
    // printf("inode index = %d\n", inode_index);
    // hexdump_block(block_inode);
    assert(block_inode[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE__FREE, "block type is not free");

    // update super next free block index
    tfs_write_addr(block_super, tfs_read_addr(block_inode));
    // printf("next free block index: %d inode=%d\n", tfs_read_addr(block_super), inode_index);
    fail_if(writeBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));

    // format inode block
    tfs_write_size(block_inode, 0);
    tfs_write_addr(block_inode, 0);
    {
        time_t t = time(NULL);
        tfs_write_tstamp(block_inode, TSTAMP_CREATE, t);
        tfs_write_tstamp(block_inode, TSTAMP_ACCESS, t);
        tfs_write_tstamp(block_inode, TSTAMP_MODIFY, t);
    }
    block_inode[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE_INODE;
    memcpy(&block_inode[TFS_BLOCK_INODE_POS__NAME], name, name_len);
    fail_if(writeBlock(tfs_meta.disk, inode_index, block_inode));


    // format file_meta
    file_meta->live = true;
    file_meta->size = 0;
    file_meta->ptr.block_num = inode_index;
    file_meta->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA;
    file_meta->inode_index = inode_index;
    memcpy(file_meta->name, name, name_len);


    return FD;
}

/* Closes the file, de-allocates all system resources, and removes table entry */ 
int tfs_closeFile(fileDescriptor FD) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;

    if (!tfs_openfile_table[FD].live)
        return TFS_ERR_BAD_FD;

    tfs_openfile_table[FD] = (struct tfs_openfile){0};

    return TFS_OK;
}
 
/* Writes buffer ‘buffer’ of size ‘size’, which represents an entire file’s content, to the file system. Previous content (if any) will be completely lost. Sets the file pointer to 0 (the start of file) when done. Returns success/error codes. */
int tfs_writeFile(fileDescriptor FD, char *buffer, int size) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;
    if (!tfs_openfile_table[FD].live)
        return TFS_ERR_BAD_FD;
    // {
    //     char block_super_tmp[BLOCKSIZE];
    //     if (readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super_tmp) < 0)
    //         return TFS_ERR_TODO;
    //     printf("block super -> %d\n", tfs_read_addr(block_super_tmp));
    // }

    int err;
    char name[TFS_FILE_NAME_LEN_MAX + 1] = {0};
    strncpy(name, tfs_openfile_table[FD].name, TFS_FILE_NAME_LEN_MAX);

    uint64_t ctime = 0;
    /* save ctime */ {
        int inode_index = tfs_openfile_table[FD].inode_index;
        if (inode_index != 0) {
            char block_inode_init[BLOCKSIZE];
            fail_if(readBlock(tfs_meta.disk, inode_index, block_inode_init));

            tfs_read_tstamp_into(block_inode_init, TSTAMP_CREATE, &ctime);
        }
    }

    struct tfs_openfile* file_meta = &tfs_openfile_table[FD];

    if (file_meta->inode_index != 0)
        fail_if(tfs_deleteFile(FD));
        // if ((err = tfs_deleteFile(FD)) < 0)
        //     return err;
    fail_if(tfs_closeFile(FD));
    
    fileDescriptor new_FD = tfs_openFile(name);
    // {
    //     char block_super_tmp[BLOCKSIZE];
    //     if (readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super_tmp) < 0)
    //         return TFS_ERR_TODO;
    //     printf("block super -> %d\n", tfs_read_addr(block_super_tmp));
    // }
    
    if (new_FD < 0)
        return new_FD;
    if (new_FD != FD) {
        // copy new meta to old meta
        memcpy(&tfs_openfile_table[FD], &tfs_openfile_table[new_FD], sizeof(struct tfs_openfile));
        // close old fd
        fail_if(tfs_closeFile(new_FD));
        // if ((err = tfs_closeFile(new_FD)) < 0)
        //     return err;
    }


    if (size == 0)
        return TFS_OK;

    int full_block_count = (size - (size % TFS_BLOCK__FILE_SIZE_DATA)) / TFS_BLOCK__FILE_SIZE_DATA;
    int last_block_size = size % TFS_BLOCK__FILE_SIZE_DATA;
    if (last_block_size == 0) {
        last_block_size = TFS_BLOCK__FILE_SIZE_DATA;
        full_block_count--;
    }

    int total_block_count = full_block_count; /* full blocks */
    if (last_block_size > 0)
        total_block_count++; /* last (partially full) block */
    assert(total_block_count >= 1, "no blocks");

    char block_inode[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, file_meta->inode_index, block_inode));
    // if (readBlock(tfs_meta.disk, file_meta->inode_index, block_inode) < 0)
    //     return TFS_ERR_TODO;

    char block_super[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));

    // printf("block super\n");
    // hexdump_block(block_super);

    /* check if there is space */ {
        int free_block_count = 0;
        addr_t next_free_block_index = tfs_read_addr(block_super);
        // printf("next_free_block_index: %d\n", next_free_block_index);
        if (next_free_block_index == 0)
            return TFS_ERR_NO_FREE_BLOCKS;
        while (next_free_block_index != 0 && free_block_count < total_block_count) {
            free_block_count++;
            char block[BLOCKSIZE];
            assert(readBlock(tfs_meta.disk, next_free_block_index, block) == 0, "failed to read block");
            assert(block[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE__FREE, "block type is not free");
            next_free_block_index = tfs_read_addr(block);
        }
        if (free_block_count < total_block_count)
            return TFS_ERR_INSUFFICIENT_SPACE;
    }

    addr_t block_index = tfs_read_addr(block_super);
    // printf("inode index = %d\nblock index = %d\n", file_meta->inode_index, block_index);
    assert(block_index != 0, "next block not free");
    assert(block_index != file_meta->ptr.block_num, "block_index != meta->ptr.block_num");
    // update inode block addr with first block addr
    tfs_write_addr(block_inode, block_index);
    tfs_write_size(block_inode, size);
    file_meta->size = size;
    // set file ptr to zero
    // assert(file_meta->ptr.block_num == block_index, "file ptr is not zero is %d", file_meta->ptr.block_num);
    file_meta->ptr.block_num = block_index;
    file_meta->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA;

    int written_blocks_count = 0;
    while (written_blocks_count < full_block_count) {
        char block[BLOCKSIZE];
        fail_if(readBlock(tfs_meta.disk, block_index, block));
        block[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE__DATA;
        char* block_data = &buffer[written_blocks_count * TFS_BLOCK__FILE_SIZE_DATA];
        memcpy(&block[TFS_BLOCK__FILE_POS__DATA], block_data, TFS_BLOCK__FILE_SIZE_DATA);
        fail_if(writeBlock(tfs_meta.disk, block_index, block));
        // if (writeBlock(tfs_meta.disk, block_index, block) < 0)
        //     return TFS_ERR_TODO;
        block_index = tfs_read_addr(block);
        written_blocks_count++;
    }

    int last_block_index = block_index;
    char block_last[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, last_block_index, block_last));
    // if (readBlock(tfs_meta.disk, last_block_index, block_last) < 0)
    //     return TFS_ERR_TODO;
    addr_t next_free_block_index = tfs_read_addr(block_last);
    tfs_write_addr(block_last, 0);
    block_last[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE__DATA;
    char* block_data = &buffer[full_block_count * TFS_BLOCK__FILE_SIZE_DATA];
    assert(last_block_size <= TFS_BLOCK__FILE_SIZE_DATA, "last block size is too big");
    memcpy(&block_last[TFS_BLOCK__FILE_POS__DATA], block_data, last_block_size);
    fail_if(writeBlock(tfs_meta.disk, last_block_index, block_last));
    // if (writeBlock(tfs_meta.disk, last_block_index, block_last) < 0)
    //     return TFS_ERR_TODO;

    tfs_write_addr(block_super, next_free_block_index);
    fail_if(writeBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));
    // if (writeBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super) < 0)
    //     return TFS_ERR_TODO;

    {
        time_t t= time(NULL);
        time_t new_ctime = t;
        if (ctime != 0)
            new_ctime = ctime;
        tfs_write_tstamp(block_inode, TSTAMP_CREATE, new_ctime);
        tfs_write_tstamp(block_inode, TSTAMP_ACCESS, t);
        tfs_write_tstamp(block_inode, TSTAMP_MODIFY, t);
    }
    // save updated inode
    fail_if(writeBlock(tfs_meta.disk, file_meta->inode_index, block_inode));
    // if (writeBlock(tfs_meta.disk, file_meta->inode_index, block_inode) < 0)
    //     return TFS_ERR_TODO;

    return TFS_OK;
}
 
/* deletes a file and marks its blocks as free on disk. */
int tfs_deleteFile(fileDescriptor FD) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;

    struct tfs_openfile* file = &tfs_openfile_table[FD];
    if (!file->live)
        return TFS_ERR_BAD_FD;

    assert(file->inode_index != 0, "inode index is zero");

    int inode_index = file->inode_index;
    /* zero out file meta - keeping name & live */ {
        struct tfs_openfile new_file_meta = {0};
        new_file_meta.live = true;
        memcpy(new_file_meta.name, file->name, TFS_FILE_NAME_LEN_MAX);
        memcpy(file, &new_file_meta, sizeof(struct tfs_openfile));
    }

    // file->size = 0;
    // file->ptr.block_num = 0;
    // file->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA;
    // file->inode_index = 0;

    char block_inode[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, inode_index, block_inode));
    assert(block_inode[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE_INODE, "block type is not inode");
    assert(block_inode[TFS_BLOCK_EVERY_POS_MAGIC] == TFS_BLOCK_MAGIC, "block magic is not correct");

    char block_super[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));
    assert(block_super[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE_SUPER, "block type is not super");
    addr_t first_free_block_index = tfs_read_addr(block_super);

    addr_t block_index = tfs_read_addr(block_inode);

    while (block_index != 0) {
        char block[BLOCKSIZE];
        fail_if(readBlock(tfs_meta.disk, block_index, block));
        // printf("before\n");
        // hexdump_block(block);
        assert(block[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE__DATA, "block type is not data");
        assert(block[TFS_BLOCK_EVERY_POS_MAGIC] == TFS_BLOCK_MAGIC, "block magic is not correct");
        addr_t next_block_index = tfs_read_addr(block);

        block[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE__FREE;
        char * block_data = &block[TFS_BLOCK__FILE_POS__DATA];
        memset(block_data, 0, TFS_BLOCK__FILE_SIZE_DATA);
        if (next_block_index == 0)
            // connect freed block to previously first in free block chain
            tfs_write_addr(block, first_free_block_index);

        // printf("after\n");
        // hexdump_block(block);
        fail_if(writeBlock(tfs_meta.disk, block_index, block));
        // if (writeBlock(tfs_meta.disk, block_index, block) < 0)
        //     return TFS_ERR_TODO;
        block_index = next_block_index;
    }

    if (block_index == 0)
        block_index = first_free_block_index;

    block_inode[TFS_BLOCK_EVERY_POS__TYPE] = TFS_BLOCK_TYPE__FREE;
    char * block_inode_data = &block_inode[TFS_BLOCK__FILE_POS__DATA];
    memset(block_inode_data, 0, TFS_BLOCK__FILE_SIZE_DATA);
    // printf("changing inode block at %d ptr from %d -> %d\n", inode_index, tfs_read_addr(block_inode), block_index);
    if (tfs_read_addr(block_inode) == 0)
        tfs_write_addr(block_inode, block_index);
    fail_if(writeBlock(tfs_meta.disk, inode_index, block_inode));

    assert(inode_index != 0, "inode index is zero");
    tfs_write_addr(block_super, inode_index);
    fail_if(writeBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));
    return TFS_OK;
}

/* reads one byte from the file and copies it to buffer, using the current file pointer location and incrementing it by one upon success. If the file pointer is already past the end of the file then tfs_readByte() should return an error and not increment the file pointer. */ 
int tfs_readByte(fileDescriptor FD, char *buffer) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;

    struct tfs_openfile* file_meta = &tfs_openfile_table[FD];
    if (!file_meta->live)
        return TFS_ERR_BAD_FD;

    if (file_meta->ptr.block_num == file_meta->inode_index)
        return TFS_ERR_OUT_OF_BOUNDS;

    /* update atime */ {
        int inode_index = file_meta->inode_index;
        if (inode_index != 0) {
            char block_inode_init[BLOCKSIZE];
            fail_if(readBlock(tfs_meta.disk, inode_index, block_inode_init));

            tfs_write_tstamp_now(block_inode_init, TSTAMP_ACCESS);
        }
    }
    char block[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, file_meta->ptr.block_num, block));
    assert(file_meta->ptr.byte_index >= TFS_BLOCK__FILE_POS__DATA, "byte index is before data");
    // assert(file_meta->ptr.byte_index != 255, "file ptr not incremented to next block");

    *buffer = block[file_meta->ptr.byte_index];
    if (file_meta->ptr.byte_index == 255) {
        addr_t next_addr = tfs_read_addr(block);
        if (next_addr == 0)
            next_addr = file_meta->inode_index;
        file_meta->ptr.block_num = next_addr;
        file_meta->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA;
    } else {
        file_meta->ptr.byte_index++;
    }

    return TFS_OK;
}
 
/* change the file pointer location to offset (absolute). Returns success/error codes.*/ 
int tfs_seek(fileDescriptor FD, int offset) {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;
    if (!tfs_openfile_table[FD].live)
        return TFS_ERR_BAD_FD;

    int byte = offset % TFS_BLOCK__FILE_SIZE_DATA;
    int block = (offset - byte) / TFS_BLOCK__FILE_SIZE_DATA;

    struct tfs_openfile* file_meta = &tfs_openfile_table[FD];

    char block_inode[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, file_meta->inode_index, block_inode));

    int block_index = tfs_read_addr(block_inode);

    while (block > 0) {
        char block_tmp[BLOCKSIZE];
        fail_if(readBlock(tfs_meta.disk, block_index, block_tmp));
        block_index = tfs_read_addr(block_tmp);
        block--;
    }

    file_meta->ptr.block_num = block_index;
    file_meta->ptr.byte_index = TFS_BLOCK__FILE_POS__DATA + byte;
    return TFS_OK;
}

struct tfs_stat tfs_readFileInfo(fileDescriptor FD) {
    if (!tfs_meta.mounted)
        return (struct tfs_stat){.err = TFS_ERR_NOT_MOUNTED, 0};
    if (!tfs_openfile_table[FD].live)
        return (struct tfs_stat){.err = TFS_ERR_BAD_FD, 0};

    struct tfs_openfile* file_meta = &tfs_openfile_table[FD];
    char block_inode[BLOCKSIZE];
    int res = readBlock(tfs_meta.disk, file_meta->inode_index, block_inode);
    if (res < 1)
        return (struct tfs_stat){.err = res, 0};

    struct tfs_stat tmp = (struct tfs_stat){
        .err = TFS_OK,
        .size = tfs_read_size(block_inode),
        .ctime = tfs_read_tstamp(block_inode, TSTAMP_CREATE),
        .atime = tfs_read_tstamp(block_inode, TSTAMP_ACCESS),
        .mtime = tfs_read_tstamp(block_inode, TSTAMP_MODIFY),
        0
    };

    memcpy(tmp.name, file_meta->name, TFS_FILE_NAME_LEN_MAX);
    return tmp;
}

/******************************************************/
/****************** Helper functions ******************/
/******************************************************/

void tfs_write_addr(char* block, uint16_t addr) {
    union {
        uint16_t addr;
        char addr_bytes[2];
    } addr_union;
    addr_union.addr = addr;
    block[TFS_BLOCK_EVERY_POS__ADDR] = addr_union.addr_bytes[0];
    block[TFS_BLOCK_EVERY_POS__ADDR + 1] = addr_union.addr_bytes[1];
}

uint16_t tfs_read_addr(char* block) {
    union {
        uint16_t addr;
        char addr_bytes[2];
    } addr_union;
    addr_union.addr_bytes[0] = block[TFS_BLOCK_EVERY_POS__ADDR];
    addr_union.addr_bytes[1] = block[TFS_BLOCK_EVERY_POS__ADDR + 1];
    return addr_union.addr;
}

void tfs_write_size(char* block, uint16_t addr) {
    union {
        uint16_t addr;
        char addr_bytes[2];
    } addr_union;
    addr_union.addr = addr;
    block[TFS_BLOCK_INODE_POS__SIZE] = addr_union.addr_bytes[0];
    block[TFS_BLOCK_INODE_POS__SIZE + 1] = addr_union.addr_bytes[1];
}

uint16_t tfs_read_size(char* block) {
    union {
        uint16_t addr;
        char addr_bytes[2];
    } addr_union;
    addr_union.addr_bytes[0] = block[TFS_BLOCK_INODE_POS__SIZE];
    addr_union.addr_bytes[1] = block[TFS_BLOCK_INODE_POS__SIZE + 1];
    return addr_union.addr;
}

void hexdump_block(char* block) {
    int i;
    for (i = 0; i < BLOCKSIZE; i++) {
        printf("%02X ", block[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    printf("\n");
    fflush(stdout);
}

void hexdump_all_blocks() {
    if (!tfs_meta.mounted)
        panic("not mounted - can't hexdump\n");
    int disk = tfs_meta.disk;
    char block[BLOCKSIZE];
    int block_index = 0;
    while (readBlock(disk, block_index, block) >= 0) {
        printf("block %d\n", block_index);
        hexdump_block(block);
        block_index++;
    }
}

int tfs_free_block_count() {
    if (!tfs_meta.mounted)
        return TFS_ERR_NOT_MOUNTED;
    char block_super[BLOCKSIZE];
    fail_if(readBlock(tfs_meta.disk, TFS_BLOCK_SUPER_INDEX, block_super));
    addr_t next_free_block_index = tfs_read_addr(block_super);

    int free_block_count = 0;
    // hexdump_all_blocks();
    while (next_free_block_index != 0) {
        // printf("free block %d\n", next_free_block_index);
        free_block_count++;
        char block[BLOCKSIZE];
        int err;
        if ((err = readBlock(tfs_meta.disk, next_free_block_index, block)) < 0)
            panic("failed to read block %d", next_free_block_index);
        // printf("free block %d\n", next_free_block_index);
        // hexdump_block(block);
        assert(block[TFS_BLOCK_EVERY_POS__TYPE] == TFS_BLOCK_TYPE__FREE, "block type is not free is %d", block[TFS_BLOCK_EVERY_POS__TYPE]);
        next_free_block_index = tfs_read_addr(block);
    }
    return free_block_count;
}

void tfs_write_tstamp(char* block, enum tstamp tstamp, time_t t) {
    int index = 0;
    if (tstamp == TSTAMP_CREATE) {
        index = TFS_BLOCK_INODE_POS_CTIME;
    } else if (tstamp == TSTAMP_ACCESS) {
        index = TFS_BLOCK_INODE_POS_ATIME;
    } else if (tstamp == TSTAMP_MODIFY) {
        index = TFS_BLOCK_INODE_POS_MTIME;
    } else {
        panic("invalid tstamp %d", tstamp);
    }
    uint64_t t64 = t;
    assert(sizeof(uint64_t) == 8, "uint64_t is not 8 bytes");
    memcpy(&block[index], &t64, sizeof(uint64_t));
}

void tfs_write_tstamp_now(char* block, enum tstamp tstamp) {
    time_t t = time(NULL);
    tfs_write_tstamp(block, tstamp, t);
}

uint64_t tfs_read_tstamp(char* block, enum tstamp tstamp) {
    int index = 0;
    if (tstamp == TSTAMP_CREATE) {
        index = TFS_BLOCK_INODE_POS_CTIME;
    } else if (tstamp == TSTAMP_ACCESS) {
        index = TFS_BLOCK_INODE_POS_ATIME;
    } else if (tstamp == TSTAMP_MODIFY) {
        index = TFS_BLOCK_INODE_POS_MTIME;
    } else {
        panic("invalid tstamp %d", tstamp);
    }
    uint64_t t;
    memcpy(&t, &block[index], sizeof(uint64_t));
    return t;
}

void tfs_read_tstamp_into(char* block, enum tstamp tstamp, uint64_t* t) {
    *t = tfs_read_tstamp(block, tstamp);
}
