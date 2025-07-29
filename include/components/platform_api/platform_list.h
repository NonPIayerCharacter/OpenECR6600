#ifndef __PLATFORM_LIST_H__
#define __PLATFORM_LIST_H__

#include <stddef.h>

typedef struct platform_list_node {
    struct platform_list_node *next;
    struct platform_list_node *prev;
} platform_list_t;

#define OFFSET_OF_FIELD(type, field) \
    ((size_t)&(((type *)0)->field))

#define CONTAINER_OF_FIELD(ptr, type, field) \
    ((type *)((unsigned char *)(ptr) - OFFSET_OF_FIELD(type, field)))

#define PLATFORM_LIST_NODE(node) \
    { &(node), &(node) }

#define PLATFORM_LIST_DEFINE(list) \
    platform_list_t list = { &(list), &(list) }

#define PLATFORM_LIST_ENTRY(list, type, field) \
    CONTAINER_OF_FIELD(list, type, field)

#define PLATFORM_LIST_FIRST_ENTRY(list, type, field) \
    PLATFORM_LIST_ENTRY((list)->next, type, field)

#define PLATFORM_LIST_FIRST_ENTRY_OR_NULL(list, type, field) \
    (platform_list_is_empty(list) ? NULL : PLATFORM_LIST_FIRST_ENTRY(list, type, field))

#define PLATFORM_LIST_FOR_EACH(curr, list) \
    for (curr = (list)->next; curr != (list); curr = curr->next)

#define PLATFORM_LIST_FOR_EACH_PREV(curr, list) \
    for (curr = (list)->prev; curr != (list); curr = curr->prev)

#define PLATFORM_LIST_FOR_EACH_SAFE(curr, next, list) \
    for (curr = (list)->next, next = curr->next; curr != (list); \
        curr = next, next = curr->next)

#define PLATFORM_LIST_FOR_EACH_PREV_SAFE(curr, next, list) \
    for (curr = (list)->prev, next = curr->prev; \
        curr != (list); \
        curr = next, next = curr->prev)

#define PLATFORM_LIST_FOR_EACH_ENTRY(pos, head, member) \
    for (pos = PLATFORM_LIST_ENTRY((head)->next, typeof(*pos), member); \
        &pos->member != (head); \
        pos = PLATFORM_LIST_ENTRY(pos->member.next, typeof(*pos), member))

#define PLATFORM_LIST_FOR_EACH_ENTRY_SAFE(pos, npos, head, member) \
    for (pos = PLATFORM_LIST_ENTRY((head)->next, typeof(*pos), member), \
        npos = PLATFORM_LIST_ENTRY(pos->member.next, typeof(*pos), member); \
        &pos->member != (head); \
        pos = npos, npos = PLATFORM_LIST_ENTRY(npos->member.next, typeof(*npos), member))


void platform_list_init(platform_list_t *list);
void platform_list_add(platform_list_t *node, platform_list_t *list);
void platform_list_add_tail(platform_list_t *node, platform_list_t *list);
void platform_list_del(platform_list_t *entry);
void platform_list_del_init(platform_list_t *entry);
void platform_list_move(platform_list_t *node, platform_list_t *list);
void platform_list_move_tail(platform_list_t *node, platform_list_t *list);
int platform_list_is_empty(platform_list_t *list);

#endif /* _LIST_H_ */


