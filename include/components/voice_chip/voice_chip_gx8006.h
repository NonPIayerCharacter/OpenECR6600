#ifndef __VOICE_CHIP_GX8006_H__
#define __VOICE_CHIP_GX8006_H__

#include "voice_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    voice_chip_device_t dev;
    voice_chip_caps_t caps;
} voice_chip_gx8006_t;

void voice_chip_gx8006_bind(voice_chip_gx8006_t *chip);
voice_chip_device_t *voice_chip_gx8006_device(voice_chip_gx8006_t *chip);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_GX8006_H__ */
