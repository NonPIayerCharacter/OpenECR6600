#ifndef _COMMON_DEFINE_H
#define _COMMON_DEFINE_H
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "oshal.h"
#include "semphr.h"
#include "reg_macro_def.h"
#include "system_queue.h"
#include "trs_tls.h"

typedef int ts_err_t;
// Defines for declaring and defining event base

typedef enum {
    TRANS_LOG_ALWAYS= 0,       
    TRANS_LOG_ERROR,       
    TRANS_LOG_WARN,        
    TRANS_LOG_INFO,       
    TRANS_LOG_DEBUG,      
    TRANS_LOG_NONE ,     
} trans_log_level;

#define TRANS_DEFAULT_LOG_LEVEL 1

// Event loop library types
typedef const char*  event_base_t; /**< unique pointer to a subsystem that exposes events */
typedef void         (*event_handler_t)(void* event_handler_arg,
                                        event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data); /**< function called when an event is posted to the queue */
typedef void*        event_handler_instance_t; /**< context identifying an instance of a registered event handler */

#define EVENT_DECLARE_BASE(id) extern event_base_t id
#define EVENT_DEFINE_BASE(id) event_base_t id = #id
// Defines for registering/unregistering event handlers
#define EVENT_ANY_BASE     NULL             /**< register handler for any event base */
#define EVENT_ANY_ID       -1               /**< register handler for any event id */

#define TRANS_LOGI(format, ...) {if(TRANS_LOG_INFO <= TRANS_DEFAULT_LOG_LEVEL)os_printf(LM_APP, LL_INFO,"[I][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define TRANS_LOGE(format, ...) {if(TRANS_LOG_ERROR <= TRANS_DEFAULT_LOG_LEVEL)os_printf(LM_APP, LL_INFO,"[E][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define TRANS_LOGD(format, ...) {if(TRANS_LOG_DEBUG <= TRANS_DEFAULT_LOG_LEVEL)os_printf(LM_APP, LL_INFO,"[D][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define TRANS_LOGW(format, ...) {if(TRANS_LOG_WARN <= TRANS_DEFAULT_LOG_LEVEL)os_printf(LM_APP, LL_INFO,"[W][%s:%d]"format"\n", __FUNCTION__,__LINE__,##__VA_ARGS__);}
#define TRANS_LOGV(format, ...) {if(TRANS_LOG_ALWAYS <= TRANS_DEFAULT_LOG_LEVEL)os_printf(LM_APP, LL_INFO,"[A][%s:%d]"format"\n",__FUNCTION__,__LINE__, ##__VA_ARGS__);}
#define TRANSPORT_MEM_CHECK(a, action) if (!(a)) {                                         \
        TRANS_LOGE("%s(%d): %s", __FUNCTION__, __LINE__, "Memory exhausted");                     \
        action;                                                                                     \
        }

/**
 * @brief      Utility macro for checking the error code of ts_err_t
 */
#define TRANSPORT_ERR_OK_CHECK(TAG, err, action) \
        {                                                     \
            ts_err_t _transport_err_to_check = err;      \
            if (_transport_err_to_check != TRANS_OK) {      \
                TRANS_LOGE("%s(%d): Expected TRANS_OK; reported: %d", __FUNCTION__, __LINE__, _transport_err_to_check); \
                action;                                       \
            }                                                 \
        }



/* Definitions for error constants. */
#define TRANS_OK          0       /*!< ts_err_t value indicating success (no error) */
#define TRANS_FAIL        -1      /*!< Generic ts_err_t code indicating failure */

#define ERR_NO_MEM              0x101   /*!< Out of memory */
#define ERR_INVALID_ARG         0x102   /*!< Invalid argument */
#define ERR_INVALID_STATE       0x103   /*!< Invalid state */
#define ERR_INVALID_SIZE        0x104   /*!< Invalid size */
#define ERR_NOT_FOUND           0x105   /*!< Requested resource not found */
#define ERR_NOT_SUPPORTED       0x106   /*!< Operation or feature not supported */
#define ERR_TIMEOUT             0x107   /*!< Operation timed out */
#define ERR_INVALID_RESPONSE    0x108   /*!< Received response was invalid */
#define ERR_INVALID_CRC         0x109   /*!< CRC or checksum was invalid */
#define ERR_INVALID_VERSION     0x10A   /*!< Version was invalid */
#define ERR_INVALID_MAC         0x10B   /*!< MAC address was invalid */

#define ERR_WIFI_BASE           0x3000  /*!< Starting number of WiFi error codes */
#define ERR_MESH_BASE           0x4000  /*!< Starting number of MESH error codes */
#define ERR_FLASH_BASE          0x6000  /*!< Starting number of flash error codes */
#define ERR_HW_CRYPTO_BASE      0xc000  /*!< Starting number of HW cryptography module error codes */




#endif