/*
 * mkfs - builds a PlainFS file system.
 *
 * Copyright (C) 2007 - Sergey Zhemerdeev <zhseal0@gmail.com>
 *
 * This file is released under the GPL.
 */

#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mntent.h>
#include <linux/fs.h>
#include "plainfs.h"

#define MKFS_VER "0.2"
#define MKFS_NAME "mkfs.plainfs"

void die(const char *, ...);
void show_usage();
void check_mount();
void write_tables();
void create_file(int, char *, int);

int fd = -1;
struct stat dev_stat;
char dev_name[100];
char die_buf[100];
struct d_sb s;



/***********************************************************/
int main(int argc, char *argv[])
{
    int rc;
    long size;

    //printf("argc: %d\n", argc);
    if (argc != 2) {
	show_usage();
	return 0;
    }

    strcpy(dev_name, argv[1]);

    //check_mount();
    fd = open(dev_name, O_RDWR);
    if (fd < 0)
	die("unable to open '%s'", dev_name);
    if (fstat(fd, &dev_stat) < 0)
	die("unable to stat '%s'", dev_name);

    int nblocks = dev_stat.st_size/FS_BSIZE; // total blocks in file
    // last lost incomplete block
    int nbytes_l = dev_stat.st_size - FS_BSIZE*nblocks;
    // inodes per block
    float ino_p_blk = (float)FS_BSIZE/sizeof(struct d_ino);
    // size on inode table in blocks
    int nino_zone = nblocks/(ino_p_blk + 1);
    int nino = nino_zone*ino_p_blk; // number of inodes
    int nblocks_l = nblocks - nino_zone - nino; // lost blocks
    printf("Block size: %d\n", FS_BSIZE);
    printf("Device size: %d(%.2f Mb), nblocks: %d, lost bytes: %d\n", dev_stat.st_size, (float)dev_stat.st_size/1024/1024, nblocks, nbytes_l);
    printf("Inode size: %d, inodes per block: %f\n", sizeof(struct d_ino), ino_p_blk);
    printf("Inodes: %d(%d blocks), data zone: %d\n", nino, nino_zone, nblocks - nino_zone);
    printf("Lost blocks: %d\n", nblocks_l);

    s.s_nnodes = nino;
    s.s_nblocks = nblocks;
    sprintf(s.s_magic, "plainfs superblock");
    write_tables();

    close(fd);
    return 0;
}



/***********************************************************/
void die(const char *format, ...)
{
    va_list arg;
    
    va_start(arg, format);
    vsprintf(die_buf, format, arg);
    va_end(arg);

    fprintf(stderr, MKFS_NAME": %s\n", die_buf);
    if (-1 != fd)
	close(fd);
    exit(-1);
}



/***********************************************************/
void show_usage()
{
    printf(MKFS_NAME " (version "MKFS_VER")\n");
    printf("Usage: " MKFS_NAME " /dev/name\n");
}



/***********************************************************/
void check_mount()
{
    FILE *f;
    struct mntent *mnt;

    if ((f = setmntent(MOUNTED, "r")) == NULL)
	return;
    while ((mnt = getmntent(f)) != NULL)
	if (strcmp(dev_name, mnt->mnt_fsname) == 0)
	    break;
    endmntent(f);
    if (!mnt)
	return;

    die("'%s' is already mounted", dev_name);
}



/***********************************************************/
void write_tables()
{
    int i, i_cnt;
    char buf[FS_BSIZE];
    struct d_sb *sb = (struct d_sb*)buf;
    struct d_ino di;

    // Writing superblock
    memset(buf, 0, FS_BSIZE);
    *sb = s;
    if (FS_BSIZE != write(fd, buf, FS_BSIZE))
	die("unable to write superblock");

    // Writing inode table
    memset(&di, 0, sizeof(di));
    di.i_nlinks = 0;
    for (i=0; i < s.s_nnodes; i++) {
	sprintf(di.name, "ino%05d", i);
	write(fd, &di, sizeof(di));
    }
    
    // Writing data area
    memset(buf, 0, FS_BSIZE);
    lseek(fd, SEEK_SET, (1 + s.s_nnodes/sizeof(di))*FS_BSIZE);
    for (i=0; i < s.s_nnodes; i++) {
	sprintf(buf, "block%05d", i);
	if (FS_BSIZE != write(fd, buf, FS_BSIZE))
	    die("unable to write block %d", i+1);
    }
    if (s.s_nblocks - 1 > s.s_nnodes) {
	sprintf(buf, "unused tail");
	write(fd, buf, FS_BSIZE);
    }

    // Creating files
    //create_file(fd, "file0", 0);
    //create_file(fd, "file1", 1);
}



void create_file(int fd, char *fname, int ino)
{
    struct d_ino di;

    memset(&di, 0, sizeof(di));
    lseek(fd, FS_INO_BLK*FS_BSIZE + sizeof(di)*ino, SEEK_SET);
    strcpy(di.name, fname);
    di.i_ino = FS_ROOT_INO + ino + 1;
    di.i_mode = 0x100;
    di.i_size = 555;
    di.i_nlinks = 1;
    di.i_data[0] = 1 + s.s_nnodes/FS_INO_PER_BLK + ino*2;
    di.i_data[1] = di.i_data[0] + 1;
    //di.i_data[2] = di.i_data[1] + 1;
    write(fd, &di, sizeof(di));
}
