#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ci1302_protocol.h"

typedef struct {
    unsigned int count;
    uint16_t command;
    uint16_t length;
    uint8_t payload[16];
} test_result_t;

static void on_frame(const ci1302_frame_view_t *frame, void *user_data)
{
    test_result_t *result = (test_result_t *)user_data;

    result->count++;
    result->command = frame->command;
    result->length = frame->payload_length;
    assert(frame->payload_length <= sizeof(result->payload));
    memcpy(result->payload, frame->payload, frame->payload_length);
}

static void test_fragmented_frame(void)
{
    ci1302_protocol_parser_t parser;
    test_result_t result = {0};
    uint8_t frame[64];
    const uint8_t payload[] = {1, 2, 3, 4};
    int length;
    int index;

    ci1302_protocol_parser_init(&parser, on_frame, &result);
    length = ci1302_protocol_encode(CI1302_CMD_PCM_MIDDLE, payload,
                                    sizeof(payload),
                                    CI1302_PROTOCOL_FILL_DEFAULT,
                                    frame, sizeof(frame));
    assert(length == 20);
    for (index = 0; index < length; ++index) {
        ci1302_protocol_feed(&parser, &frame[index], 1);
    }
    assert(result.count == 1);
    assert(result.command == CI1302_CMD_PCM_MIDDLE);
    assert(result.length == sizeof(payload));
    assert(memcmp(result.payload, payload, sizeof(payload)) == 0);
}

static void test_noise_and_concatenated_frames(void)
{
    ci1302_protocol_parser_t parser;
    test_result_t result = {0};
    uint8_t stream[80] = {0x11, 0xA5, 0x22, 0x33, 0x44};
    int first;
    int second;

    ci1302_protocol_parser_init(&parser, on_frame, &result);
    first = ci1302_protocol_encode(CI1302_CMD_VAD_START, NULL, 0,
                                   CI1302_PROTOCOL_FILL_DEFAULT,
                                   stream + 5, sizeof(stream) - 5);
    second = ci1302_protocol_encode(CI1302_CMD_PCM_FINISH, NULL, 0,
                                    CI1302_PROTOCOL_FILL_DEFAULT,
                                    stream + 5 + first,
                                    sizeof(stream) - 5 - first);
    ci1302_protocol_feed(&parser, stream, (size_t)(5 + first + second));
    assert(result.count == 2);
    assert(result.command == CI1302_CMD_PCM_FINISH);
    assert(parser.stats.bytes_discarded == 5);
}

static void test_bad_checksum_and_recovery(void)
{
    ci1302_protocol_parser_t parser;
    test_result_t result = {0};
    uint8_t stream[80];
    uint8_t payload = 0x5A;
    int bad_length;
    int good_length;

    ci1302_protocol_parser_init(&parser, on_frame, &result);
    bad_length = ci1302_protocol_encode(CI1302_CMD_PCM_MIDDLE, &payload, 1,
                                        CI1302_PROTOCOL_FILL_DEFAULT,
                                        stream, sizeof(stream));
    stream[4] ^= 0x01;
    good_length = ci1302_protocol_encode(CI1302_CMD_AUDIO_SYS_READY, NULL, 0,
                                         CI1302_PROTOCOL_FILL_DEFAULT,
                                         stream + bad_length,
                                         sizeof(stream) - bad_length);
    ci1302_protocol_feed(&parser, stream, (size_t)(bad_length + good_length));
    assert(parser.stats.checksum_errors == 1);
    assert(result.count == 1);
    assert(result.command == CI1302_CMD_AUDIO_SYS_READY);
}

static void test_invalid_length_and_recovery(void)
{
    ci1302_protocol_parser_t parser;
    test_result_t result = {0};
    uint8_t stream[64] = {0xA5, 0xA5, 0x5A, 0x5A};
    int good_length;

    ci1302_protocol_parser_init(&parser, on_frame, &result);
    stream[8] = 0x01;
    stream[9] = 0x10;
    good_length = ci1302_protocol_encode(CI1302_CMD_WAKE_UP, NULL, 0,
                                         CI1302_PROTOCOL_FILL_DEFAULT,
                                         stream + CI1302_PROTOCOL_HEADER_SIZE,
                                         sizeof(stream) - CI1302_PROTOCOL_HEADER_SIZE);
    ci1302_protocol_feed(&parser, stream,
        (size_t)(CI1302_PROTOCOL_HEADER_SIZE + good_length));
    assert(parser.stats.length_errors == 1);
    assert(result.count == 1);
    assert(result.command == CI1302_CMD_WAKE_UP);
}

int main(void)
{
    test_fragmented_frame();
    test_noise_and_concatenated_frames();
    test_bad_checksum_and_recovery();
    test_invalid_length_and_recovery();
    return 0;
}
