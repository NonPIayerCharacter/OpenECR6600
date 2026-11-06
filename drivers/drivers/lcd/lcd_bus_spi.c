#include <stdint.h>
#include <string.h>

#include "oshal.h"
#include "lcd_internal.h"

#define LCD_SPI_FILL_PIXELS (LCD_SPI_MAX_TRANSFER_BYTES / sizeof(uint16_t))

int lcd_gpio_set_level(int gpio_num, uint8_t active_high, int on)
{
    uint32_t level;

    if (gpio_num < 0) {
        return LCD_RET_OK;
    }

    level = on ? DRV_GPIO_LEVEL_HIGH : DRV_GPIO_LEVEL_LOW;
    if (!active_high) {
        level = on ? DRV_GPIO_LEVEL_LOW : DRV_GPIO_LEVEL_HIGH;
    }

    if (drv_gpio_ioctrl(gpio_num, DRV_GPIO_CTRL_SET_DIRCTION, DRV_GPIO_ARG_DIR_OUT) != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    if (drv_gpio_write(gpio_num, (GPIO_LEVEL_VALUE)level) != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    return LCD_RET_OK;
}

static int lcd_gpio_config_output(int gpio_num)
{
    if (gpio_num < 0) {
        return LCD_RET_OK;
    }

    chip_io_mode_pinmux_cfg(gpio_num, 1);

    if (drv_gpio_ioctrl(gpio_num, DRV_GPIO_CTRL_SET_DIRCTION, DRV_GPIO_ARG_DIR_OUT) != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    if (drv_gpio_ioctrl(gpio_num, DRV_GPIO_CTRL_INTR_MODE, DRV_GPIO_ARG_INTR_MODE_NOP) != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    if (drv_gpio_ioctrl(gpio_num, DRV_GPIO_CTRL_INTR_DISABLE, 0) != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    return LCD_RET_OK;
}

static int lcd_spi_init_legacy(const lcd_config_t *cfg)
{
    spi_interface_config_t spi_cfg;
    int ret;

    memset(&spi_cfg, 0, sizeof(spi_cfg));
    spi_cfg.addr_len = 1U;
    spi_cfg.data_len = 8U;
    spi_cfg.spi_clk_pol = 0U;
    spi_cfg.spi_clk_pha = 0U;
    spi_cfg.spi_trans_mode = SPI_MODE_STANDARD;
    spi_cfg.master_clk = (E_SPI_SCLK_DIV)cfg->spi_clk_div;
    spi_cfg.addr_pha_enable = 0U;
    spi_cfg.dummy_bit = 0U;
    spi_cfg.cmd_write = SPI_TRANSCTRL_TRAMODE_WO;
    spi_cfg.cmd_read = SPI_TRANSCTRL_TRAMODE_RO;
    spi_cfg.spi_dma_enable = cfg->use_dma ? 1U : 0U;

    ret = spi_init_cfg(&spi_cfg);
    if (ret != SPI_INIT_SUCCESS) {
        return LCD_RET_ERROR;
    }

    return LCD_RET_OK;
}

int lcd_spi_bus_init(const lcd_config_t *cfg)
{
    int ret;

    if (cfg == NULL) {
        return LCD_RET_INVAL;
    }

    ret = drv_gpio_init();
    if (ret != GPIO_RET_SUCCESS) {
        return LCD_RET_ERROR;
    }

    ret = lcd_gpio_config_output(cfg->dc_gpio);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_gpio_config_output(cfg->rst_gpio);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    ret = lcd_gpio_config_output(cfg->bl_gpio);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    return lcd_spi_init_legacy(cfg);
}

int lcd_spi_bus_deinit(void)
{
    drv_spi_master_close();
    return LCD_RET_OK;
}

static int lcd_spi_map_write_ret(int ret)
{
    switch (ret) {
    case 0:
        return LCD_RET_OK;
    case -1:
        return LCD_RET_INVAL;
    case -2:
        return LCD_RET_ERROR;
    default:
        return LCD_RET_ERROR;
    }
}

static uint32_t lcd_spi_get_chunk_limit(void)
{
    uint32_t limit = g_lcd_ctx.config.max_transfer_bytes;

    if (limit < LCD_SPI_MIN_TRANSFER_BYTES || limit > LCD_SPI_MAX_TRANSFER_BYTES) {
        limit = LCD_SPI_MAX_TRANSFER_BYTES;
    }

    return limit;
}

static int lcd_spi_write_once(const uint8_t *buffer, uint32_t len)
{
    spi_transaction_t trans;
    int ret;

    if (buffer == NULL || len == 0U || len > LCD_SPI_MAX_TRANSFER_BYTES) {
        return LCD_RET_INVAL;
    }

    memset(&trans, 0, sizeof(trans));
    trans.cmmand = 0U;
    trans.addr = 0U;
    trans.length = len;

    ret = spi_master_write((unsigned char *)buffer, &trans);
    ret = lcd_spi_map_write_ret(ret);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    g_lcd_ctx.perf.tx_calls++;
    g_lcd_ctx.perf.tx_bytes += len;
    if (g_lcd_ctx.config.use_dma) {
        g_lcd_ctx.perf.tx_dma_calls++;
    } else {
        g_lcd_ctx.perf.tx_cpu_calls++;
    }

    return LCD_RET_OK;
}

static int lcd_spi_write_chunks(const uint8_t *buffer, uint32_t len)
{
    const uint8_t *ptr = buffer;
    uint32_t remain = len;
    uint32_t limit = lcd_spi_get_chunk_limit();
    int ret;

    if (buffer == NULL || len == 0U) {
        return LCD_RET_INVAL;
    }

    while (remain > 0U) {
        uint32_t chunk = remain > limit ? limit : remain;

        ret = lcd_spi_write_once(ptr, chunk);
        if (ret != LCD_RET_OK) {
            return ret;
        }

        ptr += chunk;
        remain -= chunk;
    }

    return LCD_RET_OK;
}

int lcd_spi_send_cmd(const uint8_t *cmd, uint32_t cmd_size, const uint8_t *param, uint32_t param_size)
{
    int ret;

    if (cmd == NULL || cmd_size == 0U) {
        return LCD_RET_INVAL;
    }

    if (g_lcd_ctx.config.dc_gpio >= 0) {
        ret = lcd_gpio_set_level(g_lcd_ctx.config.dc_gpio, 1U, 0);
        if (ret != LCD_RET_OK) {
            return ret;
        }
    }

    ret = lcd_spi_write_chunks(cmd, cmd_size);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    if (param != NULL && param_size > 0U) {
        if (g_lcd_ctx.config.dc_gpio >= 0) {
            ret = lcd_gpio_set_level(g_lcd_ctx.config.dc_gpio, 1U, 1);
            if (ret != LCD_RET_OK) {
                return ret;
            }
        }

        ret = lcd_spi_write_chunks(param, param_size);
        if (ret != LCD_RET_OK) {
            return ret;
        }
    }

    return LCD_RET_OK;
}

int lcd_spi_write_color_data(const uint8_t *param, uint32_t param_size)
{
    int ret;

    if (param == NULL || param_size == 0U) {
        return LCD_RET_INVAL;
    }

    if (g_lcd_ctx.config.dc_gpio >= 0) {
        ret = lcd_gpio_set_level(g_lcd_ctx.config.dc_gpio, 1U, 1);
        if (ret != LCD_RET_OK) {
            return ret;
        }
    }

    return lcd_spi_write_chunks(param, param_size);
}

int lcd_spi_write_color_repeat(uint16_t color_data, uint32_t pixel_count)
{
    static uint16_t s_fill_buf[LCD_SPI_FILL_PIXELS] __attribute__((aligned(4)));
    uint32_t i;
    int ret;

    if (pixel_count == 0U) {
        return LCD_RET_INVAL;
    }

    for (i = 0; i < LCD_SPI_FILL_PIXELS; i++) {
        s_fill_buf[i] = color_data;
    }

    if (g_lcd_ctx.config.dc_gpio >= 0) {
        ret = lcd_gpio_set_level(g_lcd_ctx.config.dc_gpio, 1U, 1);
        if (ret != LCD_RET_OK) {
            return ret;
        }
    }

    while (pixel_count > 0U) {
        uint32_t pixels = pixel_count > LCD_SPI_FILL_PIXELS ? LCD_SPI_FILL_PIXELS : pixel_count;

        ret = lcd_spi_write_chunks((const uint8_t *)s_fill_buf, pixels * sizeof(uint16_t));
        if (ret != LCD_RET_OK) {
            return ret;
        }

        pixel_count -= pixels;
    }

    return LCD_RET_OK;
}

int lcd_spi_send_color(const uint8_t *cmd, uint32_t cmd_size, const uint8_t *param, uint32_t param_size)
{
    int ret;

    if (cmd == NULL || cmd_size == 0U) {
        return LCD_RET_INVAL;
    }

    ret = lcd_spi_send_cmd(cmd, cmd_size, NULL, 0U);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    return lcd_spi_write_color_data(param, param_size);
}

int lcd_panel_hw_reset(uint32_t assert_ms, uint32_t deassert_ms)
{
    int ret;

    ret = lcd_gpio_set_level(g_lcd_ctx.config.rst_gpio, g_lcd_ctx.config.rst_active_high, 1);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    if (assert_ms > 0U) {
        os_msleep(assert_ms);
    }

    ret = lcd_gpio_set_level(g_lcd_ctx.config.rst_gpio, g_lcd_ctx.config.rst_active_high, 0);
    if (ret != LCD_RET_OK) {
        return ret;
    }

    if (deassert_ms > 0U) {
        os_msleep(deassert_ms);
    }

    return LCD_RET_OK;
}

int lcd_panel_run_init_cmds(const lcd_init_cmd_t *cmds, uint32_t count)
{
    uint32_t i;
    int ret;

    if (cmds == NULL) {
        return LCD_RET_INVAL;
    }

    for (i = 0; i < count; i++) {
        ret = lcd_spi_send_cmd(&cmds[i].cmd, 1U, cmds[i].data, cmds[i].data_bytes);
        if (ret != LCD_RET_OK) {
            return ret;
        }
        if (cmds[i].delay_ms > 0U) {
            os_msleep(cmds[i].delay_ms);
        }
    }

    return LCD_RET_OK;
}

