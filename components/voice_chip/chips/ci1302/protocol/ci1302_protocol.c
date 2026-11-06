#include "ci1302_protocol.h"

#include <string.h>

static const uint8_t ci1302_magic_bytes[4] = {0xA5, 0xA5, 0x5A, 0x5A};

uint16_t ci1302_protocol_read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t ci1302_protocol_read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ci1302_protocol_write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void ci1302_protocol_write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t ci1302_protocol_checksum(const uint8_t *data, size_t length)
{
    uint32_t checksum = 0;

    while (length--) {
        checksum += *data++;
    }
    return (uint16_t)checksum;
}

static size_t ci1302_protocol_find_magic(const uint8_t *data, size_t length)
{
    size_t index;

    if (length < sizeof(ci1302_magic_bytes)) {
        return length;
    }
    for (index = 0; index <= length - sizeof(ci1302_magic_bytes); ++index) {
        if (memcmp(data + index, ci1302_magic_bytes,
                   sizeof(ci1302_magic_bytes)) == 0) {
            return index;
        }
    }
    return length;
}

static void ci1302_protocol_drop(ci1302_protocol_parser_t *parser,
                                 size_t length)
{
    if (length >= parser->used) {
        parser->used = 0;
        return;
    }
    memmove(parser->buffer, parser->buffer + length, parser->used - length);
    parser->used -= length;
}

static void ci1302_protocol_process(ci1302_protocol_parser_t *parser)
{
    while (parser->used >= sizeof(ci1302_magic_bytes)) {
        ci1302_frame_view_t frame;
        size_t magic_offset;
        size_t frame_size;
        uint16_t calculated_checksum;

        magic_offset = ci1302_protocol_find_magic(parser->buffer, parser->used);
        if (magic_offset == parser->used) {
            size_t keep = parser->used < 3U ? parser->used : 3U;
            parser->stats.bytes_discarded += (uint32_t)(parser->used - keep);
            if (keep) {
                memmove(parser->buffer, parser->buffer + parser->used - keep,
                        keep);
            }
            parser->used = keep;
            return;
        }
        if (magic_offset) {
            parser->stats.bytes_discarded += (uint32_t)magic_offset;
            ci1302_protocol_drop(parser, magic_offset);
        }
        if (parser->used < CI1302_PROTOCOL_HEADER_SIZE) {
            return;
        }

        frame.payload_length = ci1302_protocol_read_le16(parser->buffer + 8);
        if (frame.payload_length > CI1302_PROTOCOL_MAX_PAYLOAD) {
            parser->stats.length_errors++;
            parser->stats.bytes_discarded++;
            ci1302_protocol_drop(parser, 1);
            continue;
        }
        frame_size = CI1302_PROTOCOL_HEADER_SIZE + frame.payload_length;
        if (parser->used < frame_size) {
            return;
        }

        frame.checksum = ci1302_protocol_read_le16(parser->buffer + 4);
        frame.command = ci1302_protocol_read_le16(parser->buffer + 6);
        frame.version = ci1302_protocol_read_le16(parser->buffer + 10);
        frame.fill_data = ci1302_protocol_read_le32(parser->buffer + 12);
        frame.payload = parser->buffer + CI1302_PROTOCOL_HEADER_SIZE;
        calculated_checksum = ci1302_protocol_checksum(frame.payload,
                                                       frame.payload_length);

        /* Some vendor firmware revisions leave checksum at zero. */
        if (frame.checksum != 0U && frame.checksum != calculated_checksum) {
            parser->stats.checksum_errors++;
        } else {
            parser->stats.frames_received++;
            if (parser->callback) {
                parser->callback(&frame, parser->user_data);
            }
        }
        ci1302_protocol_drop(parser, frame_size);
    }
}

void ci1302_protocol_parser_init(ci1302_protocol_parser_t *parser,
                                 ci1302_frame_callback_t callback,
                                 void *user_data)
{
    memset(parser, 0, sizeof(*parser));
    parser->callback = callback;
    parser->user_data = user_data;
}

void ci1302_protocol_parser_reset(ci1302_protocol_parser_t *parser)
{
    parser->used = 0;
}

void ci1302_protocol_feed(ci1302_protocol_parser_t *parser,
                          const uint8_t *data, size_t length)
{
    while (length) {
        size_t available = sizeof(parser->buffer) - parser->used;
        size_t copy_length;

        if (available == 0U) {
            ci1302_protocol_process(parser);
            available = sizeof(parser->buffer) - parser->used;
            if (available == 0U) {
                parser->stats.length_errors++;
                parser->used = 0;
                available = sizeof(parser->buffer);
            }
        }
        copy_length = length < available ? length : available;
        memcpy(parser->buffer + parser->used, data, copy_length);
        parser->used += copy_length;
        data += copy_length;
        length -= copy_length;
        ci1302_protocol_process(parser);
    }
}

int ci1302_protocol_encode(uint16_t command, const void *payload,
                           uint16_t payload_length, uint32_t fill_data,
                           uint8_t *output, size_t output_size)
{
    size_t frame_size = CI1302_PROTOCOL_HEADER_SIZE + payload_length;

    if (!output || frame_size > output_size ||
        payload_length > CI1302_PROTOCOL_MAX_PAYLOAD ||
        (payload_length && !payload)) {
        return -1;
    }

    ci1302_protocol_write_le32(output, CI1302_PROTOCOL_MAGIC);
    ci1302_protocol_write_le16(output + 4,
        ci1302_protocol_checksum((const uint8_t *)payload, payload_length));
    ci1302_protocol_write_le16(output + 6, command);
    ci1302_protocol_write_le16(output + 8, payload_length);
    ci1302_protocol_write_le16(output + 10, CI1302_PROTOCOL_VERSION);
    ci1302_protocol_write_le32(output + 12, fill_data);
    if (payload_length) {
        memcpy(output + CI1302_PROTOCOL_HEADER_SIZE, payload, payload_length);
    }
    return (int)frame_size;
}
