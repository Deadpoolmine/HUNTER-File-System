/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple doubly linked list implementation (Non Circular).
 *
 */

static inline void INIT_LIST_HEAD_NC(struct list_head *list)
{
	list->next = NULL;
	list->prev = NULL;
}