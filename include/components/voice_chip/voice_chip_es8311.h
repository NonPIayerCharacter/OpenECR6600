#ifndef __VOICE_CHIP_ES8311_H__
#define __VOICE_CHIP_ES8311_H__

#include "voice_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ES8311_ADDR
#define ES8311_ADDR 0x18
#endif

#ifndef VOICE_CHIP_ES8311_DEFAULT_ADDR
#define VOICE_CHIP_ES8311_DEFAULT_ADDR ES8311_ADDR
#endif

#ifndef VOICE_CHIP_ES8311_VOLUME_LEVELS
#define VOICE_CHIP_ES8311_VOLUME_LEVELS 8
#endif

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
} voice_chip_es8311_config_t;

typedef struct {
    voice_chip_device_t dev;
    voice_chip_es8311_config_t cfg;
    voice_chip_caps_t caps;
    uint8_t volume_levels[VOICE_CHIP_ES8311_VOLUME_LEVELS];
    uint8_t volume_table[VOICE_CHIP_ES8311_VOLUME_LEVELS];
    uint8_t volume_steps;
    int volume_index;
    int current_volume;
    int max_volume_value;
    int min_volume_value;
    int volume_circle_up;
    int cycle_direction;
    int opened;
} voice_chip_es8311_t;

void voice_chip_es8311_default_config(voice_chip_es8311_config_t *cfg);
void voice_chip_es8311_bind(voice_chip_es8311_t *chip, const voice_chip_es8311_config_t *cfg);
voice_chip_device_t *voice_chip_es8311_device(voice_chip_es8311_t *chip);
int voice_chip_es8311_set_volume_bounds(voice_chip_es8311_t *chip, uint8_t max_volume, uint8_t min_volume);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_ES8311_H__ */
