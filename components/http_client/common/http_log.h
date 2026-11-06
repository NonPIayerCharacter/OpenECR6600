#ifndef __HTTP_LOG_H__
#define __HTTP_LOG_H__

#include "oshal.h"

#define HTTP_LOG_D(fmt, ...) \
    { \
        os_printf(LM_APP, LL_DBG, "%s[%d]", __FUNCTION__, __LINE__); \
        os_printf(LM_APP, LL_DBG, fmt, ##__VA_ARGS__); \
        os_printf(LM_APP, LL_DBG, "\n"); \
    }
#define HTTP_LOG_I(fmt, ...) \
    { \
        os_printf(LM_APP, LL_INFO, "%s[%d]", __FUNCTION__, __LINE__); \
        os_printf(LM_APP, LL_INFO, fmt, ##__VA_ARGS__); \
        os_printf(LM_APP, LL_INFO, "\n"); \
    }
#define HTTP_LOG_W(fmt, ...) \
    { \
        os_printf(LM_APP, LL_WARN, "%s[%d]", __FUNCTION__, __LINE__); \
        os_printf(LM_APP, LL_WARN, fmt, ##__VA_ARGS__); \
        os_printf(LM_APP, LL_WARN, "\n"); \
    }
#define HTTP_LOG_E(fmt, ...) \
    { \
        os_printf(LM_APP, LL_ERR, "%s[%d]", __FUNCTION__, __LINE__); \
        os_printf(LM_APP, LL_ERR, fmt, ##__VA_ARGS__); \
        os_printf(LM_APP, LL_ERR, "\n"); \
    }
#define HTTP_LOG_NOFMT(fmt, ...) \
    { \
        os_printf(LM_APP, LL_INFO, fmt, ##__VA_ARGS__); \
    }

#endif /* __HTTP_LOG_H__ */
