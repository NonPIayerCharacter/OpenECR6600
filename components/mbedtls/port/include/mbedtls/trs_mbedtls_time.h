#ifndef TRS_MBEDTLS_TIME_H
#define TRS_MBEDTLS_TIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RTC UTC seconds, with the same output-pointer contract as time(). */
time_t trs_mbedtls_time(time_t *timer);

#ifdef __cplusplus
}
#endif

#endif
