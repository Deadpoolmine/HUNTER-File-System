/*
 * HUNTER metadata areas management.
 *
 * Copyright 2023-2024 Regents of the University of Harbin Institute of Technology, Shenzhen
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

#include "hunter.h"

/* ======================= ANCHOR: Summary Header ========================= */

/* start of pblk */
u64 sm_get_addr_by_hdr(struct super_block *sb, u64 hdr)
{
    struct hk_sb_info *sbi = HK_SB(sb);
#ifndef CONFIG_LAYOUT_TIGHT
    u64 blk = (hdr - sbi->sm_addr) / sizeof(struct hk_header);
    return sbi->d_addr + (blk * HK_PBLK_SZ);
#else
    return hdr + sizeof(struct hk_header) - HK_PBLK_SZ;
#endif
}

struct hk_header *sm_get_hdr_by_blk(struct super_block *sb, u64 blk)
{
    struct hk_sb_info *sbi = HK_SB(sb);
#ifndef CONFIG_LAYOUT_TIGHT
    return (struct hk_header *)(sbi->sm_addr + blk * sizeof(struct hk_header));
#else
    return (struct hk_header *)(sbi->d_addr + (blk + 1) * HK_PBLK_SZ - sizeof(struct hk_header));
#endif
}

struct hk_header *sm_get_hdr_by_addr(struct super_block *sb, u64 addr)
{
    u64 blk;
    struct hk_sb_info *sbi = HK_SB(sb);

    if (addr < sbi->d_addr) {
        hk_info("%s: Invalid Addr %llx\n", __func__, addr);
        BUG_ON(1);
    }

    blk = (addr - sbi->d_addr) / HK_PBLK_SZ;

    hk_dbgv("sbi->sm_addr: %llx, %d, %d\n", sbi->sm_addr, sizeof(struct hk_header), blk * sizeof(struct hk_header));

    return sm_get_hdr_by_blk(sb, blk);
}

struct hk_layout_info *sm_get_layout_by_hdr(struct super_block *sb, u64 hdr)
{
    int cpuid;
    struct hk_sb_info *sbi = HK_SB(sb);
    u64 addr = sm_get_addr_by_hdr(sb, hdr);
    u64 size_per_layout = _round_down(sbi->d_size / sbi->num_layout, HK_PBLK_SZ);

    cpuid = (addr - sbi->d_addr) / size_per_layout;

    /* cpuid could larger that (sbi->num_layout - 1) */
    cpuid = cpuid >= sbi->num_layout ? cpuid - 1 : cpuid;

    return &sbi->layouts[cpuid];
}

u64 sm_get_next_addr_by_dbatch(struct super_block *sb, struct hk_inode_info_header *sih, struct hk_cmt_dbatch *batch)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return batch->blk_start < 1 ? 0 : TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, batch->blk_start - 1));
}

u64 sm_get_prev_addr_by_dbatch(struct super_block *sb, struct hk_inode_info_header *sih, struct hk_cmt_dbatch *batch)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return batch->blk_end - 1 >= sih->ix.num_slots ? 0 : TRANS_OFS_TO_ADDR(sbi, linix_get(&sih->ix, batch->blk_end));
}

int sm_remove_hdr(struct super_block *sb, struct hk_header *prev_hdr, struct hk_header *hdr)
{
    struct hk_sb_info *sbi = HK_SB(sb);

    prev_hdr->node.ofs_next = TRANS_ADDR_TO_OFS(sbi, TRANS_OFS_TO_ADDR(sbi, hdr->node.ofs_next));

    return 0;
}

// Hybrid link: Providing a consistent view in PM as DRAM
int sm_insert_hdr(struct super_block *sb, struct hk_header *prev_hdr,
                  struct hk_header *hdr, struct hk_header *next_hdr)
{
    struct hk_sb_info *sbi = HK_SB(sb);

    hdr->node.ofs_next = TRANS_ADDR_TO_OFS(sbi, next_hdr);

    // prev_hdr might be in either DRAM or PM
    prev_hdr->node.ofs_next = TRANS_ADDR_TO_OFS(sbi, hdr);

    return 0;
}

// invalid data without linking. This means, we do not intefere with inode.
// Note the consistency of hdr is delayed to allocation and remount.
int sm_delete_data_sync(struct super_block *sb, u64 blk_addr)
{
    struct inode *inode;
    struct hk_header *hdr;
    struct hk_layout_info *layout;
    struct hk_sb_info *sbi = HK_SB(sb);
    unsigned long irq_flags = 0;
    u64 blk;

    INIT_TIMING(time);

    HK_START_TIMING(sm_delete_t, time);

    hdr = sm_get_hdr_by_addr(sb, blk_addr);
    // hdr->ofs_next = NULL;
    // hdr->ofs_prev = NULL;

    // hk_memunlock_hdr(sb, hdr, &irq_flags);

    // PERSISTENT_BARRIER();
    // hdr->valid = 0;
    // hk_flush_buffer(hdr, sizeof(struct hk_header), true);
    // hk_memlock_hdr(sb, hdr, &irq_flags);

    layout = sm_get_layout_by_hdr(sb, (u64)hdr);

    ind_update(&layout->ind, INVALIDATE_BLK, 1);

    blk = hk_get_dblk_by_addr(sbi, blk_addr);
    hk_range_insert_range(&layout->gaps_tree, blk, blk);
    layout->num_gaps_indram++;

    HK_END_TIMING(sm_delete_t, time);
    return 0;
}

int sm_invalid_data_sync(struct super_block *sb, u64 prev_addr, u64 blk_addr, u64 ino)
{
    /*! Note: Do not update tstamp in invalid process, since version control */
    struct hk_header *hdr, *prev_hdr = NULL;
    struct hk_layout_info *layout;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_cmt_node *cmt_node;
    unsigned long irq_flags = 0;
    u64 blk;
    INIT_TIMING(invalid_time);

    HK_START_TIMING(sm_invalid_t, invalid_time);
    cmt_node = hk_cmt_search_node(sb, ino);
    hdr = sm_get_hdr_by_addr(sb, blk_addr);

    prev_hdr = prev_addr == 0 ? &cmt_node->root : sm_get_hdr_by_addr(sb, prev_addr);

    sm_remove_hdr(sb, prev_hdr, hdr);

    hk_memunlock_hdr(sb, hdr, &irq_flags);

    PERSISTENT_BARRIER();
    hdr->valid = 0;
    hk_flush_buffer(hdr, sizeof(struct hk_header), true);
    hk_memlock_hdr(sb, hdr, &irq_flags);

    layout = sm_get_layout_by_hdr(sb, (u64)hdr);

    ind_update(&layout->ind, INVALIDATE_BLK, 1);

    blk = hk_get_dblk_by_addr(sbi, blk_addr);
    hk_range_insert_range(&layout->gaps_tree, blk, blk);
    layout->num_gaps_indram++;

    HK_END_TIMING(sm_invalid_t, invalid_time);
    return 0;
}

int sm_update_data_sync(struct super_block *sb, u64 blk_addr, u64 size)
{
    struct hk_header *hdr;
    struct hk_sb_info *sbi = HK_SB(sb);
    unsigned long irq_flags = 0;

    INIT_TIMING(time);

    HK_START_TIMING(sm_update_t, time);

    hdr = sm_get_hdr_by_addr(sb, blk_addr);

    hk_memunlock_hdr(sb, (void *)hdr, &irq_flags);
    hdr->size = size;
    hk_flush_buffer(hdr, sizeof(struct hk_header), true);
    hk_memlock_hdr(sb, hdr, &irq_flags);

    HK_END_TIMING(sm_update_t, time);
    return 0;
}

int sm_valid_data_sync(struct super_block *sb, u64 prev_addr, u64 blk_addr, u64 next_addr,
                       u64 ino, u64 f_blk, u64 tstamp, u64 size, u32 cmtime)
{
    struct hk_header *hdr = NULL, *prev_hdr = NULL, *next_hdr = NULL;
    struct hk_layout_info *layout;
    struct hk_cmt_node *cmt_node;
    unsigned long irq_flags = 0;
    INIT_TIMING(valid_time);

    HK_START_TIMING(sm_valid_t, valid_time);

    cmt_node = hk_cmt_search_node(sb, ino);

    hdr = sm_get_hdr_by_addr(sb, blk_addr);

    prev_hdr = prev_addr == 0 ? &cmt_node->root : sm_get_hdr_by_addr(sb, prev_addr);
    next_hdr = next_addr == 0 ? &cmt_node->root : sm_get_hdr_by_addr(sb, next_addr);

    /* Write Hdr, then persist it */
    hk_memunlock_hdr(sb, (void *)hdr, &irq_flags);
    hdr->ino = ino;
    hdr->tstamp = tstamp;
    hdr->f_blk = f_blk;
    hdr->cmtime = cmtime;
    hdr->size = size;

    sm_insert_hdr(sb, prev_hdr, hdr, next_hdr);

    // Let's try fence once with crc32
    hdr->valid = 1;
    hdr->crc32 = hk_crc32c(~0, (const u8 *)hdr, sizeof(struct hk_header));
    /* this might be relatively slow */
    hk_flush_buffer(hdr, sizeof(struct hk_header), true);
    hk_memlock_hdr(sb, hdr, &irq_flags);

    layout = sm_get_layout_by_hdr(sb, (u64)hdr);

    ind_update(&layout->ind, VALIDATE_BLK, 1);

    HK_END_TIMING(sm_valid_t, valid_time);
    return 0;
}

/* ======================= ANCHOR: Attr Logs ========================= */

struct hk_attr_log *hk_get_attr_log_by_alid(struct super_block *sb, int alid)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return (struct hk_attr_log *)(sbi->al_addr + alid * sizeof(struct hk_attr_log));
}

struct hk_attr_log *hk_get_attr_log_by_ino(struct super_block *sb, u64 ino)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    int alid;

    alid = ino % sbi->al_slots;

    return hk_get_attr_log_by_alid(sb, alid);
}

/* Make sure region is memunlocked */
int hk_reset_attr_log(struct super_block *sb, struct hk_attr_log *al)
{
    al->ino = cpu_to_le64((u64)-1);
    al->last_valid_setattr = cpu_to_le64((u8)-1);
    al->last_valid_linkchange = cpu_to_le64((u8)-1);
    return 0;
}

/* pi should be unlocked */
void hk_evicting_al_entry(struct super_block *sb, struct hk_inode *pi, struct hk_al_entry *entry)
{
    switch (entry->type) {
    case SET_ATTR:
        pi->i_atime = pi->i_atime >= entry->entry.setattr.atime ? pi->i_atime : entry->entry.setattr.atime;
        pi->i_ctime = pi->i_ctime >= entry->entry.setattr.ctime ? pi->i_ctime : entry->entry.setattr.ctime;
        pi->i_mtime = pi->i_mtime >= entry->entry.setattr.mtime ? pi->i_mtime : entry->entry.setattr.mtime;
        pi->i_gid = entry->entry.setattr.gid;
        pi->i_uid = entry->entry.setattr.uid;
        pi->i_size = entry->entry.setattr.size;
        pi->i_mode = entry->entry.setattr.mode;
        pi->tstamp = pi->tstamp >= entry->entry.setattr.tstamp ? pi->tstamp : entry->entry.setattr.tstamp;
        break;
    case LINK_CHANGE:
        pi->i_links_count = entry->entry.linkchange.links;
        pi->i_ctime = pi->i_ctime >= entry->entry.linkchange.ctime ? pi->i_ctime : entry->entry.linkchange.ctime;
        pi->tstamp = pi->tstamp >= entry->entry.linkchange.tstamp ? pi->tstamp : entry->entry.linkchange.tstamp;
        break;
    default:
        break;
    }
}

/* no need to handle memlock or unlock */
void hk_evicting_al_entry_once(struct super_block *sb, struct hk_inode *pi, struct hk_al_entry *entry)
{
    struct hk_attr_log *al;
    unsigned long irq_flags = 0;

    hk_memunlock_pi(sb, pi, &irq_flags);
    hk_evicting_al_entry(sb, pi, entry);
    hk_memlock_pi(sb, pi, &irq_flags);

    al = hk_get_attr_log_by_ino(sb, le64_to_cpu(pi->ino));

    hk_memunlock_attr_log(sb, al, &irq_flags);
    switch (entry->type) {
    case SET_ATTR:
        al->last_valid_setattr = (u8)-1;
        break;
    case LINK_CHANGE:
        al->last_valid_linkchange = (u8)-1;
        break;
    default:
        break;
    }
    hk_memlock_attr_log(sb, al, &irq_flags);
}

int hk_evicting_attr_log(struct super_block *sb, struct hk_attr_log *al)
{
    u32 ino = al->ino;
    int slotid;
    struct hk_inode *pi = hk_get_pi_by_ino(sb, ino);
    unsigned long irq_flags = 0;

    if (!pi->valid) {
        return -1;
    }

    hk_memunlock_attr_log(sb, al, &irq_flags);
    al->evicting = 1;
    hk_memlock_attr_log(sb, al, &irq_flags);
    hk_flush_buffer(al, sizeof(struct hk_attr_log), true);

    hk_memunlock_pi(sb, pi, &irq_flags);
    for (slotid = 0; slotid < HK_ATTRLOG_ENTY_SLOTS; slotid++) {
        if (al->last_valid_setattr == slotid || al->last_valid_linkchange == slotid) {
            hk_evicting_al_entry(sb, pi, &al->entries[slotid]);
        }
    }
    hk_memlock_pi(sb, pi, &irq_flags);
    hk_flush_buffer(pi, sizeof(struct hk_inode), true);

    hk_memunlock_attr_log(sb, al, &irq_flags);
    al->evicting = 0;
    hk_memlock_attr_log(sb, al, &irq_flags);
    hk_flush_buffer(al, sizeof(struct hk_attr_log), true);

    /* Invalidate the region */
    hk_reset_attr_log(sb, al);

    return 0;
}

/* cur_commit is returned at @entry */
bool hk_get_cur_commit_al_entry(struct super_block *sb, struct hk_inode *pi, enum hk_entry_type type, struct hk_al_entry **entry)
{
    bool commit_found = false;
    struct hk_attr_log *al;

    al = hk_get_attr_log_by_ino(sb, pi->ino);
    if (al->ino == pi->ino) /* Cur Commit */
    {
        switch (type) {
        case SET_ATTR:
            if (al->last_valid_setattr != (u8)-1) {
                *entry = &al->entries[al->last_valid_setattr];
                commit_found = true;
            }
            break;
        case LINK_CHANGE:
            if (al->last_valid_linkchange != (u8)-1) {
                *entry = &al->entries[al->last_valid_linkchange];
                commit_found = true;
            }
            break;
        default:
            break;
        }
    }
    *entry = NULL;
    return commit_found;
}

/* apply region to pi */
int hk_evicting_attr_log_to_inode(struct super_block *sb, struct hk_inode *pi)
{
    struct hk_al_entry *entry;
    bool commit_found = false;

    commit_found = hk_get_cur_commit_al_entry(sb, pi, SET_ATTR, &entry);
    if (commit_found) {
        hk_evicting_al_entry_once(sb, pi, entry);
    }

    commit_found = hk_get_cur_commit_al_entry(sb, pi, LINK_CHANGE, &entry);
    if (commit_found) {
        hk_evicting_al_entry_once(sb, pi, entry);
    }

    return 0;
}

int hk_do_commit_al_entry(struct super_block *sb, u64 ino, struct hk_al_entry *entry)
{
    struct hk_attr_log *al;
    unsigned long irq_flags = 0;
    int slotid;

    al = hk_get_attr_log_by_ino(sb, ino);
    /* Evict Attr Log */
    if (al->ino != ino && al->ino != (u64)-1) {
        hk_evicting_attr_log(sb, al);
    }

    hk_memunlock_attr_log(sb, al, &irq_flags);
    al->ino = ino;

    for (slotid = 0; slotid < HK_ATTRLOG_ENTY_SLOTS; slotid++) {
        if (slotid != al->last_valid_linkchange && slotid != al->last_valid_setattr) {
            /* This Function Has Fence Inside */
            memcpy_to_pmem_nocache(&al->entries[slotid], entry, sizeof(struct hk_al_entry));

            /* Commit The Write */
            switch (entry->type) {
            case SET_ATTR:
                al->last_valid_setattr = slotid;
                break;
            case LINK_CHANGE:
                al->last_valid_linkchange = slotid;
                break;
            default:
                break;
            }
            hk_flush_buffer(al, sizeof(struct hk_al_entry), true);
            break;
        }
    }
    hk_memlock_attr_log(sb, al, &irq_flags);

    return 0;
}

void hk_create_al_snapshot(struct super_block *sb, struct hk_inode *pi)
{
    struct hk_al_entry *attr_entry, *link_change_entry;
    struct hk_sb_info *sbi = HK_SB(sb);

    hk_get_cur_commit_al_entry(sb, pi, SET_ATTR, &attr_entry);
    hk_get_cur_commit_al_entry(sb, pi, LINK_CHANGE, &link_change_entry);

    pi->tx_link_change_entry = TRANS_ADDR_TO_OFS(sbi, link_change_entry);
    pi->tx_attr_entry = TRANS_ADDR_TO_OFS(sbi, attr_entry);
}

/* ======================= ANCHOR: commit newattr ========================= */
int hk_commit_attrchange(struct super_block *sb, struct inode *inode)
{
    struct hk_al_entry entry;
    struct hk_setattr_entry *setattr;
    struct hk_sb_info *sbi = HK_SB(sb);

    setattr = &entry.entry.setattr;

    setattr->mode = cpu_to_le16(inode->i_mode);
    setattr->gid = cpu_to_le32(i_gid_read(inode));
    setattr->uid = cpu_to_le32(i_uid_read(inode));
    setattr->mtime = cpu_to_le32(inode->i_mtime.tv_sec);
    setattr->atime = cpu_to_le32(inode->i_atime.tv_sec);
    setattr->ctime = cpu_to_le32(inode->i_ctime.tv_sec);
    setattr->size = cpu_to_le64(inode->i_size);

    entry.type = SET_ATTR;
    setattr->tstamp = get_version(sbi);

    hk_do_commit_al_entry(sb, inode->i_ino, &entry);
}

int hk_commit_icp_attrchange(struct super_block *sb, struct hk_cmt_icp *icp)
{
    struct hk_al_entry entry;
    struct hk_setattr_entry *setattr;
    struct hk_sb_info *sbi = HK_SB(sb);

    setattr = &entry.entry.setattr;

    setattr->mode = cpu_to_le16(icp->mode);
    setattr->gid = cpu_to_le32(icp->gid);
    setattr->uid = cpu_to_le32(icp->uid);
    setattr->mtime = cpu_to_le32(icp->mtime);
    setattr->atime = cpu_to_le32(icp->atime);
    setattr->ctime = cpu_to_le32(icp->ctime);
    setattr->size = cpu_to_le64(icp->size);

    entry.type = SET_ATTR;
    setattr->tstamp = get_version(sbi);

    hk_do_commit_al_entry(sb, icp->ino, &entry);
}

/* ======================= ANCHOR: commit sizechange ========================= */
/* used only for hk_setsize(), inode must be opened */
int hk_commit_sizechange(struct super_block *sb, struct inode *inode, loff_t ia_size)
{
    struct hk_al_entry entry;
    struct hk_setattr_entry *setattr;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_inode_info_header *sih = HK_IH(inode);

    setattr = &entry.entry.setattr;
    entry.type = SET_ATTR;

    setattr->mode = cpu_to_le16(inode->i_mode);
    setattr->gid = cpu_to_le32(i_gid_read(inode));
    setattr->uid = cpu_to_le32(i_uid_read(inode));
    setattr->mtime = cpu_to_le32(inode->i_mtime.tv_sec);
    setattr->atime = cpu_to_le32(inode->i_atime.tv_sec);
    setattr->ctime = cpu_to_le32(inode->i_ctime.tv_sec);
    setattr->size = cpu_to_le64(ia_size);
    setattr->tstamp = get_version(sbi);

    hk_do_commit_al_entry(sb, inode->i_ino, &entry);

    return 0;
}

/* ======================= ANCHOR: commit linkchange ========================= */
int hk_commit_linkchange(struct super_block *sb, struct inode *inode)
{
    struct hk_al_entry entry;
    struct hk_linkchange_entry *linkchange;
    struct hk_sb_info *sbi = HK_SB(sb);

    entry.type = LINK_CHANGE;
    linkchange = &entry.entry.linkchange;
    linkchange->tstamp = get_version(sbi);
    linkchange->links = cpu_to_le16(inode->i_link);
    linkchange->ctime = cpu_to_le32(inode->i_ctime.tv_sec);

    hk_do_commit_al_entry(sb, inode->i_ino, &entry);

    return 0;
}

int hk_commit_icp_linkchange(struct super_block *sb, struct hk_cmt_icp *icp)
{
    struct hk_al_entry entry;
    struct hk_linkchange_entry *linkchange;
    struct hk_sb_info *sbi = HK_SB(sb);

    entry.type = LINK_CHANGE;
    linkchange = &entry.entry.linkchange;
    linkchange->tstamp = get_version(sbi);
    linkchange->links = cpu_to_le16(icp->links_count);
    linkchange->ctime = cpu_to_le32(icp->ctime);

    hk_do_commit_al_entry(sb, icp->ino, &entry);

    return 0;
}

/* ======================= ANCHOR: commit icp ========================= */
int hk_commit_icp(struct super_block *sb, struct hk_cmt_icp *icp)
{
    struct hk_inode *pi = NULL;
    unsigned long irq_flags = 0;

    pi = hk_get_pi_by_ino(sb, icp->ino);

    hk_memunlock_pi(sb, pi, &irq_flags);
    pi->ino = cpu_to_le64(icp->ino);
    pi->i_mode = cpu_to_le16(icp->mode);
    pi->i_uid = cpu_to_le32(icp->uid);
    pi->i_gid = cpu_to_le32(icp->gid);
    pi->i_size = cpu_to_le64(icp->size);
    pi->i_atime = cpu_to_le32(icp->atime);
    pi->i_ctime = cpu_to_le32(icp->ctime);
    pi->i_mtime = cpu_to_le32(icp->mtime);
    pi->i_links_count = cpu_to_le16(icp->links_count);
    pi->root.ofs_next = TRANS_ADDR_TO_OFS(HK_SB(sb), &pi->root);
    pi->i_generation = cpu_to_le32(icp->generation);
    pi->tstamp = icp->tstamp;
    pi->i_flags = cpu_to_le32(icp->flags);
    hk_memlock_pi(sb, pi, &irq_flags);

    return 0;
}

/* ======================= ANCHOR: Transactions ========================= */
struct hk_journal *hk_get_journal_by_txid(struct super_block *sb, int txid)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return (struct hk_journal *)(sbi->j_addr + txid * HK_JOURNAL_SIZE);
}

struct hk_jentry *hk_get_jentry_by_slotid(struct super_block *sb, int txid, int slotid)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_journal *jnl = hk_get_journal_by_txid(sb, txid);
    u64 jcur;
    int cnt = 0;

    traverse_journal_entry(sbi, jcur, jnl)
    {
        if (cnt == slotid) {
            break;
        }
        cnt++;
    }

    return (struct hk_jentry *)jcur;
}

void hk_flush_journal_in_batch(struct super_block *sb, u64 jhead, u64 jtail)
{
    /* flush journal log entries in batch */
    if (jhead < jtail) {
        hk_flush_buffer(jhead, jtail - jhead, 0);
    } else { /* circular */
        /* head to end */
        hk_flush_buffer(jhead,
                        HK_JOURNAL_SIZE - (jhead & ~PAGE_MASK), 0);

        /* start to tail */
        hk_flush_buffer((void *)((u64)jtail & PAGE_MASK),
                        jtail & ~PAGE_MASK, 0);
    }
    PERSISTENT_BARRIER();
}

enum hk_ji_obj_type {
    JI_PI = 0,
    JI_PD,
    JI_PD_NEW,
    JI_PI_PAR,
    JI_PI_NEW,
    JI_MAX
};

int hk_tx_args_map[][HK_MAX_OBJ_INVOVED] = {
    [IDLE] { JI_MAX, JI_MAX, JI_MAX, JI_MAX, JI_MAX },
    [CREATE] { JI_PI, JI_PD, JI_PI_PAR, JI_MAX, JI_MAX },
    [MKDIR] { JI_PI, JI_PD, JI_PI_PAR, JI_MAX, JI_MAX },
    [LINK] { JI_PI, JI_PD, JI_PI_PAR, JI_MAX, JI_MAX },
    [SYMLINK] { JI_PI, JI_PD, JI_PI_PAR, JI_PD_NEW, JI_MAX },
    [UNLINK] { JI_PI, JI_PD, JI_PI_PAR, JI_MAX, JI_MAX },
    [RENAME] { JI_PI, JI_PD, JI_PD_NEW, JI_PI_PAR, JI_PI_NEW },
};

struct hk_jentry_info *hk_tx_get_ji_from_tx_info(struct hk_tx_info *info,
                                                 enum hk_ji_obj_type obj_type)
{
    switch (obj_type) {
    case JI_PI:
        return &info->ji_pi;
    case JI_PD:
        return &info->ji_pd;
    case JI_PD_NEW:
        return &info->ji_pd_new;
    case JI_PI_PAR:
        return &info->ji_pi_par;
    case JI_PI_NEW:
        return &info->ji_pi_new;
    default:
        return NULL;
    }
    return NULL;
}

int hk_tx_assign_inode_to_ji(struct super_block *sb, struct hk_jentry_info *ji, struct hk_inode *pi)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_jentry *je = &ji->jentry;
    /* pi is nvmm addr */
    je->data = TRANS_ADDR_TO_OFS(sbi, pi);
    return 0;
}

int hk_tx_assign_dentry_to_ji(struct super_block *sb, struct hk_jentry_info *ji, struct hk_dentry *pd)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_jentry *je = &ji->jentry;

    /* pd is nvmm addr */
    je->data = TRANS_ADDR_TO_OFS(sbi, pd);
    return 0;
}

int do_start_tx(struct super_block *sb, int txid, struct hk_tx_info *info)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_journal *jnl;
    struct hk_jentry_info *ji;
    struct hk_jentry *je;
    u64 jhead, jtail, jend, jstart, jcur;
    unsigned long irq_flags = 0;
    int slotid;

    jnl = hk_get_journal_by_txid(sb, txid);
    hk_memunlock_journal(sb, jnl, &irq_flags);
    /* write type */
    jnl->jhdr.jtype = info->jtype;

    /* write jentries */
    jhead = TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_head);
    jtail = TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_tail);
    jstart = TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_start);
    jend = TRANS_OFS_TO_ADDR(sbi, jnl->jhdr.jofs_end);

    if (jhead != jtail) {
        BUG_ON(1);
    }

    jcur = jhead;

    traverse_tx_info(ji, slotid, info)
    {
        if (ji->valid) {
            je = &ji->jentry;
            if (jcur + sizeof(struct hk_jentry) > jend) {
                jcur = jstart;
            }
            memcpy_to_pmem_nocache((void *)jcur, je, sizeof(struct hk_jentry));
            jcur += sizeof(struct hk_jentry);
        }
    }

    jtail = jcur;

    /* commit */
    PERSISTENT_BARRIER();
    jnl->jhdr.jofs_tail = TRANS_ADDR_TO_OFS(sbi, jtail);
    hk_flush_buffer(&jnl->jhdr, sizeof(struct hk_jheader), true);

    hk_memlock_journal(sb, jnl, &irq_flags);

    return 0;
}

static bool hk_tx_obj_is_inode(enum hk_ji_obj_type obj_type)
{
    return obj_type == JI_PI || obj_type == JI_PI_PAR || obj_type == JI_PI_NEW;
}

static bool hk_tx_obj_is_dentry(enum hk_ji_obj_type obj_type)
{
    return obj_type == JI_PD || obj_type == JI_PD_NEW;
}

static int hk_tx_cnt_args(enum hk_journal_type jtype)
{
    int *args = hk_tx_args_map[jtype];
    int i;
    int cnt = 0;

    for (i = 0; i < HK_MAX_OBJ_INVOVED; i++) {
        if (args[i] != JI_MAX) {
            cnt++;
        }
    }

    return cnt;
}

int hk_start_tx(struct super_block *sb, enum hk_journal_type jtype, ...)
{
    va_list valist;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_tx_info info;
    struct hk_jentry_info *ji;
    enum hk_ji_obj_type ji_obj_type;
    struct hk_inode *pi;
    struct hk_dentry *pd;
    struct hk_journal *jnl;
    bool journal_started = false;
    int txid_cmt = -1;
    int i, txid, start_txid;
    int objs_cnt;

    if (jtype == IDLE) {
        return -1;
    }

    /* Build tx info*/
    objs_cnt = hk_tx_cnt_args(jtype);
    va_start(valist, objs_cnt);

    /* invalid all entry */
    for (i = 0; i < HK_MAX_OBJ_INVOVED; i++) {
        ji = hk_tx_get_ji_from_tx_info(&info, (enum hk_ji_obj_type)i);
        ji->valid = false;
    }

    /* valid specific entry */
    for (i = 0; i < objs_cnt; i++) {
        ji_obj_type = hk_tx_args_map[jtype][i];
        ji = hk_tx_get_ji_from_tx_info(&info, ji_obj_type);
        ji->valid = true;
        if (hk_tx_obj_is_inode(ji_obj_type)) {
            pi = va_arg(valist, struct hk_inode *);
            ji->jentry.type = J_INODE;
            hk_tx_assign_inode_to_ji(sb, ji, pi);
        } else if (hk_tx_obj_is_dentry(ji_obj_type)) {
            pd = va_arg(valist, struct hk_dentry *);
            ji->jentry.type = J_DENTRY;
            hk_tx_assign_dentry_to_ji(sb, ji, pd);
        }
    }

    /* assign journal type */
    info.jtype = jtype;

    /* find a journal to append txinfo */
    while (!journal_started) {
        txid = hk_get_cpuid(sb) * HK_PERCORE_JSLOTS;
        start_txid = txid;
        do {
            jnl = hk_get_journal_by_txid(sb, txid);
            use_journal(sb, txid);
            if (jnl->jhdr.jtype == IDLE) {
                do_start_tx(sb, txid, &info);
                txid_cmt = txid;
                journal_started = true;
                unuse_journal(sb, txid);
                break;
            }
            unuse_journal(sb, txid);
            txid = (txid + 1) % sbi->j_slots;
        } while (txid != start_txid);
    }

out:
    va_end(valist);
    return txid_cmt;
}

int hk_finish_tx(struct super_block *sb, int txid)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_journal *jnl;
    unsigned long irq_flags = 0;

    jnl = hk_get_journal_by_txid(sb, txid);
    use_journal(sb, txid);
    hk_memunlock_journal(sb, jnl, &irq_flags);
    jnl->jhdr.jtype = IDLE;
    jnl->jhdr.jofs_head = jnl->jhdr.jofs_tail;
    hk_flush_buffer(jnl, sizeof(struct hk_jheader), true);
    hk_memlock_journal(sb, jnl, &irq_flags);
    unuse_journal(sb, txid);

    return 0;
}

int hk_reinit_journal(struct super_block *sb, struct hk_journal *jnl)
{
    struct hk_sb_info *sbi = HK_SB(sb);

    jnl->jhdr.jtype = IDLE;
    jnl->jhdr.jofs_start = TRANS_ADDR_TO_OFS(sbi, (u64)jnl + sizeof(struct hk_jheader));
    jnl->jhdr.jofs_end = jnl->jhdr.jofs_start + sizeof(struct hk_jbody);

    jnl->jhdr.jofs_head = jnl->jhdr.jofs_start;
    jnl->jhdr.jofs_tail = jnl->jhdr.jofs_start;

    return 0;
}

int hk_format_meta(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    unsigned long irq_flags = 0;
    struct hk_header *hdr;
    struct hk_attr_log *al;
    struct hk_journal *jnl;
    unsigned long bid, alid, txid;

    /* Step 1: Format Inode Table */
    hk_memunlock_range(sb, (void *)sbi->ino_tab_addr, sbi->ino_tab_size, &irq_flags);
    memset_nt_large((void *)sbi->ino_tab_addr, 0, sbi->ino_tab_size);
    hk_memlock_range(sb, sbi->ino_tab_addr, sbi->ino_tab_size, &irq_flags);

    /* Step 2: Format Summary Headers  */
#ifdef CONFIG_LAYOUT_TIGHT
    /* Do nothing */
#else
    hk_memunlock_range(sb, (void *)sbi->sm_addr, sbi->sm_size, &irq_flags);
    for (bid = 0; bid < sbi->d_blks; bid++) {
        hdr = sm_get_hdr_by_blk(sb, bid);
        hdr->valid = (u8)-1;
    }
    hk_dbgv("entries: %llu\n", sbi->sm_size / sizeof(struct hk_header));
    /* Not clean ? */
    hk_dbgv("sbi->d_blks: %llu\n", sbi->d_blks);
    hk_memlock_range(sb, (void *)sbi->sm_addr, sbi->sm_size, &irq_flags);
#endif

    /* Step 3: Format Jentry */
    hk_memunlock_range(sb, (void *)sbi->j_addr, sbi->j_size, &irq_flags);
    for (txid = 0; txid < sbi->j_slots; txid++) {
        jnl = hk_get_journal_by_txid(sb, txid);
        hk_reinit_journal(sb, jnl);
        hk_flush_buffer((void *)jnl, HK_JOURNAL_SIZE, false);
    }
    hk_memlock_range(sb, (void *)sbi->j_addr, sbi->j_size, &irq_flags);

    /* Step 4: Format Attr Logs */
    hk_memunlock_range(sb, (void *)sbi->al_addr, sbi->al_size, &irq_flags);
    for (alid = 0; alid < sbi->al_slots; alid++) {
        al = hk_get_attr_log_by_alid(sb, alid);
        al->evicting = 0;
        hk_reset_attr_log(sb, al);
        hk_flush_buffer((void *)al, CACHELINE_SIZE, false);
    }
    hk_memlock_range(sb, (void *)sbi->al_addr, sbi->al_size, &irq_flags);

    hk_info("meta format done.\n");
    return 0;
}