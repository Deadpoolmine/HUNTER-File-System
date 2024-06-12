/*
 * BRIEF DESCRIPTION
 *
 * Proc fs operations
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
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "hunter.h"
#include "inode.h"

const char *proc_dirname = "fs/HUNTER";
struct proc_dir_entry *hk_proc_root;

// TODO: Modify these functions
/* ====================== Statistics ======================== */
static int hk_seq_timing_show(struct seq_file *seq, void *v)
{
	int i;

	hk_get_timing_stats();

	seq_puts(seq, "=========== HUNTER kernel timing stats ===========\n");
	for (i = 0; i < TIMING_NUM; i++) {
		/* Title */
		if (Timingstring[i][0] == '=') {
			seq_printf(seq, "\n%s\n\n", Timingstring[i]);
			continue;
		}

		if (measure_timing || Timingstats[i]) {
			seq_printf(seq, "%s: count %llu, timing %llu, average %llu\n",
				Timingstring[i],
				Countstats[i],
				Timingstats[i],
				Countstats[i] ?
				Timingstats[i] / Countstats[i] : 0);
		} else {
			seq_printf(seq, "%s: count %llu\n",
				Timingstring[i],
				Countstats[i]);
		}
	}

	seq_puts(seq, "\n");
	return 0;
}

static int hk_seq_timing_open(struct inode *inode, struct file *file)
{
	return single_open(file, hk_seq_timing_show, PDE_DATA(inode));
}

ssize_t hk_seq_clear_stats(struct file *filp, const char __user *buf,
	size_t len, loff_t *ppos)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct super_block *sb = PDE_DATA(inode);

	hk_clear_stats(sb);
	return len;
}

static const struct file_operations hk_seq_timing_fops = {
	.owner		= THIS_MODULE,
	.open		= hk_seq_timing_open,
	.read		= seq_read,
	.write		= hk_seq_clear_stats,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hk_seq_IO_show(struct seq_file *seq, void *v)
{
	struct super_block *sb = seq->private;
	struct hk_sb_info *sbi = HK_SB(sb);
	struct free_list *free_list;
	unsigned long alloc_log_count = 0;
	unsigned long alloc_log_pages = 0;
	unsigned long alloc_data_count = 0;
	unsigned long alloc_data_pages = 0;
	unsigned long free_log_count = 0;
	unsigned long freed_log_pages = 0;
	unsigned long free_data_count = 0;
	unsigned long freed_data_pages = 0;
	int i;

	hk_get_timing_stats();
	hk_get_IO_stats();

	seq_puts(seq, "============ HK allocation stats ============\n\n");

	seq_printf(seq, "alloc log count %lu, allocated log pages %lu\n"
		"alloc data count %lu, allocated data pages %lu\n"
		"free log count %lu, freed log pages %lu\n"
		"free data count %lu, freed data pages %lu\n",
		alloc_log_count, alloc_log_pages,
		alloc_data_count, alloc_data_pages,
		free_log_count, freed_log_pages,
		free_data_count, freed_data_pages);

	seq_puts(seq, "\n");

	seq_puts(seq, "================ HK I/O stats ================\n\n");
	seq_printf(seq, "Read %llu, bytes %llu, average %llu\n",
		Countstats[dax_read_t], IOstats[read_bytes],
		Countstats[dax_read_t] ?
			IOstats[read_bytes] / Countstats[dax_read_t] : 0);
	seq_printf(seq, "COW write %llu, bytes %llu, average %llu, write breaks %llu, average %llu\n",
		Countstats[do_cow_write_t], IOstats[cow_write_bytes],
		Countstats[do_cow_write_t] ?
			IOstats[cow_write_bytes] / Countstats[do_cow_write_t] : 0,
		IOstats[cow_write_breaks], Countstats[do_cow_write_t] ?
			IOstats[cow_write_breaks] / Countstats[do_cow_write_t]
			: 0);
	seq_printf(seq, "fsync %llu, fdatasync %llu\n",
			Countstats[fsync_t], IOstats[fdatasync]);

	seq_puts(seq, "\n");

	seq_puts(seq, "\n");

	return 0;
}

static int hk_seq_IO_open(struct inode *inode, struct file *file)
{
	return single_open(file, hk_seq_IO_show, PDE_DATA(inode));
}

static const struct file_operations hk_seq_IO_fops = {
	.owner		= THIS_MODULE,
	.open		= hk_seq_IO_open,
	.read		= seq_read,
	.write		= hk_seq_clear_stats,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* ====================== Setup/teardown======================== */
void hk_sysfs_init(struct super_block *sb)
{
	struct hk_sb_info *sbi = HK_SB(sb);

	if (hk_proc_root)
		sbi->s_proc = proc_mkdir(sbi->s_bdev->bd_disk->disk_name,
					 hk_proc_root);

	if (sbi->s_proc) {
		proc_create_data("timing_stats", 0444, sbi->s_proc,
				 &hk_seq_timing_fops, sb);
		proc_create_data("IO_stats", 0444, sbi->s_proc,
				 &hk_seq_IO_fops, sb);
	}
}

void hk_sysfs_exit(struct super_block *sb)
{
	struct hk_sb_info *sbi = HK_SB(sb);

	if (sbi->s_proc) {
		remove_proc_entry("timing_stats", sbi->s_proc);
		remove_proc_entry("IO_stats", sbi->s_proc);
		remove_proc_entry(sbi->s_bdev->bd_disk->disk_name,
						  hk_proc_root);
	}
}
