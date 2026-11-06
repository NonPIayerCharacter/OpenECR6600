#ifndef CI1302_H
#define CI1302_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CI1302_EVENT_READY,
    CI1302_EVENT_ERROR,
    CI1302_EVENT_WAKE_UP,
    CI1302_EVENT_EXIT_WAKE_UP,
    CI1302_EVENT_RECORD_START,
    CI1302_EVENT_RECORD_DATA,
    CI1302_EVENT_RECORD_STOP,
    CI1302_EVENT_PLAY_START,
    CI1302_EVENT_PLAY_STOP,
} ci1302_event_t;

typedef struct {
    uint32_t uart_rx_bytes;
    uint32_t uart_tx_bytes;
    uint32_t protocol_frames;
    uint32_t protocol_discarded_bytes;
    uint32_t protocol_length_errors;
    uint32_t protocol_checksum_errors;
    uint32_t record_frames;
    uint32_t record_bytes;
    uint32_t record_hash;
    uint32_t pcm_bytes;
    uint32_t playback_requests;
    uint16_t last_rx_command;
    uint16_t last_rx_payload_length;
    uint16_t last_rx_version;
    uint32_t last_rx_fill_data;
    bool trace_enabled;
    bool raw_trace_enabled;
} ci1302_stats_t;

typedef void (*ci1302_event_callback_t)(ci1302_event_t event,
                                        const uint8_t *data,
                                        uint16_t length,
                                        void *user_data);
typedef int (*ci1302_pcm_source_t)(uint8_t *buffer, uint32_t max_length,
                                   void *user_data);

/* Streaming sources: >0 PCM bytes, 0 explicit EOF, AGAIN temporary starvation.
 * Called in the RX task: the source must not block. Finite CLI sources retain
 * their existing short-read-as-EOF contract. */
#define CI1302_PCM_AGAIN (-2)
int ci1302_play_pcm_stream_start(ci1302_pcm_source_t source, void *user_data);
/* Dedicated hardware-adapter subscriber; CLI tracing/capture cannot replace it.
 * Event data is borrowed and valid only during the callback. */
void ci1302_set_platform_callback(ci1302_event_callback_t callback, void *user_data);

int ci1302_init(void);
int ci1302_deinit(void);
bool ci1302_is_initialized(void);
bool ci1302_is_ready(void);
bool ci1302_is_recording(void);
bool ci1302_is_playing(void);
uint8_t ci1302_get_volume(void);
void ci1302_set_event_callback(ci1302_event_callback_t callback,
                               void *user_data);
void ci1302_set_trace(bool enabled);
void ci1302_set_raw_trace(bool enabled);
int ci1302_send_command(uint16_t command, uint32_t fill_data,
                        const void *payload, uint16_t payload_length);
int ci1302_record_start(void);
int ci1302_record_stop(void);
int ci1302_set_volume(uint8_t volume);
int ci1302_play_pcm_start(ci1302_pcm_source_t source, void *user_data);
int ci1302_play_stop(void);
int ci1302_play_tone(uint32_t duration_ms, uint8_t volume);
int ci1302_reset(void);
void ci1302_get_stats(ci1302_stats_t *stats);
void ci1302_reset_stats(void);

#endif
