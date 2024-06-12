#ifndef _HK_H
#define _HK_H

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/dax.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/radix-tree.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/buffer_head.h>
#include <linux/uio.h>
#include <linux/iomap.h>
#include <linux/crc32c.h>
#include <asm/tlbflush.h>
#include <linux/version.h>
#include <linux/pfn_t.h>
#include <linux/pagevec.h>
#include <linux/uaccess.h>
#include <linux/falloc.h>
#include <asm/mman.h>
#include <linux/compat.h>
#include <linux/hashtable.h>
#include <linux/sched/signal.h>

#define TRANS_ADDR_TO_OFS(sbi, addr)  (addr == 0 ? 0 : ((u64)(addr) - (u64)(sbi)->virt_addr))   
#define TRANS_OFS_TO_ADDR(sbi, ofs)   (ofs == 0 ? 0 : ((u64)(ofs) + (sbi)->virt_addr))

#define HK_VALID_UMOUNT     0xffffffff
#define HK_INVALID_UMOUNT   0x00000000

/*
 * hunter inode flags
 *
 * HK_EOFBLOCKS_FL			 There are blocks allocated beyond eof
 */
#define HK_EOFBLOCKS_FL      0x20000000

/* Flags that should be inherited by new inodes from their parent. */
#define HK_FL_INHERITED (FS_SECRM_FL | FS_UNRM_FL | FS_COMPR_FL | \
						FS_SYNC_FL | FS_NODUMP_FL | FS_NOATIME_FL |	\
						FS_COMPRBLK_FL | FS_NOCOMP_FL | \
						FS_JOURNAL_DATA_FL | FS_NOTAIL_FL | FS_DIRSYNC_FL)
/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define HK_REG_FLMASK 	(~(FS_DIRSYNC_FL | FS_TOPDIR_FL))
/* Flags that are appropriate for non-directories/regular files. */
#define HK_OTHER_FLMASK (FS_NODUMP_FL | FS_NOATIME_FL)

#define S_IFPSEUDO		 	0xFFFF
#define S_ISPSEUDO(mode) 	(mode == S_IFPSEUDO)
/*
 * Debug code
 */
#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

/* #define hk_dbg(s, args...)		pr_debug(s, ## args) */
#define hk_dbg(s, args ...)		    pr_info("cpu-%d, %s: " s, smp_processor_id(), __func__, ## args)
#define hk_dbg1(s, args ...)
#define hk_err(sb, s, args ...)	    hk_error_mng(sb, s, ## args)
#define hk_warn(s, args ...)		pr_warn("cpu-%d, %s: " s, smp_processor_id(), __func__, ## args)
#define hk_info(s, args ...)		pr_info("cpu-%d, %s: " s, smp_processor_id(), __func__, ## args)

extern unsigned int hk_dbgmask;
#define HK_DBGMASK_MMAPHUGE	        (0x00000001)
#define HK_DBGMASK_MMAP4K	        (0x00000002)
#define HK_DBGMASK_MMAPVERBOSE      (0x00000004)
#define HK_DBGMASK_MMAPVVERBOSE     (0x00000008)
#define HK_DBGMASK_VERBOSE	        (0x00000010)
#define HK_DBGMASK_TRANSACTION      (0x00000020)

#define hk_dbg_mmap4k(s, args ...)		 \
	((hk_dbgmask & HK_DBGMASK_MMAP4K) ? hk_dbg(s, args) : 0)
#define hk_dbg_mmapv(s, args ...)		 \
	((hk_dbgmask & HK_DBGMASK_MMAPVERBOSE) ? hk_dbg(s, args) : 0)
#define hk_dbg_mmapvv(s, args ...)		 \
	((hk_dbgmask & HK_DBGMASK_MMAPVVERBOSE) ? hk_dbg(s, args) : 0)
#define hk_dbg_verbose(s, args ...)		 \
	((hk_dbgmask & HK_DBGMASK_VERBOSE) ? hk_dbg(s, ##args) : 0)
#define hk_dbgv(s, args ...)	         hk_dbg_verbose(s, ##args)
#define hk_dbg_trans(s, args ...)		 \
	((hk_dbgmask & HK_DBGMASK_TRANSACTION) ? hk_dbg(s, ##args) : 0)

#define HK_ASSERT(x) do {\
			       if (!(x))\
				       hk_warn("assertion failed %s:%d: %s\n", \
			       __FILE__, __LINE__, #x);\
		       } while (0)

#define hk_set_bit		           __test_and_set_bit_le
#define hk_clear_bit		       __test_and_clear_bit_le
#define hk_find_next_zero_bit	   find_next_zero_bit_le

#define clear_opt(o, opt)	       (o &= ~HUNTER_MOUNT_ ## opt)
#define set_opt(o, opt)		       (o |= HUNTER_MOUNT_ ## opt)
#define test_opt(sb, opt)	       (HK_SB(sb)->s_mount_opt & HUNTER_MOUNT_ ## opt)


#define	READDIR_END				   (ULONG_MAX)
#define	ANY_CPU					   (65536)

/* ======================= ANCHOR: Global values ========================= */
extern int measure_timing;
extern int wprotect;

/* ======================= ANCHOR: global struct ========================= */
/* A node in the linked list representing a range of pages */
/* Closed range [range_low, range_high] */
struct hk_range_node {
	struct rb_node rbnode;
	/* Block, inode */
	struct {
		unsigned long range_low;
		unsigned long range_high;
	};
};

/* ======================= ANCHOR: HUNTER Includes ========================= */
#include "infqueue.h"
#include "chash.h"
#include "ext_hashtable.h"
#include "stats.h"
#include "config.h"
#include "dw.h"
#include "namei.h"
#include "linix.h"
#include "bbuild.h"
#include "super.h"
#include "meta.h"
#include "inode.h"
#include "cmt.h"
#include "generic_cachep.h"
#include "config.h"
#include "balloc.h"
#include "mprotect.h"

/* blk_addr is the offset addr in NVMM */
static inline void *hk_get_block(struct super_block *sb, u64 blk_addr)
{
	struct hk_super_block *ps = hk_get_super(sb);

	return blk_addr ? ((void *)ps + blk_addr) : NULL;
}

/* get in nvmm reference */
static inline int hk_get_reference(struct super_block *sb, u64 block,
	void *dram, void **nvmm, size_t size)
{
	int rc;

	*nvmm = hk_get_block(sb, block);
	rc = memcpy_mcsafe(dram, *nvmm, size);
	return rc;
}

static inline u64 hk_get_addr_off(struct hk_sb_info *sbi, void *addr)
{
	HK_ASSERT((addr >= sbi->virt_addr) &&
			  (addr < (sbi->virt_addr + sbi->initsize)));
	return (u64)(addr - sbi->virt_addr);
}

static inline u64 hk_get_dblk_by_addr(struct hk_sb_info *sbi, void *d_addr)
{
	struct super_block *sb = sbi->sb;
	HK_ASSERT(d_addr >= sbi->d_addr && 
			  d_addr < (sbi->d_addr + sbi->d_size));
	return (u64)(d_addr - sbi->d_addr) / HK_PBLK_SZ;
}

static inline u64 hk_get_addr_by_dblk(struct hk_sb_info *sbi, u64 d_blk)
{
	struct super_block *sb = sbi->sb;
	return (u64)(sbi->d_addr + (d_blk * HK_PBLK_SZ));
}

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __le32 hk_mask_flags(umode_t mode, __le32 flags)
{
	flags &= cpu_to_le32(HK_FL_INHERITED);
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & cpu_to_le32(HK_REG_FLMASK);
	else
		return flags & cpu_to_le32(HK_OTHER_FLMASK);
}

/* ======================= ANCHOR: pmem specific function ========================= */
static inline int memcpy_to_pmem_nocache(void *dst, const void *src,
	unsigned int size)
{
	int ret;

	ret = __copy_from_user_inatomic_nocache(dst, src, size);

	return ret;
}

/* assumes the length to be 4-byte aligned */
static inline void memset_nt(void *dest, uint32_t dword, size_t length)
{
	uint64_t dummy1, dummy2;
	uint64_t qword = ((uint64_t)dword << 32) | dword;
	
	BUG_ON(length > ((u64)1 << 32));
	
	asm volatile ("movl %%edx,%%ecx\n"
		"andl $63,%%edx\n"
		"shrl $6,%%ecx\n"
		"jz 9f\n"
		"1:	 movnti %%rax,(%%rdi)\n"
		"2:	 movnti %%rax,1*8(%%rdi)\n"
		"3:	 movnti %%rax,2*8(%%rdi)\n"
		"4:	 movnti %%rax,3*8(%%rdi)\n"
		"5:	 movnti %%rax,4*8(%%rdi)\n"
		"8:	 movnti %%rax,5*8(%%rdi)\n"
		"7:	 movnti %%rax,6*8(%%rdi)\n"
		"8:	 movnti %%rax,7*8(%%rdi)\n"
		"leaq 64(%%rdi),%%rdi\n"
		"decl %%ecx\n"
		"jnz 1b\n"
		"9:	movl %%edx,%%ecx\n"
		"andl $7,%%edx\n"
		"shrl $3,%%ecx\n"
		"jz 11f\n"
		"10:	 movnti %%rax,(%%rdi)\n"
		"leaq 8(%%rdi),%%rdi\n"
		"decl %%ecx\n"
		"jnz 10b\n"
		"11:	 movl %%edx,%%ecx\n"
		"shrl $2,%%ecx\n"
		"jz 12f\n"
		"movnti %%eax,(%%rdi)\n"
		"12:\n"
		: "=D"(dummy1), "=d" (dummy2)
		: "D" (dest), "a" (qword), "d" (length)
		: "memory", "rcx");
}

static inline void memset_nt_large(void *dest, uint32_t dword, size_t length)
{
	uint64_t qword = ((uint64_t)dword << 32) | dword;
	BUG_ON(length % 4 != 0);
	for (; length >= 64; length -= 64, dest = (char *)dest + 64) {
		memcpy_flushcache((uint64_t *)dest, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 1, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 2, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 3, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 4, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 5, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 6, &qword, sizeof(uint64_t));
		memcpy_flushcache((uint64_t *)dest + 7, &qword, sizeof(uint64_t));
	}
	for (; length >= 8; length -= 8, dest = (char *)dest + 8)
		memcpy_flushcache((uint64_t *)dest, &qword, sizeof(uint64_t));
	if (length == 4)
		memcpy_flushcache((uint32_t *)dest, &dword, sizeof(uint32_t));
}

static inline u64 _round_down(u64 value, u64 align)
{
	BUG_ON(!align);
	if (align & (align - 1) == 0) {
		return round_down(value, align);
	}
	else {
		return (value / align) * align;
	}
}

static inline u64 _round_up(u64 value, u64 align)
{
	BUG_ON(!align);
	if (align & (align - 1) == 0) {
		return round_up(value, align);
	}
	else {
		return (value / align + 1) * align;
	}
}

/* ======================= ANCHOR: mlist.c ========================= */
int hk_range_insert_range(struct rb_root_cached *tree, unsigned long range_low, unsigned long range_high);
int hk_range_delete_range_node(struct rb_root_cached *tree, struct hk_range_node *node);
unsigned long hk_range_pop(struct rb_root_cached *tree, unsigned long *num);
void hk_range_free_all(struct rb_root_cached *tree);

/* ======================= ANCHOR: rebuild.c ========================= */
void hk_init_header(struct super_block *sb, struct hk_inode_info_header *sih, 
                    u16 i_mode);

/* ======================= ANCHOR: bbuild.c ========================= */
int hk_recovery(struct super_block *sb);
int hk_save_layouts(struct super_block *sb);
int hk_save_regions(struct super_block *sb);

/* ======================= ANCHOR: balloc.c ========================= */
u64 get_version(struct hk_sb_info *sbi);
int ind_update(struct hk_indicator *ind, enum hk_ind_upt_type type, u64 blks);
int hk_layouts_init(struct hk_sb_info *sbi, int cpus);
int hk_layouts_free(struct hk_sb_info *sbi);
unsigned long hk_count_free_blocks(struct super_block *sb);
u64 hk_prepare_layout(struct super_block* sb, int cpuid, u64 blks, enum hk_layout_type type, 
                      u64* blks_prepared, bool zero);
int hk_prepare_layouts(struct super_block *sb, u32 blks, bool zero, struct hk_layout_preps *preps);
void hk_prepare_gap(struct super_block *sb, bool zero, struct hk_layout_prep *prep);
void hk_trv_prepared_layouts_init(struct hk_layout_preps* preps);
struct hk_layout_prep* hk_trv_prepared_layouts(struct super_block *sb, 
											   struct hk_layout_preps* preps);
int hk_release_layout(struct super_block *sb, int cpuid, u64 blks, bool rls_all);

/* ======================= ANCHOR: file.c ========================= */
extern const struct inode_operations hk_file_inode_operations;
extern const struct file_operations hk_dax_file_operations;

/* ======================= ANCHOR: dir.c ========================= */
extern const struct file_operations hk_dir_operations;

/* ======================= ANCHOR: symlink.c ========================= */
extern const struct inode_operations hk_symlink_inode_operations;
int hk_block_symlink(struct super_block *sb, struct hk_inode *pi,
					 struct inode *inode, const char *symname, int len,
					 void *out_blk_addr);

/* ======================= ANCHOR: ioctl.c ========================= */
long hk_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long hk_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* ======================= ANCHOR: inode.c ========================= */
extern const struct address_space_operations hk_aops_dax;
int hk_init_free_inode_list(struct super_block *sb, bool is_init);
int hk_init_free_inode_list_percore(struct super_block *sb, int cpuid, bool is_init);
u64 hk_alloc_ino(struct super_block *sb);
struct inode *hk_iget_opened(struct super_block *sb, unsigned long ino);
struct inode *hk_iget(struct super_block *sb, unsigned long ino);
struct inode *hk_create_inode(enum hk_new_inode_type type, struct inode *dir, 
							  u64 ino, umode_t mode, size_t size, dev_t rdev, 
							  const struct qstr *qstr);
void hk_init_pi(struct super_block *sb, struct inode *inode, umode_t mode, u32 i_flags);
int hk_getattr(const struct path *path, struct kstat *stat,
		 	   u32 request_mask, unsigned int query_flags);
int hk_notify_change(struct dentry *dentry, struct iattr *attr);
int hk_write_inode(struct inode *inode, struct writeback_control *wbc);
void hk_evict_inode(struct inode *inode);
int hk_free_data_blks(struct super_block *sb, struct hk_inode_info_header *sih);

/* ======================= ANCHOR: namei.c ========================= */
extern const struct inode_operations hk_dir_inode_operations;
extern const struct inode_operations hk_special_inode_operations;
struct hk_dentry *hk_dentry_by_ix_from_blk(u64 blk_addr, u16 ix);
struct dentry *hk_get_parent(struct dentry *child);
int hk_insert_dir_table(struct super_block *sb, struct hk_inode_info_header *sih, const char *name, 
				  	    int namelen, struct hk_dentry *direntry);
int hk_update_dir_table(struct super_block *sb, struct hk_inode_info_header *sih, const char *name, 
				  		int namelen, struct hk_dentry *direntry);
void hk_remove_dir_table(struct super_block *sb, struct hk_inode_info_header *sih, 
						 const char *name, int namelen);
void hk_destory_dir_table(struct super_block *sb, struct hk_inode_info_header *sih);

/* ======================= ANCHOR: meta.c ========================= */
int hk_format_meta(struct super_block *sb);
struct hk_attr_log *hk_get_attr_log_by_alid(struct super_block *sb, int alid);
struct hk_attr_log *hk_get_attr_log_by_ino(struct super_block *sb, u64 ino);
void hk_create_al_snapshot(struct super_block *sb, struct hk_inode *pi);
int hk_reset_attr_log(struct super_block *sb, struct hk_attr_log *al);
int hk_evicting_attr_log(struct super_block *sb, struct hk_attr_log *al);
int hk_evicting_attr_log_to_inode(struct super_block *sb, struct hk_inode *pi);

int hk_commit_icp(struct super_block *sb, struct hk_cmt_icp *icp);
int hk_commit_icp_attrchange(struct super_block *sb, struct hk_cmt_icp *icp);
int hk_commit_icp_linkchange(struct super_block *sb, struct hk_cmt_icp *icp);

int hk_commit_attrchange(struct super_block *sb, struct inode *inode);
int hk_commit_linkchange(struct super_block *sb, struct inode *inode);
int hk_commit_sizechange(struct super_block *sb, struct inode *inode, loff_t ia_size);

u64 sm_get_addr_by_hdr(struct super_block *sb, u64 hdr);
struct hk_header *sm_get_hdr_by_addr(struct super_block *sb, u64 addr);
struct hk_layout_info *sm_get_layout_by_hdr(struct super_block *sb, u64 hdr);

int sm_remove_hdr(struct super_block *sb, struct hk_header *prev_hdr, struct hk_header *hdr);
int sm_insert_hdr(struct super_block *sb, struct hk_header *prev_hdr, struct hk_header *hdr, struct hk_header *next_hdr);
u64 sm_get_next_addr_by_dbatch(struct super_block *sb, struct hk_inode_info_header *sih, struct hk_cmt_dbatch *batch);
u64 sm_get_prev_addr_by_dbatch(struct super_block *sb, struct hk_inode_info_header *sih, struct hk_cmt_dbatch *batch);

int sm_delete_data_sync(struct super_block *sb, u64 blk_addr);
int sm_invalid_data_sync(struct super_block *sb, u64 prev_addr, u64 blk_addr, u64 ino);
int sm_valid_data_sync(struct super_block *sb, u64 prev_addr, u64 blk_addr, u64 next_addr,
                       u64 ino, u64 f_blk, u64 tstamp, u64 size, u32 cmtime);
int sm_update_data_sync(struct super_block *sb, u64 blk_addr, u64 size);

struct hk_journal* hk_get_journal_by_txid(struct super_block *sb, int txid);
struct hk_jentry* hk_get_jentry_by_slotid(struct super_block *sb, int txid, int slotid);
int hk_start_tx(struct super_block *sb, enum hk_journal_type jtype, ...);
int hk_finish_tx(struct super_block *sb, int txid);

/* ======================= ANCHOR: cmt.c ========================= */
#ifdef CONFIG_CMT_BACKGROUND

struct hk_cmt_node *hk_cmt_node_init(struct super_block *sb, u64 ino);
void hk_cmt_node_destroy(struct hk_cmt_node *node);
int hk_cmt_manage_node(struct super_block *sb, struct hk_cmt_node *cmt_node, struct hk_cmt_node **exist);
struct hk_cmt_node *hk_cmt_search_node(struct super_block *sb, u64 ino);
int hk_cmt_unmanage_node(struct super_block *sb, struct hk_cmt_node *cmt_node);

int hk_delegate_create_async(struct super_block *sb, struct inode *inode, struct inode *dir, struct hk_dentry *direntry);
int hk_delegate_unlink_async(struct super_block *sb, struct inode *inode, struct inode *dir, struct hk_dentry *direntry, bool invalidate);
int hk_delegate_data_async(struct super_block *sb, struct inode *inode, struct hk_cmt_dbatch *batch, u64 size, enum hk_cmt_info_type type);
int hk_delegate_close_async(struct super_block *sb, struct inode *inode);
int hk_delegate_delete_async(struct super_block *sb, struct inode *inode);

struct hk_cmt_queue *hk_init_cmt_queue(int num_workers);
void hk_free_cmt_queue(struct hk_cmt_queue *cq);
void hk_start_cmt_workers(struct super_block *sb);
void hk_stop_cmt_workers(struct super_block *sb);
void hk_flush_cmt_node_fast(struct super_block *sb, struct hk_cmt_node *cmt_node);
void hk_flush_cmt_queue(struct super_block *sb, int num_cpus);
void hk_cmt_destory_forest(struct super_block *sb);
#endif

/* ======================= ANCHOR: rebuild.c ========================= */
int hk_rebuild_inode(struct super_block *sb, struct hk_inode_info *si, u64 ino, bool build_blks);

/* ======================= ANCHOR: linix.c ========================= */
int linix_init(struct hk_sb_info *sbi, struct linix *ix, u64 num_slots);
int linix_destroy(struct linix *ix);
int linix_extend(struct linix *ix);
u64 linix_get(struct linix *ix, u64 index);
int linix_insert(struct linix *ix, u64 index, u64 blk_addr, bool extend);
int linix_delete(struct linix *ix, u64 index, u64 last_index, bool shrink);

/* ======================= ANCHOR: gc.c ========================= */
int hk_friendly_gc(struct super_block *sb);
int hk_start_equalizer(struct super_block *sb);
int hk_terminal_equalizer(struct super_block *sb);

/* ======================= ANCHOR: stats.c ========================= */
void hk_get_timing_stats(void);
void hk_get_IO_stats(void);
void hk_clear_stats(struct super_block *sb);
void hk_print_timing(void);

/* ======================= ANCHOR: sysfs.c ========================= */
extern const char *proc_dirname;
extern struct proc_dir_entry *hk_proc_root;
void hk_sysfs_init(struct super_block *sb);
void hk_sysfs_exit(struct super_block *sb);


/* ======================= ANCHOR: Static Utils ========================= */
static inline int hk_get_cpuid(struct super_block *sb)
{
	struct hk_sb_info *sbi = HK_SB(sb);

	return smp_processor_id() % sbi->cpus;
}

// BKDR String Hash Function
static inline unsigned long BKDRHash(const char *str, int length)
{
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned long hash = 0;
	int i;

	for (i = 0; i < length; i++)
		hash = hash * seed + (*str++);

	return hash;
}

static inline void use_layout(struct hk_layout_info* layout)
{
	mutex_lock(&layout->layout_lock);
}

static inline void unuse_layout(struct hk_layout_info* layout)
{
	mutex_unlock(&layout->layout_lock);
}

static inline void use_layout_id(struct super_block *sb, u64 id)
{
	struct hk_sb_info *sbi = HK_SB(sb);
	use_layout(&sbi->layouts[id]);
}

static inline void unuse_layout_id(struct super_block *sb, u64 id)
{
	struct hk_sb_info *sbi = HK_SB(sb);
	unuse_layout(&sbi->layouts[id]);
}

static inline void use_layout_for_addr(struct super_block *sb, u64 addr)
{
	int cpuid;
	struct hk_sb_info *sbi = HK_SB(sb);
    u64 size_per_layout = _round_down(sbi->d_size / sbi->num_layout, HK_PBLK_SZ);
    cpuid = (addr - sbi->d_addr) / size_per_layout;
	cpuid = cpuid >= sbi->num_layout ? cpuid - 1 : cpuid;
    use_layout_id(sb, cpuid);
}

static inline void unuse_layout_for_addr(struct super_block *sb, u64 addr)
{
	int cpuid;
	struct hk_sb_info *sbi = HK_SB(sb);
    u64 size_per_layout = _round_down(sbi->d_size / sbi->num_layout, HK_PBLK_SZ);
    cpuid = (addr - sbi->d_addr) / size_per_layout;
	cpuid = cpuid >= sbi->num_layout ? cpuid - 1 : cpuid;
    unuse_layout_id(sb, cpuid);
}

static inline void use_layout_for_hdr(struct super_block *sb, u64 hdr)
{
	struct hk_layout_info *layout;
	layout = sm_get_layout_by_hdr(sb, hdr);
	use_layout(layout);
}

static inline void unuse_layout_for_hdr(struct super_block *sb, u64 hdr)
{
	struct hk_layout_info *layout;
	layout = sm_get_layout_by_hdr(sb, hdr);
	unuse_layout(layout);
}

static inline void use_journal(struct super_block *sb, int txid)
{
	struct hk_sb_info *sbi = HK_SB(sb);
	mutex_lock(&sbi->j_locks[txid]);
}

static inline void unuse_journal(struct super_block *sb, int txid)
{
	struct hk_sb_info *sbi = HK_SB(sb);
	mutex_unlock(&sbi->j_locks[txid]);
}

static inline void hk_sync_super(struct super_block *sb)
{
	struct hk_sb_info 	  *sbi = HK_SB(sb);
	struct hk_super_block *super = hk_get_super(sb);
	struct hk_super_block *super_redund;	// TODO:
	unsigned long 		  irq_flags = 0;

	hk_memunlock_super(sb, &irq_flags);

	memcpy_to_pmem_nocache((void *)super, (void *)sbi->hk_sb,
							sizeof(struct hk_super_block));
	PERSISTENT_BARRIER();

	hk_memlock_super(sb, &irq_flags);
}

/* Update checksum for the DRAM copy */
static inline void hk_update_super_crc(struct super_block *sb)
{
	struct hk_sb_info *sbi = HK_SB(sb);
	u32 			  crc = 0;

	sbi->hk_sb->s_wtime = cpu_to_le32(get_seconds());
	sbi->hk_sb->s_sum = 0;
	crc = hk_crc32c(~0, (__u8 *)sbi->hk_sb + sizeof(__le32),
			sizeof(struct hk_super_block) - sizeof(__le32));
	sbi->hk_sb->s_sum = cpu_to_le32(crc);
}

static inline void ssleep_interruptible(unsigned int seconds)
{
	if (seconds > 0) {
		msleep_interruptible(seconds * 1000);
	}
}

#endif /* _HK_H */