#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#include "libTinyFS.c"
#include "libTinyFS.h"
#include "tinyFS.h"


int main() {
	if (tfs_mkfs("./tinyFSDemo.dsk", 8192) < 0) {
        perror("Error creating file system\n");
        exit(1);
    }
    if (tfs_mount("./tinyFSDemo.dsk") < 0) {
        perror("Error mounting file system\n");
        exit(1);
    }
    printf("Consistency Check Passed!!\n");
	char name[4096] = {0};
	char input[4096] = {0};
	char paddedInput[4096] = {0}; /* no buffer overflows allowed unless you type in 8k characters then they are completely allowed */
	printf("Enter File Name: ");
	scanf("%s", name);
	printf("Enter File Contents: ");
	scanf("%s", input);

	int fd = tfs_openFile(name);
	printf("Opened file with fd: %d\n", fd);
    printf("Writing `%s` to file (len=%lu)\n", input, strlen(input));
	tfs_writeFile(fd, input, strlen(input));
	printf("Wrote to file\n");
	struct tfs_stat info;
    errno = -info.err;
    perror("Stat Status");
	info = tfs_readFileInfo(fd);
	printf("Printing File info...\n");
	printf("File name: %s\n", info.name);
	printf("File size: %d\n", info.size);
	printf("File Creation Time: %s", ctime(&info.ctime));
    printf("File Modification Time: %s", ctime(&info.mtime));
    printf("File Access Time: %s", ctime(&info.atime));

	{
		printf("Reading file contents: \n");
		char buf;
		while (tfs_readByte(fd, &buf) >= 0) {
			printf("%c", buf);
		}
	}
    printf("\n");
    info = tfs_readFileInfo(fd);
    printf("Printing Updated File info...\n");
	printf("File Creation Time: %s", ctime(&info.ctime));
    printf("File Modification Time: %s", ctime(&info.mtime));
    printf("File Access Time: %s", ctime(&info.atime));

}
