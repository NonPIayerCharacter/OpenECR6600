#ifndef __VOICE_CHIP_SERVICE_H__
#define __VOICE_CHIP_SERVICE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "voice_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_FORMAT_PCM = 0,
    AUDIO_FORMAT_SPEEX = 1,
    AUDIO_FORMAT_OPUS = 2,
    AUDIO_FORMAT_MP3 = 3,
    AUDIO_FORMAT_NSUP = 255,
} voice_chip_service_audio_format_t;

typedef enum {
    VOICE_PLAYBACK_STREAM_DEFAULT = 0,
    VOICE_PLAYBACK_LOCAL_PROMPT_SHORT = 1,
} voice_chip_service_playback_semantic_t;

typedef enum {
    VOICE_CHIP_SERVICE_STATUS_IDLE = 0,
    VOICE_CHIP_SERVICE_STATUS_CAPTURING = 1,
    VOICE_CHIP_SERVICE_STATUS_PLAYING = 2,
    VOICE_CHIP_SERVICE_STATUS_CAPTURING_AND_PLAYING = 3,
} voice_chip_service_status_t;

typedef enum {
    VOICE_CHIP_SERVICE_OPUS_MODE_RAW = 0,
    VOICE_CHIP_SERVICE_OPUS_MODE_OGG = 1,
} voice_chip_service_opus_mode_t;

typedef void (*voice_chip_service_record_start_cb_t)(void);
typedef void (*voice_chip_service_record_data_cb_t)(uint8_t *data, uint16_t size);
typedef void (*voice_chip_service_record_stop_cb_t)(void);
typedef void (*voice_chip_service_playback_cb_t)(void);
typedef void (*voice_chip_service_prompt_play_cb_t)(uint32_t audio_id, uint32_t duration_ms);

struct audio_res;
typedef const struct audio_res *(*voice_chip_service_audio_res_provider_t)(int audio_id);

/* Opaque borrowed frame: keep it only until consumption is acknowledged.
 * The backend owns the data; callers must not free the frame or its data. */
typedef void voice_chip_service_record_frame_t;
typedef int (*voice_chip_service_record_upload_cb_t)(char *frame, int length);

typedef struct {
    bool (*ready_cb)(void);
    void (*idle_cb)(int time);
} voice_chip_service_runtime_hooks_t;

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
    voice_chip_service_runtime_hooks_t runtime_hooks;
    voice_chip_service_record_upload_cb_t record_upload_cb;
} voice_chip_service_config_t;

typedef struct {
    bool force_stop;
    bool mic_running;
    bool speaker_running;
    int status; /* voice_chip_service_status_t */
    bool ready_cb_registered;
    bool ready_cb_ready;
    bool can_start;
} voice_chip_service_capture_blocker_state_t;

struct voice_chip_service;
typedef struct voice_chip_service voice_chip_service_t;

typedef struct {
    int (*init)(void);
    int (*deinit)(void);
    int (*start_capture)(void);
    int (*stop_capture)(void);
    int (*play_stream)(const void *data, size_t len, int format);
    int (*play_stream_ex)(const void *data, size_t len, int format, int semantic);
    int (*clear_stream)(void);
    int (*cancel_playback)(void);
    int (*set_output_volume)(uint8_t volume);
    int (*get_output_volume)(void);
    int (*set_idle)(void);
    int (*set_active)(void);
    int (*get_status)(void);
    bool (*is_ready)(void);
    int (*set_record_callbacks)(voice_chip_service_record_start_cb_t start_cb,
                                voice_chip_service_record_data_cb_t data_cb,
                                voice_chip_service_record_stop_cb_t stop_cb);
    const voice_chip_caps_t *(*get_caps)(void);
    int (*set_playback_callbacks)(voice_chip_service_playback_cb_t start_cb,
                                  voice_chip_service_playback_cb_t finished_cb);
    int (*set_prompt_player)(voice_chip_service_prompt_play_cb_t cb);
    int (*set_audio_res_provider)(voice_chip_service_audio_res_provider_t cb);
    int (*set_powerup_audio_id)(int audio_id);
    int (*play_local)(int audio_id);
    int (*clear_audio_queue)(void);
    bool (*is_local_playback_busy)(void);
    int (*get_custom_opus_audio_id)(void);
    int (*get_custom_opus_resource)(const uint8_t **base,
                                    const uint8_t **end,
                                    uint16_t *framesize);
    int (*set_custom_opus_resource)(uint8_t *addr, uint32_t len, uint16_t framesize);
    int (*set_opus_mode)(voice_chip_service_opus_mode_t mode);
    int (*set_vad_threshold)(uint8_t on_or_threshold);
    int (*sync_speaker_config)(void);
    void (*set_output_volume_local)(uint8_t volume);
    int (*commit_output_volume)(void);
    int (*reset_output_volume)(void);
    /* Optional EOF: unlike clear/cancel, drains queued audio. Nonblocking. */
    int (*end_stream)(void);
    /* Optional extensions; existing backend operations retain their offsets. */
    int (*set_output_volume_raw)(int volume);
    int (*set_volume_bounds)(uint8_t max_volume, uint8_t min_volume);
    int (*volume_step_up)(void);
    int (*volume_step_down)(void);
    int (*volume_step_cycle)(void);
    int (*set_input_gain)(uint8_t gain);
    int (*get_capture_blocker_state)(voice_chip_service_capture_blocker_state_t *state);
    bool (*capture_force_stop_pending)(void);
    int (*reset_playback_cancel_latch)(void);
    uint8_t *(*record_frame_data)(voice_chip_service_record_frame_t *frame);
    bool (*record_frame_is_consumed)(const voice_chip_service_record_frame_t *frame);
    int (*record_frame_consume)(voice_chip_service_record_frame_t *frame);
} voice_chip_service_ops_t;

struct voice_chip_service {
    const char *name;
    const voice_chip_service_ops_t *ops;
    void *priv;
};

int voice_chip_service_register(const voice_chip_service_t *service);
int voice_chip_service_unregister(const voice_chip_service_t *service);
const voice_chip_service_t *voice_chip_service_current(void);

/* Configure the selected backend before start. Unsupported backends return -1.
 * Configuration strings and callback contexts must outlive the service. */
int voice_chip_service_configure(const voice_chip_service_config_t *cfg);
int voice_chip_service_set_output_volume_raw(int volume);
int voice_chip_service_set_volume_bounds(uint8_t max_volume, uint8_t min_volume);
/* Step returns retain backend semantics, including boundary indicators. */
int voice_chip_service_volume_step_up(void);
int voice_chip_service_volume_step_down(void);
int voice_chip_service_volume_step_cycle(void);
int voice_chip_service_set_input_gain(uint8_t gain);
int voice_chip_service_get_capture_blocker_state(voice_chip_service_capture_blocker_state_t *state);
bool voice_chip_service_capture_force_stop_pending(void);
int voice_chip_service_reset_playback_cancel_latch(void);
uint8_t *voice_chip_service_record_frame_data(voice_chip_service_record_frame_t *frame);
bool voice_chip_service_record_frame_is_consumed(const voice_chip_service_record_frame_t *frame);
int voice_chip_service_record_frame_consume(voice_chip_service_record_frame_t *frame);

int voice_chip_service_start(void);
int voice_chip_service_init(void);
int voice_chip_service_deinit(void);
int voice_chip_service_start_capture(void);
int voice_chip_service_stop_capture(void);
/* Returns -1 for backends without explicit stream EOF support. */
int voice_chip_service_end_stream(void);
int voice_chip_service_play_stream(const void *data, size_t len, int format);
int voice_chip_service_play_stream_ex(const void *data, size_t len, int format, int semantic);
int voice_chip_service_clear_stream(void);
int voice_chip_service_cancel_playback(void);
int voice_chip_service_set_output_volume(uint8_t volume);
int voice_chip_service_get_output_volume(void);
int voice_chip_service_set_idle(void);
int voice_chip_service_set_active(void);
int voice_chip_service_get_status(void);
bool voice_chip_service_is_ready(void);
int voice_chip_service_set_record_callbacks(voice_chip_service_record_start_cb_t start_cb,
                                            voice_chip_service_record_data_cb_t data_cb,
                                            voice_chip_service_record_stop_cb_t stop_cb);
const voice_chip_caps_t *voice_chip_service_get_caps(void);
bool voice_chip_service_status_is_capturing(int status);
bool voice_chip_service_status_is_playing(int status);

int voice_chip_service_set_playback_callbacks(voice_chip_service_playback_cb_t start_cb,
                                              voice_chip_service_playback_cb_t finished_cb);
int voice_chip_service_set_prompt_player(voice_chip_service_prompt_play_cb_t cb);
int voice_chip_service_set_audio_res_provider(voice_chip_service_audio_res_provider_t cb);
int voice_chip_service_set_powerup_audio_id(int audio_id);
int voice_chip_service_play_local(int audio_id);
int voice_chip_service_clear_audio_queue(void);
bool voice_chip_service_is_local_playback_busy(void);
int voice_chip_service_get_custom_opus_audio_id(void);
int voice_chip_service_get_custom_opus_resource(const uint8_t **base,
                                                const uint8_t **end,
                                                uint16_t *framesize);
int voice_chip_service_set_custom_opus_resource(uint8_t *addr, uint32_t len, uint16_t framesize);
int voice_chip_service_set_opus_mode(voice_chip_service_opus_mode_t mode);
int voice_chip_service_set_vad_threshold(uint8_t on_or_threshold);
int voice_chip_service_sync_speaker_config(void);
void voice_chip_service_set_output_volume_local(uint8_t volume);
int voice_chip_service_commit_output_volume(void);
int voice_chip_service_reset_output_volume(void);

/* Selected backends implement this hook when they support lazy registration. */
int voice_chip_service_register_default_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_SERVICE_H__ */
