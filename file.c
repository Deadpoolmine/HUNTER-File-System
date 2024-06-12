/*
 * HUNTER File Operation impl.
 *
 * Copyright 2023-2024 Regents of the University of Harbin Institute of Technology, Shenzhen
 * Computer science and technology, Yanqi Pan <deadpoolmine@qq.com>
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "hunter.h"

static ssize_t do_dax_mapping_read(struct file *filp, char __user *buf,
                                   size_t len, loff_t *ppos)
{
    struct inode *inode = filp->f_mapping->host;
    struct super_block *sb = inode->i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;

    pgoff_t index, end_index;
    unsigned long offset;
    loff_t isize, pos;
    size_t copied = 0;
    size_t error = 0;

    INIT_TIMING(memcpy_time);

    pos = *ppos;
    index = pos >> PAGE_SHIFT; /* Start from which blk */
    offset = pos & ~PAGE_MASK; /* Start from ofs to the blk */

    if (!access_ok(buf, len)) {
        error = -EFAULT;
        goto out;
    }

    isize = i_size_read(inode); /* Get file size */
    if (!isize)
        goto out;

    hk_dbgv("%s: inode %lu, offset %lld, count %lu, size %lld\n",
            __func__, inode->i_ino, pos, len, isize);

    if (len > isize - pos)
        len = isize - pos;

    if (len <= 0)
        goto out;

    end_index = (isize - 1) >> PAGE_SHIFT;

    do {
        unsigned long nr, left;
        unsigned long blk_addr;
        void *dax_mem = NULL;
        bool zero = false;

        nr = HK_LBLK_SZ;

        /* nr is the maximum number of bytes to copy from this page */
        if (index >= end_index) {
            if (index > end_index)
                goto out;
            nr = ((isize - 1) & ~PAGE_MASK) + 1;
            if (nr <= offset)
                goto out;
        }

        blk_addr = linix_get(&sih->ix, index);
        if (blk_addr == 0) { /* It's a file hole */
            zero = true;
        } else {
            dax_mem = hk_get_block(sb, blk_addr);
        }

        nr = nr - offset;
        if (nr > len - copied)
            nr = len - copied;

        HK_START_TIMING(memcpy_r_nvmm_t, memcpy_time);

        hk_dbgv("%s: index: %d, blk_addr: 0x%llx, dax_mem: 0x%llx, zero: %d, nr: 0x%lx\n", __func__, index, blk_addr, dax_mem, zero, nr);

        if (!zero)
            left = __copy_to_user(buf + copied,
                                  dax_mem + offset, nr);
        else /* This will not happen now */
            left = __clear_user(buf + copied, nr);

        HK_END_TIMING(memcpy_r_nvmm_t, memcpy_time);

        if (left) {
            hk_dbg("%s ERROR!: bytes %lu, left %lu\n",
                   __func__, nr, left);
            error = -EFAULT;
            goto out;
        }

        copied += nr;
        offset += nr;
        index += offset >> PAGE_SHIFT;
        offset &= ~PAGE_MASK;
    } while (copied < len);

out:
    *ppos = pos + copied;
    if (filp)
        file_accessed(filp);

    // hk_STATS_ADD(read_bytes, copied);

    hk_dbgv("%s returned %zu\n", __func__, copied);
    return copied ? copied : error;
}

static __always_inline bool hk_check_overlay(struct hk_inode_info *si, u64 index)
{
    struct hk_inode_info_header *sih = &si->header;
    bool is_overlay = false;

    if (index < sih->ix.num_slots && linix_get(&sih->ix, index) != 0) {
        is_overlay = true;
    }

    return is_overlay;
}

static bool hk_try_perform_cow(struct hk_inode_info *si, u64 cur_addr, u64 index,
                               u64 start_index, u64 end_index,
                               loff_t *each_ofs, size_t *each_size,
                               size_t len)
{
    struct super_block *sb = si->vfs_inode.i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info_header *sih = &si->header;
    bool is_overlay = false;
    unsigned char tmp_content[HK_LBLK_SZ];
    u64 blk_addr;
    unsigned long irq_flags = 0;
    INIT_TIMING(partial_time);

    if (hk_check_overlay(si, index)) {
        if (index == start_index || index == end_index) { /* Might perform cow */
            HK_START_TIMING(partial_block_t, partial_time);
            if (index == start_index && *each_ofs != 0) {
                blk_addr = linix_get(&sih->ix, index);
                memcpy_mcsafe(tmp_content, hk_get_block(sb, blk_addr), HK_LBLK_SZ);
                *each_size = min(HK_LBLK_SZ - *each_ofs, len);
                hk_memunlock_range(sb, cur_addr, *each_ofs, &irq_flags);
                memcpy_to_pmem_nocache(cur_addr, tmp_content, *each_ofs);
                hk_memlock_range(sb, cur_addr, *each_ofs, &irq_flags);
            }
            if (index == end_index && len < HK_LBLK_SZ) {
                blk_addr = linix_get(&sih->ix, index);
                memcpy_mcsafe(tmp_content, hk_get_block(sb, blk_addr), HK_LBLK_SZ);
                *each_size = len;
                hk_memunlock_range(sb, cur_addr + (len + *each_ofs), HK_LBLK_SZ - (len + *each_ofs), &irq_flags);
                memcpy_to_pmem_nocache(cur_addr + (len + *each_ofs), tmp_content + (len + *each_ofs),
                                       HK_LBLK_SZ - (len + *each_ofs));
                hk_memlock_range(sb, cur_addr + (len + *each_ofs), HK_LBLK_SZ - (len + *each_ofs), &irq_flags);
            }
            HK_END_TIMING(partial_block_t, partial_time);
        }
        is_overlay = true;
    } else { /* Set to zero */
        HK_START_TIMING(partial_block_t, partial_time);
        if (index == start_index && *each_ofs != 0) {
            *each_size = min(HK_LBLK_SZ - *each_ofs, len);
            hk_memunlock_range(sb, cur_addr, *each_ofs, &irq_flags);
            memset_nt(cur_addr, 0, *each_ofs);
            hk_memlock_range(sb, cur_addr, *each_ofs, &irq_flags);
        }
        if (index == end_index && len < HK_LBLK_SZ) {
            *each_size = len;
            hk_memunlock_range(sb, cur_addr + (len + *each_ofs), HK_LBLK_SZ - (len + *each_ofs), &irq_flags);
            memset_nt(cur_addr + (len + *each_ofs), 0, HK_LBLK_SZ - (len + *each_ofs));
            hk_memlock_range(sb, cur_addr + (len + *each_ofs), HK_LBLK_SZ - (len + *each_ofs), &irq_flags);
        }
        HK_END_TIMING(partial_block_t, partial_time);
    }

    return is_overlay;
}

static int do_perform_write(struct inode *inode, struct hk_layout_prep *prep,
                            loff_t ofs, size_t size, unsigned char *content,
                            u64 index_cur, u64 start_index, u64 end_index,
                            size_t *out_size)
{
    u64 i;
    size_t each_blks, each_size;
    loff_t each_ofs;
    u64 dst_blks;
    u64 addr, addr_overlayed;
    bool is_overlay;
    struct super_block *sb = inode->i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    struct hk_header *hdr;
    struct hk_cmt_dbatch batch, batch_tmp;
    unsigned long irq_flags = 0;
    u64 _size = 0;

    INIT_TIMING(memcpy_time);

    addr = prep->target_addr;
    *out_size = 0;

    for (i = 0; i < prep->blks_prepared; i++) {
        each_size = HK_LBLK_SZ;
        each_ofs = ofs & (HK_LBLK_SZ - 1);

#ifndef CONFIG_LAYOUT_TIGHT
        if (i == 0 || i == prep->blks_prepared - 1) {
            is_overlay = hk_try_perform_cow(si, addr, index_cur,
                                            start_index, end_index,
                                            &each_ofs, &each_size,
                                            size);

            HK_START_TIMING(memcpy_w_nvmm_t, memcpy_time);
            hk_memunlock_range(sb, addr + each_ofs, each_size, &irq_flags);
            /* Make sure Align 64 */
            memcpy_to_pmem_nocache(addr + each_ofs, content, each_size);
            hk_memlock_range(sb, addr + each_ofs, each_size, &irq_flags);
            HK_END_TIMING(memcpy_w_nvmm_t, memcpy_time);

            _size = ofs + each_size;
            hk_init_and_inc_cmt_dbatch(&batch_tmp, addr, index_cur, 1);
#ifndef CONFIG_CMT_BACKGROUND
            use_layout_for_addr(sb, addr);
            sm_valid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr, sm_get_next_addr_by_dbatch(sb, sih, &batch_tmp),
                               sih->ino, index_cur, get_version(sbi), _size, inode->i_ctime.tv_sec);
            unuse_layout_for_addr(sb, addr);
#else
            hk_delegate_data_async(sb, inode, &batch_tmp, _size, CMT_VALID_DATA);
#endif

            if (is_overlay) {
#ifndef CONFIG_CMT_BACKGROUND
                /* commit the inode */
                hk_commit_attrchange(sb, inode);

                addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));

                /* invalid the old one */
                use_layout_for_addr(sb, addr_overlayed);
                hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
                sm_invalid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr_overlayed, sih->ino); /* Then invalid the old */
                unuse_layout_for_addr(sb, addr_overlayed);

                hk_dbgv("Invalid Blk %llu\n", hk_get_dblk_by_addr(sbi, addr_overlayed));
#else
                hk_dbgv("pos %llu, size %llu, index %llu, addr %llu\n", ofs, size, index_cur, addr);
                addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));
                hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
                hk_delegate_data_async(sb, inode, &batch_tmp, 0, CMT_INVALID_DATA);
#endif
            }

            linix_insert(&sih->ix, index_cur, addr, true);

            addr += HK_PBLK_SZ;
            index_cur += 1;
        } else {
            if (prep->blks_prepared - 2 <= 0) {
                continue;
            } else {
                dst_blks = prep->blks_prepared - 2;
                each_size = (dst_blks * HK_LBLK_SZ);
            }

            HK_START_TIMING(memcpy_w_nvmm_t, memcpy_time);
            hk_memunlock_range(sb, addr, each_size, &irq_flags);
            memcpy_to_pmem_nocache(addr, content, each_size);
            hk_memlock_range(sb, addr, each_size, &irq_flags);
            HK_END_TIMING(memcpy_w_nvmm_t, memcpy_time);

            /* minus 1 due to i++ in the for loop */
            i += (dst_blks - 1);

            hk_init_cmt_dbatch(&batch, addr, index_cur, dst_blks);

            _size = ofs + HK_LBLK_SZ;

            while (dst_blks) {
                is_overlay = hk_check_overlay(si, index_cur);

#ifndef CONFIG_CMT_BACKGROUND
                use_layout_for_addr(sb, addr);
                hk_init_and_inc_cmt_dbatch(&batch_tmp, addr, index_cur, 1);
                sm_valid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr, sm_get_next_addr_by_dbatch(sb, sih, &batch_tmp),
                                   sih->ino, index_cur, get_version(sbi), _size, inode->i_ctime.tv_sec);
                unuse_layout_for_addr(sb, addr);
#endif

                hk_inc_cmt_dbatch(&batch);

                if (is_overlay) {
#ifndef CONFIG_CMT_BACKGROUND
                    /* commit the inode */
                    hk_commit_attrchange(sb, inode);

                    addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));

                    /* invalid the old one */
                    use_layout_for_addr(sb, addr_overlayed);
                    hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
                    sm_invalid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr_overlayed, sih->ino); /* Then invalid the old */
                    unuse_layout_for_addr(sb, addr_overlayed);

                    hk_dbgv("Invalid Blk %llu\n", hk_get_dblk_by_addr(sbi, addr_overlayed));
#else
                    hk_delegate_data_async(sb, inode, &batch, _size, CMT_VALID_DATA);
                    hk_next_cmt_dbatch(&batch);

                    addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));
                    hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
                    hk_delegate_data_async(sb, inode, &batch_tmp, 0, CMT_INVALID_DATA);
#endif
                }

                linix_insert(&sih->ix, index_cur, addr, true);

                dst_blks -= 1;

                addr += HK_PBLK_SZ;
                index_cur += 1;
                _size += HK_LBLK_SZ;
            }

            if (hk_is_cmt_dbatch_valid(&batch)) {
                hk_delegate_data_async(sb, inode, &batch, _size, CMT_VALID_DATA);
            }
        }
#else
        is_overlay = hk_try_perform_cow(si, addr, index_cur,
                                        start_index, end_index,
                                        &each_ofs, &each_size,
                                        size);

        HK_START_TIMING(memcpy_w_nvmm_t, memcpy_time);
        hk_memunlock_range(sb, addr + each_ofs, each_size, &irq_flags);
        /* Make sure Align 64 */
        memcpy_to_pmem_nocache(addr + each_ofs, content, each_size);
        hk_memlock_range(sb, addr + each_ofs, each_size, &irq_flags);
        HK_END_TIMING(memcpy_w_nvmm_t, memcpy_time);

        _size = ofs + each_size;
        hk_init_and_inc_cmt_dbatch(&batch_tmp, addr, index_cur, 1);
#ifndef CONFIG_CMT_BACKGROUND
        use_layout_for_addr(sb, addr);
        sm_valid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr, sm_get_next_addr_by_dbatch(sb, sih, &batch_tmp),
                           sih->ino, index_cur, get_version(sbi), _size, inode->i_ctime.tv_sec);
        unuse_layout_for_addr(sb, addr);
        /* flush header */
        hk_flush_buffer(addr + HK_LBLK_SZ, CACHELINE_SIZE, true);
#else
        hk_delegate_data_async(sb, inode, &dbatch_tmp, CMT_VALID_DATA);
#endif

        if (is_overlay) {
#ifndef CONFIG_CMT_BACKGROUND
            /* commit the inode */
            hk_commit_attrchange(sb, inode);

            addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));

            /* invalid the old one */
            use_layout_for_addr(sb, addr_overlayed);
            hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
            sm_invalid_data_sync(sb, sm_get_prev_addr_by_dbatch(sb, sih, &batch_tmp), addr_overlayed, sih->ino); /* Then invalid the old */
            unuse_layout_for_addr(sb, addr_overlayed);

            hk_dbgv("Invalid Blk %llu\n", hk_get_dblk_by_addr(sbi, addr_overlayed));
#else
            addr_overlayed = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, index_cur));
            hk_init_and_inc_cmt_dbatch(&batch_tmp, addr_overlayed, index_cur, 1);
            hk_delegate_data_async(sb, inode, &batch_tmp, 0, CMT_INVALID_DATA);
#endif
        }

        linix_insert(&sih->ix, index_cur, addr, true);

        addr += HK_PBLK_SZ;
        index_cur += 1;
#endif
        content += each_size;
        ofs += each_size;
        size -= each_size;
        *out_size += each_size;
    }

    return 0;
}

/* Check whether partial content can be written in the allocated block. */
/* `overflow` indicates that the [pos, pos + len) can not be written */
/* in the current block. */
static __always_inline bool hk_check_in_place_append(struct hk_inode_info_header *sih, loff_t pos, size_t len,
                                                     bool *overflow, size_t *out_size)
{
    bool is_inplace = false;
    loff_t end_pos = pos + len - 1;
    loff_t allocated_size = sih->i_blocks << PAGE_SHIFT;

    *overflow = false;

    if (pos >= allocated_size) {
        *out_size = 0;
        return false;
    }

    if (end_pos >= allocated_size) {
        *overflow = true;
    }

    *out_size = min(allocated_size - pos, len);

    return true;
}

static size_t hk_try_in_place_append_write(struct hk_inode_info *si, loff_t pos, size_t len, unsigned char *content)
{
    bool in_place = false, overflow = false;
    struct hk_inode_info_header *sih = &si->header;
    struct super_block *sb = si->vfs_inode.i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    unsigned long irq_flags = 0;
    size_t out_size = 0;
    u64 target_addr;
    struct hk_cmt_dbatch batch;

    INIT_TIMING(memcpy_time);

    in_place = hk_check_in_place_append(sih, pos, len, &overflow, &out_size);
    // hk_info("ino: %ld, i_size: %ld, i_blocks: %ld, in_place: %d, overflow: %d, written: %lu\n",
    //         sih->ino, sih->i_size, sih->i_blocks, in_place, overflow, written);
    if (in_place) {
        target_addr = TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, pos >> PAGE_SHIFT));

        HK_START_TIMING(memcpy_w_nvmm_t, memcpy_time);
        hk_memunlock_range(sb, target_addr, out_size, &irq_flags);
        memcpy_to_pmem_nocache(target_addr, content, out_size);
        hk_memlock_range(sb, target_addr, out_size, &irq_flags);
        HK_END_TIMING(memcpy_w_nvmm_t, memcpy_time);

#ifdef CONFIG_CMT_BACKGROUND
        hk_init_and_inc_cmt_dbatch(&batch, target_addr, pos >> PAGE_SHIFT, 1);
        hk_delegate_data_async(sb, &si->vfs_inode, &batch, sih->i_size + out_size, CMT_UPDATE_DATA);
#else
        sm_update_data_sync(sb, target_addr, sih->i_size + out_size);
#endif
        sih->i_size = sih->i_size + out_size;
    }

    return out_size;
}

ssize_t do_hk_file_write(struct file *filp, const char __user *buf,
                         size_t len, loff_t *ppos)
{
    struct inode *inode = filp->f_mapping->host;
    struct super_block *sb = inode->i_sb;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    pgoff_t index, start_index, end_index, i;
    unsigned long blks;
    loff_t isize, pos;
    size_t copied = 0;
    ssize_t written = 0;
    size_t error = 0;
    u32 time; /* TODO: For meta logging */
    u64 allocated;
    u64 addr;
    unsigned long irq_flags = 0;
    unsigned char *pbuf = buf;
    struct hk_layout_preps preps;
    struct hk_layout_prep *prep = NULL;
    struct hk_layout_prep tmp_prep;
    size_t out_size = 0;
    bool append_like = false;
    int retries = 0;

    INIT_TIMING(write_time);
    INIT_TIMING(memcpy_time);

    if (len == 0)
        return 0;

    HK_START_TIMING(write_t, write_time);

    if (!access_ok(buf, len)) {
        error = -EFAULT;
        goto out;
    }

    pos = *ppos;

    if (filp->f_flags & O_APPEND) {
        append_like = true;
        pos = i_size_read(inode);
    }

    if (pos >= i_size_read(inode)) {
        append_like = true;
    }

    error = file_remove_privs(filp);
    if (error)
        goto out;

    /* if append write, i.e., pos == file size, try to perform in-place write */
    if (append_like) {
        out_size = hk_try_in_place_append_write(si, pos, len, pbuf);

        pos += out_size;
        len -= out_size;
        pbuf += out_size;
        written += out_size;
    }

    out_size = 0;

    start_index = index = pos >> PAGE_SHIFT;   /* Start from which blk */
    end_index = (pos + len - 1) >> PAGE_SHIFT; /* End till which blk */
    blks = (end_index - index + 1);            /* Total blks to be written */

    inode->i_ctime = inode->i_mtime = current_time(inode);
    time = current_time(inode).tv_sec;

    hk_dbgv("%s: inode %lu, offset %lld, blks %lu, len %lu\n",
            __func__, inode->i_ino, pos, blks, len);

    if (len != 0) {
        hk_prepare_layouts(sb, blks, false, &preps);

        hk_trv_prepared_layouts_init(&preps);

        while (index <= end_index) {
            prep = hk_trv_prepared_layouts(sb, &preps);
            if (!prep) {
                hk_dbg("%s: ERROR: No prep found for index %lu\n", __func__, index);
            retry:
                hk_prepare_gap(sb, false, &tmp_prep);
                if (tmp_prep.target_addr == 0) {
                    retries++;
                    if (retries > 1) {
                        error = -ENOMEM;
                        goto out;
                    }
                    // make sure all the invalidated data is flushed. So that HUNTER can generate gap list.
                    hk_flush_cmt_queue(sb, sbi->cpus);
                    goto retry;
                }
                prep = &tmp_prep;
            }

            do_perform_write(inode, prep, pos, len, pbuf,
                             index, start_index, end_index,
                             &out_size);

            pos += out_size;
            len -= out_size;
            pbuf += out_size;
            written += out_size;

            index += prep->blks_prepared;
        }
    }

    sih->i_blocks = end_index + 1;

    inode->i_blocks = sih->i_blocks;

    hk_dbgv("%s: len %lu\n",
            __func__, len);

    *ppos = pos;
    if (pos > inode->i_size) {
        i_size_write(inode, pos);
        sih->i_size = pos;
    }

out:
    HK_END_TIMING(write_t, write_time);
    return written ? written : error;
}

/* ======================= ANCHOR hooks ========================= */

static loff_t hk_llseek(struct file *file, loff_t offset, int whence)
{
    struct inode *inode = file->f_path.dentry->d_inode;
    int retval;

    if (whence != SEEK_DATA && whence != SEEK_HOLE)
        return generic_file_llseek(file, offset, whence);

    return -ENOTSUPP;
}

/*
 * Wrappers. We need to use the rcu read lock to avoid
 * concurrent truncate operation. No problem for write because we held
 * lock.
 */
static ssize_t hk_dax_file_read(struct file *filp, char __user *buf,
                                size_t len, loff_t *ppos)
{
    struct inode *inode = filp->f_mapping->host;
    ssize_t res;
    INIT_TIMING(dax_read_time);

    HK_START_TIMING(dax_read_t, dax_read_time);
    inode_lock_shared(inode);

    res = do_dax_mapping_read(filp, buf, len, ppos);

    inode_unlock_shared(inode);
    HK_END_TIMING(dax_read_t, dax_read_time);
    return res;
}

static ssize_t hk_dax_file_write(struct file *filp, const char __user *buf,
                                 size_t len, loff_t *ppos)
{
    struct address_space *mapping = filp->f_mapping;
    struct inode *inode = mapping->host;
    int ret;

    if (len == 0)
        return 0;

    // TODO: Range Lock, or multi files ?
    sb_start_write(inode->i_sb);
    inode_lock(inode);

    ret = do_hk_file_write(filp, buf, len, ppos);

    inode_unlock(inode);
    sb_end_write(inode->i_sb);

    return ret;
}

static int hk_open(struct inode *inode, struct file *filp)
{
    return generic_file_open(inode, filp);
}

/* This function is called by both msync() and fsync(). */
static int hk_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
    struct address_space *mapping = file->f_mapping;
    struct inode *inode = file->f_path.dentry->d_inode;
    struct super_block *sb = inode->i_sb;
    int ret = 0;
    INIT_TIMING(fsync_time);

    HK_START_TIMING(fsync_t, fsync_time);

    if (mapping_mapped(mapping)) {
        ret = generic_file_fsync(file, start, end, datasync);
        PERSISTENT_BARRIER();
    }

    mutex_lock(&HK_IH(inode)->cmt_node->processing);
    hk_flush_cmt_node_fast(sb, HK_IH(inode)->cmt_node);
    mutex_unlock(&HK_IH(inode)->cmt_node->processing);

persist:
    HK_END_TIMING(fsync_t, fsync_time);
    return ret;
}

/* This callback is called when a file is closed */
static int hk_flush(struct file *file, fl_owner_t id)
{
    PERSISTENT_BARRIER();
    return 0;
}

const struct inode_operations hk_file_inode_operations = {
    .setattr = hk_notify_change,
    .getattr = hk_getattr,
    .get_acl = NULL,
};

const struct file_operations hk_dax_file_operations = {
    .llseek = hk_llseek,
    .read = hk_dax_file_read,
    .write = hk_dax_file_write,
    .read_iter = NULL,  /* TODO: Not support yet */
    .write_iter = NULL, /* TODO: Not support yet */
    .mmap = NULL,       /* TODO: Not support mmap yet */
    .mmap_supported_flags = MAP_SYNC,
    .open = hk_open,
    .fsync = hk_fsync,
    .flush = hk_flush,
    .unlocked_ioctl = hk_ioctl,
    .fallocate = NULL, /* TODO: Not support yet */
#ifdef CONFIG_COMPAT
    .compat_ioctl = hk_compat_ioctl,
#endif
};