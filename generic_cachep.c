/*
 * HUNTER Generic Cache pool Helper.
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

DEFINE_GENERIC_CACHEP(hk_range_node);
DEFINE_GENERIC_CACHEP(hk_dentry_info);
DEFINE_GENERIC_CACHEP(hk_inode_info_header);

DEFINE_GENERIC_CACHEP(hk_cmt_data_info);
DEFINE_GENERIC_CACHEP(hk_cmt_new_inode_info);
DEFINE_GENERIC_CACHEP(hk_cmt_unlink_inode_info);
DEFINE_GENERIC_CACHEP(hk_cmt_delete_inode_info);
DEFINE_GENERIC_CACHEP(hk_cmt_close_info);

DEFINE_GENERIC_CACHEP(hk_cmt_node);
DEFINE_GENERIC_CACHEP(hk_cmt_node_ref);

DEFINE_GENERIC_CACHEP(hk_recovery_node)

