#ifndef DSHANPI_VAXP_LAB_H
#define DSHANPI_VAXP_LAB_H

#include <stddef.h>
#include <stdint.h>

#include "vaxp_commands.h"

#define VAXP_LAB_MAX_FRAME_SIZE \
    (VAXP_MAX_HEADER_SIZE + VAXP_DEFAULT_MAX_PAYLOAD + VAXP_CRC_SIZE)

typedef enum vaxp_lab_parse_error {
    VAXP_LAB_ERROR_HEADER = 1,
    VAXP_LAB_ERROR_VERSION,
    VAXP_LAB_ERROR_LENGTH,
    VAXP_LAB_ERROR_CRC,
    VAXP_LAB_ERROR_TIMEOUT
} vaxp_lab_parse_error_t;

typedef void (*vaxp_lab_frame_cb)(void *context, const VaxpHeader *header,
                                  const uint8_t *payload,
                                  uint16_t payload_length);
typedef void (*vaxp_lab_error_cb)(void *context,
                                  vaxp_lab_parse_error_t error);

typedef struct vaxp_lab_parser {
    uint8_t frame[VAXP_LAB_MAX_FRAME_SIZE];
    size_t length;
    size_t expected_length;
    uint16_t max_payload;
    uint32_t last_byte_ms;
    uint32_t valid_frames;
    uint32_t crc_errors;
    uint32_t header_errors;
    uint32_t timeouts;
} vaxp_lab_parser_t;

void vaxp_lab_parser_init(vaxp_lab_parser_t *parser, uint16_t max_payload);
void vaxp_lab_parser_reset(vaxp_lab_parser_t *parser);
size_t vaxp_lab_parser_feed(vaxp_lab_parser_t *parser,
                            const uint8_t *data, size_t size,
                            uint32_t now_ms, vaxp_lab_frame_cb frame_cb,
                            vaxp_lab_error_cb error_cb, void *context);
void vaxp_lab_parser_tick(vaxp_lab_parser_t *parser, uint32_t now_ms,
                          uint32_t timeout_ms, vaxp_lab_error_cb error_cb,
                          void *context);

int vaxp_lab_encode_frame(uint8_t *output, size_t capacity,
                          uint8_t message_type, uint8_t flags,
                          uint16_t sequence, uint16_t command,
                          uint16_t session_id, uint8_t source,
                          uint8_t destination, uint32_t timestamp_ms,
                          const void *payload, uint16_t payload_length,
                          size_t *frame_size);
int vaxp_lab_self_test(void);
const char *vaxp_lab_message_type_name(uint8_t message_type);
const char *vaxp_lab_command_name(uint16_t command);
const char *vaxp_lab_error_name(vaxp_lab_parse_error_t error);

#endif
