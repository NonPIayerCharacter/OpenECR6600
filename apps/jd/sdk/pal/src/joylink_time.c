#include <stddef.h>
#include <stdint.h>

#include <time.h>
#include <sys/time.h>
#include "rtc.h"
#include "joylink_stdio.h"
#include "joylink_time.h"
#include "hal_rtc.h"

/**
 * 初始化系统时间
 *
 * @return: 0 - success or -1 - fail
 *
 */
int32_t jl_set_UTCtime(jl_time_stamp_t ts)
{
#ifdef __LINUX_PAL__
    struct timeval tv;

    tv.tv_sec = ts.second;
    tv.tv_usec = ts.ms * 1000;
    settimeofday(&tv, NULL);
#elif defined (CONFIG_JD_FREERTOS_PAL)
    hal_rtc_set_time(ts.second);
#endif
    return 0;
}

/**
* @brief 获取系统UTC时间，精确到毫秒
* @param none
* @return time ms
*/
int jl_get_time_msec(jl_time_stamp_t *ts)
{
#ifdef __LINUX_PAL__
    struct timeval now;

    if(gettimeofday(&now,NULL) == -1)
        return -1;

    if(ts)
    {
        ts->second = (uint32_t) now.tv_sec;
        ts->ms = (uint32_t) (now.tv_usec/1000);
    }
    return 0;
#elif defined (CONFIG_JD_FREERTOS_PAL)
    struct rtc_time now;
    int total_sec;

    if (drv_rtc_get_time(&now) == -1) //获取毫秒
        return -1;

    total_sec = hal_rtc_get_time();  //1970至现在的秒数
    if (ts)
      {
        ts->second = total_sec;
        ts->ms = (uint32_t) now.cnt_32k*1000/32768;
      }
      return 0;
#else
    return -1;
#endif
}

/**
 * 获取系统UTC时间，精确到秒
 *
 * @return: UTC Second
 *
 */
uint32_t jl_get_time_second(uint32_t *jl_time)
{
#ifdef __LINUX_PAL__
    return (uint32_t)time(NULL);
#elif defined (CONFIG_JD_FREERTOS_PAL)
    int total_sec;
    total_sec = hal_rtc_get_time();  //1970至现在的秒数
    return total_sec;
#else
    return 0;
#endif
}

/**
 * get time string，将时间转化为年月日时分秒
 *
 * @out param:
 * @return: success or fail
 *
 */
int jl_get_time(jl_time_t *jl_time)
{
#if defined (__LINUX_PAL__) || defined (CONFIG_JD_FREERTOS_PAL)
    time_t timep;
    struct tm *p;
    jl_time->timestamp = (uint32_t) jl_get_time_second(NULL);//1970年后的秒数
    p = gmtime(&timep);
    jl_time->year      = p->tm_year;
    jl_time->month     = p->tm_mon;
    jl_time->week      = p->tm_wday;
    jl_time->day       = p->tm_mday;
    jl_time->hour      = p->tm_hour;
    jl_time->minute    = p->tm_min;
    jl_time->second    = p->tm_sec;
#endif
    return 0;
}

/**
 * 获取时间字符串
 *
 * @out param: "year-month-day hour:minute:second.millisecond"
 * @return: success or fail
 *
 */
char *jl_get_time_str(void)
{
    static char time_buffer[30];
#ifdef __LINUX_PAL__
    // 获取“年-月-日 时:分:秒.毫秒”字符串类型的时间戳
    time_t timep;
    jl_time_stamp_t ts;
    struct tm *p;

    time(&timep);
    p = localtime(&timep);
    jl_get_time_msec(&ts);

    jl_platform_sprintf(time_buffer, "%02d-%02d-%02d %02d:%02d:%02d.%03d",
        1900 + p->tm_year,
        1 + p->tm_mon,
        p->tm_mday,
        p->tm_hour,
        p->tm_min,
        p->tm_sec,
        ts.ms
        );
#elif defined (CONFIG_JD_FREERTOS_PAL)
    // 获取“年-月-日 时:分:秒.毫秒”字符串类型的时间戳
    time_t timep;
    jl_time_stamp_t ts;
    struct tm *p;

    timep = (time_t)jl_get_time_second(NULL);
    p = localtime(&timep);
    jl_get_time_msec(&ts);

    jl_platform_sprintf(time_buffer, "%02d-%02d-%02d %02d:%02d:%02d.%03d",
        1900 + p->tm_year,
        1 + p->tm_mon,
        p->tm_mday,
        p->tm_hour,
        p->tm_min,
        p->tm_sec,
        ts.ms
        );
#else
    // 如果不能获取“年-月-日 时:分:秒.毫秒”时间戳，则获取UTC毫秒数时间戳
    jl_time_stamp_t ts;
    jl_get_time_msec(&ts);
    jl_platform_sprintf(time_buffer, "%d%03d", ts.second, ts.ms);
#endif
    return time_buffer;
}

/**
 * get os time
 *
 * @out param: none
 * @return: sys time ticks ms since sys start
*/
uint32_t jl_get_os_time(void)
{
#if defined (__LINUX_PAL__) || defined (CONFIG_JD_FREERTOS_PAL)
    jl_time_stamp_t ts;

    jl_get_time_msec(&ts);
    return (ts.second * 1000 + ts.ms); // FIXME do not recommand this method
        // return clock();
#else
        return 0;
#endif
}



