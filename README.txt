NOTE: Assuming tfs_deleteFile does not close the file

I worked alone.

The main tradeoffs I made were the following

1. I used unix file descriptors (the ones returned by `open (2)`) for my disks. This made reading and writing very easy
	but made not writing to out of bounds blocks difficult

2. Writing files is done by
	a) deleting the file (after saving some info like ctime and name)
	b) reopening the file
	c) closing the old file
	d) copying the new file info to where the old file info was stored
		- note this is just within the file entry table. No changes are made to the file blocks except obviously their deletion initially
	c) writing all new blocks using the free list

	This made fixing bugs significantly harder (and I'm sure there's a few I didn't catch) as I had to have all of the used functions working
	just to write files. However, I thought it would make the logic for the actual writing simpler as I wouldn't need to keep track of the free list and existing file node list.
	If I could go back and just overwrite the existing ones I would do it but it's 11pm.

3. The free list is implemented as a linked list of pointers in each block. Each pointer is two bytes

4. Files remain open after being deleted. I wasn't sure if files should be closed or left open when they were deleted.
	I opted to have them stay open

5. All of the errors defined in `TinyFS_errno.h` are just the negative of an `errno` value of similar meaning. They have to be negated
	as all errno values are positive, and negative values are used for error values in the TinyFS API.


The additional features I implemented were

1) Timestamps
	the `tfs_readFileInfo` function returns a `struct tfs_stat` that contains the files name, size, ctime, mtime, and atime as well as an `err` field
	that is an int and indicates whether an error occured (using errors from `TinyFS_errno.h`). The error return path is a bit wierd admittedly but
	I opted to stick with the function arguments signature as described in the assignment doc.

2) Consistency checks
	on Mount I check for the following
	1) all blocks having magic set
	2) all inodes having a null file content pointer if their size is zero
