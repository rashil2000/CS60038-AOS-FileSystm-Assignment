// Rashil Gandhi
// 18CS30036

#include "disk.h"
#include "sfs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define Min(x,y) ((x < y) ? x : y)
#define Max(x,y) ((x > y) ? x : y)
#define SetBit(A,k)   (A |=  (1 << k))
#define ClearBit(A,k) (A &= ~(1 << k))
#define TestBit(A,k)  (A &   (1 << k))

disk *g_diskptr = NULL;
super_block *g_sb = NULL;

int _ceili(int numerator, int denominator)
{
  return (numerator / denominator + (numerator % denominator != 0));
}

int _get_set_free_bit(int s, int e)
{
  char *buf = malloc(BLOCKSIZE * sizeof(char));
  int bits_read = 0, ret = -1;
  for (int i = s; i < e; i++)
  {
    read_block(g_diskptr, i, buf);
    for (int j = 0; j < BLOCKSIZE; j++)
    {
      for (int k = 0; k < 8; k++, bits_read++)
      {
        if (!TestBit(buf[j], k))
        {
          ret = bits_read;
          SetBit(buf[j], k);
          write_block(g_diskptr, i, (void *)buf);
          break;
        }
      }
      if (ret != -1)
        break;
    }
    if (ret != -1)
      break;
  }

  free(buf);
  return ret;
}

int _clear_bit(int idx, int s)
{
  uint32_t block_idx = (idx / 32) / 1024, block_off = (idx / 32) % 1024, bit_off = idx % 32;

  uint32_t buf[1024];
  if (read_block(g_diskptr, s + block_idx, buf))
    return -1;
  ClearBit(buf[block_off], bit_off);
  if (write_block(g_diskptr, s + block_idx, buf))
    return -1;

  return 0;
}

file* _get_directory(int inumber, int off)
{
  int inode_idx = inumber / 128, inode_off = inumber % 128, blocknr;
  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return NULL;
  inode in = buf[inode_off];

  if ((in.size / sizeof(file)) <= off)
    return NULL;

  if (off < 5 * (BLOCKSIZE / sizeof(file)))
    blocknr = in.direct[off / (BLOCKSIZE / sizeof(file))];
  else
    return NULL;

  file dir[BLOCKSIZE / sizeof(file)];

  if (read_block(g_diskptr, g_sb->data_block_idx + blocknr, dir) < 0)
    return NULL;

  file* d = (file *) malloc(sizeof(file));
  *d = dir[off % (BLOCKSIZE / sizeof(file))];

  free(buf);
  return d;
}

dir_union _get_directory_offset(int inumber, char* name, int get_offset)
{
  file *item;
  dir_union ret;
  int off = 0;

  while (1)
  {
    item = _get_directory(inumber, off);
    if (!item)
      break;
    if (strcmp(name, item->name) == 0)
    {
      if (get_offset)
      {
        ret.off = off;
        return ret;
      }
      else
      {
        ret.dir = item;
        return ret;
      }
    }
    free(item);
    off++;
  }

  if (get_offset)
    ret.off = -1;
  else
    ret.dir = item;
  return ret;
}

int _get_path_metadata(char* dirpath, char** basename)
{
  int n_parts, len = strlen(dirpath);

  if(len > 0 && dirpath[0] == '/')
    dirpath++, len--;

  if(len > 1 && dirpath[len-1] == '/')
    dirpath[len-1] = '\0';

  n_parts = 1, len = strlen(dirpath);

  for (int i = 0; i < len; ++i)
    n_parts += (dirpath[i] == '/') ? 1 : 0;

  char** parts = (char **)malloc(n_parts * sizeof(char *));
  for (int i = 0; i < n_parts; i++)
    parts[i] = (char *) malloc(MAX_LEN * sizeof(char));

  // length of dir name should be positive
  if (len==0)
  {
    n_parts = 0;
    return -1;
  }

  int j = 0;
  for (int i = 0; i < n_parts; i++)
  {
    int k = 0;
    while (k < MAX_LEN && j < len && dirpath[j] != '/')
    {
      parts[i][k] = dirpath[j];
      k++,j++;
    }
    // length of dir name should be <= MAX_LEN
    if (k >= MAX_LEN)
    {
      n_parts=0;
      return -1;
    }
    parts[i][k] = '\0';
    j++;
  }

  int curr_dir_in = 0;

  for (int i = 0; i < n_parts - 1; i++)
  {
    dir_union h = _get_directory_offset(curr_dir_in, parts[i], 0);
    if (!h.dir)
      return -1;

    curr_dir_in = h.dir->inumber;
    free(h.dir);
  }

  int parent_in = curr_dir_in;

  *basename = (char *)malloc(MAX_LEN * sizeof(char));
  strcpy(*basename, parts[n_parts - 1]);

  for (int i = 0; i < n_parts; i++)
    free(parts[i]);

  return parent_in;
}

int _init_directory(int inumber, char *name, int type)
{
  int inode_idx = inumber / 128, inode_off = inumber % 128;
  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;
  inode in = buf[inode_off];

  int num_ls = in.size / sizeof(file), nbr;
  int off = num_ls;

  if (off < 5 * (BLOCKSIZE / sizeof(file)))
  {
    int blocknr = off / (BLOCKSIZE / sizeof(file));
    int offset = off % (BLOCKSIZE / sizeof(file));

    if (offset == 0)
    {
      uint32_t free_idx = _get_set_free_bit(g_sb->data_block_bitmap_idx, g_sb->inode_block_idx);
      if (free_idx < 0)
        return -1;
      in.direct[blocknr] = free_idx;
      in.valid = 1;
      buf[inode_off] = in;
      write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);
    }

    nbr = in.direct[blocknr];
  }
  else
    nbr = -1;

  if (nbr < 0)
    return -1;

  file dir[BLOCKSIZE / sizeof(file)];
  if (read_block(g_diskptr, g_sb->data_block_idx + nbr, dir))
    return -1;

  file d;
  d.valid = 1;
  d.type = type;
  d.length = strlen(name);
  d.inumber = create_file();
  strcpy(d.name, name);

  dir[num_ls % (BLOCKSIZE / sizeof(file))] = d;
  if (write_block(g_diskptr, g_sb->data_block_idx + nbr, dir) < 0)
    return -1;

  in.size += sizeof(file);
  buf[inode_off] = in;
  if (write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;

  free(buf);
  return d.inumber;
}

int _purge_directory(int inumber, char *filename)
{
  dir_union item, h;
  item = _get_directory_offset(inumber, filename, 0);
  if (!item.dir)
    return -1;

  h = _get_directory_offset(inumber, filename,1);

  if (item.dir->type)
  {
    int off = 0;
    file *d;
    int try_once=1;

    while (1)
    {
      d = _get_directory(item.dir->inumber, off);
      if(!d && !try_once)
        break;

      if(!d && try_once)
      {
        try_once=0;
        off=0;
        continue;
      }
      if (_purge_directory(item.dir->inumber, d->name) < 0)
        return -1;
      free(d);
      off++;
    }

    int inode_idx = item.dir->inumber / 128, inode_off = item.dir->inumber % 128;
    inode *buf = malloc(BLOCKSIZE * sizeof(char));
    if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
      return -1;
    inode in = buf[inode_off];

    for (int i = 0; i < 5; i++)
      _clear_bit(in.direct[i], g_sb->data_block_bitmap_idx);

    _clear_bit(item.dir->inumber, g_sb->inode_bitmap_block_idx);
  }
  else
    remove_file(item.dir->inumber);

  int inode_idx = inumber / 128, inode_off = inumber % 128;
  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;
  inode in = buf[inode_off];

  int num_ls = in.size / sizeof(file);

  file* lst = NULL;

  if (num_ls > 1)
    lst = _get_directory(inumber, num_ls - 1);

  in.size -= sizeof(file);
  if (in.size == 0)
    in.valid = 0;
  buf[inode_off] = in;
  write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);

  if (!lst)
    return 0;

  int nbr;

  if ((in.size / sizeof(file)) > h.off)
  {
    if (h.off < 5 * (BLOCKSIZE / sizeof(file)))
      nbr = in.direct[h.off / (BLOCKSIZE / sizeof(file))];
    else
      nbr = -1;
  }
  else
    nbr = -1;

  file dir[BLOCKSIZE / sizeof(file)];
  if (read_block(g_diskptr, g_sb->data_block_idx + nbr, dir) < 0)
    return -1;

  dir[h.off % (BLOCKSIZE / sizeof(file))] = *lst;

  if (write_block(g_diskptr, g_sb->data_block_idx + nbr, dir) < 0)
    return -1;

  free(lst);
  free(buf);
  return 0;
}

int format(disk *diskptr)
{
  /* Calculating stuff */
  int i, N, M, I, IB, R, DBB, DB;

  N   = diskptr->blocks;
  M   = N - 1;                          // Number of usable blocks
  I   = 0.1 * M;                        // Number of inode blocks
                                        /* Number of inodes
                                           = I*(size of block)/(size of inode)
                                           = I*(4096)/(32)
                                           = I*128 */
  IB  = _ceili(I * 128, 8 * BLOCKSIZE); // Number of blocks needed to store inode bitmap
  R   = M - I - IB;
  DBB = _ceili(R, 8 * BLOCKSIZE);       // Number of blocks needed to store datablock bitmap
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
  void *buf = malloc(BLOCKSIZE * sizeof(char));
  memcpy(buf, &sb, sizeof(super_block));
  if (write_block(diskptr, 0, buf))
    return -1;

  /* Creating a dummy buffer to fill blocks */
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

  free(buf);
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

  free(buf);
  free(temp);
  return 0;
}

int create_file()
{
  if (!g_diskptr)
    return -1;

  int free_idx = _get_set_free_bit(g_sb->inode_bitmap_block_idx, g_sb->data_block_bitmap_idx);
  int inode_idx = free_idx / 128, inode_off = free_idx % 128;

  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;
  buf[inode_off].valid = 1;
  buf[inode_off].size = 0;
  write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);

  free(buf);
  return free_idx;
}

int remove_file(int inumber)
{
  if (!g_diskptr)
    return -1;

  int inode_idx = inumber / 128, inode_off = inumber % 128;

  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;

  int sz_inode = _ceili(buf[inode_off].size, BLOCKSIZE);
  if (sz_inode < 6)
    for (int i = 0; i < sz_inode; i++)
      _clear_bit(buf[inode_off].direct[i], g_sb->data_block_bitmap_idx);
  else
  {
    for (int i = 0; i < 5; i++)
      _clear_bit(buf[inode_off].direct[i], g_sb->data_block_bitmap_idx);
    sz_inode -= 5;

    uint32_t indirect_buf[1024];
    if (read_block(g_diskptr, g_sb->data_block_idx + buf[inode_off].indirect, indirect_buf))
      return -1;

    for (int i = 0; i < sz_inode; i++)
      _clear_bit(indirect_buf[i], g_sb->data_block_bitmap_idx);

    _clear_bit(buf[inode_off].indirect, g_sb->data_block_bitmap_idx);
  }

  buf[inode_off].valid = 0;
  buf[inode_off].size = 0;
  write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);

  _clear_bit(inumber, g_sb->inode_bitmap_block_idx);
  free(buf);
  return 0;
}

int stat(int inumber)
{
  if (!g_diskptr)
    return -1;

  int inode_idx = inumber / 128, inode_off = inumber % 128;

  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;

  inode in = buf[inode_off];
  uint32_t num_blocks = _ceili(in.size, BLOCKSIZE);
  uint32_t ndir_ptr = Min(num_blocks, 5);
  printf("\t\tValidity:     %d\n", in.valid);
  printf("\t\tLogical size: %d\n", in.size);
  printf("\t\tNumber of data blocks in use:       %d\n", num_blocks);
  printf("\t\tNumber of direct pointers in use:   %d\n", ndir_ptr);
  printf("\t\tNumber of indirect pointers in use: %d\n", num_blocks - ndir_ptr);

  return 0;
}

int read_i(int inumber, char *data, int length, int offset)
{
  if (!g_diskptr
      || inumber < 0
      || inumber > g_sb->inodes - 1)
    return -1;

  int inode_idx = inumber / 128, inode_off = inumber % 128;
  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;
  inode in = buf[inode_off];

  if (offset < 0 || offset > in.size)
    return -1;

  length = Min(length + offset, in.size);

  uint32_t lim = _ceili(length, BLOCKSIZE), i = offset / BLOCKSIZE, indirect_buf[1024];
  int read_bytes = -1, indirect_read = 0;

  while (i < lim)
  {
    uint32_t block_off;
    if (i < 5)
      block_off = in.direct[i];
    else
    {
      if (indirect_read == 0)
      {
        indirect_read = 1;
        read_block(g_diskptr, g_sb->data_block_idx + in.indirect, (void *)indirect_buf);
      }
      block_off = indirect_buf[i - 5];
    }

    char char_buf[BLOCKSIZE];
    read_block(g_diskptr, g_sb->data_block_idx + block_off, char_buf);

    if (read_bytes == -1)
    {
      read_bytes = (((i + 1) * BLOCKSIZE) < length) ? ((i + 1) * BLOCKSIZE) : length - offset;
      strncpy(data, char_buf + (offset - i * BLOCKSIZE), read_bytes);
    }
    else
    {
      int temp = Min(BLOCKSIZE, length - i * BLOCKSIZE);
      strncpy(data + read_bytes, char_buf, temp);
      read_bytes += temp;
    }

    i++;
  }

  free(buf);
  return read_bytes;
}

int write_i(int inumber, char *data, int length, int offset)
{
  if (!g_diskptr
      || inumber < 0
      || inumber > g_sb->inodes - 1)
    return -1;

  int inode_idx = inumber / 128, inode_off = inumber % 128;
  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;
  inode in = buf[inode_off];

  if (offset < 0)
    return -1;

  length += offset;

  uint32_t lim = _ceili(length, BLOCKSIZE), max_lim = _ceili(in.size, BLOCKSIZE), i = offset / BLOCKSIZE;
  int wrote_bytes = -1;

  while(i < lim)
  {
    uint32_t block_off, ret = 1, free_idx;
    if(i >= max_lim)
    {
      free_idx = _get_set_free_bit(g_sb->data_block_bitmap_idx, g_sb->inode_block_idx);
      ret = (free_idx == -1);
      if(free_idx == -1)
        return -1;
    }

    if(i < 5)
    {
      if(!ret)
      {
        max_lim++;
        in.direct[i] = free_idx;
      }
      block_off = in.direct[i];
    }
    else if(i < (1029))
    {
      if(i == 5 && !ret)
      {
        in.indirect = free_idx;
        free_idx = _get_set_free_bit(g_sb->data_block_bitmap_idx, g_sb->inode_block_idx);
        ret = (free_idx == -1);
        if(ret < 0)
          return -1;
      }

      if(!ret)
      {
        max_lim++;
        uint32_t indirect_buf[1024];

        read_block(g_diskptr, g_sb->data_block_idx + in.indirect, (void *)indirect_buf);
        indirect_buf[i-5] = free_idx;
        write_block(g_diskptr, g_sb->data_block_idx + in.indirect, (void *)indirect_buf);
      }

      block_off = free_idx;
    }

    char char_buf[BLOCKSIZE];
    read_block(g_diskptr, g_sb->data_block_idx + block_off, char_buf);

    if(wrote_bytes == -1)
    {
      wrote_bytes = Min((i + 1) * BLOCKSIZE, length) - offset;
      strncpy(char_buf + (offset - BLOCKSIZE * i), data, wrote_bytes);
    }
    else{
      int temp = Min(BLOCKSIZE, length - BLOCKSIZE * i);
      strncpy(char_buf, data + wrote_bytes, temp);
      wrote_bytes += temp;
    }
    write_block(g_diskptr, g_sb->data_block_idx + block_off, char_buf);
    i++;
  }

  in.size = Max(in.size, length);
  buf[inode_off] = in;
  write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);

  free(buf);
  return wrote_bytes;
}

int fit_to_size(int inumber, int size)
{
  if (!g_diskptr)
    return -1;

  if (size == 0)
    return remove_file(inumber);

  int inode_idx = inumber / 128, inode_off = inumber % 128;

  inode *buf = malloc(BLOCKSIZE * sizeof(char));
  if (read_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf))
    return -1;

  if (buf[inode_off].size <= size)
    return 0;

  int sz_inode = _ceili(buf[inode_off].size, BLOCKSIZE);
  int blocks_allowed = _ceili(size, BLOCKSIZE);
  int flag = 0;

  uint32_t indirect_buf[1024];

  for (int i = 0; i < sz_inode; i++)
  {
    if (i < 5)
    {
      if (i > blocks_allowed - 1)
        _clear_bit(buf[inode_off].direct[i], g_sb->data_block_bitmap_idx);
    }
    else
    {
      if (!flag)
      {
        if (read_block(g_diskptr, g_sb->data_block_idx + buf[inode_off].indirect, indirect_buf))
          return -1;
        flag = 1;
      }

      if (i > blocks_allowed - 1)
      {
        _clear_bit(indirect_buf[i - 5], g_sb->data_block_bitmap_idx);

        if (i == 1028)
          _clear_bit(buf[inode_off].indirect, g_sb->data_block_bitmap_idx);
      }
    }
  }

  buf[inode_off].size = size;
  write_block(g_diskptr, g_sb->inode_block_idx + inode_idx, buf);

  free(buf);
  return 0;
}

int read_file(char *path, char *data, int length, int offset)
{
  char* basename;
  int inum, parent_in = _get_path_metadata(path, &basename);

  if (parent_in < 0)
    return -1;

  dir_union h = _get_directory_offset(parent_in, basename, 0);
  if (!h.dir)
    return -1;

  inum = h.dir->inumber;
  free(h.dir);
  return read_i(inum, data, length, offset);
}

int write_file(char *path, char *data, int length, int offset)
{
  char* basename;
  int inum, ret, parent_in = _get_path_metadata(path, &basename);

  if (parent_in < 0)
    return -1;

  dir_union h = _get_directory_offset(parent_in, basename, 0);
  if (h.dir)
    inum = h.dir->inumber, free(h.dir);
  else
  {
    char *bn;
    parent_in = _get_path_metadata(path, &bn);
    if (parent_in < 0)
      return -1;
    inum = _init_directory(parent_in, bn, 0);
    if (inum < 0)
      return -1;
  }

  ret = write_i(inum, data, length, offset);
  return ret;
}

int create_dir(char *dirpath)
{
  char* basename;
  int parent_in = _get_path_metadata(dirpath, &basename);
  if (parent_in < 0)
    return -1;

  // check if dir already exists
  dir_union h = _get_directory_offset(parent_in, basename, 0);
  if (h.dir)
    return -1;

  int ret = _init_directory(parent_in, basename,1);
  return ret;
}

int remove_dir(char *dirpath)
{
  char *basename;
  int parent_in = _get_path_metadata(dirpath, &basename);
  if (parent_in < 0)
    return -1;

  if (_purge_directory(parent_in, basename) < 0)
    return -1;

  return 0;
}

