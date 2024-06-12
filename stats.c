/*
 * HUNTER File System statistics
 *
 * Copyright 2023-2024 Regents of the University of Harbin Institute of Technology, Shenzhen
 * Computer science and technology, Yanqi Pan <deadpoolmine@qq.com>
 * Copyright 2015-2016 Regents of the University of California,
 * UCSD Non-Volatile Systems Lab, Andiry Xu <jix024@cs.ucsd.edu>
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

const char *Timingstring[TIMING_NUM] = {
    /* Init */
    "================ Initialization ================",
    "init",
    "mount",
    "ioremap",
    "new_init",
    "recovery",

    /* Namei operations */
    "============= Directory operations =============",
    "create",
    "lookup",
    "link",
    "unlink",
    "symlink",
    "mkdir",
    "rmdir",
    "mknod",
    "rename",
    "readdir",
    "add_dentry",
    "remove_dentry",
    "setattr",
    "setsize",

    /* I/O operations */
    "================ I/O operations ================",
    "dax_read",
    "do_cow_write",
    "cow_write",
    "inplace_write",
    "copy_to_nvmm",
    "dax_get_block",
    "read_iter",
    "write_iter",
    "wrap_iter",
    "write",

    /* Memory operations */
    "============== Memory operations ===============",
    "memcpy_read_nvmm",
    "memcpy_write_nvmm",
    "handle_partial_block",

    /* Memory management */
    "============== Memory management ===============",
    "alloc_blocks",
    "free_blocks",

    /* Others */
    "================ Miscellaneous =================",
    "find_cache_page",
    "fsync",
    "write_pages",
    "fallocate",
    "direct_IO",
    "free_old_entry",
    "delete_file_tree",
    "delete_dir_tree",
    "new_vfs_inode",
    "new_hk_inode",
    "free_inode",
    "free_inode_log",
    "evict_inode",
    "test_perf",
    "wprotect",

    /* Rebuild */
    "=================== Rebuild ====================",
    "rebuild_dir",
    "rebuild_file",
    "rebuild_snapshot_table",

    /* Meta Operations */
    "=================== Meta ===================",
    "valid_summary_header",
    "invalid_summary_header",
    "delete_summary_header",
    "update_summary_header",
    "delegate_data_valid",
    "delegate_data_invalid",
    "delegate_data_delete",
    "delegate_data_update",
    "process_data_info",
    "process_new_inode_info",
    "process_unlink_inode_info",
    "process_delete_inode_info",
    "process_close_inode_info",
    "flush_cmt",

    /* Linear index */
    "=================== LinIX ===================",
    "linix_set",
    "linix_get",
};

u64 Timingstats[TIMING_NUM];
DEFINE_PER_CPU(u64[TIMING_NUM], Timingstats_percpu);
u64 Countstats[TIMING_NUM];
DEFINE_PER_CPU(u64[TIMING_NUM], Countstats_percpu);
u64 IOstats[STATS_NUM];
DEFINE_PER_CPU(u64[STATS_NUM], IOstats_percpu);

void hk_get_timing_stats(void)
{
    int i;
    int cpu;

    for (i = 0; i < TIMING_NUM; i++) {
        Timingstats[i] = 0;
        Countstats[i] = 0;
        for_each_possible_cpu(cpu)
        {
            Timingstats[i] += per_cpu(Timingstats_percpu[i], cpu);
            Countstats[i] += per_cpu(Countstats_percpu[i], cpu);
        }
    }
}

void hk_get_IO_stats(void)
{
    int i;
    int cpu;

    for (i = 0; i < STATS_NUM; i++) {
        IOstats[i] = 0;
        for_each_possible_cpu(cpu)
            IOstats[i] += per_cpu(IOstats_percpu[i], cpu);
    }
}

static void hk_clear_timing_stats(void)
{
    int i;
    int cpu;

    for (i = 0; i < TIMING_NUM; i++) {
        Countstats[i] = 0;
        Timingstats[i] = 0;
        for_each_possible_cpu(cpu)
        {
            per_cpu(Timingstats_percpu[i], cpu) = 0;
            per_cpu(Countstats_percpu[i], cpu) = 0;
        }
    }
}

static void hk_clear_IO_stats(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct free_list *free_list;
    int i;
    int cpu;

    for (i = 0; i < STATS_NUM; i++) {
        IOstats[i] = 0;
        for_each_possible_cpu(cpu)
            per_cpu(IOstats_percpu[i], cpu) = 0;
    }
}

void hk_clear_stats(struct super_block *sb)
{
    hk_clear_timing_stats();
    hk_clear_IO_stats(sb);
}

void hk_print_timing(void)
{
    int i;

    hk_get_timing_stats();

    printk("=========== HUNTER kernel timing stats ===========\n");
    for (i = 0; i < TIMING_NUM; i++) {
        /* Title */
        if (Timingstring[i][0] == '=') {
            printk("\n%s\n\n", Timingstring[i]);
            continue;
        }

        if (measure_timing || Timingstats[i]) {
            printk("%s: count %llu, timing %llu, average %llu\n",
                   Timingstring[i],
                   Countstats[i],
                   Timingstats[i],
                   Countstats[i] ? Timingstats[i] / Countstats[i] : 0);
        } else {
            printk("%s: count %llu\n",
                   Timingstring[i],
                   Countstats[i]);
        }
    }

    printk("\n");
}