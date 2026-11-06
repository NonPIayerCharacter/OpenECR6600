#ifndef CI1302_PROTOCOL_H
#define CI1302_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CI1302_PROTOCOL_MAGIC           0x5A5AA5A5UL
#define CI1302_PROTOCOL_VERSION         0x0100U
#define CI1302_PROTOCOL_FILL_DEFAULT    0x12345678UL
#define CI1302_PROTOCOL_HEADER_SIZE     16U
#define CI1302_PROTOCOL_MAX_PAYLOAD     4096U
#define CI1302_PROTOCOL_MAX_FRAME_SIZE  \
    (CI1302_PROTOCOL_HEADER_SIZE + CI1302_PROTOCOL_MAX_PAYLOAD)

typedef enum {
    CI1302_CMD_LOCAL_ASR_RESULT       = 0x0101,
    CI1302_CMD_WAKE_UP                = 0x0102,
    CI1302_CMD_PCM_MIDDLE             = 0x0105,
    CI1302_CMD_PCM_FINISH             = 0x0106,
    CI1302_CMD_VAD_START              = 0x0108,
    CI1302_CMD_EXIT_WAKE_UP           = 0x0109,
    CI1302_CMD_SET_AUDIO_VOLUME       = 0x0117,
    CI1302_CMD_SET_AUDIO_START_RECORD = 0x011A,
    CI1302_CMD_SET_AUDIO_STOP_RECORD  = 0x011B,
    CI1302_CMD_NET_PLAY_START         = 0x0201,
    CI1302_CMD_NET_PLAY_STOP          = 0x0204,
    CI1302_CMD_NET_PLAY_END           = 0x0208,
    CI1302_CMD_PLAY_DATA_GET          = 0x020A,
    CI1302_CMD_PLAY_DATA_RECV         = 0x020B,
    CI1302_CMD_PLAY_DATA_END          = 0x020C,
    CI1302_CMD_PLAY_TTS_END           = 0x020D,
    CI1302_CMD_AUDIO_SYS_READY        = 0x0601,
    CI1302_CMD_AUDIO_SYS_ERROR        = 0x0602,
    CI1302_CMD_AUDIO_RESET            = 0x0603,
    CI1302_CMD_EXEC_STATE             = 0x0801,
} ci1302_command_t;

typedef struct {
    uint16_t command;
    uint16_t payload_length;
    uint16_t version;
    uint16_t checksum;
    uint32_t fill_data;
    const uint8_t *payload;
} ci1302_frame_view_t;

typedef struct {
    uint32_t frames_received;
    uint32_t bytes_discarded;
    uint32_t length_errors;
    uint32_t checksum_errors;
} ci1302_protocol_stats_t;

typedef void (*ci1302_frame_callback_t)(const ci1302_frame_view_t *frame,
                                        void *user_data);

typedef struct {
    uint8_t buffer[CI1302_PROTOCOL_MAX_FRAME_SIZE];
    size_t used;
    ci1302_frame_callback_t callback;
    void *user_data;
    ci1302_protocol_stats_t stats;
} ci1302_protocol_parser_t;

void ci1302_protocol_parser_init(ci1302_protocol_parser_t *parser,
                                 ci1302_frame_callback_t callback,
                                 void *user_data);
void ci1302_protocol_parser_reset(ci1302_protocol_parser_t *parser);
void ci1302_protocol_feed(ci1302_protocol_parser_t *parser,
                          const uint8_t *data, size_t length);
int ci1302_protocol_encode(uint16_t command, const void *payload,
                           uint16_t payload_length, uint32_t fill_data,
                           uint8_t *output, size_t output_size);

uint16_t ci1302_protocol_read_le16(const uint8_t *data);
uint32_t ci1302_protocol_read_le32(const uint8_t *data);

#endif
