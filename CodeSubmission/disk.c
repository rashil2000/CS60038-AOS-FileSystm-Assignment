// Rashil Gandhi
// 18CS30036

#include "disk.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

disk *create_disk(int nbytes)
{
	int nblocks = (nbytes - 24) / BLOCKSIZE;

	disk* ret = malloc(sizeof(disk));
	ret->size = nbytes;
	ret->blocks = nblocks;
	ret->reads = 0;
	ret->writes = 0;
	ret->block_arr = malloc(nblocks * sizeof(char *));
	for (int i = 0; i < nblocks; i++)
		ret->block_arr[i] = malloc(BLOCKSIZE * sizeof(char));

	return ret;
}

int read_block(disk *diskptr, int blocknr, void *block_data)
{
	if (blocknr < 0 || blocknr > diskptr->blocks - 1)
		return -1;

	memcpy(block_data, diskptr->block_arr[blocknr], BLOCKSIZE * sizeof(char));
	diskptr->reads++;

	return 0;
}

int write_block(disk *diskptr, int blocknr, void *block_data)
{
	if (blocknr < 0 || blocknr > diskptr->blocks - 1)
		return -1;

	memcpy(diskptr->block_arr[blocknr], block_data, BLOCKSIZE * sizeof(char));
	diskptr->writes++;

	return 0;
}

int free_disk(disk *diskptr)
{
	if(!diskptr)
		return 0;

	for (int i = 0; i < diskptr->blocks; i++)
		free(diskptr->block_arr[i]);

	free(diskptr->block_arr);
	free(diskptr);

	return 0;
}
