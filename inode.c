/*
 * BRIEF DESCRIPTION
 *
 * HUNTER Inode methods (allocate/free/read/write).
 *
 * Copyright 2023-2024 Regents of the University of Harbin Institute of Technology, Shenzhen
 * Computer science and technology, Yanqi Pan <deadpoolmine@qq.com>
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include "hunter.h"

int hk_init_free_inode_list_percore(struct super_block *sb, int cpuid, bool is_init)
{
    struct hk_inode *pi;
    struct hk_sb_info *sbi = HK_SB(sb);
    u64 start_ino, end_ino;
    int inums_percore;
    int i;

    inums_percore = HK_NUM_INO / sbi->cpus;
    start_ino = cpuid * inums_percore;
    if (cpuid == 0) {
        start_ino = HK_RESV_NUM;
        inums_percore -= HK_RESV_NUM;
    }
    end_ino = start_ino + inums_percore - 1;

    if (is_init) {
        hk_range_insert_range(&sbi->inode_forest[cpuid], start_ino, end_ino);
    } else {
        for (i = end_ino; i >= start_ino; i--) {
            pi = hk_get_pi_by_ino(sb, i);
            if (!pi->valid) {
                hk_range_insert_range(&sbi->inode_forest[cpuid], i, i);
            }
        }
    }

    sbi->inode_forest_init[cpuid] = true;

    return 0;
}

static int hk_free_dram_resource(struct super_block *sb,
                                 struct hk_inode_info_header *sih)
{
    unsigned long last_blocknr;
    int freed = 0;

    if (!(S_ISREG(sih->i_mode)) && !(S_ISDIR(sih->i_mode)))
        return 0;

    freed = sih->ix.num_slots;

    if (S_ISREG(sih->i_mode)) {
        linix_destroy(&sih->ix);
    } else {
        linix_destroy(&sih->ix);
        hk_destory_dir_table(sb, sih);
    }

    return freed;
}

int hk_free_data_blks(struct super_block *sb, struct hk_inode_info_header *sih)
{
    int freed = 0, i = 0;
    struct hk_sb_info *sbi = HK_SB(sb);
    u64 blk_addr;
    struct hk_inode_info *si;
    struct inode *inode;
    struct hk_layout_info *layout;

    INIT_TIMING(free_time);

    HK_START_TIMING(free_inode_log_t, free_time);

    si = container_of(sih, struct hk_inode_info, header);
    inode = &si->vfs_inode;

    for (i = 0; i < sih->i_blocks; i++) {
        blk_addr = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, i));

        if (blk_addr == 0) {
            hk_info("panic: %d", i);
        }
        BUG_ON(blk_addr == 0);

#ifdef CONFIG_CMT_BACKGROUND
        struct hk_cmt_dbatch batch;
        hk_init_and_inc_cmt_dbatch(&batch, blk_addr, i, 1);
        hk_delegate_data_async(sb, inode, &batch, 0, CMT_DELETE_DATA);
#else
        use_layout_for_addr(sb, blk_addr);
        sm_delete_data_sync(sb, blk_addr);
        unuse_layout_for_addr(sb, blk_addr);
#endif
        freed += HK_PBLK_SZ;
    }

    HK_END_TIMING(free_inode_log_t, free_time);
    return freed;
}

static int hk_get_cpuid_by_ino(struct super_block *sb, u64 ino)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    int cpuid = 0;
    int inums_percore = HK_NUM_INO / sbi->cpus;
    cpuid = ino / inums_percore;
    return cpuid;
}

int hk_free_ino(struct super_block *sb, u64 ino)
{
    int err = 0;
    int cpuid;
    struct hk_sb_info *sbi = HK_SB(sb);
    INIT_TIMING(free_time);

    HK_START_TIMING(free_inode_t, free_time);

    cpuid = hk_get_cpuid_by_ino(sb, ino);

    spin_lock(&sbi->inode_forest_locks[cpuid]);
    err = hk_range_insert_range(&sbi->inode_forest[cpuid], ino, ino);
    spin_unlock(&sbi->inode_forest_locks[cpuid]);

    HK_END_TIMING(free_inode_t, free_time);
    return err;
}

static int hk_free_inode_resource(struct super_block *sb, struct hk_inode *pi,
                                  struct hk_inode_info_header *sih)
{
    unsigned long last_blocknr;
    int ret = 0;
    int freed = 0;
    unsigned long irq_flags = 0;
    struct hk_cmt_node *cmt_node = sih->cmt_node;

    hk_memunlock_pi(sb, pi, &irq_flags);
    pi->valid = 0;
    hk_flush_buffer(pi, sizeof(struct hk_inode), true);
    hk_memlock_pi(sb, pi, &irq_flags);

    /* invalid blks hdr belongs to inode */
    hk_free_data_blks(sb, sih);

    freed = hk_free_dram_resource(sb, sih);

    hk_dbg_verbose("%s: %d Blks Freed\n", __func__, freed);

    /* Then we can free the inode */
    ret = hk_free_ino(sb, sih->ino);
    if (ret)
        hk_err(sb, "%s: free inode %lu failed\n",
               __func__, sih->ino);

    return ret;
}

/* Write back routinue, but we flush inode  */
int hk_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    /* write_inode should never be called because we always keep our inodes
     */
    return 0;
}

void hk_evict_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info_header *sih = HK_IH(inode);
    INIT_TIMING(evict_time);
    int destroy = 0;
    int ret;

    HK_START_TIMING(evict_inode_t, evict_time);
    if (!sih) {
        hk_err(sb, "%s: ino %lu sih is NULL!\n",
               __func__, inode->i_ino);
        HK_ASSERT(0);
        goto out;
    }

#ifdef CONFIG_DYNAMIC_WORKLOAD
    hk_dw_forward(&sbi->dw, sih->i_size);
#endif

    hk_dbgv("%s: %lu\n", __func__, inode->i_ino);

    if (!inode->i_nlink && !is_bad_inode(inode)) {
        if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
            goto out;

#ifdef CONFIG_CMT_BACKGROUND
        /* close -> flush the idr (inode data root) to persistent memory */
        hk_delegate_close_async(sb, inode);
        hk_free_data_blks(sb, sih);
        /* delete -> I have a whole file view, and I can deallocate PM resource, including data and ino. */
        hk_delegate_delete_async(sb, inode);
        hk_free_dram_resource(sb, sih);
#else
        struct hk_inode *pi = hk_get_pi_by_ino(sb, inode->i_ino);
        ret = hk_free_inode_resource(sb, pi, sih);
        if (ret)
            goto out;
#endif

        destroy = 1;
        inode->i_mtime = inode->i_ctime = current_time(inode);
        inode->i_size = 0;
    }

out:
    if (destroy == 0) {
#ifdef CONFIG_CMT_BACKGROUND
        hk_delegate_close_async(sb, inode);
#endif
        hk_dbgv("%s: closing %lu\n", __func__, inode->i_ino);
        hk_free_dram_resource(sb, sih);
    }

    /* TODO: Since we don't use page-cache, do we really need the following
     * call?
     */
    truncate_inode_pages(&inode->i_data, 0);

    clear_inode(inode);
    HK_END_TIMING(evict_inode_t, evict_time);
}

int hk_getattr(const struct path *path, struct kstat *stat,
               u32 request_mask, unsigned int query_flags)
{
    struct inode *inode = d_inode(path->dentry);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    unsigned int flags = sih->i_flags;

    if (flags & FS_APPEND_FL)
        stat->attributes |= STATX_ATTR_APPEND;
    if (flags & FS_COMPR_FL)
        stat->attributes |= STATX_ATTR_COMPRESSED;
    if (flags & FS_IMMUTABLE_FL)
        stat->attributes |= STATX_ATTR_IMMUTABLE;
    if (flags & FS_NODUMP_FL)
        stat->attributes |= STATX_ATTR_NODUMP;

    generic_fillattr(inode, stat);
    /* stat->blocks should be the number of 512B blocks */
    stat->blocks = (inode->i_blocks << inode->i_sb->s_blocksize_bits) >> 9;
    return 0;
}

void hk_set_inode_flags(struct inode *inode, unsigned int flags)
{
    inode->i_flags &=
        ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME | S_DIRSYNC);
    if (flags & FS_SYNC_FL)
        inode->i_flags |= S_SYNC;
    if (flags & FS_APPEND_FL)
        inode->i_flags |= S_APPEND;
    if (flags & FS_IMMUTABLE_FL)
        inode->i_flags |= S_IMMUTABLE;
    if (flags & FS_NOATIME_FL)
        inode->i_flags |= S_NOATIME;
    if (flags & FS_DIRSYNC_FL)
        inode->i_flags |= S_DIRSYNC;
    inode_has_no_xattr(inode);
    inode->i_flags |= S_DAX;
}

static void hk_get_inode_flags(struct inode *inode, struct hk_inode *pi)
{
    unsigned int flags = inode->i_flags;
    unsigned int hk_flags = le32_to_cpu(pi->i_flags);

    hk_flags &= ~(FS_SYNC_FL | FS_APPEND_FL | FS_IMMUTABLE_FL |
                  FS_NOATIME_FL | FS_DIRSYNC_FL);
    if (flags & S_SYNC)
        hk_flags |= FS_SYNC_FL;
    if (flags & S_APPEND)
        hk_flags |= FS_APPEND_FL;
    if (flags & S_IMMUTABLE)
        hk_flags |= FS_IMMUTABLE_FL;
    if (flags & S_NOATIME)
        hk_flags |= FS_NOATIME_FL;
    if (flags & S_DIRSYNC)
        hk_flags |= FS_DIRSYNC_FL;

    pi->i_flags = cpu_to_le32(hk_flags);
}

void hk_init_pi(struct super_block *sb, struct inode *inode, umode_t mode, u32 i_flags)
{
    unsigned long irq_flags = 0;
    u64 ino = inode->i_ino;
    struct hk_inode *pi = hk_get_pi_by_ino(sb, ino);
    struct hk_sb_info *sbi = HK_SB(sb);
    
    hk_memunlock_pi(sb, pi, &irq_flags);
    pi->i_flags = hk_mask_flags(mode, i_flags);
    pi->ino = ino;
    pi->i_create_time = current_time(inode).tv_sec;
    pi->i_mode = cpu_to_le16(inode->i_mode);
    pi->i_uid = cpu_to_le32(i_uid_read(inode));
    pi->i_gid = cpu_to_le32(i_gid_read(inode));
    pi->i_links_count = cpu_to_le16(inode->i_nlink);
    pi->i_size = cpu_to_le64(inode->i_size);
    pi->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
    pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
    pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
    pi->i_generation = cpu_to_le32(inode->i_generation);

    pi->root.ofs_next = TRANS_ADDR_TO_OFS(sbi, &pi->root);
    pi->tstamp = cpu_to_le64(get_version(HK_SB(inode->i_sb)));

    if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
        pi->dev.rdev = cpu_to_le32(inode->i_rdev);
    hk_memlock_pi(sb, pi, &irq_flags);
}

/* copy persistent icp to struct inode */
static int hk_build_vfs_inode(struct super_block *sb, struct inode *inode,
                              u64 ino)
{
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode *pi;
    struct hk_inode_info_header *sih = &si->header;
    int ret = -EIO;

    pi = hk_get_pi_by_ino(sb, ino);

    inode->i_mode = sih->i_mode;
    i_uid_write(inode, le32_to_cpu(pi->i_uid));
    i_gid_write(inode, le32_to_cpu(pi->i_gid));

    inode->i_generation = le32_to_cpu(pi->i_generation);
    hk_set_inode_flags(inode, le32_to_cpu(pi->i_flags));

    inode->i_blocks = sih->i_blocks;
    inode->i_mapping->a_ops = &hk_aops_dax;

    switch (inode->i_mode & S_IFMT) {
    case S_IFREG:
        inode->i_op = &hk_file_inode_operations;
        inode->i_fop = &hk_dax_file_operations;
        break;
    case S_IFDIR:
        inode->i_op = &hk_dir_inode_operations;
        inode->i_fop = &hk_dir_operations;
        break;
    case S_IFLNK:
        inode->i_op = &hk_symlink_inode_operations;
        break;
    default:
        inode->i_op = &hk_special_inode_operations;
        init_special_inode(inode, inode->i_mode,
                           le32_to_cpu(pi->dev.rdev));
        break;
    }

    /* Update size and time after rebuild the tree */
    inode->i_size = le64_to_cpu(sih->i_size);
    inode->i_atime.tv_sec = (__s32)le32_to_cpu(pi->i_atime);
    inode->i_ctime.tv_sec = (__s32)le32_to_cpu(pi->i_ctime);
    inode->i_mtime.tv_sec = (__s32)le32_to_cpu(pi->i_mtime);
    inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec =
        inode->i_ctime.tv_nsec = 0;
    set_nlink(inode, le16_to_cpu(pi->i_links_count));
    return 0;

bad_inode:
    make_bad_inode(inode);
    return ret;
}

u64 hk_alloc_ino(struct super_block *sb)
{
    u64 ino = (u64)-1;
    struct hk_sb_info *sbi = HK_SB(sb);
    unsigned long req_ino_num = 1;
    INIT_TIMING(new_hk_ino_time);

    HK_START_TIMING(new_HK_inode_t, new_hk_ino_time);

    int cpuid, start_cpuid;

    cpuid = hk_get_cpuid(sb);
    start_cpuid = cpuid;

    do {
        spin_lock(&sbi->inode_forest_locks[cpuid]);
        if (unlikely(sbi->inode_forest_init[cpuid] == false)) {
            hk_init_free_inode_list_percore(sb, cpuid, false);
        }

        req_ino_num = 1;
        ino = hk_range_pop(&sbi->inode_forest[cpuid], &req_ino_num);
        if (req_ino_num != 0) {
            spin_unlock(&sbi->inode_forest_locks[cpuid]);
            break;
        }
    
        spin_unlock(&sbi->inode_forest_locks[cpuid]);
        cpuid = (cpuid + 1) % sbi->cpus;
    } while (cpuid != start_cpuid);

    if (req_ino_num == 0) {
        hk_info("No free inode\n");
        BUG_ON(1);
    }

    HK_END_TIMING(new_HK_inode_t, new_hk_ino_time);
    return ino;
}

struct inode *hk_create_inode(enum hk_new_inode_type type, struct inode *dir,
                              u64 ino, umode_t mode, size_t size, dev_t rdev,
                              const struct qstr *qstr)
{
    struct super_block *sb;
    struct hk_sb_info *sbi;
    struct inode *inode;
    struct hk_inode *diri = NULL;
    struct hk_inode_info *si;
    struct hk_inode_info_header *sih = NULL;
    struct hk_inode *pi;
    int errval;
    unsigned long irq_flags = 0;
    INIT_TIMING(new_inode_time);

    HK_START_TIMING(new_vfs_inode_t, new_inode_time);
    sb = dir->i_sb;
    sbi = (struct hk_sb_info *)sb->s_fs_info;
    inode = new_inode(sb);
    if (!inode) {
        errval = -ENOMEM;
        goto fail2;
    }

    inode_init_owner(inode, dir, mode);
    inode->i_blocks = inode->i_size = 0;
    inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);

    inode->i_generation = atomic_add_return(1, &sbi->next_generation);
    inode->i_size = size;

    diri = hk_get_pi_by_ino(sb, dir->i_ino);
    if (!diri) {
        errval = -EACCES;
        goto fail1;
    }

    pi = (struct hk_inode *)hk_get_pi_by_ino(sb, ino);
    hk_dbg_verbose("%s: allocating inode %llu @ 0x%llx\n",
                   __func__, ino, (u64)pi);

    /* chosen inode is in ino */
    inode->i_ino = ino;

    switch (type) {
    case TYPE_CREATE:
        inode->i_op = &hk_file_inode_operations;
        inode->i_mapping->a_ops = &hk_aops_dax;
        inode->i_fop = &hk_dax_file_operations;
        break;
    case TYPE_MKNOD:
        init_special_inode(inode, mode, rdev);
        inode->i_op = &hk_special_inode_operations;
        break;
    case TYPE_SYMLINK:
        inode->i_op = &hk_symlink_inode_operations;
        inode->i_mapping->a_ops = &hk_aops_dax;
        break;
    case TYPE_MKDIR:
        inode->i_op = &hk_dir_inode_operations;
        inode->i_fop = &hk_dir_operations;
        inode->i_mapping->a_ops = &hk_aops_dax;
        set_nlink(inode, 2);
        break;
    default:
        hk_dbg("Unknown new inode type %d\n", type);
        break;
    }

    si = HK_I(inode);
    sih = &si->header;
    hk_init_header(sb, sih, inode->i_mode);
    sih->ino = ino;
    sih->tstamp = le64_to_cpu(pi->tstamp);

    /* Initial Pi Outside */
    hk_set_inode_flags(inode, hk_mask_flags(mode, diri->i_flags));
    sih->i_flags = le32_to_cpu(pi->i_flags);
    sih->pi_addr = (u64)pi;

#ifdef CONFIG_CMT_BACKGROUND
    struct hk_cmt_node *cmt_node, *exsit;
    cmt_node = hk_cmt_node_init(sb, ino);
    errval = hk_cmt_manage_node(sb, cmt_node, &exsit);
    if (errval == -EEXIST) {
        hk_cmt_node_destroy(cmt_node);
        cmt_node = exsit;
    }
    sih->cmt_node = cmt_node;
#endif

    if (insert_inode_locked(inode) < 0) {
        hk_err(sb, "hk_new_inode failed ino %llu\n", inode->i_ino);
        errval = -EINVAL;
        goto fail1;
    }

    hk_flush_buffer(pi, sizeof(struct hk_inode), false);
    HK_END_TIMING(new_vfs_inode_t, new_inode_time);
    return inode;

fail1:
    make_bad_inode(inode);
    iput(inode);

fail2:
    HK_END_TIMING(new_vfs_inode_t, new_inode_time);
    return ERR_PTR(errval);
}

static int hk_handle_setattr_operation(struct super_block *sb, struct inode *inode,
                                       struct hk_inode *pi, unsigned int ia_valid, struct iattr *attr)
{
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    int ret = 0;

    if (ia_valid & ATTR_MODE)
        sih->i_mode = inode->i_mode;

    ret = hk_commit_sizechange(sb, inode, attr->ia_size);

    return ret;
}

/*
 * Zero the tail page. Used in resize request
 * to avoid to keep data in case the file grows again.
 */
void hk_prepare_truncate(struct super_block *sb,
                         struct inode *inode, loff_t newsize)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    u64 addr;
    unsigned long offset = newsize & (sb->s_blocksize - 1);
    unsigned long index, length;
    unsigned long irq_flags = 0;

    if (offset == 0 || newsize > inode->i_size)
        return;

    length = sb->s_blocksize - offset;
    index = newsize >> sb->s_blocksize_bits;

    addr = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index));
    if (addr == 0)
        return;

    hk_memunlock_range(sb, addr + offset, length, &irq_flags);
    memset_nt(addr + offset, 0, length);
    hk_memlock_range(sb, addr + offset, length, &irq_flags);
}

/*
 * Free data blocks from inode in the range start <=> end
 */
static void hk_truncate_file_blocks(struct inode *inode, loff_t start, loff_t end)
{
    struct super_block *sb = inode->i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    struct hk_cmt_dbatch dbatch;
    unsigned int data_bits = sb->s_blocksize_bits;
    s64 start_index, end_index, index;
    u64 addr;
    int freed = 0;

    // TODO: We're not handle holes in the file
    inode->i_mtime = inode->i_ctime = current_time(inode);

    start_index = (start + (1UL << data_bits) - 1) >> data_bits;

    if (end == 0)
        return;
    end_index = (end - 1) >> data_bits;

    if (start_index > end_index)
        return;

    /* the inode lock is already held */
    for (index = end_index; index >= start_index; index--) {
        addr = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index));
        linix_delete(&sih->ix, index, index, true);

        hk_init_and_inc_cmt_dbatch(&dbatch, addr, index, 1);
#ifdef CONFIG_CMT_BACKGROUND
        hk_delegate_data_async(sb, inode, &dbatch, 0, CMT_INVALID_DATA);
#else
        use_layout_for_addr(sb, addr);
        sm_invalid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch), addr, sih->ino);
        unuse_layout_for_addr(sb, addr);
#endif
        freed++;
    }

    inode->i_blocks -= (freed * (1 << (data_bits -
                                       sb->s_blocksize_bits)));

    sih->i_blocks = inode->i_blocks;
}

static void hk_setsize(struct inode *inode, loff_t oldsize, loff_t newsize)
{
    struct super_block *sb = inode->i_sb;
    struct hk_inode_info_header *sih = HK_IH(inode);
    INIT_TIMING(setsize_time);

    /* We only support truncate regular file */
    if (!(S_ISREG(inode->i_mode))) {
        hk_err(inode->i_sb, "%s:wrong file mode %x\n", inode->i_mode);
        return;
    }

    HK_START_TIMING(setsize_t, setsize_time);

    inode_dio_wait(inode);

    hk_dbgv("%s: inode %lu, old size %llu, new size %llu\n",
            __func__, inode->i_ino, oldsize, newsize);

    if (newsize != oldsize) {
        hk_prepare_truncate(sb, inode, newsize);
        i_size_write(inode, newsize);
        sih->i_size = newsize;
    }

    /* FIXME: we should make sure that there is nobody reading the inode
     * before truncating it. Also we need to munmap the truncated range
     * from application address space, if mmapped.
     */
    /* synchronize_rcu(); */

    /* FIXME: Do we need to clear truncated DAX pages? */
    //	dax_truncate_page(inode, newsize, hk_dax_get_block);
    truncate_pagecache(inode, newsize);
    hk_truncate_file_blocks(inode, newsize, oldsize);
    HK_END_TIMING(setsize_t, setsize_time);
}

int hk_notify_change(struct dentry *dentry, struct iattr *attr)
{
    struct inode *inode = dentry->d_inode;
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    struct super_block *sb = inode->i_sb;
    struct hk_inode *pi = hk_get_inode(sb, inode);
    int ret;
    unsigned int ia_valid = attr->ia_valid, attr_mask;
    loff_t oldsize = inode->i_size;
    INIT_TIMING(setattr_time);

    HK_START_TIMING(setattr_t, setattr_time);
    if (!pi) {
        ret = -EACCES;
        goto out;
    }

    ret = setattr_prepare(dentry, attr);
    if (ret)
        goto out;

    /* Update inode with attr except for size */
    setattr_copy(inode, attr);

    attr_mask = ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_SIZE | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME;

    ia_valid = ia_valid & attr_mask;

    if (ia_valid == 0)
        goto out;

    ret = hk_handle_setattr_operation(sb, inode, pi, ia_valid, attr);
    if (ret)
        goto out;

    /* Only after setattr entry is committed, we can truncate size */
    if ((ia_valid & ATTR_SIZE) && (attr->ia_size != oldsize ||
                                   pi->i_flags & cpu_to_le32(HK_EOFBLOCKS_FL))) {
        hk_setsize(inode, oldsize, attr->ia_size);
    }

out:
    HK_END_TIMING(setattr_t, setattr_time);
    return ret;
}

struct inode *hk_iget_opened(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct hk_inode_info *si;

    inode = iget_locked(sb, ino);

    if (unlikely(!inode)) {
        hk_err(sb, "%s: No memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }

    /* The inode is already exsited */
    if (!(inode->i_state & I_NEW)) {
        goto out;
    }

    si = HK_I(inode);
    hk_rebuild_inode(sb, si, ino, false);
    inode->i_ino = ino;

    /* The inode has't been load up, it's not opened */
    unlock_new_inode(inode);

    iput(inode);

    inode = NULL;
out:
    return inode;
}

struct inode *hk_iget(struct super_block *sb, unsigned long ino)
{
    struct hk_inode_info *si;
    struct inode *inode;
    struct hk_inode *pi;
    unsigned long irq_flags = 0;
    int err;

    inode = iget_locked(sb, ino);
    if (unlikely(!inode)) {
        hk_err(sb, "%s: No memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    /* The inode is already exsited */
    if (!(inode->i_state & I_NEW))
        return inode;

    si = HK_I(inode);

    hk_dbgv("%s: inode %lu\n", __func__, ino);

    pi = hk_get_pi_by_ino(sb, ino);

    if (!pi) {
        hk_dbg("%s: failed to get inode %lu, only supports up to %lu\n",
               __func__, ino, HK_NUM_INO - 1);
        err = -EACCES;
        goto fail;
    }

    err = hk_rebuild_inode(sb, si, ino, true);
    if (err) {
        hk_dbg("%s: failed to rebuild inode %lu\n", __func__, ino);
        goto fail;
    }

    err = hk_build_vfs_inode(sb, inode, ino);
    if (unlikely(err)) {
        hk_dbg("%s: failed to read inode %lu\n", __func__, ino);
        goto fail;
    }

    inode->i_ino = ino;

    unlock_new_inode(inode);
    return inode;
fail:
    iget_failed(inode);
    return ERR_PTR(err);
}

const struct address_space_operations hk_aops_dax = {
    .writepages = NULL,
    .direct_IO = NULL,
    /*.dax_mem_protect	= hk_dax_mem_protect,*/
};
