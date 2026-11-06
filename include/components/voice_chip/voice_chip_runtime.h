#ifndef __VOICE_CHIP_RUNTIME_H__
#define __VOICE_CHIP_RUNTIME_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *buf;
    bool read_enable;
} VOICE_BUFF_HANDLE_T;

typedef enum {
    I2S_CAPTURE_START = 0,
    I2S_CAPTRUE_STOP,
    I2S_CAPTURE_END,
    I2S_CAPTURE_CANCLE,
} I2S_CAPTURE_STATUS_T;

typedef enum {
    I2S_CODEC_STATUS_IDLE = 0,
    I2S_CODEC_STATUS_CAPTURING,
    I2S_CODEC_STATUS_PLAYING,
} I2S_CODEC_STATUS_T;

typedef enum {
    VOICE_RUNTIME_PLAYBACK_STREAM_DEFAULT = 0,
    VOICE_RUNTIME_PLAYBACK_LOCAL_PROMPT_SHORT = 1,
} voice_runtime_playback_semantic_t;

typedef int (*i2s_data_to_upload)(char *buff, int length);

typedef bool (*voice_runtime_ready_cb_t)(void);
typedef void (*voice_runtime_idle_cb_t)(int time);
typedef void (*voice_runtime_record_start_cb_t)(void);
typedef void (*voice_runtime_record_data_cb_t)(uint8_t *data, uint16_t size);
typedef void (*voice_runtime_record_stop_cb_t)(void);

typedef struct {
    voice_runtime_ready_cb_t ready_cb;
    voice_runtime_idle_cb_t idle_cb;
} voice_runtime_hooks_t;

typedef struct {
    bool force_stop;
    bool mic_running;
    bool speaker_running;
    int i2s_capture_status;
    bool ready_cb_registered;
    bool ready_cb_ready;
    bool can_start;
} voice_capture_blocker_state_t;

int buffer_write_data(uint8_t *data, int length, int format);
int i2s_capture_status_post(int state);
int voice_task_init(void);
int i2s_data_callback_register(i2s_data_to_upload cb);
int voice_get_capture_status(void);
int voice_get_capture_buffer_count(void);
bool voice_capture_can_start(void);
bool voice_capture_force_stop_pending(void);
void voice_capture_get_blocker_state(voice_capture_blocker_state_t *state);
void voice_runtime_set_playback_semantic(voice_runtime_playback_semantic_t semantic);
void voice_runtime_reset_playback_cancel_latch(void);
void buffer_clear_all_op(void);
void voice_runtime_set_hooks(const voice_runtime_hooks_t *hooks);
void voice_runtime_reset_hooks(void);
void voice_runtime_set_record_callbacks(voice_runtime_record_start_cb_t start_cb,
                                        voice_runtime_record_data_cb_t data_cb,
                                        voice_runtime_record_stop_cb_t stop_cb);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CHIP_RUNTIME_H__ */

