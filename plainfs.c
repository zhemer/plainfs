/*
PlainFS - simple filesystem developed for educational purpose
Copyright (C) 2007 Sergey Zhemerdeev <zhseal0@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*
PlainFS - simple filesystem for educational purpose. I wrote it for myself in order
to understand filesystems in Linux. This is sorurce code for PlainFS.
PlainFS features (may be TODOs):
- No directory support, only files
- Simple superblock
- File attributes atime and ctime has not been implemented yet
- Simple FS data structures
- File size limit is FS_IDATA*512 bytes
- File name limit is FS_FNAME_LEN
- Inodes and file names are stored in single structure
- Rest of a disk beyond files' data remains unused
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include "plainfs.h"

#ifdef DEBUG
#define d(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define d(fmt, ...)
#endif


MODULE_AUTHOR("Sergey Zhemerdeev <zhseal0@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module version " FS_MOD_VER);
MODULE_DESCRIPTION("Simple file system for educational purposes");
 
static int __init fs_init(void);
static void __exit fs_exit(void);

int fs_fill_super(struct super_block *, void *, int);
struct super_block *fs_get_sb(struct file_system_type *, int, const char *, void *);
void fs_destroy_inode(struct inode *);
void fs_delete_inode(struct inode *);
void fs_put_super(struct super_block *sb);

int fs_readpage(struct file *, struct page *);
int fs_get_block(struct inode *, sector_t, struct buffer_head *, int);
int fs_writepage(struct page *, struct writeback_control *);
int fs_prepare_write(struct file *, struct page *, unsigned, unsigned);
int fs_write_inode(struct inode *, int);
void fs_read_inode(struct inode * inode);
struct dentry *fs_lookup(struct inode *, struct dentry *, struct nameidata *);
int fs_readdir(struct file *, void *, filldir_t);
int fs_create(struct inode *, struct dentry *, int, struct nameidata *);
int fs_unlink(struct inode *, struct dentry *);
int fs_statfs(struct super_block *, struct kstatfs *);
int fs_mknod(struct inode *, struct dentry *, int, dev_t);

struct d_ino *fs_raw_inode(struct super_block *, ino_t, struct buffer_head **);
int fs_find_free_inode(struct super_block *);
int fs_count_free_blk(struct super_block *);
char *fs_inode_to_name(struct inode *);

static kmem_cache_t *fs_inode_cachep;

struct fs_inode_info *fs_i(struct inode *);
void init_once(void *, kmem_cache_t *, unsigned long);
struct inode *fs_alloc_inode(struct super_block *);
int init_inodecache(void);
void destroy_inodecache(void);
int fs_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);
int fs_hash(struct dentry *, struct qstr *);

// Superblock operations
struct super_operations fs_sops = {
    .alloc_inode	= fs_alloc_inode,
    .destroy_inode	= fs_destroy_inode,
    .read_inode		= fs_read_inode,
    .statfs		= fs_statfs,
    .delete_inode	= fs_delete_inode,
    .put_super		= fs_put_super,
    .write_inode	= fs_write_inode,
};

// File operations
struct file_operations fs_file_ops = {
    .llseek         = generic_file_llseek,
    .read           = generic_file_read,
    .write          = generic_file_write,
    .mmap           = generic_file_mmap,
    .sendfile       = generic_file_sendfile,
};

struct address_space_operations fs_aops = {                                                        
    .readpage       = fs_readpage,
    .writepage      = fs_writepage,
    .prepare_write  = fs_prepare_write,
    .commit_write   = generic_commit_write,
};

struct file_system_type fs_type = {
    .owner = THIS_MODULE,
    .name = FS_NAME,
    .get_sb = fs_get_sb,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

struct inode_operations fs_file_inops;
struct inode_operations fs_dir_inops = {
    .create = fs_create,
    .lookup = fs_lookup,
    .unlink = fs_unlink,
    .rename = fs_rename,
};

struct file_operations fs_dir_ops = {
    .read	= generic_read_dir,
    .readdir	= fs_readdir,
};

struct fs_inode_info {
    struct inode vfs_inode;
    __u16 i_data[3];
};

static struct dentry_operations fs_dentry_operations = {
    .d_hash = fs_hash,
};


module_init(fs_init);
module_exit(fs_exit);



/**********************************************************************************/
int fs_get_block(struct inode *inode, sector_t block, struct buffer_head *bh, int create)
{
    //unsigned long phys;
    struct super_block *s = inode->i_sb;
    struct m_sb *sbi = s->s_fs_info;
    int rc = 0, i;
    struct fs_inode_info *fsi = fs_i(inode);

    d("=%s(inode: %lu, block: %lu, bh: %p, create: %i)\n", fn, inode->i_ino, block, bh, create);

    if (block > FS_IDATA-1) {
	rc = -ENOSPC;
	goto out;
    }
    //phys = (int)inode->u.generic_ip;
    //d("phys: %lu\n", phys);

    if (create) {
	for (i=0; i < sbi->s_nnodes; i++) {
	    if (!test_bit(i, (void *)sbi->s_inode_bm)) {
		set_bit(i, (void *)sbi->s_inode_bm);
		fsi->i_data[block] = FS_INO_BLK + sbi->s_nnodes/FS_INO_PER_BLK + i;
d("Free bit: %i, block: %i\n", i, fsi->i_data[block]);
		break;
	    }
	}
	if (!fsi->i_data[block]) {
	    rc = -ENOSPC;
	    goto out;
	}
	set_buffer_new(bh);
    }
    map_bh(bh, s, fsi->i_data[block]);
    
out:
    d("-%s rc: %i, b_blocknr: %lu\n", fn, rc, bh->b_blocknr);
    return rc;
}



/**********************************************************************************/
int fs_readpage(struct file *file, struct page *page)
{                                                                                                   
    int rc;

    d("=%s\n", fn);
    rc = block_read_full_page(page, fs_get_block);
    d("-%s: rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_writepage(struct page *page, struct writeback_control *wbc)
{
    int rc;

    d("=%s\n", fn);
    rc = block_write_full_page(page, fs_get_block, wbc);
    d("-%s: rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
    int rc;

    d("=%s\n", fn);
    rc = block_prepare_write(page, from, to, fs_get_block);                                  
    d("-%s: rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
// Fill a superblock from disk
/**********************************************************************************/
int fs_fill_super(struct super_block *s, void *data, int silent)
{
    struct inode *inode;
    int rc = 0, i;
    struct m_sb *sbi;
    struct buffer_head *bh;
    struct d_sb *fsi;
    struct lookup_entry **le;

    d("=%s(silent: %d)\n", fn, silent);
    sbi = kmalloc(sizeof(struct m_sb), GFP_KERNEL);
    if (!sbi) {
	rc = -ENOMEM;
	goto out;
    }
    memset(sbi, 0, sizeof(struct m_sb));
    if (!sb_set_blocksize(s, FS_BSIZE)) {
	d("%s: unable to set block size\n", fn);
	rc = -EINVAL;
	goto out;
    }
    if (!(bh = sb_bread(s, FS_SB_BLK))) {
	d("%s: unable to read superblock\n", fn);
	rc = -EINVAL;
	goto out;
    }
    
    fsi = (struct d_sb *)bh->b_data;
    sbi->s_nnodes = fsi->s_nnodes;
    sbi->s_nblocks = fsi->s_nblocks;
    brelse(bh);
    d("s_nnodes: %i, s_nblocks: %i\n", sbi->s_nnodes, sbi->s_nblocks);

    le = kmalloc(sizeof(le)*sbi->s_nnodes, GFP_KERNEL);
    if (!le) {
	rc = -ENOMEM;
	goto out;
    }
    memset(le, 0, sizeof(le)*sbi->s_nnodes);
    sbi->s_lookup = le;
    s->s_fs_info = sbi;
    
    // Allocating bitmap for inodes
    i = sbi->s_nnodes/8 + (sbi->s_nnodes%8 ? 1 : 0);
d("bitmap len: %i\n", i);
    sbi->s_inode_bm = kmalloc(i, GFP_KERNEL);
    if (!sbi->s_inode_bm) {
	rc = -ENOMEM;
	goto out;
    }
    memset(sbi->s_inode_bm, 0, i);
    
    s->s_op = &fs_sops;
    inode = iget(s, FS_ROOT_INO);
    if (!inode) {
	d("%s: get root inode failed\n", fn);
	rc = -EINVAL;
	goto out;
    }

    s->s_root = d_alloc_root(inode);
    if (!s->s_root) {
	d("%s: d_alloc_root() failed\n", fn);
	iput(inode);
	rc = -EINVAL;
	goto out;
    }
    s->s_root->d_op = &fs_dentry_operations;
    return 0;

out:
    s->s_fs_info = NULL;
    if (sbi) {
	if (sbi->s_lookup)
	    kfree(sbi->s_lookup);
	kfree(sbi);
	if (sbi->s_inode_bm)
	    kfree(sbi->s_inode_bm);
    }
    d("-%s: rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
// Filesystem registration
/**********************************************************************************/
struct super_block *fs_get_sb(struct file_system_type *fs_type,
    int flags, const char *dev_name, void *data)
{
    struct super_block *rc = NULL;

    d("=%s\n", fn);
    rc = get_sb_bdev(fs_type, flags, dev_name, data, fs_fill_super);
    d("-%s rc: %p\n", fn, rc);
    return rc;
}



/**********************************************************************************/
// Initializing module
/**********************************************************************************/
int __init fs_init(void)
{
    int rc = 0;

d("=%s\n", fn);
    rc = init_inodecache();
    if (rc)
	goto out;
    rc = register_filesystem(&fs_type);

out:
d("-%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
// Module is unloaded
/**********************************************************************************/
void __exit fs_exit(void)
{
d("=%s\n", fn);
    unregister_filesystem(&fs_type);
    destroy_inodecache();
d("-%s\n\n", fn);
}



/**********************************************************************************/
void fs_delete_inode(struct inode *inode)
{
    d("=%s(inode: %lu)\n", fn, inode->i_ino);
    int i;
    struct d_ino *di;
    struct buffer_head *bh;
    struct super_block *s = inode->i_sb;
    struct m_sb *sbi = s->s_fs_info;
    struct fs_inode_info *fsi = fs_i(inode);

    truncate_inode_pages(&inode->i_data, 0);
    inode->i_size = 0;
    //truncate(inode);
    clear_inode(inode);

    // Write inode as unused to disk
    di = fs_raw_inode(s, inode->i_ino, &bh);
    if (!di) {
	d("Unable to read inode %lu\n", inode->i_ino);
	goto out;
    }
    di->name[0] = 0;
    di->i_nlinks = 0;
    mark_buffer_dirty(bh);
    brelse(bh);
    
    // Deleting name from name cache
    kfree(sbi->s_lookup[inode->i_ino - FS_ROOT_INO - 1]);
    sbi->s_lookup[inode->i_ino - FS_ROOT_INO - 1] = NULL;

    // Clearing inode bitmap
    for (i=0; i < FS_IDATA; i++) {
	//d("i_data[%i]: %i\n", i, fsi->i_data[i]);
	if (fsi->i_data[i])
	    clear_bit(fsi->i_data[i] - sbi->s_nnodes/FS_INO_PER_BLK - 1, (void *)sbi->s_inode_bm);    
    }
out:
    d("-%s\n", fn);
}



/**********************************************************************************/
void fs_put_super(struct super_block *s)
{
    struct m_sb *sbi;
    int i;

    d("=%s\n", fn);
    sbi = s->s_fs_info;
    s->s_fs_info = NULL;
    if (sbi) {
	if (sbi->s_lookup) {
	    for (i=0; i < sbi->s_nnodes; i++)
		if (sbi->s_lookup[i])
		    kfree(sbi->s_lookup[i]);
	    kfree(sbi->s_lookup);
	}
	kfree(sbi);
    }
    d("-%s\n", fn);
}



/**********************************************************************************/
int fs_write_inode(struct inode *inode, int wait)
{
    struct d_ino *di;
    struct buffer_head *bh;
    int rc = 0, i;
    struct fs_inode_info *fsi = fs_i(inode);
    char *fname0, fname1[FS_FNAME_LEN+1];

    d("=%s(inode: %lu, wait: %i)\n", fn, inode->i_ino, wait);
    if (FS_ROOT_INO == inode->i_ino) {
	d("inode %i is not a raw inode\n", FS_ROOT_INO);
	goto out;
    }
    i = inode->i_ino - FS_ROOT_INO - 1;
    bh = sb_bread(inode->i_sb, FS_INO_BLK + i/FS_INO_PER_BLK);
//d("i: %i, %i, bh: %p\n", i, FS_INO_BLK + i/FS_INO_PER_BLK, bh);
    if (!bh)
	goto out;
    di = (struct d_ino*)(bh->b_data) + i % FS_INO_PER_BLK;

    fname0 = fs_inode_to_name(inode);
    if (!fname0) {
	d("Bug: unlinked inode (%lu) was found\n", inode->i_ino);
	sprintf(fname1, "ufile%lu", inode->i_ino);
	fname0 = fname1;
    }
    strncpy(di->name, fname0, FS_FNAME_LEN);
    di->i_ino = inode->i_ino;
    di->i_mode = inode->i_mode;
    di->i_uid = inode->i_uid;
    di->i_gid = inode->i_gid;    
    di->i_size = inode->i_size;
    di->i_nlinks = 1;
    di->i_time = inode->i_mtime.tv_sec;
    for (i=0; i<FS_IDATA; i++)
        di->i_data[i] = fsi->i_data[i];
    mark_buffer_dirty(bh);
    brelse(bh);

out:
    d("-%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
void fs_read_inode(struct inode *inode)
{
    unsigned long ino = inode->i_ino;
    struct m_sb *sbi = (struct m_sb *)inode->i_sb->s_fs_info;

    d("=%s(inode: %lu)\n", fn, inode->i_ino);
    if (FS_ROOT_INO == ino) {
	inode->i_mode = 0644;
	inode->i_mode |= S_IFDIR;
	inode->i_op = &fs_dir_inops;
	inode->i_fop = &fs_dir_ops;
	inode->i_size = sbi->s_nnodes;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
    
    } else {
	struct buffer_head *bh;
	struct d_ino *di;
	struct fs_inode_info *fsi = fs_i(inode);
	int i;
    
	inode->i_op = &fs_file_inops;
	inode->i_fop = &fs_file_ops;
	inode->i_mapping->a_ops = &fs_aops;

	di = fs_raw_inode(inode->i_sb, ino, &bh);
	if (!di) {
	    make_bad_inode(inode);
	    return;
	}

        inode->i_size = di->i_size;
        //inode->u.generic_ip = (void *)di->st;
        inode->i_mode = di->i_mode;
        inode->i_mode |= S_IFREG;
	inode->i_uid = di->i_uid;
	inode->i_gid = di->i_gid;	
	inode->i_atime.tv_sec = inode->i_mtime.tv_sec = inode->i_ctime.tv_sec = di->i_time;

	int blk0_off = 1 + sbi->s_nnodes/FS_INO_PER_BLK + (sbi->s_nnodes%FS_INO_PER_BLK ? 1 : 0);
d("blk0_off: %i\n", blk0_off);
	for (i=0; i < FS_IDATA; i++) {
	    fsi->i_data[i] = di->i_data[i];
	    if (fsi->i_data[i])
		set_bit(fsi->i_data[i] - blk0_off, (void *)sbi->s_inode_bm);
	}
        brelse(bh);
    }

    inode->i_blocks = 1;
    inode->i_blksize = PAGE_SIZE;
    inode->i_nlink =  1;
    d("-%s\n", fn);
}



/**********************************************************************************/
ino_t fs_name_to_inode(struct super_block *s, struct dentry *de)
{
    ino_t rc = 0;
    int i; 
    struct m_sb *sbi = s->s_fs_info;
    struct lookup_entry *le;
    
    d("=%s(dentry: %s)\n", fn, de->d_name.name);    
    for (i=0; i < sbi->s_nnodes; i++) {
	le = sbi->s_lookup[i];
	if (!le)
	    continue;
	if (!strncmp(le->name, de->d_name.name, FS_FNAME_LEN)) {
	    rc = le->i_ino;
	    break;
	}
    }
    
    d("-%s rc: %lu\n", fn, rc);
    return rc;
}



/**********************************************************************************/
struct dentry *fs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
    struct inode *p_ino = NULL;
    int ino;
    struct dentry *rc = NULL;

    d("=%s(dentry: %s)\n", fn, dentry->d_name.name);
    ino = fs_name_to_inode(dir->i_sb, dentry);
    
    lock_kernel();
    if (ino) {
	p_ino = iget(dir->i_sb, ino);
	if (!p_ino) { 
	    rc = ERR_PTR(-EACCES);
	    unlock_kernel();
	    goto l_end;
	}
    }
    unlock_kernel();
    d_add(dentry, p_ino);

l_end:
    d("-%s rc: %p\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_readdir(struct file *f, void *dirent, filldir_t filldir)
{
    struct inode *dir = f->f_dentry->d_inode;
    struct super_block *s = dir->i_sb;
    int rc = 0;
    struct d_ino *di;
    int i, j;
    struct buffer_head *bh;
    struct m_sb *sbi = (struct m_sb *)s->s_fs_info;
    struct lookup_entry *le;
    char fname[FS_FNAME_LEN+1];

    d("=%s\n", fn);
    d("dir->i_ino: %lu, dir->i_size: %lli, f->f_pos: %lli\n", dir->i_ino, dir->i_size, f->f_pos);

    lock_kernel();
    if (f->f_pos)
	goto out;
        
    rc = filldir(dirent, ".", 1, f->f_pos++, FS_ROOT_INO, DT_UNKNOWN);
    rc = filldir(dirent, "..", 2, f->f_pos++, FS_ROOT_INO, DT_UNKNOWN);

    for (i=0; i < sbi->s_nnodes/FS_INO_PER_BLK; i++) {
	bh = sb_bread(s, FS_INO_BLK + i);
	if (!bh) {
	    d("unable to read i-node table's block %i\n", FS_INO_BLK + 1);
	    goto out;
	}
	di = (struct d_ino*)bh->b_data;
	for (j=0; j < FS_INO_PER_BLK; j++) {
//d("di[%i].i_nlinks: %i\n", j, di[j].i_nlinks);
	    if (di[j].i_nlinks) {
		int size = strnlen(di[j].name, FS_FNAME_LEN);
		rc = filldir(dirent, di[j].name, size, f->f_pos++, di[j].i_ino, DT_UNKNOWN);
		strncpy(fname, di[j].name, FS_FNAME_LEN);
		fname[FS_FNAME_LEN] = 0;
		d("di[%i].name: %s, f->f_pos: %llu, filldir: %i\n", j, fname, f->f_pos, rc);
		
		// Inserting new name into name cache
		le = kmalloc(sizeof(*le), GFP_KERNEL);
		if (!le)
		    continue;
		if (sbi->s_lookup[i*FS_INO_PER_BLK+j])
		    kfree(sbi->s_lookup[i*FS_INO_PER_BLK+j]);
		sbi->s_lookup[i*FS_INO_PER_BLK+j] = le;
		strncpy(le->name, di[j].name, FS_FNAME_LEN);
		le->i_ino = di[j].i_ino;
	    }
	}
	brelse(bh);
    }

out:
    unlock_kernel();
    d("-%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
struct d_ino *fs_raw_inode(struct super_block *s, ino_t ino, struct buffer_head **bh)
{
    struct d_ino *rc = NULL;
    int i;
    struct m_sb *sbi = s->s_fs_info;
    struct lookup_entry *le;

    d("=%s(ino: %lu)\n", fn, ino);
    
    for (i=0; i < sbi->s_nnodes; i++) {
	le = sbi->s_lookup[i];
	if (!le)
	    continue;
	if (le->i_ino == ino) {
	    *bh = sb_bread(s, FS_INO_BLK + i/FS_INO_PER_BLK);
	    if (!*bh) {
		d("unable to read inode %i\n", i);
		rc = NULL;
		goto out;
	    }
	    rc = (struct d_ino*)((*bh)->b_data) + i % FS_INO_PER_BLK;
	    break;
	}
    }
    
out:
    d("-%s rc: %p\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
    return fs_mknod(dir, dentry, mode, 0);
}



/**********************************************************************************/
int fs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
    int rc = 0, i;
    struct super_block *s = dir->i_sb;
    struct inode *inode;
    struct buffer_head *bh;
    struct lookup_entry *le;
    struct m_sb *sbi = s->s_fs_info;
 
    d("=%s(dir->i_ino: %lu)\n", fn, dir->i_ino);
    inode = new_inode(s);
    if (!inode) {
	rc = -ENOSPC;
	goto out;
    }
    lock_kernel();
    
    inode->i_uid = current->fsuid;
    inode->i_gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
    inode->i_blocks = inode->i_blksize = 0;
    inode->i_op = &fs_file_inops;
    inode->i_fop = &fs_file_ops;
    inode->i_mapping->a_ops = &fs_aops;
    inode->i_mode = mode;
    i = fs_find_free_inode(s);
d("New inode %i\n", i);
    if (FS_ROOT_INO == i) {
	rc = -ENFILE;
	unlock_kernel();
	goto out;
    }
    inode->i_ino = i;
    //inode->u.generic_ip = (void *)(i_ino + 1);
    
    insert_inode_hash(inode);
    mark_inode_dirty(inode);

    // Writing new inode to disk
    i = inode->i_ino - FS_ROOT_INO - 1;
    bh = sb_bread(inode->i_sb, FS_INO_BLK + i/FS_INO_PER_BLK);
    if (!bh) {
	unlock_kernel();
	goto out;
    }
    struct d_ino *di = (struct d_ino*)(bh->b_data) + i % FS_INO_PER_BLK;
    strncpy(di->name, dentry->d_name.name, FS_FNAME_LEN);
    di->i_ino = inode->i_ino;
    di->i_mode = inode->i_mode;
    di->i_size = inode->i_size;
    di->i_nlinks = 1;
    di->i_data[0] = di->i_data[1] = di->i_data[2] = 0;
    mark_buffer_dirty(bh);
    brelse(bh);

    // Adding inode to our cache
    le = kmalloc(sizeof(*le), GFP_KERNEL);
    if (le) {
	le->i_ino = inode->i_ino;
	strncpy(le->name, dentry->d_name.name, FS_FNAME_LEN); 
        sbi->s_lookup[i] = le;
    } else
	d("ERR: Unable to allocate memory for lookup_entry\n");
    // Adding inode to dcache
    unlock_kernel();
    d_instantiate(dentry, inode);
    
out:
    d("-%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_find_free_inode(struct super_block *s)
{
    int rc = FS_ROOT_INO;
    int i;
    struct m_sb *sbi = s->s_fs_info;
    struct lookup_entry *le;
    
    d("=%s\n", fn);
    for (i=0; i < sbi->s_nnodes; i++) {
	le = sbi->s_lookup[i];
	if (!le) {
	    rc = FS_ROOT_INO + i + 1;
	    break;
	}
    }
    d("-%s rc: %i\n", fn, rc);    
    return rc;
}



/**********************************************************************************/
int fs_unlink(struct inode *dir, struct dentry *dentry)
{
    int rc = -ENOENT;
    struct inode *inode = dentry->d_inode;

    d("=%s(dentry: %s)\n", fn, dentry->d_name.name);
    inode->i_nlink--;
    inode->i_ctime = CURRENT_TIME_SEC;
    mark_inode_dirty(inode);
    rc = 0;
//out:
    d("-%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_statfs(struct super_block *s, struct kstatfs *buf)
{
    struct m_sb *sbi = s->s_fs_info;
    int i, bfree = 0, ffree = 0;

d("* %s\n", fn);
    for (i=0; i < sbi->s_nnodes; i++) {
	bfree += test_bit(i, (void *)sbi->s_inode_bm)*(-1);
	if (sbi->s_lookup[i])
	    ffree++;
    }
    bfree = sbi->s_nnodes - bfree;
    ffree = sbi->s_nnodes - ffree;

    buf->f_type = s->s_magic;
    buf->f_bsize = s->s_blocksize;
    buf->f_namelen = FS_FNAME_LEN;
    buf->f_blocks = sbi->s_nnodes;
    buf->f_bfree = bfree;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = sbi->s_nnodes;
    buf->f_ffree = ffree;

    return 0;
}



/**********************************************************************************/
struct inode *fs_alloc_inode(struct super_block *s)
{
    struct fs_inode_info *fi;
    struct inode *rc = NULL;

    fi = (struct fs_inode_info *)kmem_cache_alloc(fs_inode_cachep, SLAB_KERNEL);
    if (!fi)
	goto out;
    fi->i_data[0] = fi->i_data[1] = fi->i_data[2] = 0;
    rc = &fi->vfs_inode;

out:
    d("*%s rc: %p\n", fn, rc);
    return rc;
}



/**********************************************************************************/
void fs_destroy_inode(struct inode *inode)
{
d("*%s\n", fn);
    kmem_cache_free(fs_inode_cachep, fs_i(inode));
}



/**********************************************************************************/
struct fs_inode_info *fs_i(struct inode *inode)
{
    return list_entry(inode, struct fs_inode_info, vfs_inode);
}



/**********************************************************************************/
int init_inodecache(void)
{
    int rc = 0;
    
    fs_inode_cachep = kmem_cache_create(FS_INODE_CACHE, sizeof(struct fs_inode_info),
	0, SLAB_RECLAIM_ACCOUNT, init_once, NULL);
    if (fs_inode_cachep == NULL)
	rc = -ENOMEM;
d("*%s rc: %i\n", fn, rc);
    return rc;
}



/**********************************************************************************/
void destroy_inodecache(void)
{
d("=%s\n", fn);
    if (kmem_cache_destroy(fs_inode_cachep))
	d(FS_INODE_CACHE ": not all structures were freed\n");
}



/**********************************************************************************/
void init_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
    struct fs_inode_info *fi = (struct fs_inode_info *)foo;

d("=%s\n", fn);
    if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR)
	inode_init_once(&fi->vfs_inode);
}



/**********************************************************************************/
char *fs_inode_to_name(struct inode *inode)
{
    char *rc = NULL;
    int i; 
    struct m_sb *sbi = inode->i_sb->s_fs_info;
    struct lookup_entry *le;
    
    d("=%s(inode: %lu)\n", fn, inode->i_ino);    
    for (i=0; i < sbi->s_nnodes; i++) {
	le = sbi->s_lookup[i];
	if (!le)
	    continue;
	if (inode->i_ino == le->i_ino) {
	    rc = le->name;
	    break;
	}
    }
    
    d("-%s rc: %p\n", fn, rc);
    return rc;
}



/**********************************************************************************/
int fs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
d("=%s(dold: %s, dnew: %s)\n", fn, old_dentry->d_name.name, new_dentry->d_name.name);
    struct inode *inode = old_dentry->d_inode;
    struct d_ino *di;
    struct buffer_head *bh;
    int rc = -ENOENT, i;
    
    if (!inode)
	goto out;
    i = inode->i_ino - FS_ROOT_INO - 1;
    bh = sb_bread(inode->i_sb, FS_INO_BLK + i/FS_INO_PER_BLK);
    if (!bh)
	goto out;
    di = (struct d_ino*)(bh->b_data) + i % FS_INO_PER_BLK;
    strncpy(di->name, new_dentry->d_name.name, FS_FNAME_LEN);
    mark_buffer_dirty(bh);
    brelse(bh);
    rc = 0;

out:
d("-%s rc: %i\n", fn, rc);
	return rc;
}




/**********************************************************************************/
void fs_print_ibitmap(struct super_block *s)
{
    struct m_sb *sbi = s->s_fs_info;
    int i, j=0;
    char s_buf[21];

    d("=%s\n", fn);
    if (!sbi->s_inode_bm) {
	d("Inode bitmap was not initialized\n");
	goto out;
    }
    for (i=0; i < sbi->s_nnodes; i++) {
        s_buf[j++] = '0' + test_bit(i, (void *)sbi->s_inode_bm)*(-1);
	if (j == 20) {
	    s_buf[j] = 0;
    	    d("inode_bm: %s\n", s_buf);
	    j = 0;
	}
    }
    if (j < 20 && j != 0) {
        s_buf[j] = 0;
	d("inode_bm: %s\n", s_buf);
    }
out:
d("-%s\n", fn);
}



/**********************************************************************************/
// fs_hash truncates file name to FS_FNAME_LEN bytes
/**********************************************************************************/
int fs_hash(struct dentry *dentry, struct qstr *qstr)
{
    d("*%s(dentry: %s, qstr: %s)\n", fn, dentry->d_name.name, qstr->name);

    if (strlen(qstr->name) > FS_FNAME_LEN) {
	qstr->hash = full_name_hash(qstr->name, FS_FNAME_LEN);
	qstr->len = FS_FNAME_LEN;
    }

    return 0;
}
