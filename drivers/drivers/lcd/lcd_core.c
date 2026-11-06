#include <string.h>

#include "oshal.h"
#include "lcd_internal.h"

lcd_context_t g_lcd_ctx;

static int lcd_config_equals(const lcd_config_t *lhs, const lcd_config_t *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }

    return lhs->panel == rhs->panel
        && lhs->if_type == rhs->if_type
        && lhs->pixel_format == rhs->pixel_format
        && lhs->spi_num == rhs->spi_num
        && lhs->use_dma == rhs->use_dma
        && lhs->spi_clk_div == rhs->spi_clk_div
        && lhs->max_transfer_bytes == rhs->max_transfer_bytes
        && lhs->hor_res == rhs->hor_res
        && lhs->ver_res == rhs->ver_res
        && lhs->x_gap == rhs->x_gap
        && lhs->y_gap == rhs->y_gap
        && lhs->dc_gpio == rhs->dc_gpio
        && lhs->rst_gpio == rhs->rst_gpio
        && lhs->bl_gpio == rhs->bl_gpio
        && lhs->rst_active_high == rhs->rst_active_high
        && lhs->bl_active_high == rhs->bl_active_high
        && lhs->bgr == rhs->bgr
        && lhs->invert_color == rhs->invert_color;
}

static int lcd_window_is_valid(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    if (xs >= xe || ys >= ye) {
        return 0;
    }

    if (xe > g_lcd_ctx.config.hor_res || ye > g_lcd_ctx.config.ver_res) {
        return 0;
    }

    return 1;
}

static int lcd_validate_gpio(int gpio_num)
{
    if (gpio_num < 0) {
        return 1;
    }

    return gpio_num <= GPIO_NUM_MAX;
}

static int lcd_validate_config(const lcd_config_t *cfg)
{
    if (cfg == NULL) {
        return LCD_RET_INVAL;
    }

    if (cfg->if_type != LCD_IF_SPI || cfg->pixel_format != LCD_PIXEL_RGB565) {
        return LCD_RET_INVAL;
    }

    if (cfg->spi_num != LCD_SPI_NUM_1) {
        return LCD_RET_INVAL;
    }

    if (cfg->max_transfer_bytes < LCD_SPI_MIN_TRANSFER_BYTES
        || cfg->max_transfer_bytes > LCD_SPI_MAX_TRANSFER_BYTES
        || cfg->hor_res == 0U || cfg->ver_res == 0U) {
        return LCD_RET_INVAL;
    }

    if (cfg->dc_gpio < 0 || !lcd_validate_gpio(cfg->dc_gpio)
        || !lcd_validate_gpio(cfg->rst_gpio)
        || !lcd_validate_gpio(cfg->bl_gpio)) {
        return LCD_RET_INVAL;
    }

    switch (cfg->panel) {
    case LCD_PANEL_ST7789:
    case LCD_PANEL_ST77916:
        return LCD_RET_OK;
    default:
        return LCD_RET_INVAL;
    }
}

static int lcd_lock(void)
{
    if (g_lcd_ctx.lock == NULL) {
        return LCD_RET_ERROR;
    }

    if (os_mutex_lock(g_lcd_ctx.lock, WAIT_FOREVER) != 0) {
        return LCD_RET_ERROR;
    }

    return LCD_RET_OK;
}

static void lcd_unlock(void)
{
    if (g_lcd_ctx.lock != NULL) {
        os_mutex_unlock(g_lcd_ctx.lock);
    }
}

int drv_lcd_get_default_config(lcd_config_t *cfg)
{
    if (cfg == NULL) {
        return LCD_RET_INVAL;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->if_type = LCD_IF_SPI;
    cfg->pixel_format = LCD_PIXEL_RGB565;
    cfg->spi_num = CONFIG_LCD_SPI_NUM;
#if defined(CONFIG_LCD_USE_DMA)
    cfg->use_dma = 1U;
#else
    cfg->use_dma = 0U;
#endif
    cfg->spi_clk_div = CONFIG_LCD_SPI_CLK_DIV;
    cfg->max_transfer_bytes = CONFIG_LCD_MAX_TRANSFER_BYTES;
    cfg->hor_res = CONFIG_LCD_HOR_RES;
    cfg->ver_res = CONFIG_LCD_VER_RES;
    cfg->dc_gpio = CONFIG_LCD_DC_GPIO;
    cfg->rst_gpio = CONFIG_LCD_RST_GPIO;
    cfg->bl_gpio = CONFIG_LCD_BL_GPIO;
#if defined(CONFIG_LCD_RST_ACTIVE_HIGH)
    cfg->rst_active_high = 1U;
#else
    cfg->rst_active_high = 0U;
#endif
#if defined(CONFIG_LCD_BL_ACTIVE_HIGH)
    cfg->bl_active_high = 1U;
#else
    cfg->bl_active_high = 0U;
#endif
#if defined(CONFIG_LCD_BGR)
    cfg->bgr = 1U;
#else
    cfg->bgr = 0U;
#endif
#if defined(CONFIG_LCD_INVERT_COLOR)
    cfg->invert_color = 1U;
#else
    cfg->invert_color = 0U;
#endif
#if defined(CONFIG_LCD_ST7789) && defined(CONFIG_LCD_ST77916)
    if (cfg->hor_res == 360U && cfg->ver_res == 360U) {
        cfg->panel = LCD_PANEL_ST77916;
    } else {
        cfg->panel = LCD_PANEL_ST7789;
    }
#elif defined(CONFIG_LCD_ST77916)
    cfg->panel = LCD_PANEL_ST77916;
#elif defined(CONFIG_LCD_ST7789)
    cfg->panel = LCD_PANEL_ST7789;
#else
    return LCD_RET_ERROR;
#endif
    return LCD_RET_OK;
}

int drv_lcd_get_config(lcd_config_t *cfg)
{
    if (cfg == NULL) {
        return LCD_RET_INVAL;
    }

    if (!g_lcd_ctx.initialized) {
        return LCD_RET_NOT_INIT;
    }

    *cfg = g_lcd_ctx.config;
    return LCD_RET_OK;
}

int drv_lcd_init(const lcd_config_t *cfg)
{
    const lcd_panel_ops_t *ops;
    int ret;

    ret = lcd_validate_config(cfg);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    if (g_lcd_ctx.initialized) {
        if (lcd_config_equals(&g_lcd_ctx.init_config, cfg)) {
            return LCD_RET_OK;
        }

        return LCD_RET_BUSY;
    }

    ops = lcd_probe_get_ops(cfg->panel);
    if (ops == NULL) {
        return LCD_RET_ERROR;
    }

    memset(&g_lcd_ctx, 0, sizeof(g_lcd_ctx));
    g_lcd_ctx.init_config = *cfg;
    g_lcd_ctx.config = *cfg;
    g_lcd_ctx.panel_hor_res = cfg->hor_res;
    g_lcd_ctx.panel_ver_res = cfg->ver_res;
    g_lcd_ctx.rotation = LCD_ROTATE_0;
    g_lcd_ctx.ops = ops;

    ret = lcd_spi_bus_init(cfg);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    g_lcd_ctx.lock = os_mutex_create();
    if (g_lcd_ctx.lock == NULL) {
        lcd_spi_bus_deinit();
        memset(&g_lcd_ctx, 0, sizeof(g_lcd_ctx));
        return LCD_RET_NOMEM;
    }

    ret = g_lcd_ctx.ops->init(cfg);
    if (ret != LCD_RET_OK) {
        os_mutex_destroy(g_lcd_ctx.lock);
        lcd_spi_bus_deinit();
        memset(&g_lcd_ctx, 0, sizeof(g_lcd_ctx));
        return ret;
    }

    g_lcd_ctx.initialized = 1U;
    return LCD_RET_OK;
}

int drv_lcd_deinit(void)
{
    os_mutex_handle_t lock;
    int ret = LCD_RET_OK;

    if (!g_lcd_ctx.initialized) {
        return LCD_RET_NOT_INIT;
    }

    lock = g_lcd_ctx.lock;
    if (lcd_lock() != LCD_RET_OK) {
        return LCD_RET_ERROR;
    }

    if (g_lcd_ctx.ops != NULL && g_lcd_ctx.ops->deinit != NULL) {
        ret = g_lcd_ctx.ops->deinit();
    }

    lcd_spi_bus_deinit();
    lcd_unlock();
    if (lock != NULL) {
        os_mutex_destroy(lock);
    }
    memset(&g_lcd_ctx, 0, sizeof(g_lcd_ctx));
    return ret;
}

int drv_lcd_reset(void)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->reset == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->reset();
    if (ret == LCD_RET_OK) {
        g_lcd_ctx.rotation = LCD_ROTATE_0;
        g_lcd_ctx.config.hor_res = g_lcd_ctx.panel_hor_res;
        g_lcd_ctx.config.ver_res = g_lcd_ctx.panel_ver_res;
    }
    lcd_unlock();
    return ret;
}

int drv_lcd_display_on(int on)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->display_on == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->display_on(on);
    lcd_unlock();
    return ret;
}

int drv_lcd_backlight_on(int on)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->backlight_on == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->backlight_on(on);
    lcd_unlock();
    return ret;
}

int drv_lcd_set_invert(int on)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->set_invert == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->set_invert(on);
    if (ret == LCD_RET_OK) {
        g_lcd_ctx.config.invert_color = on ? 1U : 0U;
    }
    lcd_unlock();
    return ret;
}

int drv_lcd_set_rotation(lcd_rotate_t rot)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->set_rotation == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->set_rotation(rot);
    if (ret == LCD_RET_OK) {
        g_lcd_ctx.rotation = rot;
        /* Keep the shared logical draw size aligned with the panel scan direction. */
        if (rot == LCD_ROTATE_90 || rot == LCD_ROTATE_270) {
            g_lcd_ctx.config.hor_res = g_lcd_ctx.panel_ver_res;
            g_lcd_ctx.config.ver_res = g_lcd_ctx.panel_hor_res;
        } else {
            g_lcd_ctx.config.hor_res = g_lcd_ctx.panel_hor_res;
            g_lcd_ctx.config.ver_res = g_lcd_ctx.panel_ver_res;
        }
    }
    lcd_unlock();
    return ret;
}

int drv_lcd_set_gap(uint16_t x_gap, uint16_t y_gap)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->set_gap == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->set_gap(x_gap, y_gap);
    lcd_unlock();
    return ret;
}

int drv_lcd_set_window(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->set_window == NULL) {
        return LCD_RET_NOT_INIT;
    }

    if (!lcd_window_is_valid(xs, ys, xe, ye)) {
        return LCD_RET_INVAL;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->set_window(xs, ys, xe, ye);
    lcd_unlock();
    return ret;
}

int drv_lcd_draw_bitmap(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const void *color_data)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->draw_bitmap == NULL) {
        return LCD_RET_NOT_INIT;
    }

    if (color_data == NULL || !lcd_window_is_valid(xs, ys, xe, ye)) {
        return LCD_RET_INVAL;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->draw_bitmap(xs, ys, xe, ye, color_data);
    lcd_unlock();
    return ret;
}

int drv_lcd_fill_color(uint16_t rgb565)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->fill_color == NULL) {
        return LCD_RET_NOT_INIT;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->fill_color(rgb565);
    lcd_unlock();
    return ret;
}

int drv_lcd_fill_rect(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t rgb565)
{
    int ret;

    if (!g_lcd_ctx.initialized || g_lcd_ctx.ops == NULL || g_lcd_ctx.ops->fill_rect == NULL) {
        return LCD_RET_NOT_INIT;
    }

    if (!lcd_window_is_valid(xs, ys, xe, ye)) {
        return LCD_RET_INVAL;
    }

    ret = lcd_lock();
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = g_lcd_ctx.ops->fill_rect(xs, ys, xe, ye, rgb565);
    lcd_unlock();
    return ret;
}

void drv_lcd_perf_reset(void)
{
    if (g_lcd_ctx.initialized && lcd_lock() != LCD_RET_OK) {
        return;
    }

    memset(&g_lcd_ctx.perf, 0, sizeof(g_lcd_ctx.perf));
    if (g_lcd_ctx.ops != NULL && g_lcd_ctx.ops->perf_reset != NULL) {
        g_lcd_ctx.ops->perf_reset();
    }

    if (g_lcd_ctx.initialized) {
        lcd_unlock();
    }
}

void drv_lcd_perf_get(lcd_perf_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    if (g_lcd_ctx.initialized && lcd_lock() != LCD_RET_OK) {
        return;
    }

    *stats = g_lcd_ctx.perf;
    if (g_lcd_ctx.ops != NULL && g_lcd_ctx.ops->perf_get != NULL) {
        g_lcd_ctx.ops->perf_get(stats);
    }

    if (g_lcd_ctx.initialized) {
        lcd_unlock();
    }
}

