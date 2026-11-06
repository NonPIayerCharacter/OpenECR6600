/**
 * @file hal_sntp.c
 * @brief SNTP HAL configuration and runtime control.
 * @details Wraps lwIP SNTP setup, RTC synchronization, timezone/DST handling, server configuration, and NV persistence.
 * @par Copyright by http://eswincomputing.com/.
 */

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "lwip/tcpip.h"
#include "rtc.h"
#include "sntp.h"
#include "easyflash.h"
#include "cli.h"
#include "hal_rtc.h"
#include "hal_sntp.h"
#include "system_config.h"

#define TIME_ZONE_MIN 12
#define TIME_ZONE_MAX 14
#define SNTP_UPDATE_PERIOD_MIN_MS 15000
#define SNTP_UPDATE_PERIOD_MAX_MS 0x3FFFFFFF

typedef struct {
    unsigned char sntpSync;
    unsigned char rtcSync;
    unsigned char isDst;
    sntp_ctrl_cfg_t sntpCfg;
    hal_sntp_get_servertime_callback backFunc;
} sntp_ctrl_priv_t;

sntp_ctrl_priv_t g_sntp_ctrl_priv;

char g_sntp_server[SNTP_MAX_SERVERS][SNTP_SERVER_DNS_NAME_MAX_LEN + 1] =
{
    "ntp.sjtu.edu.cn",
#if SNTP_MAX_SERVERS > 1
    "cn.ntp.org.cn",
#endif
#if SNTP_MAX_SERVERS > 2
    "us.pool.ntp.org",
#endif
};

static void hal_sntp_apply_cb(void *arg)
{
    int action = (int)(uintptr_t)arg;
    unsigned int idx;

    if ((action & HAL_SNTP_SETTING_SERVER) != 0) {
        for (idx = 0; idx < SNTP_MAX_SERVERS; idx++) {
            sntp_setservername(idx, g_sntp_server[idx][0] ? g_sntp_server[idx] : NULL);
        }
    }

    if ((action & HAL_SNTP_SYNC_NTP) != 0) {
        sntp_stop();
        sntp_init();
    }
}

static void hal_sntp_stop_cb(void *arg)
{
    (void)arg;

    sntp_stop();
}

static void hal_sntp_apply_async(int action)
{
    err_t ret;

    ret = tcpip_callback(hal_sntp_apply_cb, (void *)(uintptr_t)action);
    if (ret != ERR_OK) {
        os_printf(LM_APP, LL_ERR, "sntp tcpip callback failed: %d\n", ret);
    }
}

void hal_sntp_stop_async(void)
{
    err_t ret;

    ret = tcpip_callback(hal_sntp_stop_cb, NULL);
    if (ret != ERR_OK) {
        os_printf(LM_APP, LL_ERR, "sntp stop tcpip callback failed: %d\n", ret);
    }
}

static sntp_ctrl_priv_t *hal_sntp_ctrl_get_priv(void)
{
    return &g_sntp_ctrl_priv;
}

static time_t hal_sntp_rtc_get_utctime(void)
{
    return (time_t)hal_rtc_get_time();
}

static int hal_sntp_rtc_set_utctime(time_t utcTime)
{
    if (utcTime < 0) {
        return -1;
    }

    return hal_rtc_set_time((int)utcTime);
}

void hal_sntp_sync_func_register(hal_sntp_get_servertime_callback func)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    priv->backFunc = func;
}

const sntp_ctrl_cfg_t *hal_sntp_config_get(void)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    return &priv->sntpCfg;
}

int hal_sntp_timezone_sec(float timezone)
{
    int secResp;

    if (-TIME_ZONE_MIN <= timezone && timezone <= TIME_ZONE_MAX) {
        /* format 1: -12 ~ +14 */
        secResp = (int)(timezone * ONE_HOUR2SEC);
    } else {
        /* format 2: UTC +/-xx:xx */
        int sign = (timezone < 0) ? -1 : 1;
        int utcOffset = (int)fabsf(timezone);
        int hour = utcOffset / 100;
        int minute = utcOffset % 100;

        secResp = sign * ((hour * ONE_HOUR2SEC) + (minute * MIN_TO_SEC));
    }

    return secResp;
}

unsigned int hal_sntp_dst_sec(void)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    if (priv->isDst) {
        return ONE_HOUR2SEC;
    }

    return 0;
}

/* ntp server update time */
int hal_sntp_set_system_time(unsigned int ntpTime)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();
    time_t rtcUtcTimeSec = ntpTime;
    int timeZoneSec = hal_sntp_timezone_sec(priv->sntpCfg.timeZone);

    if (timeZoneSec >= 0) {
        rtcUtcTimeSec += (time_t)timeZoneSec;
    } else {
        rtcUtcTimeSec -= (time_t)(-1 * timeZoneSec);
    }

    rtcUtcTimeSec += hal_sntp_dst_sec();
    if (hal_sntp_rtc_set_utctime(rtcUtcTimeSec) < 0) {
        return -1;
    }

    priv->sntpSync = 1;
    if (priv->backFunc != NULL) {
        /* notice app ntp server update */
        priv->backFunc();
    }

    return 0;
}

/**
 * Check timezone in decimal-hour format: range -12 ~ +14, only .0 or .5.
 */
static int hal_sntp_check_timezone(float tz)
{
    float integer_part;
    float fractional_part = modff(tz, &integer_part);

    fractional_part = fabsf(fractional_part);

    if (fractional_part != 0.0f && fractional_part != 0.5f) {
        os_printf(LM_APP, LL_INFO, "timezone should be between -12 to 14 with dot 0 or dot 5\n");
        return -1;
    }

    return 0;
}

int hal_sntp_timezone_set(float tz, int action)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();
    int ret;

    if (tz == priv->sntpCfg.timeZone) {
        /* TimeZone duplicate setting, do nothing */
        return 0;
    }

    if (-TIME_ZONE_MIN <= tz && tz <= TIME_ZONE_MAX) {
        ret = hal_sntp_check_timezone(tz);
        if (ret != 0) {
            return ret;
        }
    } else {
        int sign = (tz < 0) ? -1 : 1;
        int utcOffset = (int)fabsf(tz);
        int hour = utcOffset / 100;
        int minute = utcOffset % 100;

        if ((sign < 0 && hour > TIME_ZONE_MIN) || (sign > 0 && hour > TIME_ZONE_MAX)) {
            os_printf(LM_APP, LL_INFO, "TZ Hour Shall be between -12 to 14.\n");
            return -1;
        }

        if (minute >= MIN_TO_SEC) {
            os_printf(LM_APP, LL_INFO, "TZ Minute Shall be between 0 to 59.\n");
            return -1;
        }

        if ((sign < 0 && hour == TIME_ZONE_MIN && minute != 0) ||
            (sign > 0 && hour == TIME_ZONE_MAX && minute != 0)) {
            os_printf(LM_APP, LL_INFO, "TZ Shall be between UTC-12:00 and UTC+14:00.\n");
            return -1;
        }
    }

    int timeZoneSec = (-1) * hal_sntp_timezone_sec(priv->sntpCfg.timeZone) + hal_sntp_timezone_sec(tz);
    os_printf(LM_APP, LL_INFO, "timezone change from %f to %f, sec offset %d\n", priv->sntpCfg.timeZone, tz, timeZoneSec);
    time_t rtcUtcTime = hal_sntp_rtc_get_utctime();
    if (timeZoneSec >= 0) {
        hal_sntp_rtc_set_utctime(rtcUtcTime + timeZoneSec);
    } else {
        time_t utcTimeSet = (time_t)(-1 * timeZoneSec);
        if (utcTimeSet > rtcUtcTime) {
            os_printf(LM_APP, LL_ERR, "Rtc Time Cannot Sync with TimeZone %f\n", tz);
            utcTimeSet = 0;
        }
        hal_sntp_rtc_set_utctime(rtcUtcTime - utcTimeSet);
    }

    priv->sntpCfg.timeZone = tz;
    if ((action & HAL_SNTP_SETTING_SAVE) != 0) {
        hal_system_set_config(CUSTOMER_NV_SNTP_CONFIG, &priv->sntpCfg, sizeof(priv->sntpCfg));
    }

    if ((action & HAL_SNTP_SYNC_NTP) != 0) {
        hal_sntp_apply_async(action);
    }

    return 0;
}

float hal_sntp_timezone_get(void)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    return priv->sntpCfg.timeZone;
}

int hal_sntp_period_set(unsigned int periodMs, int action)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    if (periodMs > SNTP_UPDATE_PERIOD_MAX_MS) {
        os_printf(LM_APP, LL_INFO, "period(%u) too large cannot set\n", SNTP_UPDATE_PERIOD_MAX_MS);
        return -1;
    } else if (periodMs < SNTP_UPDATE_PERIOD_MIN_MS)  {
        os_printf(LM_APP, LL_INFO, "period(%u) too small cannot set\n", SNTP_UPDATE_PERIOD_MIN_MS);
        return -1;
    }

    priv->sntpCfg.sntpPeriod = periodMs;
    if ((action & HAL_SNTP_SETTING_SAVE) != 0) {
        hal_system_set_config(CUSTOMER_NV_SNTP_CONFIG, &priv->sntpCfg, sizeof(priv->sntpCfg));
    }

    if ((action & HAL_SNTP_SYNC_NTP) != 0) {
        hal_sntp_apply_async(action);
    }

    return 0;
}

unsigned int hal_sntp_periodms_get(void)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();

    return priv->sntpCfg.sntpPeriod;
}

int hal_sntp_set_servername(unsigned char idx, const char *server, int action)
{
    int ret = 0;

    if ((idx >= SNTP_MAX_SERVERS) || ((server != NULL) && (strlen(server) > SNTP_SERVER_DNS_NAME_MAX_LEN))) {
        os_printf(LM_APP, LL_INFO, "Sntp Server(%d) len %d set failed\r\n", idx, server ? strlen(server) : 0);
        return -1;
    }

    if (server == NULL || server[0] == '\0') {
        memset(g_sntp_server[idx], 0, sizeof(g_sntp_server[idx]));
    } else {
        snprintf(g_sntp_server[idx], sizeof(g_sntp_server[idx]), "%s", server);
    }

    if ((action & HAL_SNTP_SETTING_SAVE) != 0) {
        ret = hal_system_set_config(CUSTOMER_NV_SNTP_SERVER, (void *)g_sntp_server, sizeof(g_sntp_server));
        if (ret != 0) {
            return -1;
        }
    }

    action |= HAL_SNTP_SETTING_SERVER;
    hal_sntp_apply_async(action);

    return 0;
}

const char *hal_sntp_get_servername(unsigned char idx)
{
    if (idx >= SNTP_MAX_SERVERS) {
        os_printf(LM_APP, LL_INFO, "Sntp Server(%d) Cannot Get\r\n", idx);
        return NULL;
    }

    return g_sntp_server[idx];
}

void hal_sntp_init(int action)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();
    struct rtc_time initRtc = {0};
    unsigned int status;

    if (priv->rtcSync == 0) {
        status = hal_system_get_config(CUSTOMER_NV_SNTP_CONFIG, &priv->sntpCfg, sizeof(priv->sntpCfg));
        if (status > 0) {
            priv->isDst = priv->sntpCfg.isDst;
            os_printf(LM_APP, LL_INFO, "Restore Sntp Cfg isDst %d sntpPeriod %d timeZone %f\n",
                priv->sntpCfg.isDst, priv->sntpCfg.sntpPeriod, priv->sntpCfg.timeZone);
        }

        if (priv->sntpCfg.sntpPeriod == 0) {
            priv->sntpCfg.sntpPeriod = SNTP_UPDATE_DELAY;
        }

        status = hal_system_get_config(CUSTOMER_NV_SNTP_SERVER, &g_sntp_server, sizeof(g_sntp_server));
        if (status > 0) {
            int idx;
            os_printf(LM_APP, LL_INFO, "Restore Sntp server:");
            for (idx = 0; idx < SNTP_MAX_SERVERS; idx++) {
                os_printf(LM_APP, LL_INFO, "%s ", g_sntp_server[idx]);
            }
            os_printf(LM_APP, LL_INFO, "\n");
        }

        drv_rtc_get_time(&initRtc);
        os_printf(LM_APP, LL_INFO, "Sntp Init Time [%02d:%02d:%02d]\n", initRtc.hour, initRtc.min, initRtc.sec);
        if ((initRtc.hour == 0) && (initRtc.min == 0) && (initRtc.sec < 10)) {
            /* RTC not hold last time at hw reboot time. Apply TimeZone/DST offset. */
            time_t utcTimeSet = 0;
            time_t rtcUtcTime = hal_sntp_rtc_get_utctime();
            int timeZoneSec = hal_sntp_timezone_sec(priv->sntpCfg.timeZone);

            if (timeZoneSec >= 0) {
                hal_sntp_rtc_set_utctime(rtcUtcTime + timeZoneSec + hal_sntp_dst_sec());
            } else {
                utcTimeSet = (time_t)(-1 * timeZoneSec);
                if (utcTimeSet > rtcUtcTime) {
                    os_printf(LM_APP, LL_ERR, "Rtc Time Cannot Sync with TimeZone %f\n", priv->sntpCfg.timeZone);
                    utcTimeSet = 0;
                }
                hal_sntp_rtc_set_utctime(rtcUtcTime - utcTimeSet + hal_sntp_dst_sec());
            }

            /* Force Sync NTP */
            action |= HAL_SNTP_SYNC_NTP;
        }

        action |= HAL_SNTP_SETTING_SERVER;
        priv->rtcSync = 1;
    } else if (priv->sntpSync != 0) {
        if (priv->backFunc != NULL) {
            /* notice app ntp server update */
            priv->backFunc();
        }
    }

    hal_sntp_apply_async(action);
}

int hal_sntp_set_utc_dst(unsigned char dst, int action)
{
    sntp_ctrl_priv_t *priv = hal_sntp_ctrl_get_priv();
    time_t rtcUtcTime = hal_sntp_rtc_get_utctime();

    if (dst > 1) {
        os_printf(LM_APP, LL_ERR, "dst must be 0 or 1\n");
        return -1;
    }

    if (priv->isDst == dst) {
        if (priv->sntpCfg.isDst != dst) {
            priv->sntpCfg.isDst = dst;
            if ((action & HAL_SNTP_SETTING_SAVE) != 0) {
                hal_system_set_config(CUSTOMER_NV_SNTP_CONFIG, &priv->sntpCfg, sizeof(priv->sntpCfg));
            }
        }
        /* DST duplicate setting, do nothing */
        return 0;
    }

    if (dst == 0) {
        rtcUtcTime -= ONE_HOUR2SEC;
    } else {
        rtcUtcTime += ONE_HOUR2SEC;
    }
    priv->isDst = dst;
    priv->sntpCfg.isDst = dst;
    hal_sntp_rtc_set_utctime(rtcUtcTime);

    if ((action & HAL_SNTP_SETTING_SAVE) != 0) {
        hal_system_set_config(CUSTOMER_NV_SNTP_CONFIG, &priv->sntpCfg, sizeof(priv->sntpCfg));
    }

    if ((action & HAL_SNTP_SYNC_NTP) != 0) {
        hal_sntp_apply_async(action);
    }

    return 0;
}
