CC = gcc
CFLAGS = -Wall -g
PROG = tinyFSDemo
OBJS = tinyFSDemo.o libTinyFS.o libDisk.o

$(PROG):
	gcc -o tinyFSDemo tinyFSDemo.c libTinyFS.c libDisk.c

tinyFsDemo.o: tinyFSDemo.c libTinyFS.h tinyFS.h TinyFS_errno.h
	$(CC) $(CFLAGS) -c -o $@ $<

libTinyFS.o: libTinyFS.c libTinyFS.h tinyFS.h libDisk.h libDisk.o TinyFS_errno.h
	$(CC) $(CFLAGS) -c -o $@ $<

libDisk.o: libDisk.c libDisk.h tinyFS.h TinyFS_errno.h
	$(CC) $(CFLAGS) -c -o $@ $<

submission:
	tar -cvf submission.tar tinyFSDemo.c libTinyFS.c libDisk.c libTinyFS.h libDisk.h tinyFS.h TinyFS_errno.h Makefile README.txt
	gzip submission.tar


clean:
	rm -f $(PROG) $(OBJS)
