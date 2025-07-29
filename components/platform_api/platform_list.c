#include "platform_list.h"

static void _platform_list_add(platform_list_t *node, platform_list_t *prev, platform_list_t *next)
{
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

static void _platform_list_del(platform_list_t *prev, platform_list_t *next)
{
    next->prev = prev;
    prev->next = next;
}

static void _platform_list_del_entry(platform_list_t *entry)
{
    _platform_list_del(entry->prev, entry->next);
}

void platform_list_init(platform_list_t *list)
{
    list->next = list;
    list->prev = list;
}

void platform_list_add(platform_list_t *node, platform_list_t *list)
{
    _platform_list_add(node, list, list->next);
}

void platform_list_add_tail(platform_list_t *node, platform_list_t *list)
{
    _platform_list_add(node, list->prev, list);
}

void platform_list_del(platform_list_t *entry)
{
    _platform_list_del(entry->prev, entry->next);
}

void platform_list_del_init(platform_list_t *entry)
{
    _platform_list_del_entry(entry);
    platform_list_init(entry);
}

void platform_list_move(platform_list_t *node, platform_list_t *list)
{
    _platform_list_del_entry(node);
    platform_list_add(node, list);
}

void platform_list_move_tail(platform_list_t *node, platform_list_t *list)
{
    _platform_list_del_entry(node);
    platform_list_add_tail(node, list);
}

int platform_list_is_empty(platform_list_t *list)
{
    return list->next == list;
}

