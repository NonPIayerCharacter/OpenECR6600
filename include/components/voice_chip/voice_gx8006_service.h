#ifndef __VOICE_GX8006_SERVICE_H__
#define __VOICE_GX8006_SERVICE_H__

#include <stdbool.h>
#include <stdint.h>
#include "voice_chip_service.h"
#include "voice_chip_gx8006.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef voice_chip_service_opus_mode_t voice_gx8006_opus_mode_t;

#define VOICE_GX8006_OPUS_MODE_RAW VOICE_CHIP_SERVICE_OPUS_MODE_RAW
#define VOICE_GX8006_OPUS_MODE_OGG VOICE_CHIP_SERVICE_OPUS_MODE_OGG

typedef enum {
    VOICE_GX8006_SERVICE_WAKEUP_EVENT_KEYWORDS = 0,
    VOICE_GX8006_SERVICE_WAKEUP_EVENT_EXTERNAL,
} voice_gx8006_service_wakeup_event_t;

typedef voice_chip_service_playback_cb_t voice_gx8006_service_playback_cb_t;
typedef voice_chip_service_prompt_play_cb_t voice_gx8006_service_prompt_play_cb_t;
typedef voice_chip_service_audio_res_provider_t voice_gx8006_service_audio_res_provider_t;
typedef void (*voice_gx8006_service_wakeup_event_cb_t)(voice_gx8006_service_wakeup_event_t event);

int voice_gx8006_service_register_default(void);
int voice_gx8006_service_start(void);
voice_chip_device_t *voice_gx8006_service_device(void);

int voice_gx8006_service_set_protocol_output(int (*output)(uint8_t *, uint16_t));
int voice_gx8006_service_set_protocol_input(void (*input)(uint8_t *, uint16_t));
int voice_gx8006_service_set_flowctrl_ready(bool (*is_io_ready)(void));
int voice_gx8006_service_set_wakeup_callback(void (*cb)(void));
int voice_gx8006_service_set_wakeup_event_callback(voice_gx8006_service_wakeup_event_cb_t cb);
int voice_gx8006_service_set_mode_switch_callback(void (*cb)(void));
int voice_gx8006_service_set_playback_callbacks(voice_gx8006_service_playback_cb_t start_cb,
                                                voice_gx8006_service_playback_cb_t finished_cb);
int voice_gx8006_service_set_prompt_player(voice_gx8006_service_prompt_play_cb_t cb);
int voice_gx8006_service_set_audio_res_provider(voice_gx8006_service_audio_res_provider_t cb);
int voice_gx8006_service_set_powerup_audio_id(int audio_id);
int voice_gx8006_service_set_record_callbacks(voice_chip_service_record_start_cb_t start_cb,
                                              voice_chip_service_record_data_cb_t data_cb,
                                              voice_chip_service_record_stop_cb_t stop_cb);
int voice_gx8006_service_play_local(int audio_id);
int voice_gx8006_service_clear_audio_queue(void);
bool voice_gx8006_service_is_local_playback_busy(void);
int voice_gx8006_service_get_custom_opus_audio_id(void);
int voice_gx8006_service_get_custom_opus_resource(const uint8_t **base,
                                                  const uint8_t **end,
                                                  uint16_t *framesize);
int voice_gx8006_service_set_custom_opus_res(uint8_t *addr, uint32_t len, uint16_t framesize);
int voice_gx8006_service_set_opus_mode(voice_gx8006_opus_mode_t mode);
int voice_gx8006_service_set_vad_threshold(uint8_t on_or_threshold);
int voice_gx8006_service_sync_speaker_config(void);
void voice_gx8006_service_set_volume_local(uint8_t volume);
int voice_gx8006_service_commit_volume(void);
int voice_gx8006_service_reset_volume(void);
int voice_gx8006_service_restart(void);
int voice_gx8006_service_get_version(char *str);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_GX8006_SERVICE_H__ */

