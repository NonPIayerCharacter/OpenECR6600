// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _OSAL_H_
#define _OSAL_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unistd.h>
#include <stdint.h>
//#include <esp_timer.h>
#include "oshal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OS_SUCCESS ESP_OK
#define OS_FAIL    ESP_FAIL

typedef TaskHandle_t othread_t;
/*
	TaskHandle_t xTaskCreateStaticPinnedToCore(	TaskFunction_t pvTaskCode,
												const char * const pcName,
												const uint32_t ulStackDepth,
												void * const pvParameters,
												UBaseType_t uxPriority,
												StackType_t * const pxStackBuffer,
												StaticTask_t * const pxTaskBuffer,
												const BaseType_t xCoreID );

    BaseType_t xTaskCreate( TaskFunction_t pxTaskCode,
                            const char * const pcName,
                            const configSTACK_DEPTH_TYPE usStackDepth,
                            void * const pvParameters,
                            UBaseType_t uxPriority,
                            TaskHandle_t * const pxCreatedTask )

*/
static inline int httpd_os_thread_create(othread_t *thread,
                                 const char *name, uint16_t stacksize, int prio,
                                 void (*thread_routine)(void *arg), void *arg,
                                 BaseType_t core_id)
{
    //int ret = xTaskCreatePinnedToCore(thread_routine, name, stacksize, arg, prio, thread, core_id);
    int ret = xTaskCreate(thread_routine, name, stacksize, arg, prio, thread);
    if (ret == pdPASS) {
        return OS_SUCCESS;
    }
    return OS_FAIL;
}

/* Only self delete is supported */
static inline void httpd_os_thread_delete(void)
{
    vTaskDelete(xTaskGetCurrentTaskHandle());
}

static inline void httpd_os_thread_sleep(int msecs)
{
    vTaskDelay(msecs / portTICK_RATE_MS);
}

static inline othread_t httpd_os_thread_handle(void)
{
    return xTaskGetCurrentTaskHandle();
}

#ifdef __cplusplus
}
#endif

#endif /* ! _OSAL_H_ */
