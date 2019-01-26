/*
 * plainfs.h - some common definitions for kernel module and mkfs utility
 *
 * Copyright (C) 2007 - Sergey Zhemerdeev <zhseal0@gmail.com>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>

// Common definitions
#define FS_NAME		"plainfs"
#define FS_MOD_VER	"0.2.1"
#define FS_BSIZE_BITS	9
#define FS_BSIZE	(1<<FS_BSIZE_BITS)
#define FS_ROOT_INO	1
//#define FS_SB_SIZE	512
//#define FS_MAGIC	0x25850101
#define FS_FNAME_LEN	10
#define fn		__func__
#define FS_BOOT_BLK	0
#define FS_SB_BLK	0
#define FS_INO_BLK	1
#define FS_INODE_CACHE	FS_NAME"_inode_cache"
#define FS_INO_PER_BLK  ((FS_BSIZE)/(sizeof(struct d_ino)))
#define FS_IDATA	3
//#define DEBUG		// switches a lot of debug messages from module

/*
 * inode data on disk
 */
struct d_ino {
    char name[FS_FNAME_LEN];    // file name
    __u16 i_ino;         	// inode number
    __u16 i_mode;
    __u16 i_size;		// size in bytes
    __u8  i_nlinks;		// number of file's links, 0 - inode is free
    __u8 i_uid;
    __u8 i_gid;
    __u32 i_time;
    __u16 i_data[FS_IDATA];
};

/*
 * super-block data on disk
 */
struct d_sb {
	char s_magic[30];
	__u16 s_nnodes;  // amount of blocks for inode table
	__u16 s_nblocks; // total number of blocks
};

/*
 * super-block data in memory
 */
struct m_sb {
	__u16 s_nnodes;
	__u16 s_nblocks;
	struct lookup_entry **s_lookup;
	char *s_inode_bm;
};

struct lookup_entry {
    char name[FS_FNAME_LEN];
    __u16 i_ino;
};
