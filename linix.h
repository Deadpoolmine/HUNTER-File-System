#ifndef _HK_LINIX_H
#define _HK_LINIX_H

#include "hunter.h"

struct linslot;

#define IX_SLOT_SZ sizeof(struct linslot)

struct linslot {
    u64 blk_addr;
};

struct linix {
    u64 num_slots;
    struct hk_sb_info *sbi;
    struct linslot *slots;
};

#endif /* _HK_LINIX_H */
