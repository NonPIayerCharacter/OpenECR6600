#include "mbedtls/build_info.h"

#if defined(MBEDTLS_HAVE_TIME)
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "hal_rtc.h"
#include "oshal.h"

time_t trs_mbedtls_time(time_t *timer)
{
    time_t now = hal_rtc_get_time();

    if (timer != NULL) {
        *timer = now;
    }
    return now;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return os_time_get();
}

#if defined(MBEDTLS_HAVE_TIME_DATE)
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    if (tt == NULL || tm_buf == NULL || *tt == (mbedtls_time_t) -1) {
        return NULL;
    }
    return gmtime_r(tt, tm_buf);
}
#endif
#endif
