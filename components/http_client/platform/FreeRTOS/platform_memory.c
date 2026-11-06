/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-04-27 16:32:58
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_memory.h"
#include "http_log.h"
#include "string.h"
#include <stdint.h>
#include "FreeRTOS.h"

void *platform_memory_alloc(size_t size)
{
    void *ptr = pvPortMalloc(size);

    if (ptr == NULL) {
        HTTP_LOG_E("alloc %lu bytes failed", (unsigned long)size);
        return NULL;
    }

    if (size != 0U) {
        memset(ptr, 0, size);
    }

    return ptr;
}

void *platform_memory_calloc(size_t num, size_t size)
{
    size_t total;
    void *ptr;

    if ((num != 0U) && (size > (SIZE_MAX / num))) {
        HTTP_LOG_E("calloc overflow: num %lu size %lu",
                   (unsigned long)num, (unsigned long)size);
        return NULL;
    }

    total = num * size;
    ptr = pvPortMalloc(total);
    if (ptr == NULL) {
        HTTP_LOG_E("calloc %lu x %lu bytes failed",
                   (unsigned long)num, (unsigned long)size);
        return NULL;
    }

    if (total != 0U) {
        memset(ptr, 0, total);
    }

    return ptr;
}

void platform_memory_free(void *ptr)
{
    if (ptr != NULL) {
        vPortFree(ptr);
    }
}

void *platform_memory_realloc(void *ptr, size_t size)
{
    void *new_ptr;

    if (ptr == NULL) {
        new_ptr = pvPortMalloc(size);
    } else {
        new_ptr = pvPortReMalloc(ptr, size);
    }

    if ((new_ptr == NULL) && (size != 0U)) {
        HTTP_LOG_E("realloc to %lu bytes failed", (unsigned long)size);
    }

    return new_ptr;
}
