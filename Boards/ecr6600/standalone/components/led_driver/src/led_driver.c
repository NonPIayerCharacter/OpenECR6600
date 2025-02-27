/**
* @file led_driver.c
* @author 小王同学
* @brief led_driver module is used to led driver
* @version 0.1
* @date 2022-08-09
*
*/
#include <string.h>
#include <stdlib.h>

#include "led_driver.h"

#ifdef IS_FREERTOS
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "timers.h"
#endif

/***********************************************************
*************************micro define***********************
***********************************************************/

 
/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct led_driver_list {
    struct led_driver_list *next;
    uint32_t pin; 
    bool polarity;
#ifdef IS_FREERTOS
    TimerHandle_t timer_handle; /* 软件定时器句柄 */
    LED_MODE_E mode;
    LED_BLINK_T blink_data;
    bool onoff_status;
#endif
} LED_DRIVER_LIST_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
static LED_DRIVER_REG_T* led_driver = NULL;
static LED_DRIVER_LIST_T *p_led_driver_list = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/
/**
 * @brief 链表遍历
 *
 * @param[in] node:链表起始节点
 * @return LED_DRIVER_LIST_T* 最后一个链表节点地址
 */
static LED_DRIVER_LIST_T* __led_list_traverse(LED_DRIVER_LIST_T *node)
{
    LED_DRIVER_LIST_T* p = node;
    while (NULL != p->next) {
        p = p->next;
    }
    return p;
}

/**
 * @brief 链表尾插
 *
 * @param[in] node:需要插入的链表节点
 * @return NULL
 */
static void __led_list_tail(LED_DRIVER_LIST_T *node)
{
    LED_DRIVER_LIST_T *p;
    p = __led_list_traverse(p_led_driver_list);
    p->next = node;
}

/**
 * @brief 寻找pin脚对应的链表节点
 *
 * @param[in] node:链表起始节点 
 * @param[in] pin:pin脚
 * @return LED_DRIVER_LIST_T* 寻找到的链表节点地址
 */
static LED_DRIVER_LIST_T *__led_list_find_pin(LED_DRIVER_LIST_T *node, uint32_t pin)
{
    LED_DRIVER_LIST_T* p = node;
    while (NULL != p) {
        if (pin == p->pin) {
            return p;
        }

        if (p->next != NULL) {
            p = p->next;
        } else {
            break;
        }
    }
    return NULL;
}

#ifdef IS_FREERTOS
/**
* @brief led 闪烁定时器回调
*
* @param[in] 
* @return NULL
*/
static void __led_blink_timer_cb(TimerHandle_t xTimer)
{
    LED_DRIVER_LIST_T *p = NULL;
    uint32_t timer_id = 0;

    timer_id = (uint32_t)pvTimerGetTimerID(xTimer);
    p = __led_list_find_pin(p_led_driver_list, timer_id);
    if (NULL == p) {
        // xTimerStop(xTimer, 0);
        return;
    }

    if (LED_BLINK == p->mode) {
        if (p->blink_data.cnt % 2) {
            if (true == p->polarity) {
                led_driver->set_led_status(p->pin, true);
            } else {
                led_driver->set_led_status(p->pin, false);
            }
        } else {
            if (true == p->polarity) {
                led_driver->set_led_status(p->pin, false);
            } else {
                led_driver->set_led_status(p->pin, true);
            }
        }

        if ((p->blink_data.cnt > 0) && (p->blink_data.cnt < 0xffff)) {
            p->blink_data.cnt--;
        } else if (0 == p->blink_data.cnt) {
            p->mode = LED_ONOFF;
        }
        xTimerChangePeriod(p->timer_handle, p->blink_data.timer, 0);
    } else if (LED_ONOFF == p->mode) {
        led_driver_set_status((LED_HANDLE)p, p->onoff_status);
    }
}

/**
 * @brief led 闪烁配置
 *
 * @param[in] handle：led句柄，用于控制某个led
 * @param[in] data；led闪烁配置，具体可查看LED_BLINK_T结构体说明
 * @return NULL
 */
void led_blink_cfg(LED_HANDLE handle, LED_BLINK_T data)
{
    LED_DRIVER_LIST_T *p = (LED_DRIVER_LIST_T *)handle;
    if (data.cnt > 0x7fff) {
        p->blink_data.cnt = 0xffff;
    } else {
        p->blink_data.cnt = data.cnt * 2;
    }
    p->blink_data.timer = data.timer / portTICK_PERIOD_MS;
}

/**
 * @brief led 模式配置以及led显示
 *
 * @param[in] handle：led句柄，用于控制某个led
 * @param[in] mode：led模式配置
 * @return NULL
 */
void led_set_mode_control(LED_HANDLE handle, LED_MODE_E mode)
{
    LED_DRIVER_LIST_T *p = (LED_DRIVER_LIST_T *)handle;
    p->mode = mode;
    if (LED_ONOFF == mode) {
        xTimerStart(p->timer_handle, 0);
    } else if (LED_BLINK == mode) {
        if (pdFALSE == xTimerChangePeriod(p->timer_handle, p->blink_data.timer, 100)) {
            // uint32_t timer_id = 0;
            // timer_id = pvTimerGetTimerID(p->timer_handle);
            // printf("start timer:%ld timer:%dms error!", timer_id, p->blink_data.timer);
        }
    }
}
#endif

/**
 * @brief led 硬件函数注册
 *
 * @param[in] reg：函数指针注册
 * @return NULL
 */
void led_driver_reg(LED_DRIVER_REG_T* reg)
{
    if (NULL == led_driver) {
        led_driver = malloc(sizeof(LED_DRIVER_REG_T));
    }
    memcpy(led_driver, reg, sizeof(LED_DRIVER_REG_T));
}

/**
 * @brief led 初始化
 *
 * @param[in] pin:led pin脚
 * @param[in] polarity:led 极性（true：高电平有效  false：低电平有效）
 * @return handle:led 控制句柄
 */
LED_HANDLE led_driver_init(uint32_t pin, bool polarity)
{
    LED_HANDLE handle = NULL;
    if (NULL == led_driver->led_init) {
        return handle;
    }

    LED_DRIVER_LIST_T *p_led_list = NULL;
    p_led_list = malloc(sizeof(LED_DRIVER_LIST_T));
    p_led_list->next = NULL;
    p_led_list->pin = pin;
    p_led_list->polarity = polarity;
#ifdef IS_FREERTOS
    p_led_list->mode = LED_ONOFF;
    p_led_list->blink_data.cnt = 0;
    p_led_list->blink_data.timer = 100;
    p_led_list->onoff_status = false;
    p_led_list->timer_handle = xTimerCreate((const char *)"led_blink_timer",
                                            (TickType_t)p_led_list->blink_data.timer / portTICK_PERIOD_MS, /*定时器周期 1000(tick) */
                                            (UBaseType_t)pdFALSE,                                           /* 单次模式 */
                                            (void *)p_led_list->pin,                                       /*为每个计时器分配一个索引的唯一 ID */
                                            (TimerCallbackFunction_t)__led_blink_timer_cb);
#endif

    if (NULL == p_led_driver_list) {
        p_led_driver_list = p_led_list;
        handle = (LED_HANDLE)p_led_driver_list;
    } else {
        __led_list_tail(p_led_list);
        handle = (LED_HANDLE)p_led_list;
    }

    led_driver->led_init(pin, polarity);
    return handle;
}

/**
 * @brief led 初始化
 *
 * @param[in] handle:led 句柄
 * @param[in] onoff:true：打开  false：关闭
 * @return NULL
 */
void led_driver_set_status(LED_HANDLE handle, bool onoff)
{
    if (NULL == led_driver->set_led_status) {
        return;
    }
    LED_DRIVER_LIST_T *p = (LED_DRIVER_LIST_T *)handle;

#ifdef IS_FREERTOS
    p->onoff_status = onoff;
    if (LED_BLINK == p->mode) {
        return;
    }
#endif

    if (true == p->polarity) {
        led_driver->set_led_status(p->pin, onoff);
    } else {
        led_driver->set_led_status(p->pin, !onoff);
    }
}

/**
 * @brief led 初始化
 *
 * @param[in] handle:led 句柄

 * @return bool:true：打开  false：关闭
 */
bool led_driver_get_status(LED_HANDLE handle)
{
    if (NULL == led_driver->get_led_status) {
        return false;
    }

    LED_DRIVER_LIST_T *p = (LED_DRIVER_LIST_T *)handle;
    bool flag = false;

    flag = led_driver->get_led_status(p->pin);
    if (false == p->polarity) {
        flag = !flag;
    }

    return flag;
}
