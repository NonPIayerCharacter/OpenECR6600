#ifndef __VOICE_CHIP_H__
#define __VOICE_CHIP_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_CHIP_TRANSPORT_I2C = 1u << 0,
    VOICE_CHIP_TRANSPORT_I2S = 1u << 1,
    VOICE_CHIP_TRANSPORT_UART = 1u << 2,
} voice_chip_transport_flags_t;

typedef enum {
    VOICE_CHIP_CAP_OUTPUT_VOLUME_RAW = 1u << 0,
    VOICE_CHIP_CAP_OUTPUT_VOLUME_PERCENT = 1u << 1,
    VOICE_CHIP_CAP_OUTPUT_VOLUME_STEP = 1u << 2,
    VOICE_CHIP_CAP_OUTPUT_VOLUME_CYCLE = 1u << 3,
    VOICE_CHIP_CAP_INPUT_GAIN = 1u << 4,
    VOICE_CHIP_CAP_OUTPUT_MUTE = 1u << 5,
    VOICE_CHIP_CAP_ABORT_ALL = 1u << 6,
    VOICE_CHIP_CAP_PERSISTED_VOLUME = 1u << 7,
    VOICE_CHIP_CAP_PCM_PLAYBACK = 1u << 8,
    VOICE_CHIP_CAP_PCM_CAPTURE = 1u << 9,
} voice_chip_capability_flags_t;

#define VOICE_CHIP_CAP_OUTPUT_VOLUME \
    (VOICE_CHIP_CAP_OUTPUT_VOLUME_RAW | \
     VOICE_CHIP_CAP_OUTPUT_VOLUME_PERCENT | \
     VOICE_CHIP_CAP_OUTPUT_VOLUME_STEP | \
     VOICE_CHIP_CAP_OUTPUT_VOLUME_CYCLE)

#define VOICE_CHIP_CAP_VOLUME_PERSIST VOICE_CHIP_CAP_PERSISTED_VOLUME

typedef struct {
    uint16_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t channels;
} voice_chip_pcm_profile_t;

typedef struct {
    uint32_t capability_flags;
    uint32_t transport_flags;
    voice_chip_pcm_profile_t playback;
    voice_chip_pcm_profile_t capture;
} voice_chip_caps_t;

typedef struct {
    int (*save_blob)(const char *key, const void *value_buf, size_t buf_len, void *ctx);
    int (*load_blob)(const char *key, void *value_buf, size_t buf_len, int *value_len, void *ctx);
    void *ctx;
} voice_chip_storage_ops_t;

typedef struct {
    int (*start_capture)(void *ctx);
    int (*stop_capture)(void *ctx);
    int (*playback_write)(const void *buf, size_t len, void *ctx);
    int (*playback_flush)(void *ctx);
    int (*playback_cancel)(void *ctx);
    void *ctx;
} voice_chip_stream_offload_ops_t;

struct voice_chip_device;
typedef struct voice_chip_device voice_chip_device_t;

typedef struct {
    int (*open)(voice_chip_device_t *chip);
    int (*close)(voice_chip_device_t *chip);
    int (*reset)(voice_chip_device_t *chip);
    int (*flush)(voice_chip_device_t *chip);
    int (*abort_all)(voice_chip_device_t *chip);
    int (*set_output_volume_raw)(voice_chip_device_t *chip, int volume);
    int (*set_output_volume_percent)(voice_chip_device_t *chip, int percent);
    int (*get_output_volume_percent)(voice_chip_device_t *chip);
    int (*volume_step_up)(voice_chip_device_t *chip);
    int (*volume_step_down)(voice_chip_device_t *chip);
    int (*volume_step_cycle)(voice_chip_device_t *chip);
    int (*set_input_gain)(voice_chip_device_t *chip, uint8_t gain);
    const voice_chip_caps_t *(*get_caps)(voice_chip_device_t *chip);
} voice_chip_ops_t;

struct voice_chip_device {
    const char *name;
    void *priv;
    const voice_chip_ops_t *ops;
    const voice_chip_caps_t *caps;
};

int voice_chip_open(voice_chip_device_t *chip);
int voice_chip_close(voice_chip_device_t *chip);
int voice_chip_reset(voice_chip_device_t *chip);
int voice_chip_flush(voice_chip_device_t *chip);
int voice_chip_abort_all(voice_chip_device_t *chip);
int voice_chip_set_output_volume_raw(voice_chip_device_t *chip, int volume);
int voice_chip_set_output_volume_percent(voice_chip_device_t *chip, int percent);
int voice_chip_get_output_volume_percent(voice_chip_device_t *chip);
int voice_chip_volume_step_up(voice_chip_device_t *chip);
int voice_chip_volume_step_down(voice_chip_device_t *chip);
int voice_chip_volume_step_cycle(voice_chip_device_t *chip);
int voice_chip_set_input_gain(voice_chip_device_t *chip, uint8_t gain);
const voice_chip_caps_t *voice_chip_get_caps(voice_chip_device_t *chip);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_H__ */
