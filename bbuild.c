/*
 * HUNTER Recovery routines.
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

int hk_save_layouts(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_super_block *hk_sb = sbi->hk_sb;
    struct hk_layout_info *layout;
    int ret = 0;
    int cpuid;

    for (cpuid = 0; cpuid < sbi->num_layout; cpuid++) {
        layout = &sbi->layouts[cpuid];

        if (layout->ind.prep_blks != 0) {
            hk_dump_layout_info(layout);
        }

        hk_sb->s_layout->s_atomic_counter = cpu_to_le64(layout->atomic_counter);

        hk_sb->s_layout->s_ind.free_blks = cpu_to_le64(layout->ind.free_blks);
        hk_sb->s_layout->s_ind.invalid_blks = cpu_to_le64(layout->ind.invalid_blks);
        hk_sb->s_layout->s_ind.prep_blks = cpu_to_le64(layout->ind.prep_blks);
        HK_ASSERT(hk_sb->s_layout->s_ind.prep_blks == 0);
        hk_sb->s_layout->s_ind.valid_blks = cpu_to_le64(layout->ind.valid_blks);
        hk_sb->s_layout->s_ind.total_blks = cpu_to_le64(layout->ind.total_blks);
    }

    hk_update_super_crc(sb);

    hk_sync_super(sb);
    hk_info("layouts dumped OK\n");
    return ret;
}

/* Apply all regions to inode */
int hk_save_regions(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_attr_log *al;
    int alid;

    for (alid = 0; alid < sbi->al_slots; alid++) {
        al = hk_get_attr_log_by_alid(sb, alid);
        if (le64_to_cpu(al->ino) != (u64)-1) {
            hk_evicting_attr_log(sb, al);
        }
    }
    hk_info("attr dumps OK\n");

    return 0;
}

static void hk_revert_al_snapshot(struct super_block *sb, struct hk_inode *pi)
{
    struct hk_al_entry *attr_entry, *link_change_entry;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_attr_log *al;
    int slotid;

    attr_entry = TRANS_OFS_TO_ADDR(sbi, pi->tx_attr_entry);
    link_change_entry = TRANS_OFS_TO_ADDR(sbi, pi->tx_link_change_entry);

    al = hk_get_attr_log_by_ino(sb, pi->ino);
    for (slotid = 0; slotid < HK_ATTRLOG_ENTY_SLOTS; slotid++) {
        if (attr_entry == &al->entries[slotid]) {
            al->last_valid_setattr = slotid;
        } else if (link_change_entry == &al->entries[slotid]) {
            al->last_valid_linkchange = slotid;
        }
    }
}

/* Undo Recovery (Undo Journal) */
static int hk_journal_recovery(struct super_block *sb, int txid, struct hk_journal *jnl)
{
    int ret = 0;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_jentry *je_pi;
    struct hk_jentry *je_pd;
    struct hk_jentry *je_pd_new;
    struct hk_jentry *je_pi_par;
    struct hk_jentry *je_pi_new;

    struct hk_inode *pi, *pi_par, *pi_new;
    struct hk_dentry *pd, *pd_new;

    struct hk_attr_log *al;

    unsigned long irq_flags = 0;
    u8 jtype = jnl->jhdr.jtype;

    hk_memunlock_all(sb, &irq_flags);
    switch (jtype) {
    case IDLE:
        goto out;
    case CREATE:
    case MKDIR:
    case LINK:
    case SYMLINK:
        /* fall thru */
        je_pi = hk_get_jentry_by_slotid(sb, txid, 0);
        je_pd = hk_get_jentry_by_slotid(sb, txid, 1);
        je_pi_par = hk_get_jentry_by_slotid(sb, txid, 2);

        /* clear pi */
        pi = TRANS_OFS_TO_ADDR(sbi, je_pi->data);
        if (jtype != LINK) {
            pi->valid = 0;
        }

        /* clear dentry */
        pd = TRANS_OFS_TO_ADDR(sbi, je_pd->data);
        pd->valid = 0;

        /* clear pi_par's attr log, since we've apply before transaction start */
        pi_par = TRANS_OFS_TO_ADDR(sbi, je_pi_par->data);
        al = hk_get_attr_log_by_ino(sb, pi_par->ino);
        if (al->ino == pi_par->ino) {
            hk_revert_al_snapshot(sb, pi_par);
        }

        /* if this is a symlink, we clear its data block for symname later */
        break;
    case UNLINK:
        je_pi = hk_get_jentry_by_slotid(sb, txid, 0);
        je_pd = hk_get_jentry_by_slotid(sb, txid, 1);
        je_pi_par = hk_get_jentry_by_slotid(sb, txid, 2);

        /* validate inode */
        pi = TRANS_OFS_TO_ADDR(sbi, je_pi->data);
        pi->valid = 1;
        al = hk_get_attr_log_by_ino(sb, pi->ino);
        if (al->ino == pi->ino) {
            hk_revert_al_snapshot(sb, pi);
        }

        /* valid dentry */
        pd = TRANS_OFS_TO_ADDR(sbi, je_pd->data);
        pd->valid = 1;

        /* 3. invalid blks belongs to inode, we don't need invalidators */
        pi_par = TRANS_OFS_TO_ADDR(sbi, je_pi_par->data);
        al = hk_get_attr_log_by_ino(sb, pi_par->ino);
        if (al->ino == pi_par->ino) {
            hk_revert_al_snapshot(sb, pi_par);
        }
        break;
    case RENAME:
        je_pi = hk_get_jentry_by_slotid(sb, txid, 0);     /* self */
        je_pd = hk_get_jentry_by_slotid(sb, txid, 1);     /* self-dentry */
        je_pd_new = hk_get_jentry_by_slotid(sb, txid, 2); /* new-dentry */
        je_pi_par = hk_get_jentry_by_slotid(sb, txid, 3); /* parent */
        je_pi_new = hk_get_jentry_by_slotid(sb, txid, 4); /* new-parent */

        /* revert to rename non happen */
        pi = TRANS_OFS_TO_ADDR(sbi, je_pi->data);
        al = hk_get_attr_log_by_ino(sb, pi->ino);
        if (al->ino == pi->ino) {
            hk_revert_al_snapshot(sb, pi);
        }

        pd = TRANS_OFS_TO_ADDR(sbi, je_pd->data);
        pd->valid = 1;

        pd_new = TRANS_OFS_TO_ADDR(sbi, je_pd_new->data);
        pd->valid = 0;

        pi_par = TRANS_OFS_TO_ADDR(sbi, je_pi_par->data);
        al = hk_get_attr_log_by_ino(sb, pi_par->ino);
        if (al->ino == pi_par->ino) {
            hk_revert_al_snapshot(sb, pi_par);
        }

        pi_new = TRANS_OFS_TO_ADDR(sbi, je_pi_new->data);
        al = hk_get_attr_log_by_ino(sb, pi_new->ino);
        if (al->ino == pi_new->ino) {
            hk_revert_al_snapshot(sb, pi_new);
        }

        break;
    default:
        break;
    }
    hk_memlock_all(sb, &irq_flags);

    hk_finish_tx(sb, txid);
out:
    return ret;
}

struct hk_recovery_node *hk_get_recovery_node(struct rb_root *table, u64 ino)
{
    struct rb_node *node = table->rb_node;
    struct hk_recovery_node *rn;

    while (node) {
        rn = container_of(node, struct hk_recovery_node, rbnode);
        if (ino < rn->ino) {
            node = node->rb_left;
        } else if (ino > rn->ino) {
            node = node->rb_right;
        } else {
            return rn;
        }
    }
    return NULL;
}

int hk_insert_recovery_node(struct rb_root *table, struct hk_recovery_node *rn)
{
    struct rb_node **new = &(table->rb_node), *parent = NULL;
    struct hk_recovery_node *this;

    while (*new) {
        this = container_of(*new, struct hk_recovery_node, rbnode);
        parent = *new;
        if (rn->ino < this->ino) {
            new = &((*new)->rb_left);
        } else if (rn->ino > this->ino) {
            new = &((*new)->rb_right);
        } else {
            return -EEXIST;
        }
    }
    rb_link_node(&rn->rbnode, parent, new);
    rb_insert_color(&rn->rbnode, table);
    return 0;
}

int hk_invalidate_data_blocks(struct super_block *sb, struct linix *ix, loff_t start, loff_t end)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    s64 start_index, end_index, index;
    u64 addr, blk = 0;
    int freed = 0;

    start_index = (start + (1UL << PAGE_SHIFT) - 1) >> PAGE_SHIFT;

    if (end == 0)
        return -1;
    end_index = (end - 1) >> PAGE_SHIFT;

    if (start_index > end_index)
        return -1;

    /* the inode lock is already held */
    for (index = end_index; index >= start_index; index--) {
        addr = TRANS_OFS_TO_ADDR(sbi, linix_get(ix, index));
        linix_delete(ix, index, index, true);

        use_layout_for_addr(sb, addr);
        // Just delete the data but not maintain the link structure
        sm_delete_data_sync(sb, addr);
        unuse_layout_for_addr(sb, addr);

        freed++;
    }

    return 0;
}

u64 sm_get_next_addr_by_cur_index(struct super_block *sb, struct linix *ix, u64 cur_index)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return cur_index < 1 ? 0 : TRANS_OFS_TO_ADDR(sbi, linix_get(ix, cur_index - 1));
}

u64 sm_get_prev_addr_by_cur_index(struct super_block *sb, struct linix *ix, u64 cur_index)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return cur_index - 1 >= ix->num_slots ? 0 : TRANS_OFS_TO_ADDR(sbi, linix_get(ix, cur_index + 1));
}

u64 sm_get_cur_addr_by_cur_index(struct super_block *sb, struct linix *ix, u64 cur_index)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    return TRANS_OFS_TO_ADDR(sbi, linix_get(ix, cur_index));
}

int hk_failure_recovery(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_super_block *hk_sb = sbi->hk_sb;
    struct hk_layout_info *layout;
    struct hk_journal *jnl;
    struct hk_attr_log *al;
    struct hk_header *hdr, *est_hdr;
    struct hk_inode *pi;
    u64 blk = 0, ino = 0;
    u64 addr = 0, est_addr = 0;
    int cpuid, alid, txid;
    unsigned long irq_flags = 0;
    bool hdr_real_valid = false;
    struct rb_root recovery_table = RB_ROOT;
    struct hk_recovery_node *rn = NULL, *rn_next = NULL;
    struct hk_range_node *node;
    int ret = 0;

    /* Step 1: Undo Transactions */
    for (txid = 0; txid < sbi->j_slots; txid++) {
        jnl = hk_get_journal_by_txid(sb, txid);
        if (jnl->jhdr.jofs_head != jnl->jhdr.jofs_tail) {
            hk_journal_recovery(sb, txid, jnl);
        }
    }

    /* Step 2: Redo Attr Log, pi's metadata is now consistent at pi->tstamp */
    for (alid = 0; alid < sbi->al_slots; alid++) {
        al = hk_get_attr_log_by_alid(sb, alid);
        if (al->evicting || le64_to_cpu(al->ino) != (u64)-1) {
            hk_evicting_attr_log(sb, al);
        }
    }

    /* Step 3: Recovery Layouts */
    init_hk_recovery_node_cache();
    for (cpuid = 0; cpuid < sbi->num_layout; cpuid++) {
        layout = &sbi->layouts[cpuid];
        blk = 0;

        use_layout(layout);
        layout->atomic_counter = (layout->layout_blks * HK_PBLK_SZ);
        layout->num_gaps_indram = 0;
        ind_update(&layout->ind, PREP_LAYOUT_APPEND, layout->layout_blks);

        traverse_layout_blks(addr, layout)
        {
            HK_ASSERT(addr != 0);
            hdr = sm_get_hdr_by_addr(sb, addr);
            if (hdr->valid == 1) {
                ino = le64_to_cpu(hdr->ino);
                rn = hk_get_recovery_node(&recovery_table, ino);
                if (!rn) {
                    rn = hk_alloc_hk_recovery_node();
                    rn->ino = ino;
                    rn->tstamp = 0;
                    rn->size = 0;
                    rn->cmtime = 0;
                    linix_init(sbi, &rn->ix, 1);
                    hk_insert_recovery_node(&recovery_table, rn);
                }

                est_addr = TRANS_OFS_TO_ADDR(sbi, linix_get(&rn->ix, hdr->f_blk));
                if (est_addr) {
                    HK_ASSERT(est_addr != 0);
                    est_hdr = sm_get_hdr_by_addr(sb, est_addr);
                    if (est_hdr->tstamp < hdr->tstamp) {
                        hdr_real_valid = true;
                    }
                } else {
                    hdr_real_valid = true;
                }

                if (hdr_real_valid) {
                    rn->size = hdr->size > rn->size ? hdr->size : rn->size;
                    rn->tstamp = hdr->tstamp > rn->tstamp ? hdr->tstamp : rn->tstamp;
                    rn->cmtime = hdr->cmtime > rn->cmtime ? hdr->cmtime : rn->cmtime;
                    HK_ASSERT(addr != 0);
                    linix_insert(&rn->ix, hdr->f_blk, addr, true);
                    ind_update(&layout->ind, VALIDATE_BLK, 1);
                } else {
                    blk = hk_get_dblk_by_addr(sbi, addr);
                    hk_range_insert_range(&layout->gaps_tree, blk, blk);
                    layout->num_gaps_indram++;
                    ind_update(&layout->ind, INVALIDATE_BLK, 1);
                }
            } else {
                blk = hk_get_dblk_by_addr(sbi, addr);
                hk_range_insert_range(&layout->gaps_tree, blk, blk);
                layout->num_gaps_indram++;
                ind_update(&layout->ind, PREP_LAYOUT_REMOVE, 1);
            }
        }
        unuse_layout(layout);
    }

    /* Step 4: Check Inode Table and Recover In-PM Link */
    bool revert_by_rn = true;
    u64 size = 0, tstamp = 0;
    u32 cmtime = 0;
    struct hk_header *prev_hdr, *next_hdr;
    u64 prev_addr = 0, next_addr = 0;
    int count = 0;
    struct rb_node *temp;

    temp = rb_first(&recovery_table);
    while (temp) {
        rn = container_of(temp, struct hk_recovery_node, rbnode);
        temp = rb_next(temp);

        count++;
        // Pi is in valid state?
        pi = hk_get_pi_by_ino(sb, rn->ino);
        revert_by_rn = false;
        if (pi->valid == 1) {
            if (pi->tstamp > rn->tstamp) {
                // Regard pi as true
                hk_dbgv("pi size %llu and rn size %llu\n", pi->i_size, rn->size);
                ret = hk_invalidate_data_blocks(sb, &rn->ix, rn->size, pi->i_size);
                if (ret < 0) {
                    hk_dbgv("pi size %llu is larger than or equal to rn size %llu, revert pi's state using rn\n", pi->i_size, rn->size);
                    revert_by_rn = true;
                }
                size = rn->size = pi->i_size;
                tstamp = rn->tstamp = pi->tstamp;
                cmtime = rn->cmtime = pi->i_ctime;
            } else {
                // Regard rn as true
                revert_by_rn = true;
            }

            if (revert_by_rn) {
                size = pi->i_size = rn->size;
                tstamp = pi->tstamp = rn->tstamp;
                cmtime = pi->i_ctime = rn->cmtime;
            }

            hk_dbgv("size: %llu, round blks: %llu", size, _round_up(size, PAGE_SIZE) / PAGE_SIZE);

            for (blk = 0; blk < (_round_up(size, PAGE_SIZE) / PAGE_SIZE); blk++) {
                next_addr = sm_get_next_addr_by_cur_index(sb, &rn->ix, blk);
                prev_addr = sm_get_prev_addr_by_cur_index(sb, &rn->ix, blk);
                addr = sm_get_cur_addr_by_cur_index(sb, &rn->ix, blk);

                if (addr == 0) {
                    hk_dbgv("hole blk %llu\n", blk);
                    continue;
                }

                prev_hdr = prev_addr == 0 ? &pi->root : sm_get_hdr_by_addr(sb, prev_addr);
                next_hdr = next_addr == 0 ? &pi->root : sm_get_hdr_by_addr(sb, next_addr);
                hdr = sm_get_hdr_by_addr(sb, addr);

                sm_insert_hdr(sb, prev_hdr, hdr, next_hdr);
            }
        } else {
            // No need check this pi again.
            hk_invalidate_data_blocks(sb, &rn->ix, rn->size, 0);
        }
        // Clean up recovery node
        rb_erase(&rn->rbnode, &recovery_table);
        linix_destroy(&rn->ix);
        hk_free_hk_recovery_node(rn);
    }
    hk_info("recovery table count %d\n", count);
    destroy_hk_recovery_node_cache();

    /* Step 5: Restore Allocator */
    for (cpuid = 0; cpuid < sbi->num_layout; cpuid++) {
        layout = &sbi->layouts[cpuid];
        // the last gap blocks in gap_tree must be removed
        node = rb_entry_safe(rb_last(&layout->gaps_tree.rb_root), struct hk_range_node, rbnode);
        if (node) {
            use_layout(layout);
            hk_release_layout(sb, cpuid, node->range_high - node->range_low + 1, false);
            unuse_layout(layout);
            hk_range_delete_range_node(&layout->gaps_tree, node);
        }
    }

    hk_info("Failure recovery done\n");
    
    /* Now, HUNTER is OK to receive next requets */
    return 0;
}

static bool hk_try_normal_recovery(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_super_block *super = sbi->hk_sb;
    struct hk_layout_info *layout;
    bool is_failure = false;
    u64 blk = 0, addr = 0;
    struct hk_header *hdr;
    int cpuid;

    if (le32_to_cpu(super->s_valid_umount) == HK_VALID_UMOUNT) {
        hk_dbgv("normal recovery\n");
        sbi->tstamp = le64_to_cpu(super->s_tstamp);
        for (cpuid = 0; cpuid < sbi->num_layout; cpuid++) {
            layout = &sbi->layouts[cpuid];
            layout->atomic_counter = le64_to_cpu(super->s_layout->s_atomic_counter);

            layout->ind.free_blks = le64_to_cpu(super->s_layout->s_ind.free_blks);
            layout->ind.invalid_blks = le64_to_cpu(super->s_layout->s_ind.invalid_blks);
            layout->ind.prep_blks = le64_to_cpu(super->s_layout->s_ind.prep_blks);
            layout->ind.valid_blks = le64_to_cpu(super->s_layout->s_ind.valid_blks);
            layout->ind.total_blks = le64_to_cpu(super->s_layout->s_ind.total_blks);

            /* Rebuilding Gap Tree */
            traverse_layout_blks(addr, layout)
            {
                hdr = sm_get_hdr_by_addr(sb, addr);
                if (hdr->valid != 1) {
                    blk = hk_get_dblk_by_addr(sbi, addr);
                    hk_range_insert_range(&layout->gaps_tree, blk, blk);
                    layout->num_gaps_indram++;
                }
            }
        }
        goto out;
    } else {
        is_failure = true;
    }
out:
    return is_failure;
}

int hk_recovery(struct super_block *sb)
{
    bool is_failure = false;

    INIT_TIMING(start);

    hk_dbgv("%s\n", __func__);

    HK_START_TIMING(recovery_t, start);

    is_failure = hk_try_normal_recovery(sb);

    if (!is_failure) {
        hk_dbg("HUNTER: Normal shutdown\n");
    } else {
        hk_failure_recovery(sb);
    }

    HK_END_TIMING(recovery_t, start);

    return 0;
}
