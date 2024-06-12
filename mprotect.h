/*
 * BRIEF DESCRIPTION
 *
 * Memory protection definitions for the HUNTER filesystem.
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

#ifndef _HK_WPROTECT_H
#define _HK_WPROTECT_H

#include "hunter.h"

extern void hk_error_mng(struct super_block *sb, const char *fmt, ...);

static inline int hk_range_check(struct super_block *sb, void *p,
					 			 unsigned long len)
{
	struct hk_sb_info *sbi = HK_SB(sb);

	if (p < sbi->virt_addr ||
			p + len > sbi->virt_addr + sbi->initsize) {
		hk_err(sb, "access pmem out of range: pmem range 0x%lx - 0x%lx, "
				"access range 0x%lx - 0x%lx\n",
				(unsigned long)sbi->virt_addr,
				(unsigned long)(sbi->virt_addr + sbi->initsize),
				(unsigned long)p, (unsigned long)(p + len));
		dump_stack();
		return -EINVAL;
	}

	return 0;
}

static inline void wprotect_disable(void)
{
	unsigned long cr0_val;

	cr0_val = read_cr0();
	cr0_val &= (~X86_CR0_WP);
	write_cr0(cr0_val);
}

static inline void wprotect_enable(void)
{
	unsigned long cr0_val;

	cr0_val = read_cr0();
	cr0_val |= X86_CR0_WP;
	write_cr0(cr0_val);
}

/* FIXME: Assumes that we are always called in the right order.
 * hk_writeable(vaddr, size, 1);
 * hk_writeable(vaddr, size, 0);
 */
static inline int
hk_writeable(void *vaddr, unsigned long size, int rw, unsigned long *flags)
{
	INIT_TIMING(wprotect_time);

	HK_START_TIMING(wprotect_t, wprotect_time);
	if (rw) {
		local_irq_save(*flags);
		wprotect_disable();
	} else {
		wprotect_enable();
		local_irq_restore(*flags);
	}
	HK_END_TIMING(wprotect_t, wprotect_time);
	return 0;
}

extern int wprotect;

static inline int hk_is_protected(struct super_block *sb)
{
	struct hk_sb_info *sbi = (struct hk_sb_info *)sb->s_fs_info;

	if (wprotect)
		return wprotect;

	return sbi->s_mount_opt & HUNTER_MOUNT_PROTECT;
}

static inline int hk_is_wprotected(struct super_block *sb)
{
	return hk_is_protected(sb);
}

static inline void
__hk_memunlock_range(void *p, unsigned long len, unsigned long *flags)
{
	/*
	 * NOTE: Ideally we should lock all the kernel to be memory safe
	 * and avoid to write in the protected memory,
	 * obviously it's not possible, so we only serialize
	 * the operations at fs level. We can't disable the interrupts
	 * because we could have a deadlock in this path.
	 */
	hk_writeable(p, len, 1, flags);
}

static inline void
__hk_memlock_range(void *p, unsigned long len, unsigned long *flags)
{
	hk_writeable(p, len, 0, flags);
}

static inline void hk_memunlock_range(struct super_block *sb, void *p,
					 				  unsigned long len, unsigned long *flags)
{
	if (hk_range_check(sb, p, len))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(p, len, flags);
}

static inline void hk_memlock_range(struct super_block *sb, void *p,
				       				unsigned long len, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(p, len, flags);
}

static inline void hk_memunlock_super(struct super_block *sb, unsigned long *flags)
{
	struct hk_super_block *ps = hk_get_super(sb);

	if (hk_is_protected(sb))
		__hk_memunlock_range(ps, HK_SB_SIZE, flags);
}

static inline void hk_memlock_super(struct super_block *sb, unsigned long *flags)
{
	struct hk_super_block *ps = hk_get_super(sb);

	if (hk_is_protected(sb))
		__hk_memlock_range(ps, HK_SB_SIZE, flags);
}

static inline void hk_memunlock_hdr(struct super_block *sb, 
									struct hk_header *hdr, unsigned long *flags)
{
	if (hk_range_check(sb, hdr, sizeof(struct hk_header)))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(hdr, sizeof(struct hk_header), flags);
}

static inline void hk_memlock_hdr(struct super_block *sb,
				       			  struct hk_header *hdr, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(hdr, sizeof(struct hk_header), flags);
}

static inline void hk_memunlock_attr_log(struct super_block *sb, 
									struct hk_attr_log *rg, unsigned long *flags)
{
	if (hk_range_check(sb, rg, sizeof(struct hk_attr_log)))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(rg, sizeof(struct hk_attr_log), flags);
}

static inline void hk_memlock_attr_log(struct super_block *sb,
				       			  struct hk_attr_log *rg, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(rg, sizeof(struct hk_attr_log), flags);
}

static inline void hk_memunlock_journal(struct super_block *sb, 
									struct hk_journal *jnl, unsigned long *flags)
{
	if (hk_range_check(sb, jnl, sizeof(struct hk_journal)))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(jnl, sizeof(struct hk_journal), flags);
}

static inline void hk_memlock_journal(struct super_block *sb,
				       			  struct hk_journal *jnl, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(jnl, sizeof(struct hk_journal), flags);
}


static inline void hk_memunlock_dentry(struct super_block *sb, 
									   struct hk_dentry *direntry, unsigned long *flags)
{
	if (hk_range_check(sb, direntry, sizeof(struct hk_dentry)))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(direntry, sizeof(struct hk_dentry), flags);
}

static inline void hk_memlock_dentry(struct super_block *sb,
				       			     struct hk_dentry *direntry, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(direntry, sizeof(struct hk_dentry), flags);
}

static inline void hk_memunlock_all(struct super_block *sb, unsigned long *flags)
{
	struct hk_sb_info *sbi = (struct hk_sb_info *)sb->s_fs_info;
	if (hk_is_protected(sb))
		__hk_memunlock_range(sbi->virt_addr, sbi->initsize, flags);
}

static inline void hk_memlock_all(struct super_block *sb, unsigned long *flags)
{
	struct hk_sb_info *sbi = (struct hk_sb_info *)sb->s_fs_info;
	if (hk_is_protected(sb))
		__hk_memlock_range(sbi->virt_addr, sbi->initsize, flags);
}

static inline void hk_memunlock_pi(struct super_block *sb, 
									  struct hk_inode *pi, unsigned long *flags)
{
	if (hk_range_check(sb, pi, sizeof(struct hk_inode)))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(pi, sizeof(struct hk_inode), flags);
}

static inline void hk_memlock_pi(struct super_block *sb,
				       struct hk_inode *pi, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(pi, sizeof(struct hk_inode), flags);
}

static inline void hk_memunlock_block(struct super_block *sb, void *bp, unsigned long *flags)
{
	if (hk_range_check(sb, bp, sb->s_blocksize))
		return;

	if (hk_is_protected(sb))
		__hk_memunlock_range(bp, sb->s_blocksize, flags);
}

static inline void hk_memlock_block(struct super_block *sb, void *bp, unsigned long *flags)
{
	if (hk_is_protected(sb))
		__hk_memlock_range(bp, sb->s_blocksize, flags);
}


#endif /* _HK_WPROTECT_H */
