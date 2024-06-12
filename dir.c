/*
 * BRIEF DESCRIPTION
 *
 * File operations for directories.
 * 
 * Copyright 2023-2024 Regents of the University of Harbin Institute of Technology, Shenzhen
 * Computer science and technology, Yanqi Pan <deadpoolmine@qq.com>
 * Copyright 2015-2016 Regents of the University of California,
 * UCSD Non-Volatile Systems Lab, Andiry Xu <jix024@cs.ucsd.edu>
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

#define DT2IF(dt)  (((dt) << 12) & S_IFMT)
#define IF2DT(sif) (((sif) & S_IFMT) >> 12)

static int hk_readdir(struct file *file, struct dir_context *ctx)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct hk_inode *pidir;
    struct hk_inode *child_pi;
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;

    unsigned bkt;
    struct hk_dentry_info *cur;
    u64 pi_addr;
    unsigned long pos = 0;
    ino_t ino;
    void *addr;
    int ret;

    INIT_TIMING(readdir_time);

    HK_START_TIMING(readdir_t, readdir_time);

    pos = ctx->pos;

    if (pos == READDIR_END)
        goto out;

    /* Commit dots */
    if (!dir_emit_dots(file, ctx))
        return 0;

    hash_for_each_bits(sih->dirs, HK_HASH_BITS, bkt, cur, node)
    {
        child_pi = hk_get_pi_by_ino(sb, cur->direntry->ino);
        if (!dir_emit(ctx, cur->direntry->name, cur->direntry->name_len,
                      cur->direntry->ino,
                      IF2DT(le16_to_cpu(child_pi->i_mode)))) {
            hk_dbg("%s: dir_emit failed\n", __func__);
            return -EIO;
        }
    }

    ctx->pos = READDIR_END;
out:
    HK_END_TIMING(readdir_t, readdir_time);
    hk_dbgv("%s return\n", __func__);
    return 0;
}

const struct file_operations hk_dir_operations = {
    .llseek = generic_file_llseek,
    .read = generic_read_dir,
    .iterate = hk_readdir,
    .fsync = noop_fsync,
    .unlocked_ioctl = hk_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = hk_compat_ioctl,
#endif
};
