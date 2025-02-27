/**
 * @file led.c
 * @author 小王同学
 * @brief led module is used to
 * @version 0.1
 * @date 2022-05-27
 *
 */
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// #include "esp_log.h"
#include "led_driver.h"
#include "led.h"
#include "cli.h"
#include "chip_pinmux.h"
#include "gpio.h"
/***********************************************************
*************************micro define***********************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
// static const char *tag = "led";

/***********************************************************
***********************function define**********************
***********************************************************/
/**
 * @brief led init!
 *
 * @param[NULL]
 * @return ESP_OK is ok! other please look esp_err.h
 */
static void __user_led_init(uint32_t pin, bool polarity)
{
    hal_gpio_dir_set(pin, DRV_GPIO_ARG_DIR_OUT);

    if (true == polarity) {
        hal_gpio_write(pin, 0);
    } else {
        hal_gpio_write(pin, 1);
    }
    return;
}

/**
 * @brief set led status
 *
 * @param[in] pin: led pin
 * @param[in] onoff: set led status
 * @return NULL
 */
static void __user_led_set_status(uint32_t pin, bool onoff)
{
    // ESP_LOGI(tag, "set:pin:%d, status:%d", pin, onoff);
    hal_gpio_write(pin, onoff);
}

/**
 * @brief get led status
 *
 * @param[in] pin: led pin
 * @return NULL
 */
static bool __user_led_get_status(uint32_t pin)
{

    // ESP_LOGI(tag, "get:pin:%d, status:%d", pin, gpio_get_level((gpio_num_t)pin));
    return (bool)hal_gpio_read(pin);
}

/**
 * @brief register cb to led_driver
 *
 * @param[NULL]
 * @return NULL
 */
void user_led_reg(void)
{
    LED_DRIVER_REG_T reg;

    reg.led_init = __user_led_init;
    reg.set_led_status = __user_led_set_status;
    reg.get_led_status = __user_led_get_status;
    led_driver_reg(&reg);
}

#define LED_PIN GPIO_NUM_0

static void led_test_task(void *arg)
{
    LED_HANDLE led1_handle;
    // bool onoff_status = false;

    user_led_reg();
    PIN_FUNC_SET(IO_MUX_GPIO0, FUNC_GPIO0_GPIO0);
    led1_handle = led_driver_init(LED_PIN, true);
    LED_BLINK_T blink_data = {.cnt=0xffff,.timer=1000};
    led_blink_cfg(led1_handle, blink_data);
    led_set_mode_control(led1_handle, LED_BLINK);

    // while(1)
    // {
        // os_printf(LM_APP, LL_INFO, "onoff_status %d\n", onoff_status);
        // onoff_status = led_driver_get_status(led1_handle);
        // if (false == onoff_status) {
        //     led_driver_set_status(led1_handle, true);
        // } else {
        //     led_driver_set_status(led1_handle, false);
        // }
    //     os_printf(LM_APP, LL_INFO, "test running %d\n");
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
    vTaskDelay((TickType_t)NULL);
}

void user_led_test(void)
{
    xTaskCreate(&led_test_task, "led_test_task", 1024, NULL, 15, NULL); //创建扫描任务
}
