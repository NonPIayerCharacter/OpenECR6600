#ifndef __COMPONENTS_LVGL_LVGL_PORT_H__
#define __COMPONENTS_LVGL_LVGL_PORT_H__

#include <stdbool.h>
#include <stdint.h>

/* Keep the public port header usable from examples and board code even when
 * component-local include paths are not expanded to LVGL's internal src tree.
 */
#include "../../../components/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lvgl_port_display_flush_cb_t)(lv_display_t *display,
                                             const lv_area_t *area,
                                             uint8_t *px_map,
                                             void *user_data);

typedef void (*lvgl_port_input_read_cb_t)(lv_indev_t *indev,
                                          lv_indev_data_t *data,
                                          void *user_data);

typedef struct {
    lvgl_port_display_flush_cb_t flush_cb;
    void *user_data;
} lvgl_port_display_config_t;

typedef struct {
    lv_indev_type_t type;
    lvgl_port_input_read_cb_t read_cb;
    void *user_data;
} lvgl_port_input_config_t;

typedef struct {
    bool ready;
    bool lvmem_builtin_allocator;
    bool lvmem_monitor_supported;
    uint8_t drawbuf_px_size;
    uint8_t drawbuf_cover_pct;
    uint8_t drawbuf_full_refresh_estimate;
    uint8_t lvmem_used_pct;
    uint8_t lvmem_frag_pct;
    uint32_t hor_res;
    uint32_t ver_res;
    uint32_t drawbuf_lines;
    uint32_t drawbuf_bytes;
    uint32_t lvmem_total_bytes;
    uint32_t lvmem_free_bytes;
    uint32_t lvmem_free_biggest_bytes;
    uint32_t lvmem_max_used_bytes;
} lvgl_port_debug_status_t;

int lvgl_port_init(void);
bool lvgl_port_is_ready(void);
int lvgl_port_register_display(const lvgl_port_display_config_t *config);
int lvgl_port_register_input(const lvgl_port_input_config_t *config);
lv_display_t *lvgl_port_get_display(void);
lv_indev_t *lvgl_port_get_input(void);
void lvgl_port_get_debug_status(lvgl_port_debug_status_t *status);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif

