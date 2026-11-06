#include <stdlib.h>
#include <string.h>

#include "mqtt_queue.h"
#include "mqtt_outbox.h"

typedef struct outbox_item {
    char *buffer;
    int len;
    int msg_id;
    int msg_type;
    int msg_qos;
    outbox_tick_t tick;
    pending_state_t pending;
    STAILQ_ENTRY(outbox_item) next;
} outbox_item_t;

STAILQ_HEAD(outbox_list_t, outbox_item);

outbox_handle_t outbox_init(void)
{
    outbox_handle_t outbox = mqtt_calloc(1, sizeof(struct outbox_list_t));
    MEM_CHECK(outbox, return NULL);
    STAILQ_INIT(outbox);
    return outbox;
}

outbox_item_handle_t outbox_enqueue(outbox_handle_t outbox, outbox_message_handle_t message, outbox_tick_t tick)
{
    if (outbox == NULL) {
        return NULL;
    }
    outbox_item_handle_t item = mqtt_calloc(1, sizeof(outbox_item_t));
    MEM_CHECK(item, return NULL);
    item->msg_id   = message->msg_id;
    item->msg_type = message->msg_type;
    item->msg_qos  = message->msg_qos;
    item->tick     = tick;
    item->len      = message->len + message->remaining_len;
    item->pending  = QUEUED;
    item->buffer   = mqtt_malloc(message->len + message->remaining_len);
    MEM_CHECK(item->buffer, {
        if (item != NULL) {
            mqtt_free(item);
            item = NULL;
        }
        return NULL;
    });
    memcpy(item->buffer, message->data, message->len);
    if (message->remaining_data) {
        memcpy(item->buffer + message->len, message->remaining_data, message->remaining_len);
    }
    STAILQ_INSERT_TAIL(outbox, item, next);
    os_printf(LM_APP, LL_INFO, "MQTT:ENQUEUE msgid=%d, msg_type=%d, len=%d, size=%d\r\n", message->msg_id, message->msg_type, message->len, outbox_get_size(outbox));
    return item;
}

outbox_item_handle_t outbox_get(outbox_handle_t outbox, int msg_id)
{
    outbox_item_handle_t item;
    if (outbox == NULL) {
        return NULL;
    }
    STAILQ_FOREACH(item, outbox, next) {
        if (item->msg_id == msg_id) {
            return item;
        }
    }
    return NULL;
}

outbox_item_handle_t outbox_dequeue(outbox_handle_t outbox, pending_state_t pending, outbox_tick_t *tick)
{
    outbox_item_handle_t item;
    if (outbox == NULL) {
        return NULL;
    }
    STAILQ_FOREACH(item, outbox, next) {
        if (item->pending == pending) {
            if (tick) {
                *tick = item->tick;
            }
            return item;
        }
    }
    return NULL;
}

int outbox_delete_item(outbox_handle_t outbox, outbox_item_handle_t item_to_delete)
{
    outbox_item_handle_t item;
    if (outbox == NULL) {
        return -1;
    }
    STAILQ_FOREACH(item, outbox, next) {
        if (item == item_to_delete) {
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                os_free(item->buffer);
                item->buffer = NULL;
            }
            if (item != NULL) {
                os_free(item);
                item = NULL;
            }
            return 0;
        }
    }
    return -1;
}

uint8_t *outbox_item_get_data(outbox_item_handle_t item,  size_t *len, uint16_t *msg_id, int *msg_type, int *qos)
{
    if (item) {
        *len = item->len;
        *msg_id = item->msg_id;
        *msg_type = item->msg_type;
        *qos = item->msg_qos;
        return (uint8_t *)item->buffer;
    }
    return NULL;
}

int outbox_delete(outbox_handle_t outbox, int msg_id, int msg_type)
{
    outbox_item_handle_t item, tmp;
    if (outbox == NULL) {
        return -1;
    }
    STAILQ_FOREACH_SAFE(item, outbox, next, tmp) {
        if (item->msg_id == msg_id && (0xFF & (item->msg_type)) == msg_type) {
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                mqtt_free(item->buffer);
                item->buffer = NULL;
            }
            if (item != NULL) {
                mqtt_free(item);
                item = NULL;
            }
            os_printf(LM_APP, LL_INFO, "MQTT:DELETED msgid=%d, msg_type=%d, remain size=%d\r\n", msg_id, msg_type, outbox_get_size(outbox));
            return 0;
        }
    }
    return -1;
}

void outbox_delete_msgid(outbox_handle_t outbox, int msg_id)
{
    outbox_item_handle_t item, tmp;
    if (outbox == NULL) {
        return;
    }
    STAILQ_FOREACH_SAFE(item, outbox, next, tmp) {
        if (item->msg_id == msg_id) {
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                mqtt_free(item->buffer);
                item->buffer = NULL;
            }
            if (item != NULL) {
                mqtt_free(item);
                item = NULL;
            }
        }
    }
}

int outbox_set_pending(outbox_handle_t outbox, int msg_id, pending_state_t pending)
{
    if (outbox == NULL) {
        return -1;
    }
    outbox_item_handle_t item = outbox_get(outbox, msg_id);
    if (item) {
        item->pending = pending;
        return 0;
    }
    return -1;
}

pending_state_t outbox_item_get_pending(outbox_item_handle_t item)
{
    if (item) {
        return item->pending;
    }
    return QUEUED;
}

int outbox_set_tick(outbox_handle_t outbox, int msg_id, outbox_tick_t tick)
{
    if (outbox == NULL) {
        return -1;
    }
    outbox_item_handle_t item = outbox_get(outbox, msg_id);
    if (item) {
        item->tick = tick;
        return 0;
    }
    return -1;
}

void outbox_delete_msgtype(outbox_handle_t outbox, int msg_type)
{
    outbox_item_handle_t item, tmp;
    if (outbox == NULL) {
        return;
    }
    STAILQ_FOREACH_SAFE(item, outbox, next, tmp) {
        if (item->msg_type == msg_type) {
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                mqtt_free(item->buffer);
                item->buffer = NULL;
            }
            if (item != NULL) {
                mqtt_free(item);
                item = NULL;
            }
        }
    }
}

int outbox_delete_single_expired(outbox_handle_t outbox, outbox_tick_t current_tick, outbox_tick_t timeout)
{
    int msg_id = -1;
    outbox_item_handle_t item;
    if (outbox == NULL) {
        return msg_id;
    }
    STAILQ_FOREACH(item, outbox, next) {
        if (current_tick - item->tick > timeout) {
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                os_free(item->buffer);
                item->buffer = NULL;
            }
            msg_id = item->msg_id;
            if (item != NULL) {
                os_free(item);
                item = NULL;
            }
            return msg_id;
        }
    }
    return msg_id;
}

int outbox_delete_expired(outbox_handle_t outbox, outbox_tick_t current_tick, outbox_tick_t timeout)
{
    int deleted_items = 0;
    outbox_item_handle_t item, tmp;
    if (outbox == NULL) {
        return deleted_items;
    }
    STAILQ_FOREACH_SAFE(item, outbox, next, tmp) {
        if (current_tick - item->tick > timeout) { // consider overflow
            STAILQ_REMOVE(outbox, item, outbox_item, next);
            if (item->buffer != NULL) {
                mqtt_free(item->buffer);
                item->buffer = NULL;
            }
            if (item != NULL) {
                mqtt_free(item);
                item = NULL;
            }
            deleted_items ++;
        }
    }
    return deleted_items;
}

int outbox_get_size(outbox_handle_t outbox)
{
    int siz = 0;
    outbox_item_handle_t item;
    if (outbox == NULL) {
        return siz;
    }
    STAILQ_FOREACH(item, outbox, next) {
        // Suppressing "use after free" warning as this could happen only if queue is in inconsistent state
        // which never happens if STAILQ interface used
        siz += item->len; // NOLINT(clang-analyzer-unix.Malloc)
    }
    return siz;
}

void outbox_delete_all_items(outbox_handle_t outbox)
{
    outbox_item_handle_t item, tmp;
    if (outbox == NULL) {
        return;
    }
    STAILQ_FOREACH_SAFE(item, outbox, next, tmp) {
        STAILQ_REMOVE(outbox, item, outbox_item, next);
        if (item->buffer != NULL) {
            mqtt_free(item->buffer);
            item->buffer = NULL;
        }
        if (item != NULL) {
            mqtt_free(item);
            item = NULL;
        }
    }
}

void outbox_destroy(outbox_handle_t outbox)
{
    outbox_delete_all_items(outbox);
    if(outbox != NULL) {
        mqtt_free(outbox);
        outbox = NULL;
    }
}
