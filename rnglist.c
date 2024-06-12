/*
 * HUNTER range list tools.
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

#include "hunter.h"

#define hk_traverse_tree(tree, temp, node) \
    for (temp = rb_first_cached(tree), node = rb_entry(temp, struct hk_range_node, rbnode); temp; temp = rb_next(temp), node = rb_entry(temp, struct hk_range_node, rbnode))

static inline int hk_rbtree_compare_range_node(struct hk_range_node *curr, unsigned long key)
{
    return key < curr->range_low ? -1 : (key == curr->range_low ? 0 : 1);
}

int hk_range_delete_range_node(struct rb_root_cached *tree, struct hk_range_node *node)
{
    rb_erase_cached(&node->rbnode, tree);
    hk_free_hk_range_node(node);
    return 0;
}

int hk_range_insert_range_node(struct rb_root_cached *tree, struct hk_range_node *new_node)
{
    struct hk_range_node *curr;
    struct rb_node **temp, *parent;
    int compVal;
    bool left_most = true;

    temp = &(tree->rb_root.rb_node);
    parent = NULL;

    while (*temp) {
        curr = container_of(*temp, struct hk_range_node, rbnode);
        compVal = hk_rbtree_compare_range_node(curr, new_node->range_low);
        parent = *temp;

        if (compVal == -1) {
            temp = &((*temp)->rb_left);
        } else if (compVal == 1) {
            temp = &((*temp)->rb_right);
            left_most = false;
        } else {
            hk_dbg("entry %lu - %lu already exists: "
                   "%lu - %lu\n",
                   new_node->range_low,
                   new_node->range_high, curr->range_low, curr->range_high);
            return -EINVAL;
        }
    }

    rb_link_node(&new_node->rbnode, parent, temp);
    rb_insert_color_cached(&new_node->rbnode, tree, left_most);

    return 0;
}

int hk_find_range_node(struct rb_root_cached *tree, unsigned long key, struct hk_range_node **ret_node)
{
    struct hk_range_node *curr = NULL;
    struct rb_node *temp;
    int compVal;
    int ret = 0;

    temp = tree->rb_root.rb_node;

    while (temp) {
        curr = container_of(temp, struct hk_range_node, rbnode);
        compVal = hk_rbtree_compare_range_node(curr, key);

        if (compVal == -1) {
            temp = temp->rb_left;
        } else if (compVal == 1) {
            temp = temp->rb_right;
        } else {
            ret = 1;
            break;
        }
    }

    *ret_node = curr;
    return ret;
}

int hk_find_free_slot(struct rb_root_cached *tree, unsigned long range_low,
                      unsigned long range_high, struct hk_range_node **prev,
                      struct hk_range_node **next)
{
    struct hk_range_node *ret_node = NULL;
    struct rb_node *tmp;
    int check_prev = 0, check_next = 0;
    int ret;

    ret = hk_find_range_node(tree, range_low, &ret_node);
    if (ret) {
        hk_dbg("ERROR: %lu - %lu already in free list\n",
               range_low, range_high);
        return -EINVAL;
    }

    if (!ret_node) {
        *prev = *next = NULL;
    } else if (ret_node->range_high < range_low) {
        *prev = ret_node;
        tmp = rb_next(&ret_node->rbnode);
        if (tmp) {
            *next = container_of(tmp, struct hk_range_node, rbnode);
            check_next = 1;
        } else {
            *next = NULL;
        }
    } else if (ret_node->range_low > range_high) {
        *next = ret_node;
        tmp = rb_prev(&ret_node->rbnode);
        if (tmp) {
            *prev = container_of(tmp, struct hk_range_node, rbnode);
            check_prev = 1;
        } else {
            *prev = NULL;
        }
    } else {
        hk_dbg("ERROR: %lu - %lu overlaps with existing "
               "node %lu - %lu\n",
               range_low, range_high, ret_node->range_low,
               ret_node->range_high);
        return -EINVAL;
    }

    return 0;
}

int hk_range_insert_range(struct rb_root_cached *tree, unsigned long range_low, unsigned long range_high)
{
    struct hk_range_node *next = NULL;
    struct hk_range_node *prev = NULL;
    bool inserted = false;
    int ret = 0;

    ret = hk_find_free_slot(tree, range_low, range_high, &prev, &next);
    if (ret) {
        hk_dbg("ERROR: %lu - %lu already in free list\n",
               range_low, range_high);
        return -EINVAL;
    }

    u64 prev_rng_low = prev ? prev->range_low : 0;
    u64 prev_rng_high = prev ? prev->range_high : 0;
    u64 next_rng_low = next ? next->range_low : 0;
    u64 next_rng_high = next ? next->range_high : 0;

    if (prev && next && (range_low == prev_rng_high + 1) &&
        (range_high + 1 == next_rng_low)) {
        /* fits the hole */
        rb_erase_cached(&next->rbnode, tree);
        prev->range_high = next->range_high;
        hk_free_hk_range_node(next);
        inserted = true;
    } else if (prev && (range_low == prev_rng_high + 1)) {
        /* Aligns left */
        prev->range_high = range_high;
        inserted = true;
    } else if (next && (range_high + 1 == next_rng_low)) {
        /* Aligns right */
        next->range_low = range_low;
        inserted = true;
    }

    if (!inserted) {
        struct hk_range_node *new_node;
        new_node = hk_alloc_hk_range_node();
        if (!new_node) {
            hk_dbg("ERROR: failed to allocate new node\n");
            return -ENOMEM;
        }
        new_node->range_low = range_low;
        new_node->range_high = range_high;
        ret = hk_range_insert_range_node(tree, new_node);
    }

    return ret;
}

// num is the request number passed in and the allocated number returned
unsigned long hk_range_pop(struct rb_root_cached *tree, unsigned long *num)
{
    struct rb_node *temp;
    struct hk_range_node *curr = NULL;
    unsigned long ret = 0;
    u64 allocated = 0;

    hk_traverse_tree(tree, temp, curr)
    {
        allocated = curr->range_high - curr->range_low + 1 >= *num ? *num : curr->range_high - curr->range_low + 1;
        break;
    }

    if (curr) {
        ret = curr->range_low;
        curr->range_low += allocated;
        *num = allocated;
        if (curr->range_low > curr->range_high) {
            rb_erase_cached(&curr->rbnode, tree);
            hk_free_hk_range_node(curr);
        }
    } else {
        *num = 0;
    }

    return ret;
}

void hk_range_free_all(struct rb_root_cached *tree)
{
    struct rb_node *temp;
    struct hk_range_node *curr;

    temp = rb_first_cached(tree);
    while (temp) {
        curr = container_of(temp, struct hk_range_node, rbnode);
        temp = rb_next(temp);
        rb_erase_cached(&curr->rbnode, tree);
        hk_free_hk_range_node(curr);
    }
}