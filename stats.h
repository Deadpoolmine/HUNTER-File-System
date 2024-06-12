/*
 * HUNTER File System statistics
 *
 * Copyright 2022-2023 Regents of the University of Harbin Institute of Technology, Shenzhen
 * Computer science and technology, Yanqi Pan <deadpoolmine@qq.com>
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

#ifndef _HK_STATS_H
#define _HK_STATS_H

#include "hunter.h"
// TODO: Timing statistics for HUNTER
/* ======================= Timing ========================= */
enum timing_category {
    /* Init */
    init_title_t,
    init_t,
    mount_t,
    ioremap_t,
    new_init_t,
    recovery_t,

    /* Namei operations */
    namei_title_t,
    create_t,
    lookup_t,
    link_t,
    unlink_t,
    symlink_t,
    mkdir_t,
    rmdir_t,
    mknod_t,
    rename_t,
    readdir_t,
    add_dentry_t,
    remove_dentry_t,
    setattr_t,
    setsize_t,

    /* I/O operations */
    io_title_t,
    dax_read_t,
    do_cow_write_t,
    cow_write_t,
    inplace_write_t,
    copy_to_nvmm_t,
    dax_get_block_t,
    read_iter_t,
    write_iter_t,
    wrap_iter_t,
    write_t,

    /* Memory operations */
    memory_title_t,
    memcpy_r_nvmm_t, /* Memory copy read NVMM time */
    memcpy_w_nvmm_t, /* Memory copy write NVMM time */
    partial_block_t,

    /* Memory management */
    mm_title_t,
    new_blocks_t,
    free_blocks_t,

    /* Others */
    others_title_t,
    find_cache_t,
    fsync_t,
    write_pages_t,
    fallocate_t,
    direct_IO_t,
    free_old_t,
    delete_file_tree_t,
    delete_dir_tree_t,
    new_vfs_inode_t,
    new_HK_inode_t,
    free_inode_t,
    free_inode_log_t,
    evict_inode_t,
    perf_t,
    wprotect_t,

    /* Rebuild */
    rebuild_title_t,
    rebuild_dir_t,
    rebuild_blks_t,
    rebuild_snapshot_t,

    /* Meta Operations */
    meta_title_t,
    sm_valid_t,
    sm_invalid_t,
    sm_delete_t,
    sm_update_t,
    delegate_data_valid_t,
    delegate_data_invalid_t,
    delegate_data_delete_t,
    delegate_data_update_t,
    process_data_info_t,
    process_new_inode_info_t,
    process_unlink_inode_info_t,
    process_delete_inode_info_t,
    process_close_inode_info_t,
    flush_cmt_t,

    /* Linix */
    linix_title_t,
    linix_set_t,
    linix_get_t,

    /* Sentinel */
    TIMING_NUM,
};

enum stats_category {
    alloc_steps,
    cow_write_breaks,
    inplace_write_breaks,
    read_bytes,
    cow_write_bytes,
    inplace_write_bytes,
    fast_checked_pages,
    thorough_checked_pages,
    fast_gc_pages,
    thorough_gc_pages,
    dirty_pages,
    protect_head,
    protect_tail,
    block_csum_parity,
    dax_cow_during_snapshot,
    mapping_updated_pages,
    cow_overlap_mmap,
    dax_new_blocks,
    inplace_new_blocks,
    fdatasync,

    /* Sentinel */
    STATS_NUM,
};

extern const char *Timingstring[TIMING_NUM];
extern u64 Timingstats[TIMING_NUM];
DECLARE_PER_CPU(u64[TIMING_NUM], Timingstats_percpu);
extern u64 Countstats[TIMING_NUM];
DECLARE_PER_CPU(u64[TIMING_NUM], Countstats_percpu);
extern u64 IOstats[STATS_NUM];
DECLARE_PER_CPU(u64[STATS_NUM], IOstats_percpu);

typedef struct timespec timing_t;

#define INIT_TIMING(X) timing_t X = {0}

#define HK_START_TIMING(name, start) \
    {                                \
        if (measure_timing) {        \
            MFENCE();                \
            getrawmonotonic(&start); \
        }                            \
    }

#define HK_END_TIMING(name, start)                                    \
    {                                                                 \
        if (measure_timing) {                                         \
            INIT_TIMING(end);                                         \
            MFENCE();                                                 \
            getrawmonotonic(&end);                                    \
            __this_cpu_add(Timingstats_percpu[name],                  \
                           (end.tv_sec - start.tv_sec) * 1000000000 + \
                               (end.tv_nsec - start.tv_nsec));        \
            __this_cpu_add(Countstats_percpu[name], 1);               \
        }                                                             \
    }

#define HK_STATS_ADD(name, value)                    \
    {                                                \
        __this_cpu_add(IOstats_percpu[name], value); \
    }

#endif /* _HK_STATS_H */
