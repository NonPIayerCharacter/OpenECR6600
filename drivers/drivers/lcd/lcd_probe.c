#include "lcd_internal.h"

const lcd_panel_ops_t *lcd_probe_get_ops(lcd_panel_type_t panel)
{
    const lcd_panel_ops_t *ops = NULL;

    switch (panel) {
#if defined(CONFIG_LCD_ST7789)
    case LCD_PANEL_ST7789:
        if (drv_lcd_st7789_get_ops(&ops) != LCD_RET_OK) {
            return NULL;
        }
        return ops;
#endif
#if defined(CONFIG_LCD_ST77916)
    case LCD_PANEL_ST77916:
        if (drv_lcd_st77916_get_ops(&ops) != LCD_RET_OK) {
            return NULL;
        }
        return ops;
#endif
    default:
        return NULL;
    }
}
