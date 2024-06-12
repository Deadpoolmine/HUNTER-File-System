#ifndef _HK_DW_H_
#define _HK_DW_H_

#include "hunter.h"

/* dynamic workload */
struct hk_dym_wkld
{
    struct mutex dw_lock;
    int hstart;
    int hend;
    size_t histories[HK_HISTORY_WINDOWS];
};

static inline void hk_dw_init(struct hk_dym_wkld *dw, size_t startup)
{
    int i;
    dw->hstart = 0;
    dw->hend = HK_HISTORY_WINDOWS - 1;
    mutex_init(&dw->dw_lock);
    for (i = 0; i < HK_HISTORY_WINDOWS; i++) {
        dw->histories[i] = startup;
    }
}

static inline void hk_dw_forward(struct hk_dym_wkld *dw, size_t value) 
{
    mutex_lock(&dw->dw_lock);
    dw->histories[dw->hstart] = value;
    dw->hstart = (dw->hstart + 1) % HK_HISTORY_WINDOWS;
    dw->hend = (dw->hend + 1) % HK_HISTORY_WINDOWS;
    mutex_unlock(&dw->dw_lock);
}

static inline size_t hk_dw_stat_avg(struct hk_dym_wkld *dw)
{
    int    i;
    size_t total = 0;
    size_t avg = 0;

    mutex_lock(&dw->dw_lock);
    for (i = 0; i < HK_HISTORY_WINDOWS; i++) {
        total += dw->histories[i];
    }
    avg = total / HK_HISTORY_WINDOWS;
    mutex_unlock(&dw->dw_lock);

    return avg;
}



#endif