#include <string.h>

#include "oshal.h"
#include "st77916.h"
#include "lcd_internal.h"

#define ST77916_CMD_MADCTL   0x36
#define ST77916_CMD_COLMOD   0x3A
#define ST77916_CMD_CASET    0x2A
#define ST77916_CMD_RASET    0x2B
#define ST77916_CMD_RAMWR    0x2C

static int st77916_fill_rect(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t rgb565);

static const uint8_t s_st77916_colmod[] = { 0x55 };
static const uint8_t s_st77916_madctl_bgr[] = { 0x08 };
static const uint8_t s_st77916_madctl_rgb[] = { 0x00 };
static const uint8_t s_st77916_c3[] = { 0x40, 0x00, 0x28 };
static const uint8_t s_st77916_c4[] = { 0xc0 };
static const uint8_t s_st77916_c5[] = { 0x02 };
static const uint8_t s_st77916_ea[] = { 0x38, 0x00, 0x12, 0x78 };
static const uint8_t s_st77916_b2[] = { 0x00, 0x64, 0x0e, 0x0d, 0x2f };
static const uint8_t s_st77916_b3[] = { 0x00, 0x00 };
static const uint8_t s_st77916_b7[] = { 0x75 };
static const uint8_t s_st77916_bb[] = { 0x19 };
static const uint8_t s_st77916_c0[] = { 0x2c };
static const uint8_t s_st77916_c2[] = { 0x01 };
static const uint8_t s_st77916_c6[] = { 0x0f };
static const uint8_t s_st77916_d0[] = { 0xa4, 0xa1 };

static const lcd_init_cmd_t s_st77916_init_cmds[] = {
    { 0x01, NULL, 0, 120 },
    { 0x11, NULL, 0, 120 },
    { 0x3A, s_st77916_colmod, sizeof(s_st77916_colmod), 0 },
    { 0xC3, s_st77916_c3, sizeof(s_st77916_c3), 0 },
    { 0xC4, s_st77916_c4, sizeof(s_st77916_c4), 0 },
    { 0xC5, s_st77916_c5, sizeof(s_st77916_c5), 0 },
    { 0xEA, s_st77916_ea, sizeof(s_st77916_ea), 0 },
    { 0xB2, s_st77916_b2, sizeof(s_st77916_b2), 0 },
    { 0xB3, s_st77916_b3, sizeof(s_st77916_b3), 0 },
    { 0xB7, s_st77916_b7, sizeof(s_st77916_b7), 0 },
    { 0xBB, s_st77916_bb, sizeof(s_st77916_bb), 0 },
    { 0xC0, s_st77916_c0, sizeof(s_st77916_c0), 0 },
    { 0xC2, s_st77916_c2, sizeof(s_st77916_c2), 0 },
    { 0xC6, s_st77916_c6, sizeof(s_st77916_c6), 0 },
    { 0xD0, s_st77916_d0, sizeof(s_st77916_d0), 0 },
};

static void st77916_write_u16be_pair(uint16_t first, uint16_t second, uint8_t *buf)
{
    buf[0] = (uint8_t)(first >> 8);
    buf[1] = (uint8_t)(first & 0xffU);
    buf[2] = (uint8_t)(second >> 8);
    buf[3] = (uint8_t)(second & 0xffU);
}

static uint16_t st77916_swap_rgb565(uint16_t rgb565)
{
    return (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
}

static int st77916_init(const lcd_config_t *cfg)
{
    int ret;
    const uint8_t *madctl;
    uint8_t invert_cmd;

    ret = lcd_panel_hw_reset(10U, 120U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_panel_run_init_cmds(s_st77916_init_cmds, sizeof(s_st77916_init_cmds) / sizeof(s_st77916_init_cmds[0]));
    if (ret != LCD_RET_OK) {
        return ret;
    }

    madctl = cfg->bgr ? s_st77916_madctl_bgr : s_st77916_madctl_rgb;
    ret = lcd_spi_send_cmd((const uint8_t[]){ ST77916_CMD_MADCTL }, 1U, madctl, 1U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    invert_cmd = cfg->invert_color ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    ret = lcd_spi_send_cmd(&invert_cmd, 1U, NULL, 0U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_spi_send_cmd((const uint8_t[]){ LCD_CMD_DISPON }, 1U, NULL, 0U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    return lcd_gpio_set_level(cfg->bl_gpio, cfg->bl_active_high, 1);
}

static int st77916_deinit(void)
{
    lcd_gpio_set_level(g_lcd_ctx.config.bl_gpio, g_lcd_ctx.config.bl_active_high, 0);
    return LCD_RET_OK;
}

static int st77916_reset(void)
{
    return st77916_init(&g_lcd_ctx.config);
}

static int st77916_display_on(int on)
{
    uint8_t cmd = on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    return lcd_spi_send_cmd(&cmd, 1U, NULL, 0U);
}

static int st77916_backlight_on(int on)
{
    return lcd_gpio_set_level(g_lcd_ctx.config.bl_gpio, g_lcd_ctx.config.bl_active_high, on);
}

static int st77916_set_invert(int on)
{
    uint8_t cmd = on ? LCD_CMD_INVON : LCD_CMD_INVOFF;

    return lcd_spi_send_cmd(&cmd, 1U, NULL, 0U);
}

static int st77916_set_rotation(lcd_rotate_t rot)
{
    uint8_t val;

    switch (rot) {
    case LCD_ROTATE_90:
        val = g_lcd_ctx.config.bgr ? 0x68 : 0x60;
        break;
    case LCD_ROTATE_180:
        val = g_lcd_ctx.config.bgr ? 0xC8 : 0xC0;
        break;
    case LCD_ROTATE_270:
        val = g_lcd_ctx.config.bgr ? 0xA8 : 0xA0;
        break;
    case LCD_ROTATE_0:
    default:
        val = g_lcd_ctx.config.bgr ? 0x08 : 0x00;
        break;
    }

    return lcd_spi_send_cmd((const uint8_t[]){ ST77916_CMD_MADCTL }, 1U, &val, 1U);
}

static int st77916_set_gap(uint16_t x_gap, uint16_t y_gap)
{
    g_lcd_ctx.config.x_gap = x_gap;
    g_lcd_ctx.config.y_gap = y_gap;
    return LCD_RET_OK;
}

static int st77916_set_window(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    uint8_t buf[4];
    int ret;

    xs = (uint16_t)(xs + g_lcd_ctx.config.x_gap);
    xe = (uint16_t)(xe + g_lcd_ctx.config.x_gap);
    ys = (uint16_t)(ys + g_lcd_ctx.config.y_gap);
    ye = (uint16_t)(ye + g_lcd_ctx.config.y_gap);

    st77916_write_u16be_pair(xs, (uint16_t)(xe - 1U), buf);
    ret = lcd_spi_send_cmd((const uint8_t[]){ ST77916_CMD_CASET }, 1U, buf, sizeof(buf));
    if (ret != LCD_RET_OK) {
        return ret;
    }

    st77916_write_u16be_pair(ys, (uint16_t)(ye - 1U), buf);
    return lcd_spi_send_cmd((const uint8_t[]){ ST77916_CMD_RASET }, 1U, buf, sizeof(buf));
}

static int st77916_draw_bitmap(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, const void *color_data)
{
    uint32_t bytes;
    int ret;

    ret = st77916_set_window(xs, ys, xe, ye);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    bytes = (uint32_t)(xe - xs) * (uint32_t)(ye - ys) * 2U;
    g_lcd_ctx.perf.flush_pixels += (uint64_t)(xe - xs) * (uint64_t)(ye - ys);
    return lcd_spi_send_color((const uint8_t[]){ ST77916_CMD_RAMWR }, 1U, (const uint8_t *)color_data, bytes);
}

static int st77916_fill_color(uint16_t rgb565)
{
    return st77916_fill_rect(0U, 0U, g_lcd_ctx.config.hor_res, g_lcd_ctx.config.ver_res, rgb565);
}

static int st77916_fill_rect(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t rgb565)
{
    uint32_t total_pixels;
    uint16_t width;
    uint16_t height;
    int ret;

    if (xe <= xs || ye <= ys) {
        return LCD_RET_INVAL;
    }

    width = (uint16_t)(xe - xs);
    height = (uint16_t)(ye - ys);
    total_pixels = (uint32_t)width * (uint32_t)height;

    ret = st77916_set_window(xs, ys, xe, ye);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_spi_send_cmd((const uint8_t[]){ ST77916_CMD_RAMWR }, 1U, NULL, 0U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_spi_write_color_repeat(st77916_swap_rgb565(rgb565), total_pixels);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    g_lcd_ctx.perf.flush_pixels += total_pixels;
    return LCD_RET_OK;
}

static void st77916_perf_get(lcd_perf_stats_t *stats)
{
    (void)stats;
}

static void st77916_perf_reset(void)
{
}

static const lcd_panel_ops_t s_st77916_ops = {
    .init = st77916_init,
    .deinit = st77916_deinit,
    .reset = st77916_reset,
    .display_on = st77916_display_on,
    .backlight_on = st77916_backlight_on,
    .set_invert = st77916_set_invert,
    .set_rotation = st77916_set_rotation,
    .set_gap = st77916_set_gap,
    .set_window = st77916_set_window,
    .draw_bitmap = st77916_draw_bitmap,
    .fill_color = st77916_fill_color,
    .fill_rect = st77916_fill_rect,
    .perf_get = st77916_perf_get,
    .perf_reset = st77916_perf_reset,
};

int drv_lcd_st77916_get_default_config(lcd_config_t *cfg)
{
    int ret;

    ret = drv_lcd_get_default_config(cfg);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    cfg->panel = LCD_PANEL_ST77916;
    return LCD_RET_OK;
}

int drv_lcd_st77916_get_ops(const lcd_panel_ops_t **ops)
{
    if (ops == NULL) {
        return LCD_RET_INVAL;
    }

    *ops = &s_st77916_ops;
    return LCD_RET_OK;
}

