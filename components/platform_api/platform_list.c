#include "platform_list.h"

void platform_list_init(platform_list_t *list)
{
    list->next = list;
    list->prev = list;
}

static void __platform_list_add(platform_list_t *new, platform_list_t *prev, platform_list_t *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

void platform_list_add(platform_list_t *new, platform_list_t *head)
{
    __platform_list_add(new, head, head->next);
}

void platform_list_add_tail(platform_list_t *new, platform_list_t *head)
{
    __platform_list_add(new, head->prev, head);
}

static void __platform_list_del(platform_list_t *prev, platform_list_t *next)
{
    next->prev = prev;
    prev->next = next;
}

static void __platform_list_del_entry(platform_list_t *entry)
{
    __platform_list_del(entry->prev, entry->next);
}

void platform_list_del(platform_list_t *entry)
{
    __platform_list_del_entry(entry);
}

void platform_list_del_init(platform_list_t *entry)
{
    __platform_list_del_entry(entry);
    platform_list_init(entry);
}

void platform_list_replace(platform_list_t *old, platform_list_t *new)
{
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;
}

void platform_list_replace_init(platform_list_t *old, platform_list_t *new)
{
    platform_list_replace(old, new);
    platform_list_init(old);
}

void platform_list_move(platform_list_t *list, platform_list_t *head)
{
    __platform_list_del_entry(list);
    platform_list_add(list, head);
}

void platform_list_move_tail(platform_list_t *list, platform_list_t *head)
{
    __platform_list_del_entry(list);
    platform_list_add_tail(list, head);
}

int platform_list_is_empty(platform_list_t *head)
{
    return head->next == head;
}