#ifndef __VOICE_CHIP_ES8311_SERVICE_H__
#define __VOICE_CHIP_ES8311_SERVICE_H__

#include <stdint.h>
#include "voice_chip_es8311.h"
#include "voice_chip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*voice_es8311_service_record_upload_cb_t)(char *buff, int length);

typedef struct {
    uint8_t device_addr;
    uint8_t volume_min;
    uint8_t volume_max;
    uint8_t default_volume_index;
    unsigned int i2c_speed;
    unsigned char i2c_address_bit;
    unsigned char i2c_mode;
    unsigned int i2c_dma;
    unsigned char scl_num;
    unsigned char sda_num;
    const char *volume_store_key;
    voice_chip_storage_ops_t storage_ops;
    voice_runtime_hooks_t runtime_hooks;
    voice_es8311_service_record_upload_cb_t record_upload_cb;
} voice_es8311_service_config_t;

int voice_es8311_service_register_default(const voice_es8311_service_config_t *cfg);
int voice_es8311_service_start(void);
voice_chip_device_t *voice_es8311_service_device(void);
int voice_es8311_service_open(const voice_es8311_service_config_t *cfg);
int voice_es8311_service_set_volume_bounds(uint8_t max_volume, uint8_t min_volume);
int voice_es8311_service_set_volume_raw(int volume);
int voice_es8311_service_set_volume_percent(int percent);
int voice_es8311_service_get_volume_percent(void);
int voice_es8311_service_volume_step_up(void);
int voice_es8311_service_volume_step_down(void);
int voice_es8311_service_volume_step_cycle(void);
int voice_es8311_service_set_mic_gain(uint8_t gain);
int voice_es8311_service_get_volume_max(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_ES8311_SERVICE_H__ */
