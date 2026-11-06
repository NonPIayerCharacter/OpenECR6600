#ifndef DRV_LCD_H
#define DRV_LCD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_RET_OK         0
#define LCD_RET_ERROR     -1
#define LCD_RET_INVAL     -2
#define LCD_RET_NOMEM     -3
#define LCD_RET_BUSY      -4
#define LCD_RET_TIMEOUT   -5
#define LCD_RET_NOT_INIT  -6

typedef enum {
    LCD_PANEL_ST7789 = 0,
    LCD_PANEL_ST77916 = 1,
} lcd_panel_type_t;

typedef enum {
    LCD_IF_SPI = 0,
} lcd_if_type_t;

typedef enum {
    LCD_PIXEL_RGB565 = 0,
} lcd_pixel_format_t;

typedef enum {
    LCD_ROTATE_0 = 0,
    LCD_ROTATE_90,
    LCD_ROTATE_180,
    LCD_ROTATE_270,
} lcd_rotate_t;

typedef struct {
    lcd_panel_type_t panel;
    lcd_if_type_t if_type;
    lcd_pixel_format_t pixel_format;
    uint8_t spi_num;
    uint8_t use_dma;
    uint32_t spi_clk_div;
    uint32_t max_transfer_bytes;
    uint16_t hor_res;
    uint16_t ver_res;
    uint16_t x_gap;
    uint16_t y_gap;
    int dc_gpio;
    int rst_gpio;
    int bl_gpio;
    uint8_t rst_active_high;
    uint8_t bl_active_high;
    uint8_t bgr;
    uint8_t invert_color;
} lcd_config_t;

typedef struct {
    uint64_t tx_bytes;
    uint64_t tx_calls;
    uint64_t tx_dma_calls;
    uint64_t tx_cpu_calls;
    uint64_t flush_pixels;
} lcd_perf_stats_t;

int drv_lcd_get_default_config(lcd_config_t *cfg);
int drv_lcd_get_config(lcd_config_t *cfg);
int drv_lcd_init(const lcd_config_t *cfg);
int drv_lcd_deinit(void);
int drv_lcd_reset(void);
int drv_lcd_display_on(int on);
int drv_lcd_backlight_on(int on);
int drv_lcd_set_invert(int on);
int drv_lcd_set_rotation(lcd_rotate_t rot);
int drv_lcd_set_gap(uint16_t x_gap, uint16_t y_gap);
int drv_lcd_set_window(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
int drv_lcd_draw_bitmap(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const void *color_data);
int drv_lcd_fill_color(uint16_t rgb565);
int drv_lcd_fill_rect(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t rgb565);
void drv_lcd_perf_reset(void);
void drv_lcd_perf_get(lcd_perf_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif

