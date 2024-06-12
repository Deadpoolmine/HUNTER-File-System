#ifndef _HK_NAMEI_H
#define _HK_NAMEI_H

#include "hunter.h"

struct hk_dentry {
	u8	    name_len;		        /* length of the dentry name */
	u8	    valid;		            /* Invalid now? */
	__le16	links_count;
	__le32	mtime;			        /* For both mtime and ctime */
	__le64	ino;			        /* inode no pointed to by this entry */
    __le64  tstamp;					/* FIXME: tstamp should be used to append */
    //! We don't need this now
	__le32	csum;			        /* entry checksum */
	u8	    name[HK_NAME_LEN + 1];	/* File name */
} __attribute((__packed__));

static_assert(sizeof(struct hk_dentry) == 128, "sizeof(struct hk_dentry) != 128");

struct hk_dentry_info {
	struct hlist_node node;
	unsigned long hash;
	struct hk_dentry *direntry;	
};

#define MAX_DENTRY_PER_BLK (HK_PBLK_SZ / sizeof(struct hk_dentry))

#endif /* _HK_NAMEI_H */