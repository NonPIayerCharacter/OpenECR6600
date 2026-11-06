#ifndef DRV_LCD_INTERNAL_H
#define DRV_LCD_INTERNAL_H

#include "lcd.h"
#include "oshal.h"
#include "gpio.h"
#include "spi.h"
#include "chip_pinmux.h"
#include "chip_clk_ctrl.h"

#define LCD_CMD_CASET     0x2A
#define LCD_CMD_RASET     0x2B
#define LCD_CMD_RAMWR     0x2C
#define LCD_CMD_MADCTL    0x36
#define LCD_CMD_COLMOD    0x3A
#define LCD_CMD_INVON     0x21
#define LCD_CMD_INVOFF    0x20
#define LCD_CMD_NORON     0x13
#define LCD_CMD_DISPON    0x29
#define LCD_CMD_DISPOFF   0x28
#define LCD_CMD_SLPIN     0x10
#define LCD_CMD_SLPOUT    0x11

#define LCD_SPI_NUM_1                 1U
#define LCD_SPI_MIN_TRANSFER_BYTES    4U
#define LCD_SPI_MAX_TRANSFER_BYTES    512U

typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    uint16_t data_bytes;
    uint16_t delay_ms;
} lcd_init_cmd_t;

typedef struct {
    int (*init)(const lcd_config_t *cfg);
    int (*deinit)(void);
    int (*reset)(void);
    int (*display_on)(int on);
    int (*backlight_on)(int on);
    int (*set_invert)(int on);
    int (*set_rotation)(lcd_rotate_t rot);
    int (*set_gap)(uint16_t x_gap, uint16_t y_gap);
    int (*set_window)(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
    int (*draw_bitmap)(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const void *color_data);
    int (*fill_color)(uint16_t rgb565);
    int (*fill_rect)(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t rgb565);
    void (*perf_get)(lcd_perf_stats_t *stats);
    void (*perf_reset)(void);
} lcd_panel_ops_t;

typedef struct {
    uint8_t initialized;
    lcd_rotate_t rotation;
    lcd_config_t init_config;
    lcd_config_t config;
    uint16_t panel_hor_res;
    uint16_t panel_ver_res;
    os_mutex_handle_t lock;
    lcd_perf_stats_t perf;
    const lcd_panel_ops_t *ops;
} lcd_context_t;

extern lcd_context_t g_lcd_ctx;

const lcd_panel_ops_t *lcd_probe_get_ops(lcd_panel_type_t panel);

int lcd_spi_bus_init(const lcd_config_t *cfg);
int lcd_spi_bus_deinit(void);
int lcd_spi_send_cmd(const uint8_t *cmd, uint32_t cmd_size, const uint8_t *param, uint32_t param_size);
int lcd_spi_send_color(const uint8_t *cmd, uint32_t cmd_size, const uint8_t *param, uint32_t param_size);
int lcd_spi_write_color_data(const uint8_t *param, uint32_t param_size);
/* color_data must already match the panel bus byte order. */
int lcd_spi_write_color_repeat(uint16_t color_data, uint32_t pixel_count);
int lcd_gpio_set_level(int gpio_num, uint8_t active_high, int on);
int lcd_panel_hw_reset(uint32_t assert_ms, uint32_t deassert_ms);
int lcd_panel_run_init_cmds(const lcd_init_cmd_t *cmds, uint32_t count);

int drv_lcd_st7789_get_ops(const lcd_panel_ops_t **ops);
int drv_lcd_st77916_get_ops(const lcd_panel_ops_t **ops);

#endif

