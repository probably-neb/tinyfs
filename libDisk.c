#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "tinyFS.h"
#include "TinyFS_errno.h"

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

bool fd_valid(int fd);
int tlbntopbn(int lbn);
int seek_inbounds(int fd, off_t offset);

/**
 * This functions opens a regular UNIX file and designates the first 
 * nBytes of it as space for the emulated disk. If nBytes is not exactly a 
 * multiple of BLOCKSIZE then the disk size will be the closest multiple 
 * of BLOCKSIZE that is lower than nByte (but greater than 0) If nBytes is 
 * less than BLOCKSIZE failure should be returned. If nBytes > BLOCKSIZE 
 * and there is already a file by the given filename, that file’s content 
 * may be overwritten. If nBytes is 0, an existing disk is opened, and the 
 * content must not be overwritten in this function. There is no requirement 
 * to maintain integrity of any file content beyond nBytes. The return value 
 * is negative on failure or a disk number on success.
 */
int openDisk(char *filename, int nBytes) {
    if (nBytes < BLOCKSIZE && nBytes != 0) {
        return -1;
    }
    int flags = O_RDWR;
    if (nBytes != 0) {
        flags = flags | O_CREAT;
    }
    int fd = open(filename, flags, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return fd;
    }

    // int rem = nBytes % BLOCKSIZE;
    // nBytes -= rem;

    // set file len to nBytes
    // note: before adjusting nBytes by block size
    if (nBytes != 0 && ftruncate(fd, nBytes) < 0)
        return -(errno);

    // if (nBytes == 0) {
    //     return fd;
    // }
    // TODO : overwrite file contents if nBytes > 0
    return fd;
}

/**
 * self explanatory
 */
int closeDisk(int disk) {
    if (!fd_valid(disk)) {
        // file already closed
        return -1;
    }
    return close(disk);
}

/**
 * readBlock() reads an entire block of BLOCKSIZE bytes from the open 
 * disk (identified by ‘disk’) and copies the result into a local buffer 
 * (must be at least of BLOCKSIZE bytes). The bNum is a logical block 
 * number, which must be translated into a byte offset within the disk. The 
 * translation from logical to physical block is straightforward: bNum=0 
 * is the very first byte of the file. bNum=1 is BLOCKSIZE bytes into the 
 * disk, bNum=n is n*BLOCKSIZE bytes into the disk. On success, it returns 
 * 0. -1 or smaller is returned if disk is not available (hasn’t been 
 * opened) or any other failures. You must define your own error code 
 * system.
 */
int readBlock(int disk, int bNum, void *block) {
    int pbn = tlbntopbn(bNum);
    int err;
    if ((err = seek_inbounds(disk, pbn)) < 0) {
        return err;
    }
    if ((err = read(disk, block, BLOCKSIZE)) < 0) {
        return err;
    }
    return 0;
} 

 
/**
 * writeBlock() takes disk number ‘disk’ and logical block number ‘bNum’ 
 * and writes the content of the buffer ‘block’ to that location. ‘block’ 
 * must be integral with BLOCKSIZE. Just as in readBlock(), writeBlock()
 * must translate the logical block bNum to the correct byte position in 
 * the file. On success, it returns 0. -1 or smaller is returned if disk 
 * is not available (i.e. hasn’t been opened) or any other failures. You 
 * must define your own error code system.
 */
int writeBlock(int disk, int bNum, void *block) {
    int pbn = tlbntopbn(bNum);
    int err;
    if ((err = seek_inbounds(disk, pbn)) < 0) {
        return err;
    }
    if ((err = write(disk, block, BLOCKSIZE)) < 0) {
        return err;
    }
    return 0;
}

int seek_inbounds(int disk, off_t offset) {
    int err;
    off_t size;
    if ((err = size = lseek(disk, 0, SEEK_END)) < 0) {
        return err;
    }
    size = size - (size % BLOCKSIZE);
    if (offset > size) {
        return TFS_ERR_OUT_OF_BOUNDS;
    }
    if ((err = lseek(disk, offset, SEEK_SET)) < 0) {
        return err;
    }
    return 0;
}


bool fd_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

int tlbntopbn(int lbn) {
    return lbn * BLOCKSIZE;
}
