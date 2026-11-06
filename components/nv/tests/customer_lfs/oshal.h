#ifndef TEST_OSHAL_H
#define TEST_OSHAL_H
#include <stddef.h>
#include <stdio.h>
typedef void *os_mutex_handle_t;
void *os_mutex_create(void);
int os_scheduler_lock(void);
int os_scheduler_unlock(void);
int os_mutex_destroy(void *);
int os_mutex_lock(void *, size_t);
int os_mutex_unlock(void *);
#define LM_APP 0
#define LL_ERR 0
#define os_printf(mod, level, ...) ((void)0)
static inline void test_log(const char *format, ...) { (void)format; }
#define system_printf(...) test_log(__VA_ARGS__)
#endif
