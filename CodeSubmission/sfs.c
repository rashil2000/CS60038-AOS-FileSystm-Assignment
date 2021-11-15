#include "disk.h"
#include "sfs.h"

#include <stdlib.h>
#include <string.h>

disk *g_diskptr = NULL;
super_block *g_sb = NULL;

int ceili(int numerator, int denominator)
{
	return (numerator / denominator + (numerator % denominator != 0));
}

int format(disk *diskptr)
{
	/* Calculating stuff */
	int i, N, M, I, IB, R, DBB, DB;

	N   = diskptr->blocks;
	M   = N - 1;
	I   = 0.1 * M;
	IB  = ceili(I * 128, 8 * BLOCKSIZE);
	R   = M - I - IB;
	DBB = ceili(R, 8 * BLOCKSIZE);
	DB  = R - DBB;

	/* Creating superblock */
	super_block sb = {
		.magic_number           = 12345,
		.blocks                 = M,
		.inode_blocks           = I,
		.inodes                 = I * 128,
		.inode_bitmap_block_idx = 1,
		.data_block_bitmap_idx  = 1 + IB,
		.inode_block_idx        = 1 + IB + DBB,
		.data_block_idx         = 1 + IB + DBB + I,
		.data_blocks            = DB
	};

	/* Writing the superblock to disk */
	if (write_block(diskptr, 0, &sb))
		return -1;

	/* Creating a dummy buffer to fill blocks */
	void *buf = malloc(BLOCKSIZE * sizeof(char));
	memset(buf, 0, BLOCKSIZE * sizeof(char));

	/* Writing inode bitmap to disk */
	for (i = sb.inode_bitmap_block_idx; i < sb.data_block_bitmap_idx; i++)
		if (write_block(diskptr, i, buf))
			return -1;

	/* Writing datablock bitmap to disk */
	for (i = sb.data_block_bitmap_idx; i < sb.inode_block_idx; i++)
		if (write_block(diskptr, i, buf))
			return -1;

	/* Create a dummy inode array to fill blocks */
	inode dum[128];
	for (i = 0; i < 128; i++)
		dum[i].valid = 0;

	/* Writing empty inodes to disk */
	for (i = sb.inode_block_idx; i < sb.data_block_idx; i++)
		if (write_block(diskptr, i, (void *)dum))
			return -1;

	return 0;
}

int mount(disk *diskptr)
{
	if (!diskptr)
		return -1;

	/* Trying to read superblock into a temporary variable */
	void *buf = malloc(BLOCKSIZE * sizeof(char));
	super_block *temp = malloc(sizeof(super_block));
	read_block(diskptr, 0, buf);
	memcpy(temp, buf, sizeof(super_block));

	if (temp->magic_number != MAGIC)
		return -1;

	/* Successful, update global state */
	g_diskptr = diskptr;
	g_sb = temp;

	return 0;
}
