/*
 * A simple circular hash table implementation
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

#ifndef _HK_CHASH_H_
#define _HK_CHASH_H_

/* circular hash table implement */
#define DEFINE_CHASHTABLE(table, bits) \
    struct list_head table[1 << (bits)]

static void chash_init(struct list_head *table, int bits)
{
    int i;
    struct list_head *sentinal;
    for (i = 0; i < (1 << bits); i++)
    {
        sentinal = &table[i];
        sentinal->next = sentinal;
        sentinal->prev = sentinal;
    }
} 

static void chash_add_head(struct list_head *table, struct list_head *slot, int key)
{
    struct list_head *sentinal = &table[key];
    slot->next = sentinal->next;
    slot->prev = sentinal;
    sentinal->next->prev = slot;
    sentinal->next = slot;
}

static void chash_del(struct list_head *slot)
{
    slot->prev->next = slot->next;
    slot->next->prev = slot->prev;
}

static struct list_head *chash_last(struct list_head *table, int key)
{
    struct list_head *sentinal = &table[key];
    return sentinal->prev;
}

#define chash_is_sentinal(table, key, slot) \
    ((slot) == &(table)[key])

#define chash_for_each_possible(table, slot, key) \
    for (slot = (table)[key].next; slot != &(table)[key]; slot = slot->next)

#define chlist_entry(ptr, type, member) container_of(ptr, type, member)
#endif