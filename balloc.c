/*
 * HUNTER block allocation routines.
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

u64 get_version(struct hk_sb_info *sbi)
{
    u64 tstamp;
    spin_lock(&sbi->ts_lock);
    tstamp = sbi->tstamp;
    spin_unlock(&sbi->ts_lock);
    return tstamp;
}

int up_version(struct hk_sb_info *sbi)
{
    spin_lock(&sbi->ts_lock);
    sbi->tstamp++;
    spin_unlock(&sbi->ts_lock);
    return 0;
}

int ind_init(struct super_block *sb, int cpuid, u64 layout_blks)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout = &sbi->layouts[cpuid];

    layout->ind.invalid_blks = 0;
    layout->ind.valid_blks = 0;
    layout->ind.prep_blks = 0;
    layout->ind.free_blks = layout_blks;
    layout->ind.total_blks = layout_blks;

    return 0;
}

/* Infinite state machine */
int ind_update(struct hk_indicator *ind, enum hk_ind_upt_type type, u64 blks)
{
    struct hk_layout_info *layout = container_of(ind, struct hk_layout_info, ind);

    if (!mutex_is_locked(&layout->layout_lock)) {
        hk_info("%s: layout_lock is not locked\n", __func__);
        BUG_ON(1);
    }

    switch (type) {
    case VALIDATE_BLK:
        ind->valid_blks++;
        ind->prep_blks--;
        break;
    case INVALIDATE_BLK:
        ind->invalid_blks++;
        ind->valid_blks--;
        break;
    case PREP_LAYOUT_APPEND:
        ind->free_blks -= blks;
        ind->prep_blks += blks;
        break;
    case PREP_LAYOUT_GAP:
        ind->invalid_blks--;
        ind->prep_blks++;
        break;
    case PREP_LAYOUT_REMOVE:
        ind->invalid_blks += blks;
        ind->prep_blks -= blks;
        break;
    case FREE_LAYOUT:
        ind->invalid_blks -= blks;
        ind->free_blks += blks;
        break;
    default:
        break;
    }

    if (ind->invalid_blks + ind->valid_blks + ind->prep_blks != layout->atomic_counter / HK_PBLK_SZ && ind->free_blks + layout->atomic_counter / HK_PBLK_SZ != layout->layout_blks) {
        hk_info("Wrong Calculations for Indicator %d!\n", layout->cpuid);
        hk_dump_layout_info(layout);
        BUG_ON(1);
    }

    if (ind->invalid_blks & 0x1000000000000000) {
        hk_info("Invalid Blks Overflow!\n");
        BUG_ON(1);
    }

    if (ind->prep_blks & 0x1000000000000000) {
        hk_info("Prep Blks Overflow!\n");
        hk_dump_layout_info(layout);
        hk_print_timing();
        // BUG_ON(1);
    }

    return 0;
}

u64 hk_prepare_gap_in_layout(struct super_block *sb, int cpuid)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout = &sbi->layouts[cpuid];
    struct hk_header *hdr;
    unsigned long req_blks = 1;
    u64 blk;
    u64 addr;

    if (layout->num_gaps_indram == 0) {
        hk_info("%s: No more invalid blks in cpuid: %d \n", __func__, cpuid);
        return 0;
    }

    /* TODO: pop batched blocks */
    req_blks = 1;
    blk = hk_range_pop(&layout->gaps_tree, &req_blks);
    if (req_blks == 0) {
        hk_info("%s: Wrong Gaps %llu\n", __func__, blk);
        BUG_ON(1);
    }
    layout->num_gaps_indram--;

    addr = hk_get_addr_by_dblk(sbi, blk);

    return addr;
}

u64 hk_prepare_layout(struct super_block *sb, int cpuid, u64 blks, enum hk_layout_type type,
                      u64 *blks_prepared, bool zero)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout = &sbi->layouts[cpuid];
    u64 target_addr = layout->layout_start;
    u64 blk_start = 0;
    u64 blk_end = 0;
    unsigned long irq_flags = 0;

    if (unlikely(!mutex_is_locked(&layout->layout_lock))) {
        hk_info("%s: layout_lock is not locked\n", __func__);
        BUG_ON(1);
    }

    if (likely(type == LAYOUT_APPEND)) {
        up_version(sbi);

        hk_dbgv("%s: layout start @0x%llx, layout tail @0x%llx", __func__, target_addr, layout->atomic_counter);
        target_addr += layout->atomic_counter;

        if (unlikely(target_addr + (blks * HK_PBLK_SZ) >= layout->layout_end)) {
            blks = (layout->layout_end - target_addr) / HK_PBLK_SZ;
        }

        if (unlikely(blks <= 0)) {
            return 0;
        }

        layout->atomic_counter += (blks * HK_PBLK_SZ);
        if (blks_prepared != NULL) {
            *blks_prepared = blks;
        }

        /* The blks that has been prepared */
        ind_update(&layout->ind, PREP_LAYOUT_APPEND, blks);
    } else if (type == LAYOUT_GAP && blks == 1) {
        // TODO: Gap write
        up_version(sbi);
        target_addr = hk_prepare_gap_in_layout(sb, cpuid);

        if (target_addr == 0) {
            return 0;
        }
        
        blk_start = hk_get_dblk_by_addr(sbi, target_addr);
        hk_dbg("%s: prep gap: %lu", __func__, blk_start);

        ind_update(&layout->ind, PREP_LAYOUT_GAP, blks);

        return target_addr;
    } else {
        hk_dbgv("%s: not support args\n", __func__);
        return 0;
    }

    if (zero) {
        hk_memunlock_range(sb, (void *)target_addr, blks * HK_PBLK_SZ, &irq_flags);
        memset_nt((void *)target_addr, 0, blks * HK_PBLK_SZ);
        hk_memlock_range(sb, (void *)target_addr, blks * HK_PBLK_SZ, &irq_flags);
    }

#ifndef CONFIG_LAYOUT_TIGHT
    if (!IS_ALIGNED(TRANS_ADDR_TO_OFS(sbi, target_addr), HK_LBLK_SZ)) {
        hk_warn("%s: target_addr [%llu] is not aligned to BLOCK\n", __func__, TRANS_ADDR_TO_OFS(sbi, target_addr));
    }
#else
    if (!IS_ALIGNED(TRANS_ADDR_TO_OFS(sbi, target_addr), CACHELINE_SIZE)) {
        hk_warn("%s: target_addr [%llu] is not aligned to CACHELINE\n", __func__, TRANS_ADDR_TO_OFS(sbi, target_addr));
    }
#endif
    hk_dbgv("%s: prepare addr 0x%llx, virt addr start @0x%llx", __func__, target_addr, sbi->virt_addr);

    return target_addr;
}

int hk_prepare_layouts(struct super_block *sb, u32 blks, bool zero, struct hk_layout_preps *preps)
{
    struct hk_layout_info *layout;
    struct hk_indicator *ind;
    struct hk_sb_info *sbi = HK_SB(sb);
    int cpuid;
    int start_cpuid;
    u64 blks_prepared = 0;
    u64 target_addr;
    INIT_TIMING(alloc_time);
    HK_START_TIMING(new_blocks_t, alloc_time);

    preps->num_layout = 0;
    preps->is_enough_space = false;
    preps->idx = 0;

    start_cpuid = hk_get_cpuid(sb);
    cpuid = start_cpuid;

    for (; cpuid < sbi->num_layout; cpuid++) {
        layout = &sbi->layouts[cpuid];

        use_layout(layout);
        target_addr = hk_prepare_layout(sb, cpuid, blks, LAYOUT_APPEND, &blks_prepared, zero);
        unuse_layout(layout);

        if (target_addr == 0) {
            continue;
        }

        preps->preps[preps->num_layout].blks_prepared = blks_prepared;
        preps->preps[preps->num_layout].cpuid = cpuid;
        preps->preps[preps->num_layout].target_addr = target_addr;
        preps->preps[preps->num_layout].is_overflow = false;

        preps->num_layout++;

        blks -= blks_prepared;
        if (blks == 0) {
            preps->is_enough_space = true;
            HK_END_TIMING(new_blocks_t, alloc_time);
            return 0;
        }
    }

    for (cpuid = 0; cpuid < start_cpuid; cpuid++) {
        layout = &sbi->layouts[cpuid];

        use_layout(layout);
        target_addr = hk_prepare_layout(sb, cpuid, blks, LAYOUT_APPEND, &blks_prepared, zero);
        unuse_layout(layout);

        if (target_addr == 0) {
            continue;
        }

        preps->preps[preps->num_layout].blks_prepared = blks_prepared;
        preps->preps[preps->num_layout].cpuid = cpuid;
        preps->preps[preps->num_layout].target_addr = target_addr;
        preps->preps[preps->num_layout].is_overflow = false;

        preps->num_layout++;

        blks -= blks_prepared;
        if (blks == 0) {
            preps->is_enough_space = true;
            HK_END_TIMING(new_blocks_t, alloc_time);
            return 0;
        }
    }

    preps->is_enough_space = false;
    HK_END_TIMING(new_blocks_t, alloc_time);
    return 0;
}

void hk_trv_prepared_layouts_init(struct hk_layout_preps *preps)
{
    preps->idx = 0;
}

struct hk_layout_prep *hk_trv_prepared_layouts(struct super_block *sb, struct hk_layout_preps *preps)
{
    int i;
    int cpuid;
    struct hk_layout_prep *prep = NULL;
    u64 target_addr;

    if (preps->idx >= preps->num_layout) {
        return NULL;
    }

    prep = &preps->preps[preps->idx];
    preps->idx++;

    return prep;
}

/* same to hk_prepare_layouts: using LAYOUT_GAP instead */
void hk_prepare_gap(struct super_block *sb, bool zero, struct hk_layout_prep *prep)
{
    u64 target_addr = 0;
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout;
    int cpuid;
    int start_cpuid;

    start_cpuid = hk_get_cpuid(sb);
    cpuid = start_cpuid;

    prep->target_addr = 0;
    prep->blks_prepared = 0;
    prep->is_overflow = false;
    prep->cpuid = -1;

    for (; cpuid < sbi->num_layout; cpuid++) {
        layout = &sbi->layouts[cpuid];

        use_layout(layout);
        target_addr = hk_prepare_layout(sb, cpuid, 1, LAYOUT_GAP, NULL, zero);
        unuse_layout(layout);

        if (target_addr == 0) {
            continue;
        } else {
            prep->is_overflow = true;
            prep->cpuid = cpuid;
            prep->blks_prepared = 1;
            prep->target_addr = target_addr;
            return;
        }
    }

    for (cpuid = 0; cpuid < start_cpuid; cpuid++) {
        layout = &sbi->layouts[cpuid];

        use_layout(layout);
        target_addr = hk_prepare_layout(sb, cpuid, 1, LAYOUT_GAP, NULL, zero);
        unuse_layout(layout);

        if (target_addr == 0) {
            continue;
        } else {
            prep->is_overflow = true;
            prep->cpuid = cpuid;
            prep->blks_prepared = 1;
            prep->target_addr = target_addr;
            return;
        }
    }
}

int hk_release_layout(struct super_block *sb, int cpuid, u64 blks, bool rls_all)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout = &sbi->layouts[cpuid];

    if (rls_all) {
        layout->atomic_counter = 0;
        ind_init(sb, cpuid, layout->layout_blks);
    } else {
        layout->atomic_counter -= (blks * HK_PBLK_SZ);
        ind_update(&layout->ind, FREE_LAYOUT, blks);
    }

    return 0;
}

unsigned long hk_count_free_blocks(struct super_block *sb)
{
    struct hk_sb_info *sbi = HK_SB(sb);
    struct hk_layout_info *layout;
    unsigned long num_free_blocks = 0;
    int i;

    for (i = 0; i < sbi->cpus; i++) {
        layout = &sbi->layouts[i];
        use_layout(layout);
        // TODO: Change this to indicator
        // num_free_blocks += layout->layout_blks - layout->atomic_counter >> sb->s_blocksize_bits;
        num_free_blocks += (layout->ind.free_blks + layout->ind.invalid_blks);
        unuse_layout(layout);
    }

    return num_free_blocks;
}

int hk_layouts_init(struct hk_sb_info *sbi, int cpus)
{
    struct super_block *sb = sbi->sb;
    struct hk_layout_info *layout;
    int cpuid;
    u64 size_per_layout;
    u64 blks_per_layout;

    size_per_layout = _round_down(sbi->d_size / cpus, HK_PBLK_SZ);
    sbi->num_layout = cpus;
    sbi->layouts = (struct hk_layout_info *)kcalloc(cpus, sizeof(struct hk_layout_info), GFP_KERNEL);

    for (cpuid = 0; cpuid < cpus; cpuid++) {
        layout = &sbi->layouts[cpuid];
        layout->layout_start = sbi->d_addr + size_per_layout * cpuid;
        if (cpuid == cpus - 1) {
            size_per_layout = _round_down((sbi->d_size - cpuid * size_per_layout), HK_PBLK_SZ);
        }
        blks_per_layout = size_per_layout / HK_PBLK_SZ;
        layout->atomic_counter = 0;
        layout->cpuid = cpuid;
        layout->layout_blks = blks_per_layout;
        layout->layout_end = layout->layout_start + size_per_layout;
        layout->num_gaps_indram = 0;
        layout->gaps_tree = RB_ROOT_CACHED;
        ind_init(sb, cpuid, blks_per_layout);
        mutex_init(&layout->layout_lock);
        hk_dbgv("layout[%d]: 0x%llx-0x%llx, total_blks: %llu\n", cpuid, layout->layout_start, layout->layout_end, layout->layout_blks);
    }
    return 0;
}

int hk_layouts_free(struct hk_sb_info *sbi)
{
    struct hk_layout_info *layout;

    int cpuid;
    if (sbi->layouts) {
        for (cpuid = 0; cpuid < sbi->num_layout; cpuid++) {
            layout = &sbi->layouts[cpuid];

            hk_range_free_all(&layout->gaps_tree);
        }
        kfree(sbi->layouts);
    }
    return 0;
}