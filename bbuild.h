
#ifndef _HK_BBUILD_H_
#define _HK_BBUILD_H_

#include "hunter.h"

struct hk_recovery_node {
    struct rb_node rbnode;
    u64 ino;
    u64 tstamp;
    u64 size;
    u32 cmtime;
    struct linix ix;
};

#endif