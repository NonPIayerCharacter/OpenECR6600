
/*******************************************************************************
 * Copyright by Transa Semi.
 *
 * File Name:    event_loop.c
 * File Mark:
 * Description:
 * Others:
 * Version:       V1.0
 * Author:        lixiao
 * Date:          2018-12-21
 * History 1:
 *     Date:
 *     Version:
 *     Author:
 *     Modification:
 * History 2:
  ********************************************************************************/

/****************************************************************************
* 	                                           Include files
****************************************************************************/
#include "system_event.h"
#include "oshal.h"
#include "os_event.h"
/****************************************************************************
* 	                                           Local Macros
****************************************************************************/

/****************************************************************************
* 	                                           Local Types
****************************************************************************/

/****************************************************************************
* 	                                           Local Constants
****************************************************************************/

/****************************************************************************
* 	                                           Local Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Global Constants
****************************************************************************/

/****************************************************************************
* 	                                          Global Variables
****************************************************************************/

/****************************************************************************
* 	                                          Global Function Prototypes
****************************************************************************/

/****************************************************************************
* 	                                          Function Definitions
****************************************************************************/
#define USER_EVENT_HANDLERS_NUM     (3)

static os_queue_handle_t s_event_queue = NULL;
static system_event_cb_t s_event_handler_cb[USER_EVENT_HANDLERS_NUM];
static void *s_event_ctx[USER_EVENT_HANDLERS_NUM];
EventGroupHandle_t os_event_handle;

static system_event_handler_t wifi_event_cb = NULL;

void wifi_event_cb_register(system_event_handler_t cb)
{
	wifi_event_cb = cb;
}

static sys_err_t sys_event_post_to_user(system_event_t *event)
{
    int idx;

    for (idx = 0; idx < USER_EVENT_HANDLERS_NUM; idx++) {
        if (s_event_handler_cb[idx]) {
            return s_event_handler_cb[idx](s_event_ctx[idx], event);
        }
    }

    return SYS_OK;
}

static void sys_event_loop_task(void *arg)
{
    os_event_handle = xEventGroupCreate();
    if (!os_event_handle) {
        os_printf(LM_WIFI, LL_ERR, "os_event create failed.\n");
    }

    while (1) {
        system_event_t evt;
        if (os_queue_receive(s_event_queue, (char*)&evt, sizeof(system_event_t),WAIT_FOREVER) == 0) {
            #if !defined(CONFIG_WIFI_MESH)
            sys_err_t ret = sys_event_process_default(&evt);
            if (ret != SYS_OK) {
                //SYS_LOGE("default event handler failed!");
            }
            ret = sys_event_post_to_user(&evt);
            if (ret != SYS_OK) {
                //SYS_LOGE("post event to user fail!");
            }

            if(wifi_event_cb != NULL)
                wifi_event_cb(&evt);
            #else
            sys_err_t ret = sys_event_post_to_user(&evt);
            if (ret != SYS_OK) {
               //SYS_LOGE("post event to user fail!");
            }
             ret = sys_event_process_default(&evt);
            if (ret != SYS_OK) {
               //SYS_LOGE("default event handler failed!");
            }
            #endif
        }
    }
	return;
}

system_event_cb_t sys_event_loop_set_cb(system_event_cb_t cb, void *ctx)
{
    system_event_cb_t old_cb = NULL;
    int result = -1;
    int idx;

    for (idx = 0; idx < USER_EVENT_HANDLERS_NUM; idx++) {
        if (s_event_handler_cb[idx] == NULL) {
            s_event_handler_cb[idx] = cb;
            s_event_ctx[idx] = ctx;
            result = 0;
        }
    }

    if (result) {
        old_cb = s_event_handler_cb[0];
        s_event_handler_cb[0] = cb;
        s_event_ctx[0] = ctx;
    }
    
    return old_cb;
}

sys_err_t sys_event_loop_clear_cb(system_event_cb_t cb)
{
    int idx;

    for (idx = 0; idx < USER_EVENT_HANDLERS_NUM; idx++) {
        if (s_event_handler_cb[idx] == cb) {
            s_event_handler_cb[idx] = NULL;
            s_event_ctx[idx] = NULL;
        }
    }

    return SYS_OK;
}

sys_err_t sys_event_send(system_event_t *event)
{
    if (s_event_queue == NULL) {
        //SYS_LOGE("Event loop not initialized via sys_event_loop_init, but sys_event_send called");
        return SYS_ERR_INVALID_STATE;
    }

    int ret = os_queue_send(s_event_queue, (char*)event, sizeof(system_event_t),0);
    if (ret <0) {
        //if (event) {
        //    SYS_LOGE("e=%d f", event->event_id);
        //} else {
        //    SYS_LOGE("e null");
        //}
        return SYS_ERR;
    }
    return SYS_OK;
}

void* sys_event_loop_get_queue(void)
{
    return s_event_queue;
}

sys_err_t sys_event_loop_init(system_event_cb_t cb, void *ctx)
{
    static bool s_event_init_flag = false;
    int handle;

    if (cb) {
        sys_event_loop_set_cb(cb, ctx);
    }

    if (s_event_init_flag) {
        return SYS_ERR;
    }

    sys_event_set_default_wifi_handlers();

    s_event_queue = os_queue_create("event queue", 32, sizeof(system_event_t), 0);
    if (s_event_queue == NULL) {
        return SYS_ERR_NO_MEM;
    }

    handle=os_task_create("sys_event_loop", SYSTEM_EVENT_LOOP_PRIORITY, 4096, sys_event_loop_task, NULL);
    if (handle < 0) {
        return SYS_ERR_NO_MEM;
    }
    s_event_init_flag = true;

    return SYS_OK;
}


