#include "ext_list.h"
#include "hunter.h"

struct hk_inf_queue {
    struct list_head queue;
    spinlock_t lock;
    int num;
};

static inline void hk_inf_queue_init(struct hk_inf_queue *queue)
{
    INIT_LIST_HEAD(&queue->queue);
    queue->num = 0;
    spin_lock_init(&queue->lock);
}

static inline void hk_inf_queue_destory(struct hk_inf_queue *queue, void (*destor)(void *node))
{
    struct list_head *pos = NULL, *n = NULL;

    spin_lock(&queue->lock);
    list_for_each_safe(pos, n, &queue->queue)
    {
        list_del(pos);
        queue->num--;
        if (destor)
            destor(pos);
    }
    spin_unlock(&queue->lock);
}

static inline void hk_inf_queue_modify(struct hk_inf_queue *queue, void (*callback)(void *node))
{
    struct list_head *pos = NULL;

    spin_lock(&queue->lock);
    list_for_each(pos, &queue->queue)
    {
        if (callback)
            callback(pos);
    }
    spin_unlock(&queue->lock);
}

static inline int hk_inf_queue_length(struct hk_inf_queue *queue)
{
    spin_lock(&queue->lock);
    int num = queue->num;
    spin_unlock(&queue->lock);
    return num;
}

static inline void hk_inf_queue_add_tail_locked(struct hk_inf_queue *queue, struct list_head *node)
{
    spin_lock(&queue->lock);
    list_add_tail(node, &queue->queue);
    queue->num++;
    spin_unlock(&queue->lock);
}

static inline int hk_inf_queue_try_pop_front_batch_locked(struct hk_inf_queue *queue, struct list_head *popped_head, int batch_num)
{   
    struct list_head *pos = NULL;
    int pop_num = 0, i = 0;

    spin_lock(&queue->lock);
    pop_num = min(batch_num, queue->num);
    
    if (pop_num == 0) {
        spin_unlock(&queue->lock);
        return 0;
    }

    list_for_each(pos, &queue->queue)
    {
        if (i >= pop_num)
            break;
        i++;
    }

    // NOTE: popped_head is a circular doubly linked list,
    //       while queue->queue remains a non-circular
    //       doubly linked list
    list_cut_position(popped_head, &queue->queue, pos->prev);

    queue->num -= pop_num;
    spin_unlock(&queue->lock);

    return pop_num;
}
