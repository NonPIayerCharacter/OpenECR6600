/**
* @file led_driver.h
* @author 小王同学
* @brief led_driver module is used to led driver
* @version 0.1
* @date 2022-08-09
*
*/
 
#ifndef __LED_DRIVER_H__
#define __LED_DRIVER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
 
#ifdef __cplusplus
extern "C" {
#endif
 
/***********************************************************
*************************micro define***********************
***********************************************************/
#define IS_FREERTOS                     //是否为RTOS，支持闪烁功能

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef void* LED_HANDLE;

typedef struct {
    void (*led_init)(uint32_t pin, bool polarity);
    void (*set_led_status)(uint32_t pin, bool onoff);
    bool (*get_led_status)(uint32_t pin); /* data */
} LED_DRIVER_REG_T;
 
#ifdef IS_FREERTOS  
typedef struct {
    uint16_t cnt;    //led 闪烁次数（范围1~0x7fff，大于0x7fff的为一直闪烁）
    uint16_t timer; // led 闪烁时间（ms）
} LED_BLINK_T;

typedef enum {
    LED_ONOFF = 0,
    LED_BLINK,
} LED_MODE_E;
#endif

/***********************************************************
***********************variable define**********************
***********************************************************/
 
/***********************************************************
***********************function define**********************
***********************************************************/
/**
 * @brief led 硬件函数注册
 *
 * @param[in] reg：函数指针注册
 * @return NULL
 */
void led_driver_reg(LED_DRIVER_REG_T *reg);

/**
 * @brief led 初始化
 *
 * @param[in] pin:led pin脚
 * @param[in] polarity:led 极性（true：高电平有效  false：低电平有效）
 * @return handle:led 控制句柄
 */
LED_HANDLE led_driver_init(uint32_t pin, bool polarit);

/**
 * @brief led 初始化
 *
 * @param[in] handle:led 句柄
 * @param[in] onoff:true：打开  false：关闭
 * @return NULL
 */
void led_driver_set_status(LED_HANDLE handle, bool onoff);

/**
 * @brief led 初始化
 *
 * @param[in] handle:led 句柄

 * @return bool:true：打开  false：关闭
 */
bool led_driver_get_status(LED_HANDLE handle);

#ifdef IS_FREERTOS
/**
 * @brief led 闪烁配置
 *
 * @param[in] handle：led句柄，用于控制某个led
 * @param[in] data；led闪烁配置，具体可查看LED_BLINK_T结构体说明
 * @return NULL
 */
void led_blink_cfg(LED_HANDLE handle, LED_BLINK_T data);

/**
 * @brief led 模式配置以及led显示
 *
 * @param[in] handle：led句柄，用于控制某个led
 * @param[in] mode：led模式配置
 * @return NULL
 */
void led_set_mode_control(LED_HANDLE handle, LED_MODE_E mode);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*__LED_DRIVER_H__*/