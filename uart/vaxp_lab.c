#include "vaxp_lab.h"

#include <string.h>

static void vaxp_lab_decode_header(const uint8_t *wire, VaxpHeader *header)
{
    memset(header, 0, sizeof(*header));
    header->magic[0] = wire[0];
    header->magic[1] = wire[1];
    header->version = wire[2];
    header->header_length = wire[3];
    header->message_type = wire[4];
    header->flags = wire[5];
    header->sequence = vaxp_read_le16(wire + 6);
    header->command = vaxp_read_le16(wire + 8);
    header->payload_length = vaxp_read_le16(wire + 10);
    header->session_id = vaxp_read_le16(wire + 12);
    header->source = wire[14];
    header->destination = wire[15];
    header->timestamp_ms = vaxp_read_le32(wire + 16);
}

uint16_t vaxp_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;

    if (data == NULL && length != 0)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u) != 0
                      ? (uint16_t)((crc << 1) ^ 0x1021u)
                      : (uint16_t)(crc << 1);
    }
    return crc;
}

void vaxp_lab_parser_init(vaxp_lab_parser_t *parser, uint16_t max_payload)
{
    if (parser == NULL)
        return;
    memset(parser, 0, sizeof(*parser));
    parser->max_payload = max_payload == 0 ||
                                  max_payload > VAXP_DEFAULT_MAX_PAYLOAD
                              ? VAXP_DEFAULT_MAX_PAYLOAD : max_payload;
}

void vaxp_lab_parser_reset(vaxp_lab_parser_t *parser)
{
    if (parser == NULL)
        return;
    parser->length = 0;
    parser->expected_length = 0;
    parser->last_byte_ms = 0;
}

static void vaxp_lab_report_error(vaxp_lab_parser_t *parser,
                                  vaxp_lab_parse_error_t error,
                                  vaxp_lab_error_cb callback, void *context)
{
    if (error == VAXP_LAB_ERROR_CRC)
        ++parser->crc_errors;
    else if (error == VAXP_LAB_ERROR_TIMEOUT)
        ++parser->timeouts;
    else
        ++parser->header_errors;
    if (callback != NULL)
        callback(context, error);
}

static int vaxp_lab_header_valid(vaxp_lab_parser_t *parser,
                                 const VaxpHeader *header,
                                 vaxp_lab_parse_error_t *error)
{
    if (header->header_length < VAXP_BASE_HEADER_SIZE ||
        header->header_length > VAXP_MAX_HEADER_SIZE) {
        *error = VAXP_LAB_ERROR_HEADER;
        return 0;
    }
    if (header->version != VAXP_PROTOCOL_VERSION) {
        *error = VAXP_LAB_ERROR_VERSION;
        return 0;
    }
    if (header->message_type < VAXP_MSG_REQUEST ||
        header->message_type > VAXP_MSG_ACK) {
        *error = VAXP_LAB_ERROR_HEADER;
        return 0;
    }
    if (header->payload_length > parser->max_payload) {
        *error = VAXP_LAB_ERROR_LENGTH;
        return 0;
    }
    return 1;
}

static size_t vaxp_lab_parser_byte(vaxp_lab_parser_t *parser, uint8_t byte,
                                   uint32_t now_ms,
                                   vaxp_lab_frame_cb frame_cb,
                                   vaxp_lab_error_cb error_cb,
                                   void *context);

static size_t vaxp_lab_resync(vaxp_lab_parser_t *parser, uint32_t now_ms,
                              vaxp_lab_frame_cb frame_cb,
                              vaxp_lab_error_cb error_cb, void *context)
{
    size_t frames = 0;
    size_t search_start = 1;

    (void)error_cb;

    for (;;) {
        VaxpHeader header;
        vaxp_lab_parse_error_t ignored_error;
        size_t start = parser->length;

        for (size_t i = search_start; i + 1 < parser->length; ++i) {
            if (parser->frame[i] == VAXP_MAGIC0 &&
                parser->frame[i + 1] == VAXP_MAGIC1) {
                start = i;
                break;
            }
        }
        if (start == parser->length) {
            uint8_t trailing_magic = parser->length > 0 &&
                                     parser->frame[parser->length - 1] ==
                                         VAXP_MAGIC0;
            vaxp_lab_parser_reset(parser);
            if (trailing_magic) {
                parser->frame[0] = VAXP_MAGIC0;
                parser->length = 1;
                parser->last_byte_ms = now_ms;
            }
            return frames;
        }

        parser->length -= start;
        memmove(parser->frame, parser->frame + start, parser->length);
        parser->expected_length = 0;
        parser->last_byte_ms = now_ms;
        if (parser->length < VAXP_BASE_HEADER_SIZE)
            return frames;

        vaxp_lab_decode_header(parser->frame, &header);
        if (!vaxp_lab_header_valid(parser, &header, &ignored_error)) {
            search_start = 1;
            continue;
        }
        parser->expected_length =
            vaxp_frame_size(header.header_length, header.payload_length);
        if (parser->length < parser->expected_length)
            return frames;

        {
            uint16_t received_crc =
                vaxp_read_le16(parser->frame + parser->expected_length - 2);
            uint16_t computed_crc =
                vaxp_crc16_ccitt_false(parser->frame + 2,
                                       parser->expected_length - 4);

            if (received_crc != computed_crc) {
                search_start = 1;
                continue;
            }
        }

        ++parser->valid_frames;
        if (frame_cb != NULL)
            frame_cb(context, &header,
                     parser->frame + header.header_length,
                     header.payload_length);
        ++frames;
        {
            size_t remaining = parser->length - parser->expected_length;

            memmove(parser->frame,
                    parser->frame + parser->expected_length, remaining);
            parser->length = remaining;
            parser->expected_length = 0;
        }
        if (parser->length == 0) {
            parser->last_byte_ms = 0;
            return frames;
        }
        search_start = 0;
    }
}

static size_t vaxp_lab_parser_byte(vaxp_lab_parser_t *parser, uint8_t byte,
                                   uint32_t now_ms,
                                   vaxp_lab_frame_cb frame_cb,
                                   vaxp_lab_error_cb error_cb,
                                   void *context)
{
    VaxpHeader header;
    vaxp_lab_parse_error_t error;
    size_t frames = 0;

    if (parser->length == 0) {
        if (byte == VAXP_MAGIC0)
            parser->frame[parser->length++] = byte;
        parser->last_byte_ms = now_ms;
        return 0;
    }
    if (parser->length == 1) {
        if (byte == VAXP_MAGIC1) {
            parser->frame[parser->length++] = byte;
        } else if (byte == VAXP_MAGIC0) {
            parser->frame[0] = byte;
        } else {
            parser->length = 0;
        }
        parser->last_byte_ms = now_ms;
        return 0;
    }
    if (parser->length >= sizeof(parser->frame)) {
        vaxp_lab_report_error(parser, VAXP_LAB_ERROR_LENGTH,
                              error_cb, context);
        return vaxp_lab_resync(parser, now_ms, frame_cb, error_cb, context);
    }
    parser->frame[parser->length++] = byte;
    parser->last_byte_ms = now_ms;

    if (parser->length == VAXP_BASE_HEADER_SIZE) {
        vaxp_lab_decode_header(parser->frame, &header);
        if (!vaxp_lab_header_valid(parser, &header, &error)) {
            vaxp_lab_report_error(parser, error, error_cb, context);
            return vaxp_lab_resync(parser, now_ms, frame_cb, error_cb,
                                   context);
        }
        parser->expected_length =
            vaxp_frame_size(header.header_length, header.payload_length);
    }
    if (parser->expected_length != 0 &&
        parser->length == parser->expected_length) {
        uint16_t received_crc =
            vaxp_read_le16(parser->frame + parser->expected_length - 2);
        uint16_t computed_crc =
            vaxp_crc16_ccitt_false(parser->frame + 2,
                                   parser->expected_length - 4);
        if (received_crc != computed_crc) {
            vaxp_lab_report_error(parser, VAXP_LAB_ERROR_CRC,
                                  error_cb, context);
            return vaxp_lab_resync(parser, now_ms, frame_cb, error_cb,
                                   context);
        }
        vaxp_lab_decode_header(parser->frame, &header);
        ++parser->valid_frames;
        if (frame_cb != NULL)
            frame_cb(context, &header,
                     parser->frame + header.header_length,
                     header.payload_length);
        frames = 1;
        vaxp_lab_parser_reset(parser);
    }
    return frames;
}

size_t vaxp_lab_parser_feed(vaxp_lab_parser_t *parser,
                            const uint8_t *data, size_t size,
                            uint32_t now_ms, vaxp_lab_frame_cb frame_cb,
                            vaxp_lab_error_cb error_cb, void *context)
{
    size_t frames = 0;

    if (parser == NULL || (data == NULL && size != 0))
        return 0;
    for (size_t i = 0; i < size; ++i)
        frames += vaxp_lab_parser_byte(parser, data[i], now_ms, frame_cb,
                                       error_cb, context);
    return frames;
}

void vaxp_lab_parser_tick(vaxp_lab_parser_t *parser, uint32_t now_ms,
                          uint32_t timeout_ms, vaxp_lab_error_cb error_cb,
                          void *context)
{
    if (parser == NULL || parser->length == 0 || timeout_ms == 0)
        return;
    if ((uint32_t)(now_ms - parser->last_byte_ms) >= timeout_ms) {
        vaxp_lab_report_error(parser, VAXP_LAB_ERROR_TIMEOUT,
                              error_cb, context);
        vaxp_lab_parser_reset(parser);
    }
}

int vaxp_lab_encode_frame(uint8_t *output, size_t capacity,
                          uint8_t message_type, uint8_t flags,
                          uint16_t sequence, uint16_t command,
                          uint16_t session_id, uint8_t source,
                          uint8_t destination, uint32_t timestamp_ms,
                          const void *payload, uint16_t payload_length,
                          size_t *frame_size)
{
    size_t size = VAXP_BASE_HEADER_SIZE + payload_length + VAXP_CRC_SIZE;
    uint16_t crc;

    if (output == NULL || frame_size == NULL || capacity < size ||
        (payload == NULL && payload_length != 0) ||
        message_type < VAXP_MSG_REQUEST || message_type > VAXP_MSG_ACK)
        return -1;
    output[0] = VAXP_MAGIC0;
    output[1] = VAXP_MAGIC1;
    output[2] = VAXP_PROTOCOL_VERSION;
    output[3] = VAXP_BASE_HEADER_SIZE;
    output[4] = message_type;
    output[5] = flags;
    vaxp_write_le16(output + 6, sequence);
    vaxp_write_le16(output + 8, command);
    vaxp_write_le16(output + 10, payload_length);
    vaxp_write_le16(output + 12, session_id);
    output[14] = source;
    output[15] = destination;
    vaxp_write_le32(output + 16, timestamp_ms);
    if (payload_length != 0)
        memcpy(output + VAXP_BASE_HEADER_SIZE, payload, payload_length);
    crc = vaxp_crc16_ccitt_false(output + 2, size - 4);
    vaxp_write_le16(output + size - 2, crc);
    *frame_size = size;
    return 0;
}

typedef struct vaxp_lab_test_context {
    unsigned frames;
    unsigned errors;
    uint16_t last_command;
    uint16_t last_payload_length;
    uint8_t last_payload[16];
    vaxp_lab_parse_error_t last_error;
} vaxp_lab_test_context_t;

static void vaxp_lab_test_frame(void *context, const VaxpHeader *header,
                                const uint8_t *payload,
                                uint16_t payload_length)
{
    vaxp_lab_test_context_t *test = context;
    size_t copy_length = payload_length;

    ++test->frames;
    test->last_command = header->command;
    test->last_payload_length = payload_length;
    if (copy_length > sizeof(test->last_payload))
        copy_length = sizeof(test->last_payload);
    if (copy_length != 0)
        memcpy(test->last_payload, payload, copy_length);
}

static void vaxp_lab_test_error(void *context,
                                vaxp_lab_parse_error_t error)
{
    vaxp_lab_test_context_t *test = context;

    ++test->errors;
    test->last_error = error;
}

int vaxp_lab_self_test(void)
{
    static const uint8_t ping_expected[] = {
        0xAA, 0x55, 0x10, 0x14, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x34, 0x12, 0x00, 0x01,
        0x00, 0x10, 0x00, 0x00, 0xBF, 0x15
    };
    static const uint8_t hello_payload[] = {
        0x10, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t hello_expected[] = {
        0xAA, 0x55, 0x10, 0x14, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x10,
        0x00, 0x00, 0x00, 0x00, 0xD0, 0x9F
    };
    static vaxp_lab_parser_t parser;
    static uint8_t max_payload[VAXP_DEFAULT_MAX_PAYLOAD];
    static uint8_t large_frame[VAXP_LAB_MAX_FRAME_SIZE];
    uint8_t frame[96];
    uint8_t stream[96];
    uint8_t embedded_magic[] = {0x11, 0xAA, 0x55, 0x22, 0x33};
    vaxp_lab_test_context_t test;
    size_t size;

    if (vaxp_lab_encode_frame(frame, sizeof(frame), VAXP_MSG_REQUEST, 0,
                              1, VAXP_CMD_PING, 0x1234,
                              VAXP_ADDR_HOST, 0x01, 0x1000,
                              NULL, 0, &size) != 0 ||
        size != sizeof(ping_expected) ||
        memcmp(frame, ping_expected, sizeof(ping_expected)) != 0)
        return -1;
    if (vaxp_lab_encode_frame(frame, sizeof(frame), VAXP_MSG_REQUEST, 0,
                              1, VAXP_CMD_HELLO, 0,
                              VAXP_ADDR_HOST, 0x01, 0,
                              hello_payload, sizeof(hello_payload),
                              &size) != 0 ||
        size != sizeof(hello_expected) ||
        memcmp(frame, hello_expected, sizeof(hello_expected)) != 0)
        return -2;

    /* Arbitrary one-byte input granularity. */
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    for (size_t i = 0; i < sizeof(ping_expected); ++i)
        vaxp_lab_parser_feed(&parser, ping_expected + i, 1,
                             (uint32_t)i, vaxp_lab_test_frame,
                             vaxp_lab_test_error, &test);
    if (test.frames != 1 || test.errors != 0 ||
        test.last_command != VAXP_CMD_PING)
        return -3;

    /* Two complete frames in a single UART read. */
    memcpy(stream, ping_expected, sizeof(ping_expected));
    memcpy(stream + sizeof(ping_expected), hello_expected,
           sizeof(hello_expected));
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    if (vaxp_lab_parser_feed(&parser, stream,
                             sizeof(ping_expected) + sizeof(hello_expected),
                             10, vaxp_lab_test_frame, vaxp_lab_test_error,
                             &test) != 2 || test.frames != 2 ||
        test.errors != 0)
        return -4;

    /* AA AA 55 must synchronize on the second AA. */
    stream[0] = VAXP_MAGIC0;
    memcpy(stream + 1, ping_expected, sizeof(ping_expected));
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_lab_parser_feed(&parser, stream, sizeof(ping_expected) + 1,
                         20, vaxp_lab_test_frame, vaxp_lab_test_error,
                         &test);
    if (test.frames != 1 || test.errors != 0)
        return -5;

    /* Magic is legal inside a payload and must not cause resynchronization. */
    if (vaxp_lab_encode_frame(frame, sizeof(frame), VAXP_MSG_EVENT, 0,
                              2, 0x8123, 0x1234, 1, VAXP_ADDR_HOST,
                              30, embedded_magic, sizeof(embedded_magic),
                              &size) != 0)
        return -6;
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_lab_parser_feed(&parser, frame, size, 30, vaxp_lab_test_frame,
                         vaxp_lab_test_error, &test);
    if (test.frames != 1 || test.errors != 0 ||
        test.last_payload_length != sizeof(embedded_magic) ||
        memcmp(test.last_payload, embedded_magic,
               sizeof(embedded_magic)) != 0)
        return -7;

    /* A corrupt frame must not consume the valid frame following it. */
    memcpy(stream, ping_expected, sizeof(ping_expected));
    stream[sizeof(ping_expected) - 1] ^= 0x01;
    memcpy(stream + sizeof(ping_expected), hello_expected,
           sizeof(hello_expected));
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_lab_parser_feed(&parser, stream,
                         sizeof(ping_expected) + sizeof(hello_expected),
                         40, vaxp_lab_test_frame, vaxp_lab_test_error,
                         &test);
    if (test.frames != 1 || test.errors != 1 ||
        test.last_error != VAXP_LAB_ERROR_CRC ||
        test.last_command != VAXP_CMD_HELLO)
        return -8;

    /* Header extensions are skipped but remain covered by CRC. */
    memcpy(frame, ping_expected, VAXP_BASE_HEADER_SIZE);
    frame[3] = 24;
    frame[20] = 0xDE;
    frame[21] = 0xAD;
    frame[22] = 0xBE;
    frame[23] = 0xEF;
    vaxp_write_le16(frame + 24,
                    vaxp_crc16_ccitt_false(frame + 2, 22));
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_lab_parser_feed(&parser, frame, 26, 50, vaxp_lab_test_frame,
                         vaxp_lab_test_error, &test);
    if (test.frames != 1 || test.errors != 0)
        return -9;

    /* A deletion may make a bad frame consume the start of the next frame.
     * Resync must preserve that next valid header even when it is more than
     * one base-header length behind the current error boundary. */
    for (size_t i = 0; i < 100; ++i)
        max_payload[i] = (uint8_t)(i + 1u);
    if (vaxp_lab_encode_frame(large_frame, sizeof(large_frame),
                              VAXP_MSG_EVENT, 0, 4, 0x8124, 0x1234,
                              1, VAXP_ADDR_HOST, 55, max_payload, 100,
                              &size) != 0 || size != 122)
        return -10;
    for (size_t i = 0; i < 64; ++i)
        stream[i] = (uint8_t)(0x80u + i);
    {
        size_t next_size;

        if (vaxp_lab_encode_frame(frame, sizeof(frame), VAXP_MSG_EVENT, 0,
                                  5, 0x8125, 0x1234, 1, VAXP_ADDR_HOST,
                                  56, stream, 64, &next_size) != 0)
            return -11;
        memcpy(max_payload, large_frame, 40);
        memcpy(max_payload + 40, large_frame + 65, size - 65);
        memcpy(max_payload + size - 25, frame, next_size);
        memset(&test, 0, sizeof(test));
        vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
        vaxp_lab_parser_feed(&parser, max_payload,
                             size - 25 + next_size, 56,
                             vaxp_lab_test_frame, vaxp_lab_test_error,
                             &test);
        if (test.frames != 1 || test.errors != 1 ||
            test.last_command != 0x8125)
            return -12;
    }

    /* The negotiated maximum payload is accepted in random-sized chunks. */
    for (size_t i = 0; i < sizeof(max_payload); ++i)
        max_payload[i] = (uint8_t)(i * 29u + 7u);
    if (vaxp_lab_encode_frame(large_frame, sizeof(large_frame),
                              VAXP_MSG_EVENT, 0, 3, 0x8124, 0x1234,
                              1, VAXP_ADDR_HOST, 60, max_payload,
                              sizeof(max_payload), &size) != 0)
        return -13;
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    for (size_t offset = 0; offset < size;) {
        size_t chunk = (offset % 23u) + 1u;
        if (chunk > size - offset)
            chunk = size - offset;
        vaxp_lab_parser_feed(&parser, large_frame + offset, chunk,
                             (uint32_t)(60 + offset),
                             vaxp_lab_test_frame, vaxp_lab_test_error,
                             &test);
        offset += chunk;
    }
    if (test.frames != 1 || test.errors != 0 ||
        test.last_payload_length != VAXP_DEFAULT_MAX_PAYLOAD)
        return -14;

    /* Incomplete frames time out and leave the parser synchronized. */
    memset(&test, 0, sizeof(test));
    vaxp_lab_parser_init(&parser, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_lab_parser_feed(&parser, ping_expected, 10, 100,
                         vaxp_lab_test_frame, vaxp_lab_test_error, &test);
    vaxp_lab_parser_tick(&parser, 200, 100, vaxp_lab_test_error, &test);
    if (test.frames != 0 || test.errors != 1 ||
        test.last_error != VAXP_LAB_ERROR_TIMEOUT || parser.length != 0)
        return -15;
    return 0;
}

const char *vaxp_lab_message_type_name(uint8_t message_type)
{
    switch (message_type) {
    case VAXP_MSG_REQUEST: return "REQUEST";
    case VAXP_MSG_RESPONSE: return "RESPONSE";
    case VAXP_MSG_EVENT: return "EVENT";
    case VAXP_MSG_ACK: return "ACK";
    default: return "INVALID";
    }
}

const char *vaxp_lab_command_name(uint16_t command)
{
    switch (command) {
    case VAXP_CMD_HELLO: return "HELLO";
    case VAXP_CMD_GET_CAPABILITIES: return "GET_CAPABILITIES";
    case VAXP_CMD_PING: return "PING";
    case VAXP_CMD_GET_DEVICE_INFO: return "GET_DEVICE_INFO";
    case VAXP_CMD_GET_STATUS: return "GET_STATUS";
    case VAXP_CMD_SET_TIME: return "SET_TIME";
    case VAXP_CMD_TIME_SYNC: return "TIME_SYNC";
    case VAXP_CMD_GET_HEALTH: return "GET_HEALTH";
    case VAXP_EVENT_DEVICE_BOOT: return "DEVICE_BOOT";
    case VAXP_EVENT_HEARTBEAT: return "HEARTBEAT";
    case VAXP_EVENT_STATE_CHANGED: return "STATE_CHANGED";
    case VAXP_EVENT_OPERATION_PROGRESS: return "OPERATION_PROGRESS";
    case VAXP_EVENT_OPERATION_COMPLETE: return "OPERATION_COMPLETE";
    case VAXP_EVENT_DETECTION_RESULT: return "DETECTION_RESULT";
    case VAXP_EVENT_CLASSIFICATION_RESULT: return "CLASSIFICATION_RESULT";
    case VAXP_EVENT_POSE_RESULT: return "POSE_RESULT";
    case VAXP_EVENT_SEGMENTATION_RESULT: return "SEGMENTATION_RESULT";
    case VAXP_EVENT_OCR_RESULT: return "OCR_RESULT";
    case VAXP_EVENT_FACE_RESULT: return "FACE_RESULT";
    case VAXP_EVENT_ALARM_START: return "ALARM_START";
    case VAXP_EVENT_ALARM_UPDATE: return "ALARM_UPDATE";
    case VAXP_EVENT_ALARM_END: return "ALARM_END";
    case VAXP_EVENT_DEVICE_ERROR: return "DEVICE_ERROR";
    case VAXP_EVENT_LOG: return "LOG";
    default: return "UNKNOWN";
    }
}

const char *vaxp_lab_error_name(vaxp_lab_parse_error_t error)
{
    switch (error) {
    case VAXP_LAB_ERROR_HEADER: return "invalid header";
    case VAXP_LAB_ERROR_VERSION: return "unsupported version";
    case VAXP_LAB_ERROR_LENGTH: return "invalid length";
    case VAXP_LAB_ERROR_CRC: return "CRC mismatch";
    case VAXP_LAB_ERROR_TIMEOUT: return "assembly timeout";
    default: return "parser error";
    }
}
