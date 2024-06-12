#ifndef _HK_INODE_H
#define _HK_INODE_H

#include "hunter.h"

struct hk_inode;
struct hk_inode_info_header;

enum hk_new_inode_type {
    TYPE_CREATE = 0,
    TYPE_MKNOD,
    TYPE_SYMLINK,
    TYPE_MKDIR
};

/*
 * Structure of an inode in PMEM
 */
struct hk_inode {
    struct hk_header_node root; /* Data Header for this inode */
    u8 valid;                   /* Is this inode valid? */
    __le32 i_flags;             /* Inode flags */
    __le64 i_size;              /* Size of data in bytes */
    __le32 i_ctime;             /* Inode modification time */
    __le32 i_mtime;             /* Inode Linear Index Modification time */
    __le32 i_atime;             /* Access time */
    __le16 i_mode;              /* File mode */
    __le16 i_links_count;       /* Links count */

    __le64 i_xattr; /* Extended attribute block */

    /* second 40 bytes */
    __le32 i_uid;         /* Owner Uid */
    __le32 i_gid;         /* Group Id */
    __le32 i_generation;  /* File version (for NFS) */
    __le32 i_create_time; /* Create time */
    __le64 ino;           /* hk inode number */
    __le64 tstamp;        /* Time stamp */

    struct {
        __le32 rdev; /* major/minor # */
    } dev;           /* device inode */
    
    __le64 tx_attr_entry; /* Used attr entry slot for transcation */
    __le64 tx_link_change_entry; /* Used linkchanged entry slot for transcation */

    //! We don't need this for now
    __le32 csum; /* CRC32 checksum */
    u8 padding[27]; /* Padding to 128 bytes */
} __attribute((__packed__));

static_assert(sizeof(struct hk_inode) == 128, "hk_inode size mismatch");

/*
 * hk-specific inode icp kept in DRAM
 */
struct hk_inode_info_header {
    struct linix ix; /* Linear Index for blks in use */

    struct hk_cmt_node *cmt_node; /* Commit node for this inode */

    struct hlist_head *dirs; /* Hash table for dirs */
    u64 i_num_dentrys;       /* Dentrys tail */

    unsigned short i_mode; /* Dir or file? */
    unsigned int i_flags;
    unsigned long i_size;
    unsigned long i_blocks;
    unsigned long ino;

    u64 pi_addr;          /* Exact hk_inode addr */
    u64 last_setattr;     /* Last setattr entry */
    u64 last_link_change; /* Last link change entry */
    u64 last_dentry;      /* Last updated dentry */

    u64 tstamp; /* Time stamp for Version Control */
};

// TODO: This could be jentry
/* For rebuild purpose, temporarily store pi infomation */
struct hk_inode_rebuild {
    u64 i_size;
    u32 i_flags;       /* Inode flags */
    u32 i_ctime;       /* Inode modification time */
    u32 i_mtime;       /* Inode b-tree Modification time */
    u32 i_atime;       /* Access time */
    u32 i_uid;         /* Owner Uid */
    u32 i_gid;         /* Group Id */
    u32 i_generation;  /* File version (for NFS) */
    u16 i_links_count; /* Links count */
    u16 i_mode;        /* File mode */
    u64 i_num_entrys;  /* Number of entries in this inode */
    u64 tstamp;
};

/*
 * DRAM icp for inodes
 */
struct hk_inode_info {
    struct hk_inode_info_header header;
    struct inode vfs_inode;
};

static inline void hk_dump_inode(struct super_block *sb, struct hk_inode *pi)
{
    struct hk_sb_info *sbi = HK_SB(sb);

    hk_info("ino: %lu\n", pi->ino);
    hk_info("i_size: %lu\n", pi->i_size);
    hk_info("i_flags: %u\n", pi->i_flags);
    hk_info("i_mode: %u\n", pi->i_mode);
    hk_info("ofs_next: @0x%llx\n", pi->root.ofs_next);
    hk_info("tstamp: 0x%llx\n", pi->tstamp);
}

static inline struct hk_inode_info *HK_I(struct inode *inode)
{
    return container_of(inode, struct hk_inode_info, vfs_inode);
}

static inline struct hk_inode_info_header *HK_IH(struct inode *inode)
{
    struct hk_inode_info *si = HK_I(inode);
    return &si->header;
}

/* If this is part of a read-modify-write of the inode metadata,
 * hk_memunlock_pi() before calling!
 */
static inline struct hk_inode *hk_get_pi_by_ino(struct super_block *sb, u64 ino)
{
    struct hk_sb_info *sbi = HK_SB(sb);

    if (ino >= HK_NUM_INO)
        return NULL;

    return (struct hk_inode *)(sbi->ino_tab_addr + ino * sizeof(struct hk_inode));
}

static inline struct hk_inode *hk_get_inode(struct super_block *sb,
                                            struct inode *inode)
{
    struct hk_inode_info *si = HK_I(inode);
    struct hk_inode_info_header *sih = &si->header;
    struct hk_inode fake_pi;
    void *addr;
    int rc;
    struct hk_super_block *super = hk_get_super(sb);

    addr = sih->pi_addr;
    rc = memcpy_mcsafe(&fake_pi, addr, sizeof(struct hk_inode));
    if (rc)
        return NULL;

    return (struct hk_inode *)addr;
}

#endif
