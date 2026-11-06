#ifndef CI1302_TEST_OSHAL_H
#define CI1302_TEST_OSHAL_H
#include <stddef.h>
#include <stdlib.h>
#define os_malloc malloc
#define os_free free
#define WAIT_FOREVER 0xffffffffU
#define LM_CMD 0
#define LM_APP 0
#define LL_INFO 0
typedef void *os_mutex_handle_t;
typedef void (*task_entry_t)(void *);
os_mutex_handle_t os_mutex_create(void);
int os_mutex_destroy(os_mutex_handle_t handle);
int os_mutex_lock(os_mutex_handle_t handle, unsigned int timeout);
int os_mutex_unlock(os_mutex_handle_t handle);
int os_task_create(const char *, int, int, task_entry_t, void *);
int os_task_delete(int);
int os_task_get_running_handle(void);
void os_msleep(unsigned int ms);
int os_printf(int, int, const char *, ...);
#endif
