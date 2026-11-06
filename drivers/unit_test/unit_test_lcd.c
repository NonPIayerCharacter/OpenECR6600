#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "lcd.h"

static const char *utest_lcd_panel_name(lcd_panel_type_t panel)
{
    switch (panel) {
    case LCD_PANEL_ST7789:
        return "ST7789";
    case LCD_PANEL_ST77916:
        return "ST77916";
    default:
        return "unknown";
    }
}

static void utest_lcd_print_config(const char *tag, const lcd_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    os_printf(LM_APP, LL_INFO,
              "[ut_lcd] %s panel=%s if=spi fmt=rgb565 res=%ux%u spi=%u dma=%u clk_div=%u max_tx=%u\n",
              tag,
              utest_lcd_panel_name(cfg->panel),
              cfg->hor_res,
              cfg->ver_res,
              cfg->spi_num,
              cfg->use_dma,
              cfg->spi_clk_div,
              cfg->max_transfer_bytes);
    os_printf(LM_APP, LL_INFO,
              "[ut_lcd] %s gpio dc=%d rst=%d rst_active=%u bl=%d bl_active=%u\n",
              tag,
              cfg->dc_gpio,
              cfg->rst_gpio,
              cfg->rst_active_high,
              cfg->bl_gpio,
              cfg->bl_active_high);
    os_printf(LM_APP, LL_INFO,
              "[ut_lcd] %s color bgr=%u invert=%u gap=%u,%u\n",
              tag,
              cfg->bgr,
              cfg->invert_color,
              cfg->x_gap,
              cfg->y_gap);
}

static int utest_lcd_ensure_init(void)
{
    lcd_config_t cfg;
    int ret;

    ret = drv_lcd_get_config(&cfg);
    if (ret == LCD_RET_OK) {
        return LCD_RET_OK;
    }
    if (ret != LCD_RET_NOT_INIT) {
        return ret;
    }

    ret = drv_lcd_get_default_config(&cfg);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = drv_lcd_init(&cfg);
    if (ret == LCD_RET_OK) {
        os_printf(LM_APP, LL_INFO, "[ut_lcd] auto init ok\n");
    }

    return ret;
}

static int utest_lcd_init(int argc, char *argv[])
{
    lcd_config_t cfg;
    int ret;
    (void)argc;
    (void)argv;

    ret = drv_lcd_get_default_config(&cfg);
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] default config failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    ret = drv_lcd_init(&cfg);
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] init failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, init, utest_lcd_init, "unit test lcd init", "ut_lcd init");

static int utest_lcd_cfg(int argc, char *argv[])
{
    lcd_config_t cfg;
    int ret;
    (void)argc;
    (void)argv;

    ret = drv_lcd_get_config(&cfg);
    if (ret == LCD_RET_OK) {
        utest_lcd_print_config("current", &cfg);
        return CMD_RET_SUCCESS;
    }

    ret = drv_lcd_get_default_config(&cfg);
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] default config failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    os_printf(LM_APP, LL_INFO, "[ut_lcd] lcd is not initialized, show default config\n");
    utest_lcd_print_config("default", &cfg);
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, cfg, utest_lcd_cfg, "unit test lcd config", "ut_lcd cfg");

static int utest_lcd_fill(int argc, char *argv[])
{
    unsigned int color = 0U;
    int ret;

    if (argc != 2) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd fill <rgb565>\n");
        return CMD_RET_FAILURE;
    }

    color = strtoul(argv[1], NULL, 0);
    ret = drv_lcd_fill_color((uint16_t)color);
    if (ret == LCD_RET_NOT_INIT) {
        ret = utest_lcd_ensure_init();
        if (ret == LCD_RET_OK) {
            ret = drv_lcd_fill_color((uint16_t)color);
        }
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] fill failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, fill, utest_lcd_fill, "unit test lcd fill", "ut_lcd fill <rgb565>");

static int utest_lcd_bars(int argc, char *argv[])
{
    static const uint16_t colors[] = {
        0x0000U, 0xffffU, 0xf800U, 0x07e0U,
        0x001fU, 0xffe0U, 0x07ffU, 0xf81fU,
    };
    lcd_config_t cfg;
    uint32_t i;
    uint32_t count = sizeof(colors) / sizeof(colors[0]);
    int ret;
    (void)argc;
    (void)argv;

    ret = utest_lcd_ensure_init();
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] init failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    ret = drv_lcd_get_config(&cfg);
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] get config failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    for (i = 0; i < count; i++) {
        uint16_t xs = (uint16_t)((i * cfg.hor_res) / count);
        uint16_t xe = (uint16_t)(((i + 1U) * cfg.hor_res) / count);

        if (xe <= xs) {
            xe = (uint16_t)(xs + 1U);
        }
        ret = drv_lcd_fill_rect(xs, 0U, xe, cfg.ver_res, colors[i]);
        if (ret != LCD_RET_OK) {
            os_printf(LM_APP, LL_ERR, "[ut_lcd] bars failed at %u: %d\n", i, ret);
            return CMD_RET_FAILURE;
        }
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, bars, utest_lcd_bars, "unit test lcd color bars", "ut_lcd bars");

static int utest_lcd_rect(int argc, char *argv[])
{
    unsigned int xs;
    unsigned int ys;
    unsigned int xe;
    unsigned int ye;
    unsigned int color;
    int ret;

    if (argc != 6) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd rect <xs> <ys> <xe> <ye> <rgb565>\n");
        return CMD_RET_FAILURE;
    }

    xs = strtoul(argv[1], NULL, 0);
    ys = strtoul(argv[2], NULL, 0);
    xe = strtoul(argv[3], NULL, 0);
    ye = strtoul(argv[4], NULL, 0);
    color = strtoul(argv[5], NULL, 0);

    ret = drv_lcd_fill_rect((uint16_t)xs, (uint16_t)ys, (uint16_t)xe, (uint16_t)ye, (uint16_t)color);
    if (ret == LCD_RET_NOT_INIT) {
        ret = utest_lcd_ensure_init();
        if (ret == LCD_RET_OK) {
            ret = drv_lcd_fill_rect((uint16_t)xs, (uint16_t)ys, (uint16_t)xe, (uint16_t)ye, (uint16_t)color);
        }
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] rect failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, rect, utest_lcd_rect, "unit test lcd rect", "ut_lcd rect <xs> <ys> <xe> <ye> <rgb565>");

static int utest_lcd_disp(int argc, char *argv[])
{
    unsigned int on;
    int ret;

    if (argc != 2) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd disp <0|1>\n");
        return CMD_RET_FAILURE;
    }

    on = strtoul(argv[1], NULL, 0);
    if (on > 1U) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] invalid display value: %u\n", on);
        return CMD_RET_FAILURE;
    }

    ret = utest_lcd_ensure_init();
    if (ret == LCD_RET_OK) {
        ret = drv_lcd_display_on(on != 0U);
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] disp failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, disp, utest_lcd_disp, "unit test lcd display on/off", "ut_lcd disp <0|1>");

static int utest_lcd_bl(int argc, char *argv[])
{
    lcd_config_t cfg;
    unsigned int on;
    int ret;

    if (argc != 2) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd bl <0|1>\n");
        return CMD_RET_FAILURE;
    }

    on = strtoul(argv[1], NULL, 0);
    if (on > 1U) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] invalid backlight value: %u\n", on);
        return CMD_RET_FAILURE;
    }

    ret = utest_lcd_ensure_init();
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] init failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    ret = drv_lcd_get_config(&cfg);
    if (ret == LCD_RET_OK && cfg.bl_gpio < 0) {
        os_printf(LM_APP, LL_INFO, "[ut_lcd] backlight gpio is external/no-op\n");
    }

    ret = drv_lcd_backlight_on(on != 0U);
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] bl failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, bl, utest_lcd_bl, "unit test lcd backlight", "ut_lcd bl <0|1>");

static int utest_lcd_reset(int argc, char *argv[])
{
    int ret;
    (void)argc;
    (void)argv;

    ret = utest_lcd_ensure_init();
    if (ret == LCD_RET_OK) {
        ret = drv_lcd_reset();
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] reset failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, reset, utest_lcd_reset, "unit test lcd reset", "ut_lcd reset");

static int utest_lcd_gap(int argc, char *argv[])
{
    unsigned int x_gap;
    unsigned int y_gap;
    int ret;

    if (argc != 3) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd gap <x> <y>\n");
        return CMD_RET_FAILURE;
    }

    x_gap = strtoul(argv[1], NULL, 0);
    y_gap = strtoul(argv[2], NULL, 0);

    ret = utest_lcd_ensure_init();
    if (ret == LCD_RET_OK) {
        ret = drv_lcd_set_gap((uint16_t)x_gap, (uint16_t)y_gap);
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] gap failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, gap, utest_lcd_gap, "unit test lcd gap", "ut_lcd gap <x> <y>");

static int utest_lcd_rot(int argc, char *argv[])
{
    unsigned int rot;
    int ret;

    if (argc != 2) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd rot <0|1|2|3>\n");
        return CMD_RET_FAILURE;
    }

    rot = strtoul(argv[1], NULL, 0);
    if (rot > 3U) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] invalid rotation: %u\n", rot);
        return CMD_RET_FAILURE;
    }

    ret = utest_lcd_ensure_init();
    if (ret == LCD_RET_OK) {
        ret = drv_lcd_set_rotation((lcd_rotate_t)rot);
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] rot failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, rot, utest_lcd_rot, "unit test lcd rotation", "ut_lcd rot <0|1|2|3>");

static int utest_lcd_inv(int argc, char *argv[])
{
    unsigned int on;
    int ret;

    if (argc != 2) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] usage: ut_lcd inv <0|1>\n");
        return CMD_RET_FAILURE;
    }

    on = strtoul(argv[1], NULL, 0);
    if (on > 1U) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] invalid invert value: %u\n", on);
        return CMD_RET_FAILURE;
    }

    ret = utest_lcd_ensure_init();
    if (ret == LCD_RET_OK) {
        ret = drv_lcd_set_invert(on != 0U);
    }
    if (ret != LCD_RET_OK) {
        os_printf(LM_APP, LL_ERR, "[ut_lcd] inv failed: %d\n", ret);
        return CMD_RET_FAILURE;
    }

    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, inv, utest_lcd_inv, "unit test lcd inversion", "ut_lcd inv <0|1>");

static int utest_lcd_stats(int argc, char *argv[])
{
    lcd_perf_stats_t stats;

    if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
        drv_lcd_perf_reset();
        return CMD_RET_SUCCESS;
    }

    drv_lcd_perf_get(&stats);
    os_printf(LM_APP, LL_INFO,
              "[ut_lcd] stats bytes=%llu calls=%llu dma=%llu cpu=%llu pixels=%llu\n",
              stats.tx_bytes,
              stats.tx_calls,
              stats.tx_dma_calls,
              stats.tx_cpu_calls,
              stats.flush_pixels);
    return CMD_RET_SUCCESS;
}
CLI_SUBCMD(ut_lcd, stats, utest_lcd_stats, "unit test lcd stats", "ut_lcd stats [reset]");

CLI_CMD(ut_lcd, NULL, "unit test lcd", "ut_lcd [init|cfg|fill|bars|rect|disp|bl|reset|gap|inv|rot|stats]");

