/**
 * @file cmd_system_debug.c
 * @brief This is a brief description
 * @details This is the detail description
 * @author liuyong
 * @date 2021-6-9
 * @version V0.1
 * @par Copyright by http://eswin.com/.
 * @par History 1:
 *      Date:
 *      Version:
 *      Author:
 *      Modification:
 *
 * @par History 2:
 */


/*--------------------------------------------------------------------------
*												Include files
--------------------------------------------------------------------------*/
#include "cli.h"
#include "debug_core.h"
#include "pit.h"

/*--------------------------------------------------------------------------
* 	                                           	Local Macros
--------------------------------------------------------------------------*/
/** Description of the macro */

/*--------------------------------------------------------------------------
* 	                                           	Local Types
--------------------------------------------------------------------------*/
/**
 * @brief The brief description
 * @details The detail description
 */

/*--------------------------------------------------------------------------
* 	                                           	Local Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                           	Local Function Prototypes
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Global Constants
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Global Variables
--------------------------------------------------------------------------*/
/**  Description of global variable  */

/*--------------------------------------------------------------------------
* 	                                          	Global Function Prototypes
--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
* 	                                          	Function Definitions
--------------------------------------------------------------------------*/
#if defined(CONFIG_HEAP_DEBUG)
int system_heap_print(int argc, char *argv[])
{

	if(argc>1)
	{
		heap_print(argv[1]); //pcTaskName
	}
	else
	{
		heap_print(NULL);
	}

	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(os_debug, heaptrace, system_heap_print, "heap stats", "os_debug heaptrace [NONE]/[task name]");
#endif



#if defined(CONFIG_RUNTIME_DEBUG)
int system_runtime_print(int argc, char *argv[])
{
	runtime_print(argc);  //runtime_init

	return CMD_RET_SUCCESS;
}
CLI_SUBCMD(os_debug, cpu, system_runtime_print, "cpu percentage", "os_debug cpu [NONE]/[reset]");
#endif //CONFIG_RUNTIME_DEBUG



#if defined(CONFIG_TASK_IRQ_RUN_NUM)
extern
int task_irq_num_fun( int argc, char *argv[])
{
    extern unsigned int g_uTaskRunNum[MAX_TASK_NUM+1];
    extern unsigned int g_uIrqRunNum[32];
    for(int i=0; i<MAX_TASK_NUM+1; i++){
        if(g_uTaskRunNum[i] != 0){
            os_printf(LM_OS, LL_INFO, "PID:%d\t%d\n", i, g_uTaskRunNum[i]);
        }
    }
    for(int i=0; i<32; i++){
        if(g_uIrqRunNum[i] != 0){
            os_printf(LM_OS, LL_INFO, "IRQ:%d\t%d\n", i, g_uIrqRunNum[i]);
        }
    }
    if(argc>1) {
        memset(g_uTaskRunNum, 0, sizeof(g_uTaskRunNum));
        memset(g_uIrqRunNum, 0, sizeof(g_uIrqRunNum));
    }
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(os_debug, switch_num, task_irq_num_fun, "task/irq run num", "os_debug switch_num [NONE]/[reset]");
#endif //CONFIG_TASK_IRQ_RUN_NUM



#if defined(CONFIG_FLASH_DUMP)
static int flash_dump_fun(int argc, char *argv[])
{
    extern void flash_dumpstack();
    flash_dumpstack();
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(os_debug, flash_dump,    flash_dump_fun, "show crash info from flash", "os_debug flash_dump");
#endif


/*==========================================================================
 *                         OSHAL API TEST
 *=========================================================================*/

/* Test result structure */
typedef struct {
    const char *name;
    int passed;
    int failed;
    unsigned int time_us;
} api_test_result_t;

/* Test statistics */
static struct {
    int total_tests;
    int total_passed;
    int total_failed;
    unsigned int total_time_us;
} g_test_stats;

/* Synchronization for multi-thread test */
static volatile int g_thread_test_flag = 0;
static volatile int g_thread_test_count = 0;
static os_sem_handle_t g_test_sem = NULL;
static os_mutex_handle_t g_test_mutex = NULL;
static os_queue_handle_t g_test_queue = NULL;

/* Timer test flag */
static volatile int g_timer_fired = 0;
static volatile void *g_timer_arg_received = NULL;

/* PIT clock is 40MHz, 1 tick = 0.025us, so divide by 40 to get us */
#define PIT_CLOCK_MHZ  40

/* Get current time in ticks */
static unsigned int get_time_tick(void)
{
    return drv_pit_get_tick();
}

/* Convert ticks to microseconds */
static unsigned int tick_to_us(unsigned int ticks)
{
    return ticks / PIT_CLOCK_MHZ;
}

/* Print test result */
static void print_test_result(const char *name, int pass, unsigned int time_us)
{
    if (pass) {
        g_test_stats.total_passed++;
        os_printf(LM_OS, LL_INFO, "  [PASS] %-30s  %u us\r\n", name, time_us);
    } else {
        g_test_stats.total_failed++;
        os_printf(LM_OS, LL_INFO, "  [FAIL] %-30s  %u us\r\n", name, time_us);
    }
    g_test_stats.total_tests++;
    g_test_stats.total_time_us += time_us;
}

/*--------------------------------------------------------------------------
 * Memory API Tests
 *--------------------------------------------------------------------------*/
static void test_memory_api(void)
{
    unsigned int t1, t2;
    void *ptr;
    char *str;

    os_printf(LM_OS, LL_INFO, "\r\n[Memory API Tests]\r\n");

    /* Test os_malloc */
    t1 = get_time_tick();
    ptr = os_malloc(128);
    t2 = get_time_tick();
    if (ptr) {
        memset(ptr, 0xAA, 128);
        print_test_result("os_malloc(128)", 1, tick_to_us(t2 - t1));
        os_free(ptr);
    } else {
        print_test_result("os_malloc(128)", 0, tick_to_us(t2 - t1));
    }

    /* Test os_zalloc */
    t1 = get_time_tick();
    ptr = os_zalloc(128);
    t2 = get_time_tick();
    if (ptr) {
        int zeroed = 1;
        unsigned char *p = (unsigned char *)ptr;
        for (int i = 0; i < 128; i++) {
            if (p[i] != 0) { zeroed = 0; break; }
        }
        print_test_result("os_zalloc(128)", zeroed, tick_to_us(t2 - t1));
        os_free(ptr);
    } else {
        print_test_result("os_zalloc(128)", 0, tick_to_us(t2 - t1));
    }

    /* Test os_calloc */
    t1 = get_time_tick();
    ptr = os_calloc(4, 32);
    t2 = get_time_tick();
    if (ptr) {
        int zeroed = 1;
        unsigned char *p = (unsigned char *)ptr;
        for (int i = 0; i < 128; i++) {
            if (p[i] != 0) { zeroed = 0; break; }
        }
        print_test_result("os_calloc(4,32)", zeroed, tick_to_us(t2 - t1));
        os_free(ptr);
    } else {
        print_test_result("os_calloc(4,32)", 0, tick_to_us(t2 - t1));
    }

    /* Test os_realloc */
    ptr = os_malloc(64);
    if (ptr) {
        memset(ptr, 0x55, 64);
        t1 = get_time_tick();
        void *new_ptr = os_realloc(ptr, 128);
        t2 = get_time_tick();
        if (new_ptr) {
            unsigned char *p = (unsigned char *)new_ptr;
            int preserved = 1;
            for (int i = 0; i < 64; i++) {
                if (p[i] != 0x55) { preserved = 0; break; }
            }
            print_test_result("os_realloc(64->128)", preserved, tick_to_us(t2 - t1));
            os_free(new_ptr);
        } else {
            print_test_result("os_realloc(64->128)", 0, tick_to_us(t2 - t1));
            os_free(ptr);
        }
    }

    /* Test os_strdup */
    t1 = get_time_tick();
    str = os_strdup("test_string");
    t2 = get_time_tick();
    if (str && strcmp(str, "test_string") == 0) {
        print_test_result("os_strdup", 1, tick_to_us(t2 - t1));
        os_free(str);
    } else {
        print_test_result("os_strdup", 0, tick_to_us(t2 - t1));
        if (str) os_free(str);
    }

    /* Test os_free */
    ptr = os_malloc(64);
    if (ptr) {
        t1 = get_time_tick();
        os_free(ptr);
        t2 = get_time_tick();
        print_test_result("os_free", 1, tick_to_us(t2 - t1));
    }
}

/*--------------------------------------------------------------------------
 * Semaphore API Tests
 *--------------------------------------------------------------------------*/
static void test_semaphore_api(void)
{
    unsigned int t1, t2;
    os_sem_handle_t sem;
    int ret;

    os_printf(LM_OS, LL_INFO, "\r\n[Semaphore API Tests]\r\n");

    /* Test os_sem_create */
    t1 = get_time_tick();
    sem = os_sem_create(10, 0);
    t2 = get_time_tick();
    print_test_result("os_sem_create(10,0)", sem != NULL, tick_to_us(t2 - t1));

    if (sem) {
        /* Test os_sem_get (initial count = 0) */
        t1 = get_time_tick();
        ret = os_sem_get(sem);
        t2 = get_time_tick();
        print_test_result("os_sem_get (count=0)", ret == 0, tick_to_us(t2 - t1));

        /* Test os_sem_post */
        t1 = get_time_tick();
        ret = os_sem_post(sem);
        t2 = get_time_tick();
        print_test_result("os_sem_post", ret == 0, tick_to_us(t2 - t1));

        /* Test os_sem_get after post */
        t1 = get_time_tick();
        ret = os_sem_get(sem);
        t2 = get_time_tick();
        print_test_result("os_sem_get (count=1)", ret == 1, tick_to_us(t2 - t1));

        /* Test os_sem_wait (with signal) - count is still 1, should succeed */
        t1 = get_time_tick();
        ret = os_sem_wait(sem, 100);
        t2 = get_time_tick();
        print_test_result("os_sem_wait (signaled)", ret == 0, tick_to_us(t2 - t1));

        /* Test os_sem_wait (timeout) - count is now 0, should timeout */
        t1 = get_time_tick();
        ret = os_sem_wait(sem, 10);
        t2 = get_time_tick();
        print_test_result("os_sem_wait (timeout)", ret == -1, tick_to_us(t2 - t1));

        /* Test os_sem_destroy */
        t1 = get_time_tick();
        ret = os_sem_destroy(sem);
        t2 = get_time_tick();
        print_test_result("os_sem_destroy", ret == 0, tick_to_us(t2 - t1));
    }
}

/*--------------------------------------------------------------------------
 * Mutex API Tests
 *--------------------------------------------------------------------------*/
static void test_mutex_api(void)
{
    unsigned int t1, t2;
    os_mutex_handle_t mutex;
    int ret;

    os_printf(LM_OS, LL_INFO, "\r\n[Mutex API Tests]\r\n");

    /* Test os_mutex_create */
    t1 = get_time_tick();
    mutex = os_mutex_create();
    t2 = get_time_tick();
    print_test_result("os_mutex_create", mutex != NULL, tick_to_us(t2 - t1));

    if (mutex) {
        /* Test os_mutex_lock */
        t1 = get_time_tick();
        ret = os_mutex_lock(mutex, 100);
        t2 = get_time_tick();
        print_test_result("os_mutex_lock", ret == 0, tick_to_us(t2 - t1));

        /* Test os_mutex_unlock */
        t1 = get_time_tick();
        ret = os_mutex_unlock(mutex);
        t2 = get_time_tick();
        print_test_result("os_mutex_unlock", ret == 0, tick_to_us(t2 - t1));

        /* Test recursive lock */
        ret = os_mutex_lock(mutex, 100);
        t1 = get_time_tick();
        int ret2 = os_mutex_lock(mutex, 100);
        t2 = get_time_tick();
        print_test_result("os_mutex_lock (recursive)", ret2 == 0, tick_to_us(t2 - t1));
        os_mutex_unlock(mutex);
        os_mutex_unlock(mutex);

        /* Test os_mutex_destroy */
        t1 = get_time_tick();
        ret = os_mutex_destroy(mutex);
        t2 = get_time_tick();
        print_test_result("os_mutex_destroy", ret == 0, tick_to_us(t2 - t1));
    }
}

/*--------------------------------------------------------------------------
 * Queue API Tests
 *--------------------------------------------------------------------------*/
static void test_queue_api(void)
{
    unsigned int t1, t2;
    os_queue_handle_t queue;
    int ret;
    char send_msg[16] = "test_queue_msg";
    char recv_msg[16] = {0};

    os_printf(LM_OS, LL_INFO, "\r\n[Queue API Tests]\r\n");

    /* Test os_queue_create */
    t1 = get_time_tick();
    queue = os_queue_create("test_q", 5, 16, 0);
    t2 = get_time_tick();
    print_test_result("os_queue_create", queue != NULL, tick_to_us(t2 - t1));

    if (queue) {
        /* Test os_queue_send */
        t1 = get_time_tick();
        ret = os_queue_send(queue, send_msg, 16, 100);
        t2 = get_time_tick();
        print_test_result("os_queue_send", ret == 0, tick_to_us(t2 - t1));

        /* Test os_queue_receive */
        t1 = get_time_tick();
        ret = os_queue_receive(queue, recv_msg, 16, 100);
        t2 = get_time_tick();
        int msg_match = (ret == 0 && strcmp(recv_msg, send_msg) == 0);
        print_test_result("os_queue_receive", msg_match, tick_to_us(t2 - t1));

        /* Test os_queue_receive (timeout) */
        t1 = get_time_tick();
        ret = os_queue_receive(queue, recv_msg, 16, 10);
        t2 = get_time_tick();
        print_test_result("os_queue_receive (timeout)", ret == -1, tick_to_us(t2 - t1));

        /* Test os_queue_destory */
        t1 = get_time_tick();
        ret = os_queue_destroy(queue);
        t2 = get_time_tick();
        print_test_result("os_queue_destory", ret == 0, tick_to_us(t2 - t1));
    }
}

/*--------------------------------------------------------------------------
 * Timer API Tests
 *--------------------------------------------------------------------------*/
static void timer_callback(os_timer_handle_t timer)
{
    g_timer_fired = 1;
    g_timer_arg_received = os_timer_get_arg(timer);
}

static void test_timer_api(void)
{
    unsigned int t1, t2;
    os_timer_handle_t timer;
    int ret;
    int test_arg = 0x12345678;

    os_printf(LM_OS, LL_INFO, "\r\n[Timer API Tests]\r\n");

    g_timer_fired = 0;
    g_timer_arg_received = NULL;

    /* Test os_timer_create */
    t1 = get_time_tick();
    timer = os_timer_create("test_timer", 50, 0, timer_callback, (void *)(size_t)test_arg);
    t2 = get_time_tick();
    print_test_result("os_timer_create", timer != NULL, tick_to_us(t2 - t1));

    if (timer) {
        /* Test os_timer_get_arg */
        t1 = get_time_tick();
        void *arg = os_timer_get_arg(timer);
        t2 = get_time_tick();
        print_test_result("os_timer_get_arg", arg == (void *)(size_t)test_arg, tick_to_us(t2 - t1));

        /* Test os_timer_start */
        t1 = get_time_tick();
        ret = os_timer_start(timer);
        t2 = get_time_tick();
        print_test_result("os_timer_start", ret == 0, tick_to_us(t2 - t1));

        /* Wait for timer callback */
        os_msleep(100);
        print_test_result("timer_callback fired", g_timer_fired == 1, 0);
        print_test_result("timer_callback arg", g_timer_arg_received == (void *)(size_t)test_arg, 0);

        /* Test os_timer_changeperiod */
        g_timer_fired = 0;
        t1 = get_time_tick();
        ret = os_timer_changeperiod(timer, 30);
        t2 = get_time_tick();
        print_test_result("os_timer_changeperiod", ret == 0, tick_to_us(t2 - t1));
        os_msleep(50);
        print_test_result("timer new period", g_timer_fired == 1, 0);

        /* Test os_timer_stop */
        t1 = get_time_tick();
        ret = os_timer_stop(timer);
        t2 = get_time_tick();
        print_test_result("os_timer_stop", ret == 0, tick_to_us(t2 - t1));

        /* Test os_timer_delete */
        t1 = get_time_tick();
        ret = os_timer_delete(timer);
        t2 = get_time_tick();
        print_test_result("os_timer_delete", ret == 0, tick_to_us(t2 - t1));
    }
}

/*--------------------------------------------------------------------------
 * Sleep API Tests
 *--------------------------------------------------------------------------*/
static void test_sleep_api(void)
{
    unsigned int t1, t2;
    long long time_val;
    unsigned int elapsed_us;

    os_printf(LM_OS, LL_INFO, "\r\n[Sleep/Time API Tests]\r\n");

    /* Test os_time_get */
    t1 = get_time_tick();
    time_val = os_time_get();
    t2 = get_time_tick();
    print_test_result("os_time_get", time_val > 0, tick_to_us(t2 - t1));

    /* Test os_msleep (10ms = 10000us) */
    t1 = get_time_tick();
    os_msleep(10);
    t2 = get_time_tick();
    elapsed_us = tick_to_us(t2 - t1);
    print_test_result("os_msleep(10)", elapsed_us >= 8000 && elapsed_us <= 20000, elapsed_us);

    /* Test os_usdelay (100us) */
    t1 = get_time_tick();
    os_usdelay(100);
    t2 = get_time_tick();
    elapsed_us = tick_to_us(t2 - t1);
    print_test_result("os_usdelay(100)", elapsed_us >= 50 && elapsed_us <= 500, elapsed_us);

    /* Test os_msdelay (5ms = 5000us) */
    t1 = get_time_tick();
    os_msdelay(5);
    t2 = get_time_tick();
    elapsed_us = tick_to_us(t2 - t1);
    print_test_result("os_msdelay(5)", elapsed_us >= 4000 && elapsed_us <= 10000, elapsed_us);
}

/*--------------------------------------------------------------------------
 * Interrupt API Tests
 *--------------------------------------------------------------------------*/
static void test_interrupt_api(void)
{
    unsigned int t1, t2;
    unsigned int psw;
    int enabled;

    os_printf(LM_OS, LL_INFO, "\r\n[Interrupt API Tests]\r\n");

    /* Test system_irq_is_enable (should be enabled) */
    t1 = get_time_tick();
    enabled = system_irq_is_enable();
    t2 = get_time_tick();
    print_test_result("system_irq_is_enable (on)", enabled != 0, tick_to_us(t2 - t1));

    /* Test system_irq_save */
    t1 = get_time_tick();
    psw = system_irq_save();
    t2 = get_time_tick();
    print_test_result("system_irq_save", 1, tick_to_us(t2 - t1));

    /* Test system_irq_is_enable (should be disabled) */
    t1 = get_time_tick();
    enabled = system_irq_is_enable();
    t2 = get_time_tick();
    print_test_result("system_irq_is_enable (off)", enabled == 0, tick_to_us(t2 - t1));

    /* Test system_irq_restore */
    t1 = get_time_tick();
    system_irq_restore(psw);
    t2 = get_time_tick();
    print_test_result("system_irq_restore", 1, tick_to_us(t2 - t1));

    /* Verify interrupt is restored */
    enabled = system_irq_is_enable();
    print_test_result("irq restored check", enabled != 0, 0);
}

/*--------------------------------------------------------------------------
 * Task API Tests (Multi-thread)
 *--------------------------------------------------------------------------*/
static void test_thread_entry(void *arg)
{
    int id = (int)(size_t)arg;

    /* Signal we started */
    g_thread_test_count++;

    /* Wait for semaphore */
    if (g_test_sem) {
        os_sem_wait(g_test_sem, WAIT_FOREVER);
    }

    /* Test mutex in multi-thread */
    if (g_test_mutex) {
        os_mutex_lock(g_test_mutex, WAIT_FOREVER);
        g_thread_test_flag++;
        os_msleep(5);
        os_mutex_unlock(g_test_mutex);
    }

    /* Test queue in multi-thread */
    if (g_test_queue) {
        char msg[8];
        snprintf(msg, sizeof(msg), "T%d", id);
        os_queue_send(g_test_queue, msg, 8, 100);
    }

    /* Mark thread as done and delete self */
    g_thread_test_count--;
    os_task_delete(0);
}

static void test_task_api(void)
{
    unsigned int t1, t2;
    int task1, task2;
    int ret;

    os_printf(LM_OS, LL_INFO, "\r\n[Task API Tests]\r\n");

    /* Test os_task_get_running_handle */
    t1 = get_time_tick();
    int current = os_task_get_running_handle();
    t2 = get_time_tick();
    print_test_result("os_task_get_running_handle", current != 0, tick_to_us(t2 - t1));

    /* Test os_scheduler_lock/unlock */
    t1 = get_time_tick();
    ret = os_scheduler_lock();
    t2 = get_time_tick();
    print_test_result("os_scheduler_lock", ret >= 0, tick_to_us(t2 - t1));

    t1 = get_time_tick();
    ret = os_scheduler_unlock();
    t2 = get_time_tick();
    print_test_result("os_scheduler_unlock", ret >= 0, tick_to_us(t2 - t1));

    /* Setup for multi-thread test */
    g_thread_test_flag = 0;
    g_thread_test_count = 0;
    g_test_sem = os_sem_create(10, 0);
    g_test_mutex = os_mutex_create();
    g_test_queue = os_queue_create("mt_q", 4, 8, 0);

    /* Test os_task_create */
    t1 = get_time_tick();
    task1 = os_task_create("test_t1", 10, 1024, test_thread_entry, (void *)1);
    t2 = get_time_tick();
    print_test_result("os_task_create (task1)", task1 != 0, tick_to_us(t2 - t1));

    t1 = get_time_tick();
    task2 = os_task_create("test_t2", 10, 1024, test_thread_entry, (void *)2);
    t2 = get_time_tick();
    print_test_result("os_task_create (task2)", task2 != 0, tick_to_us(t2 - t1));

    /* Wait for threads to start */
    os_msleep(20);
    print_test_result("threads started", g_thread_test_count == 2, 0);

    /* Test os_task_suspend/resume */
    if (task1) {
        t1 = get_time_tick();
        ret = os_task_suspend(task1);
        t2 = get_time_tick();
        print_test_result("os_task_suspend", ret == 0, tick_to_us(t2 - t1));

        os_msleep(10);

        t1 = get_time_tick();
        ret = os_task_resume(task1);
        t2 = get_time_tick();
        print_test_result("os_task_resume", ret == 0, tick_to_us(t2 - t1));
    }

    /* Signal threads to continue */
    os_sem_post(g_test_sem);
    os_sem_post(g_test_sem);

    /* Wait for threads to complete mutex test */
    os_msleep(50);
    print_test_result("mutex multi-thread", g_thread_test_flag == 2, 0);

    /* Check queue messages */
    char recv[8];
    int q_count = 0;
    while (os_queue_receive(g_test_queue, recv, 8, 10) == 0) {
        q_count++;
    }
    print_test_result("queue multi-thread", q_count == 2, 0);

    /* Wait for threads to exit */
    os_msleep(50);
    print_test_result("threads exited", g_thread_test_count == 0, 0);

    /* Cleanup */
    if (g_test_sem) os_sem_destroy(g_test_sem);
    if (g_test_mutex) os_mutex_destroy(g_test_mutex);
    if (g_test_queue) os_queue_destroy(g_test_queue);
    g_test_sem = NULL;
    g_test_mutex = NULL;
    g_test_queue = NULL;
}

/*--------------------------------------------------------------------------
 * Other API Tests
 *--------------------------------------------------------------------------*/
static void test_other_api(void)
{
    unsigned int t1, t2;
    unsigned long r1, r2;

    os_printf(LM_OS, LL_INFO, "\r\n[Other API Tests]\r\n");

    /* Test os_random */
    t1 = get_time_tick();
    r1 = os_random();
    t2 = get_time_tick();
    r2 = os_random();
    print_test_result("os_random", r1 != r2, tick_to_us(t2 - t1));
}

/*--------------------------------------------------------------------------
 * Main Test Entry
 *--------------------------------------------------------------------------*/
static int os_api_test_handler(int argc, char *argv[])
{
    os_printf(LM_OS, LL_INFO, "\r\n");
    os_printf(LM_OS, LL_INFO, "============================================================\r\n");
    os_printf(LM_OS, LL_INFO, "               OSHAL API Complete Test Suite               \r\n");
    os_printf(LM_OS, LL_INFO, "============================================================\r\n");

    /* Reset statistics */
    memset(&g_test_stats, 0, sizeof(g_test_stats));

    /* Run all tests */
    test_memory_api();
    test_semaphore_api();
    test_mutex_api();
    test_queue_api();
    test_timer_api();
    test_sleep_api();
    test_interrupt_api();
    test_task_api();
    test_other_api();

    /* Print summary report */
    os_printf(LM_OS, LL_INFO, "\r\n");
    os_printf(LM_OS, LL_INFO, "============================================================\r\n");
    os_printf(LM_OS, LL_INFO, "                      TEST REPORT                          \r\n");
    os_printf(LM_OS, LL_INFO, "============================================================\r\n");
    os_printf(LM_OS, LL_INFO, "  Total Tests:   %d\r\n", g_test_stats.total_tests);
    os_printf(LM_OS, LL_INFO, "  Passed:        %d\r\n", g_test_stats.total_passed);
    os_printf(LM_OS, LL_INFO, "  Failed:        %d\r\n", g_test_stats.total_failed);
    os_printf(LM_OS, LL_INFO, "  Total Time:    %u us\r\n", g_test_stats.total_time_us);
    os_printf(LM_OS, LL_INFO, "------------------------------------------------------------\r\n");

    if (g_test_stats.total_failed == 0) {
        os_printf(LM_OS, LL_INFO, "  Result:        ALL TESTS PASSED!\r\n");
    } else {
        os_printf(LM_OS, LL_INFO, "  Result:        SOME TESTS FAILED!\r\n");
    }
    os_printf(LM_OS, LL_INFO, "============================================================\r\n");
    os_printf(LM_OS, LL_INFO, "\r\n");

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(os_debug, os_api_test, os_api_test_handler, "test all oshal APIs", "os_debug os_api_test");


#if defined(CONFIG_HEAP_DEBUG) || defined(CONFIG_RUNTIME_DEBUG) || defined(CONFIG_TASK_IRQ_RUN_NUM)
CLI_CMD(os_debug, NULL, "os debug function", "os_debug heaptrace/cpu/switch_num/os_api_test");
#endif

