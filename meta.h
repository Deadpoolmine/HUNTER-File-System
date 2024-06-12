#ifndef _HK_META_H
#define _HK_META_H

#include "hunter.h"

struct hk_header_node {
    u64 ofs_next;
};

struct hk_header {
    struct hk_header_node node; // Next addr relative to NVM start
    u64 ino;               // Indicate which inode it belongs to
    u64 tstamp;            // Version control
    u64 f_blk;             // Indicate which blk it resides in
    u64 size;
    u32 cmtime;
    u32 crc32;
    u8 valid;        // 8B atomic persistence
    u8 paddings[15]; // Padding to make it 64B
} __attribute((__packed__));

static_assert(sizeof(struct hk_header) == 64, "hk_header size mismatch");

struct hk_setattr_entry {
    __le16 mode;
    __le32 uid;
    __le32 gid;
    __le32 atime;
    __le32 mtime;
    __le32 ctime;
    __le64 size; /* File size after truncation */
    __le64 tstamp;
};

struct hk_linkchange_entry {
    __le16 links;
    __le32 ctime;
    __le64 tstamp;
};

enum hk_entry_type {
    SET_ATTR,
    LINK_CHANGE,
};

struct hk_al_entry {
    u8 type;
    union {
        struct hk_setattr_entry setattr;
        struct hk_linkchange_entry linkchange;
    } entry;
};

struct hk_attr_log {
    u8 evicting;
    u8 last_valid_setattr;
    u8 last_valid_linkchange;
    __le64 ino;
    struct hk_al_entry entries[HK_ATTRLOG_ENTY_SLOTS];
    u8 paddings[256 - sizeof(u8) - sizeof(u8) - sizeof(u8) - sizeof(__le64) - sizeof(struct hk_al_entry) * HK_ATTRLOG_ENTY_SLOTS];
} __attribute((__packed__));

static_assert(sizeof(struct hk_attr_log) == 256, "hk_attr_log size mismatch");

static inline void hk_dump_mentry(struct super_block *sb, struct hk_al_entry *entry)
{
    switch (entry->type) {
    case SET_ATTR:
        hk_info("SET_ATTR: mode %u, id: %u, gid: %u, atime: %u, mtime: %u \n ctime: %u, size: %llu, tstamp: %llu\n",
                le32_to_cpu(entry->entry.setattr.mode),
                le32_to_cpu(entry->entry.setattr.uid),
                le32_to_cpu(entry->entry.setattr.gid),
                le32_to_cpu(entry->entry.setattr.atime),
                le32_to_cpu(entry->entry.setattr.mtime),
                le32_to_cpu(entry->entry.setattr.ctime),
                le64_to_cpu(entry->entry.setattr.size),
                le64_to_cpu(entry->entry.setattr.tstamp));
        break;
    case LINK_CHANGE:
        hk_info("LINK_CHANGE: links: %u, ctime: %u, tstamp: %llu\n",
                le16_to_cpu(entry->entry.linkchange.links),
                le32_to_cpu(entry->entry.linkchange.ctime),
                le64_to_cpu(entry->entry.linkchange.tstamp));
        break;
    default:
        break;
    }
}

static inline void hk_dump_mregion(struct super_block *sb, struct hk_attr_log *rg)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    int slotid;
    if (le64_to_cpu(rg->ino) != (u64)-1) {
        for (slotid = 0; slotid < HK_ATTRLOG_ENTY_SLOTS; slotid++) {
            if (rg->last_valid_linkchange == slotid || rg->last_valid_setattr == slotid) {
                hk_dump_mentry(sb, &rg->entries[slotid]);
            }
        }
    }
}

struct hk_jdentry {
    u8 name_len; /* length of the dentry name */
    __le16 links_count;
    __le32 mtime; /* For both mtime and ctime */
    __le64 ino;   /* inode no pointed to by this entry */
    __le64 tstamp;
    u8 name[HK_NAME_LEN + 1]; /* File name */
} __attribute((__packed__));

struct hk_jinode {
    __le32 i_flags;       /* Inode flags */
    __le64 i_size;        /* Size of data in bytes */
    __le32 i_ctime;       /* Inode modification time */
    __le32 i_mtime;       /* Inode Linear Index Modification time */
    __le32 i_atime;       /* Access time */
    __le16 i_mode;        /* File mode */
    __le16 i_links_count; /* Links count */

    __le64 i_xattr; /* Extended attribute block */

    __le32 i_uid;         /* Owner Uid */
    __le32 i_gid;         /* Group Id */
    __le32 i_generation;  /* File version (for NFS) */
    __le32 i_create_time; /* Create time */
    __le64 ino;           /* hk inode number */

    __le64 h_addr; /* Inode as the head of the files */
    __le64 tstamp; /* Time stamp */

    struct {
        __le32 rdev; /* major/minor # */
    } dev;           /* device inode */
} __attribute((__packed__));

enum hk_jentry_type {
    J_INODE,
    J_DENTRY,
};

struct hk_jentry {
    u8 type;
    __le64 data;
} __attribute((__packed__));

enum hk_journal_type {
    IDLE,
    CREATE,
    MKDIR,
    LINK,
    SYMLINK,
    UNLINK,
    RENAME
};

struct hk_jheader {
    u8 jtype;
    __le64 jofs_start; /* Start addr relative to NVM start */
    __le64 jofs_end;
    __le64 jofs_head; /* Head Addr relative to NVM start  */
    __le64 jofs_tail;
} __attribute((__packed__));

struct hk_jbody {
    u8 jbody[HK_JOURNAL_SIZE - sizeof(struct hk_jheader)];
} __attribute((__packed__));

struct hk_journal {
    struct hk_jheader jhdr;
    struct hk_jbody jbody;
} __attribute((__packed__));

#define HK_MAX_OBJ_INVOVED 5
struct hk_jentry_info {
    u8 valid;
    struct hk_jentry jentry;
};

struct hk_tx_info {
    enum hk_journal_type jtype;
    struct hk_jentry_info ji_pi;
    struct hk_jentry_info ji_pd;
    struct hk_jentry_info ji_pd_new;
    struct hk_jentry_info ji_pi_par;
    struct hk_jentry_info ji_pi_new;
};

#define traverse_inode_hdr(sbi, pi, hdr_traverse) for (hdr_traverse = TRANS_OFS_TO_ADDR(sbi, le64_to_cpu(pi->root.ofs_next)); hdr_traverse != &pi->root; hdr_traverse = TRANS_OFS_TO_ADDR(sbi, (((struct hk_header *)hdr_traverse)->node.ofs_next)))

#define traverse_inode_hdr_safe(sbi, pi, hdr_traverse, hdr_next) for (hdr_traverse = TRANS_OFS_TO_ADDR(sbi, le64_to_cpu(pi->root.ofs_next)), hdr_next = TRANS_OFS_TO_ADDR(sbi, (((struct hk_header *)hdr_traverse)->node.ofs_next)); hdr_traverse != &pi->root; hdr_traverse = hdr_next, hdr_next = TRANS_OFS_TO_ADDR(sbi, (((struct hk_header *)hdr_traverse)->node.ofs_next)))

#define traverse_tx_info(ji, slotid, info) for (ji = &info->ji_pi, slotid = 0; slotid < HK_MAX_OBJ_INVOVED; slotid++, ji = hk_tx_get_ji_from_tx_info(info, slotid))

#define traverse_journal_entry(sbi, jcur, jnl) for (jcur = TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_head); jcur != TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_tail); jcur = jcur + sizeof(struct hk_jentry) > TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_end) ? TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_start) + sizeof(struct hk_jentry) : jcur + sizeof(struct hk_jentry))

static void hk_dump_jentry(struct super_block *sb, struct hk_jentry *je)
{
    switch (je->type) {
    case J_INODE:
        hk_info("J_INODE: data @ %llx\n", le64_to_cpu(je->data));
        break;
    case J_DENTRY:
        hk_info("J_DENTRY: data @ %llx\n", le64_to_cpu(je->data));
        break;
    }
}

static void hk_dump_journal(struct super_block *sb, struct hk_journal *jnl)
{
    struct hk_jentry *je;
    struct hk_sb_info *sbi = HK_SB(sb);
    u64 jcur;
    int i;

    hk_info("JOURNAL: jtype: %u, jofs_start: %llu, jofs_end: %llu, jofs_head: %llu, jofs_tail: %llu\n",
            jnl->jhdr.jtype,
            le64_to_cpu(jnl->jhdr.jofs_start),
            le64_to_cpu(jnl->jhdr.jofs_end),
            le64_to_cpu(jnl->jhdr.jofs_head),
            le64_to_cpu(jnl->jhdr.jofs_tail));

    if (jnl->jhdr.jofs_head != jnl->jhdr.jofs_tail) {
        traverse_journal_entry(sbi, jcur, jnl)
        {
            je = (struct hk_jentry *)jcur;
            hk_dump_jentry(sb, je);
        }
    }
}

#endif /* _HK_META_H */