/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-10 22:16:41
 * @LastEditTime: 2020-04-27 22:35:34
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#include "platform_timer.h"
#include "http_log.h"
#include <limits.h>
#include "FreeRTOS.h"
#include "task.h"

static TickType_t platform_ms_to_ticks(unsigned int ms)
{
    uint64_t ticks = ((uint64_t)ms * (uint64_t)configTICK_RATE_HZ) + 999U;

    ticks /= 1000U;
    if ((ms != 0U) && (ticks == 0U)) {
        ticks = 1U;
    }
    if (ticks > (uint64_t)portMAX_DELAY) {
        HTTP_LOG_W("timeout %u ms exceeds portMAX_DELAY, clamp to %lu ticks",
                   ms, (unsigned long)portMAX_DELAY);
        return portMAX_DELAY;
    }

    return (TickType_t)ticks;
}

static TickType_t platform_us_to_ticks(unsigned long usec)
{
    uint64_t ticks = ((uint64_t)usec * (uint64_t)configTICK_RATE_HZ) + 999999U;

    ticks /= 1000000U;
    if ((usec != 0UL) && (ticks == 0U)) {
        ticks = 1U;
    }
    if (ticks > (uint64_t)portMAX_DELAY) {
        HTTP_LOG_W("delay %lu us exceeds portMAX_DELAY, clamp to %lu ticks",
                   usec, (unsigned long)portMAX_DELAY);
        return portMAX_DELAY;
    }

    return (TickType_t)ticks;
}

static uint64_t platform_ticks_to_ms(TickType_t ticks)
{
    if (ticks == 0U) {
        return 0U;
    }

    return ((((uint64_t)ticks * 1000U) + (uint64_t)configTICK_RATE_HZ - 1U) /
            (uint64_t)configTICK_RATE_HZ);
}

static void platform_timer_load_timeout(const platform_timer_t *timer,
                                        TimeOut_t *timeout_state,
                                        TickType_t *ticks_to_wait)
{
    timeout_state->xTimeOnEntering = timer->time_on_entering;
    timeout_state->xOverflowCount = timer->overflow_count;
    *ticks_to_wait = timer->ticks_to_wait;
}

void platform_timer_init(platform_timer_t* timer)
{
    if (timer == NULL) {
        HTTP_LOG_E("timer is NULL");
        return;
    }

    timer->ticks_to_wait = 0;
    timer->time_on_entering = 0;
    timer->overflow_count = 0;
}

void platform_timer_cutdown(platform_timer_t* timer, unsigned int timeout)
{
    TimeOut_t timeout_state;
    unsigned int timeout_ms = timeout;

    if (timer == NULL) {
        HTTP_LOG_E("timer is NULL");
        return;
    }

    if (timeout_ms > PLATFORM_TIMER_MAX_TIMEOUT_MS) {
        HTTP_LOG_W("timeout %u ms exceeds max %u ms, clamp to max value",
                   timeout_ms, PLATFORM_TIMER_MAX_TIMEOUT_MS);
        timeout_ms = PLATFORM_TIMER_MAX_TIMEOUT_MS;
    }

    vTaskSetTimeOutState(&timeout_state);
    timer->ticks_to_wait = platform_ms_to_ticks(timeout_ms);
    timer->time_on_entering = timeout_state.xTimeOnEntering;
    timer->overflow_count = timeout_state.xOverflowCount;
}

char platform_timer_is_expired(platform_timer_t* timer)
{
    TimeOut_t timeout_state;
    TickType_t ticks_to_wait;

    if (timer == NULL) {
        HTTP_LOG_E("timer is NULL");
        return 1;
    }

    platform_timer_load_timeout(timer, &timeout_state, &ticks_to_wait);

    return xTaskCheckForTimeOut(&timeout_state, &ticks_to_wait) != pdFALSE ? 1 : 0;
}

int platform_timer_remain(platform_timer_t* timer)
{
    TimeOut_t timeout_state;
    TickType_t ticks_to_wait;
    uint64_t remain_ms;

    if (timer == NULL) {
        HTTP_LOG_E("timer is NULL");
        return 0;
    }

    platform_timer_load_timeout(timer, &timeout_state, &ticks_to_wait);
    if (xTaskCheckForTimeOut(&timeout_state, &ticks_to_wait) != pdFALSE) {
        return 0;
    }

    remain_ms = platform_ticks_to_ms(ticks_to_wait);
    if (remain_ms > (uint64_t)INT_MAX) {
        HTTP_LOG_W("remain %llu ms exceeds INT_MAX, clamp to %d",
                   (unsigned long long)remain_ms, INT_MAX);
        return INT_MAX;
    }

    return (int)remain_ms;
}

unsigned long platform_timer_now(void)
{
    uint64_t now_ms;

    now_ms = platform_ticks_to_ms(xTaskGetTickCount());
    if (now_ms > (uint64_t)ULONG_MAX) {
        return ULONG_MAX;
    }

    return (unsigned long)now_ms;
}

void platform_timer_usleep(unsigned long usec)
{
    TickType_t ticks;

    if (usec == 0UL) {
        return;
    }

    ticks = platform_us_to_ticks(usec);
    vTaskDelay(ticks);
}

