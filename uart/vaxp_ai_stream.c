#include "vaxp_ai_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drv_uart.h"
#include "uart_lab.h"
#include "vaxp_lab.h"
#include "system_settings.h"

#define AI_TX_PAYLOAD_MAX VAXP_DEFAULT_MAX_PAYLOAD
#define AI_DEVICE_ADDRESS VAXP_ADDR_DEVICE_MIN
#define AI_BOOT_ACK_TIMEOUT_MS 500u
#define AI_BOOT_MAX_RETRIES 3u
#define AI_CLASS_CAPACITY 512u
#define AI_CLASS_NAME_CAPACITY 48u
#define AI_RESPONSE_CACHE_CAPACITY 32u

typedef struct ai_class_entry {
    uint16_t model_id;
    uint16_t class_id;
    char name[AI_CLASS_NAME_CAPACITY];
} ai_class_entry_t;

typedef struct ai_cached_response {
    uint8_t valid;
    uint8_t source;
    uint16_t session_id;
    uint16_t sequence;
    uint16_t command;
    uint16_t payload_length;
    uint8_t payload[AI_TX_PAYLOAD_MAX];
} ai_cached_response_t;

typedef struct ai_stream_state {
    dshanpi_uart_lab_t *uart;
    vaxp_lab_parser_t parser;
    uint8_t started;
    uint8_t stopping;
    uint8_t host_ready;
    uint8_t stream_enabled;
    uint8_t channel_id;
    uint8_t max_objects;
    uint8_t object_limit;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t peer_max_rx_payload;
    uint16_t session_id;
    uint16_t tx_sequence;
    uint16_t boot_sequence;
    uint16_t current_pipeline_id;
    uint16_t current_model_id;
    uint16_t subscribed_pipeline_id;
    uint16_t result_mask;
    uint16_t result_fps;
    uint16_t result_fps_limit;
    uint8_t current_task_type;
    uint8_t boot_pending;
    uint8_t boot_retries;
    uint32_t boot_sent_ms;
    uint32_t last_heartbeat_ms;
    uint32_t started_ms;
    uint32_t frame_id;
    uint32_t last_result_ms[8];
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t parser_errors;
    uint32_t dropped_results;
    uint32_t baud_rate;
    uint64_t capabilities;
    char application_name[32];
    char mode_name[32];
    ai_class_entry_t classes[AI_CLASS_CAPACITY];
    size_t class_count;
} ai_stream_state_t;

static ai_stream_state_t g_ai;
static uint8_t g_tx_frame[VAXP_LAB_MAX_FRAME_SIZE];
static uint8_t g_payload[AI_TX_PAYLOAD_MAX];
static uint8_t g_response_body[AI_TX_PAYLOAD_MAX - sizeof(VaxpResponseHeader)];
static ai_cached_response_t g_response_cache[AI_RESPONSE_CACHE_CAPACITY];
static size_t g_response_cache_next;

static uint32_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u +
                      (uint64_t)now.tv_nsec / 1000000u);
}

static uint8_t object_limit_for_baud(uint32_t baud_rate)
{
    if (baud_rate <= 115200u)
        return 16;
    if (baud_rate <= 460800u)
        return 32;
    return VAXP_K230_MAX_OBJECTS;
}

static uint16_t result_fps_limit_for_baud(uint32_t baud_rate)
{
    if (baud_rate <= 115200u)
        return 10;
    if (baud_rate <= 460800u)
        return 20;
    return 30;
}

static uint32_t assembly_timeout_ms(void)
{
    uint32_t timeout_ms = 100;
    if (g_ai.parser.expected_length != 0 && g_ai.baud_rate != 0) {
        uint32_t wire_ms = (uint32_t)
            (((uint64_t)g_ai.parser.expected_length * 10000u +
              g_ai.baud_rate - 1u) / g_ai.baud_rate);
        if (wire_ms * 3u > timeout_ms)
            timeout_ms = wire_ms * 3u;
    }
    return timeout_ms > 2000u ? 2000u : timeout_ms;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u +
           (uint64_t)now.tv_nsec / 1000u;
}

static uint16_t next_sequence(void)
{
    ++g_ai.tx_sequence;
    if (g_ai.tx_sequence == 0)
        ++g_ai.tx_sequence;
    return g_ai.tx_sequence;
}

static size_t bounded_text_length(const char *text, size_t maximum)
{
    size_t length = 0;
    if (text == NULL)
        return 0;
    while (length < maximum && text[length] != '\0')
        ++length;
    return length;
}

static uint16_t confidence_wire(float confidence)
{
    if (confidence <= 0.0f)
        return 0;
    if (confidence >= 1.0f)
        return VAXP_CONFIDENCE_MAX;
    return (uint16_t)(confidence * 10000.0f + 0.5f);
}

static uint16_t coordinate_wire(float coordinate, uint16_t extent)
{
    float normalized;
    if (extent == 0 || coordinate <= 0.0f)
        return 0;
    normalized = coordinate * 10000.0f / (float)extent;
    if (normalized >= 10000.0f)
        return VAXP_COORD_MAX;
    return (uint16_t)(normalized + 0.5f);
}

static uint16_t dimension_wire(float origin, float dimension,
                               uint16_t extent)
{
    float clipped_origin = origin;
    float clipped_end = origin + dimension;
    if (clipped_origin < 0.0f)
        clipped_origin = 0.0f;
    if (clipped_end < clipped_origin)
        clipped_end = clipped_origin;
    if (clipped_end > (float)extent)
        clipped_end = (float)extent;
    return coordinate_wire(clipped_end - clipped_origin, extent);
}

static int send_frame(uint8_t message_type, uint8_t flags,
                      uint16_t sequence, uint16_t command,
                      uint8_t destination, const uint8_t *payload,
                      uint16_t payload_length)
{
    size_t frame_size = 0;
    if (g_ai.uart == NULL ||
        vaxp_lab_encode_frame(g_tx_frame, sizeof(g_tx_frame), message_type,
                              flags, sequence, command, g_ai.session_id,
                              AI_DEVICE_ADDRESS, destination,
                              monotonic_ms(), payload, payload_length,
                              &frame_size) != 0)
        return -1;
    if (dshanpi_uart_lab_write_all(g_ai.uart, g_tx_frame, frame_size) != 0)
        return -2;
    ++g_ai.tx_packets;
    return 0;
}

static void write_response_header(uint8_t *payload, int16_t status,
                                  uint16_t detail)
{
    vaxp_write_le16(payload, (uint16_t)status);
    vaxp_write_le16(payload + 2, detail);
}

static ai_cached_response_t *find_cached_response(
    const VaxpHeader *request)
{
    for (size_t index = 0; index < AI_RESPONSE_CACHE_CAPACITY; ++index) {
        ai_cached_response_t *cached = &g_response_cache[index];
        if (cached->valid && cached->session_id == request->session_id &&
            cached->source == request->source &&
            cached->sequence == request->sequence &&
            cached->command == request->command)
            return cached;
    }
    return NULL;
}

static void store_cached_response(const VaxpHeader *request,
                                  const uint8_t *payload,
                                  uint16_t payload_length)
{
    ai_cached_response_t *cached;
    if (payload_length > AI_TX_PAYLOAD_MAX)
        return;
    cached = &g_response_cache[g_response_cache_next];
    g_response_cache_next = (g_response_cache_next + 1u) %
                            AI_RESPONSE_CACHE_CAPACITY;
    memset(cached, 0, sizeof(*cached));
    cached->valid = 1;
    cached->source = request->source;
    cached->session_id = request->session_id;
    cached->sequence = request->sequence;
    cached->command = request->command;
    cached->payload_length = payload_length;
    memcpy(cached->payload, payload, payload_length);
}

static void replay_cached_response(const ai_cached_response_t *cached)
{
    send_frame(VAXP_MSG_RESPONSE, 0, cached->sequence, cached->command,
               cached->source, cached->payload, cached->payload_length);
}

static void send_response(const VaxpHeader *request, int16_t status,
                          uint16_t detail, const uint8_t *body,
                          uint16_t body_length)
{
    uint16_t length = (uint16_t)(sizeof(VaxpResponseHeader) + body_length);
    if (length > sizeof(g_payload))
        return;
    write_response_header(g_payload, status, detail);
    if (body_length != 0 && body != NULL)
        memcpy(g_payload + sizeof(VaxpResponseHeader), body, body_length);
    if (length > g_ai.peer_max_rx_payload) {
        uint16_t required = length;
        length = sizeof(VaxpResponseHeader);
        write_response_header(g_payload, VAXP_ERR_RESOURCE_LIMIT, required);
    }
    store_cached_response(request, g_payload, length);
    send_frame(VAXP_MSG_RESPONSE, 0, request->sequence, request->command,
               request->source, g_payload, length);
}

static void remember_class(uint16_t model_id, uint16_t class_id,
                           const char *name)
{
    size_t index;
    if (name == NULL || name[0] == '\0')
        return;
    for (index = 0; index < g_ai.class_count; ++index) {
        if (g_ai.classes[index].model_id == model_id &&
            g_ai.classes[index].class_id == class_id) {
            snprintf(g_ai.classes[index].name,
                     sizeof(g_ai.classes[index].name), "%s", name);
            return;
        }
    }
    if (g_ai.class_count >= AI_CLASS_CAPACITY)
        return;
    g_ai.classes[g_ai.class_count].model_id = model_id;
    g_ai.classes[g_ai.class_count].class_id = class_id;
    snprintf(g_ai.classes[g_ai.class_count].name,
             sizeof(g_ai.classes[g_ai.class_count].name), "%s", name);
    ++g_ai.class_count;
}

static uint16_t model_class_count(uint16_t model_id)
{
    uint16_t count = 0;
    for (size_t index = 0; index < g_ai.class_count; ++index) {
        if (g_ai.classes[index].model_id == model_id)
            ++count;
    }
    return count;
}

static uint16_t write_class_list(uint8_t *body, size_t capacity,
                                 uint16_t model_id)
{
    size_t offset = 0;
    for (size_t index = 0; index < g_ai.class_count; ++index) {
        const ai_class_entry_t *entry = &g_ai.classes[index];
        size_t name_length;
        if (entry->model_id != model_id)
            continue;
        name_length = bounded_text_length(entry->name,
                                          AI_CLASS_NAME_CAPACITY - 1u);
        if (offset + sizeof(VaxpClassEntryHeader) + name_length > capacity)
            break;
        vaxp_write_le16(body + offset, entry->class_id);
        vaxp_write_le16(body + offset + 2, (uint16_t)name_length);
        memcpy(body + offset + sizeof(VaxpClassEntryHeader), entry->name,
               name_length);
        offset += sizeof(VaxpClassEntryHeader) + name_length;
    }
    return (uint16_t)offset;
}

static void write_hello_response(uint8_t *body)
{
    body[0] = VAXP_PROTOCOL_VERSION;
    body[1] = 0;
    vaxp_write_le16(body + 2, g_ai.session_id);
    vaxp_write_le16(body + 4, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_write_le16(body + 6, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_write_le16(body + 8, VAXP_DEFAULT_MAX_PENDING);
    vaxp_write_le16(body + 10, VAXP_DEFAULT_HEARTBEAT_MS);
    vaxp_write_le64(body + 12, g_ai.capabilities);
}

static void write_capabilities(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpCapabilities));
    vaxp_write_le64(body, g_ai.capabilities);
    vaxp_write_le16(body + 8, VAXP_DEFAULT_MAX_PAYLOAD);
    vaxp_write_le16(body + 10, g_ai.object_limit);
    body[12] = 1;
    body[13] = 16;
    body[14] = 16;
    body[15] = 0;
    body[16] = 0;
    body[17] = 0;
}

static void write_device_info(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpDeviceInfo));
    snprintf((char *)body, 32, "DongshanPI %.20s", g_ai.application_name);
    snprintf((char *)body + 32, 32, "DongshanPI");
    snprintf((char *)body + 64, 32, "K230-UART2");
    snprintf((char *)body + 96, 16, "CanMV-K230");
    snprintf((char *)body + 112, 16, "VAXP-AI-1.0");
    snprintf((char *)body + 128, 16, "CanMV-1.6");
    snprintf((char *)body + 144, 16, "K230");
    snprintf((char *)body + 160, 24, "VAXP-K230-%lu",
             (unsigned long)g_ai.baud_rate);
}

static void write_device_status(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpDeviceStatus));
    body[0] = VAXP_DEVICE_RUNNING;
    body[1] = 1;
    body[2] = g_ai.current_model_id != 0 ? 1 : 0;
    body[3] = g_ai.current_pipeline_id != 0 ? 1 : 0;
    vaxp_write_le32(body + 4, (monotonic_ms() - g_ai.started_ms) / 1000u);
}

static void write_health(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpHealthStatus));
}

static void write_model_info(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpModelInfo));
    vaxp_write_le16(body, g_ai.current_model_id);
    body[18] = g_ai.current_task_type;
    body[19] = VAXP_MODEL_IN_USE;
    snprintf((char *)body + 20, 32, "%s", g_ai.mode_name);
    snprintf((char *)body + 52, 16, "1.0");
    vaxp_write_le16(body + 68, g_ai.source_width);
    vaxp_write_le16(body + 70, g_ai.source_height);
    vaxp_write_le16(body + 72, model_class_count(g_ai.current_model_id));
    vaxp_write_le16(body + 74, g_ai.object_limit);
}

static void write_pipeline_info(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpPipelineDescriptor));
    vaxp_write_le16(body, g_ai.current_pipeline_id);
    vaxp_write_le16(body + 2, g_ai.current_model_id);
    body[4] = g_ai.channel_id;
    body[5] = g_ai.current_task_type;
    body[6] = 1;
    body[7] = VAXP_PIPELINE_RUNNING;
    vaxp_write_le16(body + 8, g_ai.source_width);
    vaxp_write_le16(body + 10, g_ai.source_height);
    vaxp_write_le16(body + 12, 30);
    vaxp_write_le16(body + 14, g_ai.result_fps);
}

static void write_input_info(uint8_t *body)
{
    memset(body, 0, sizeof(VaxpInputConfig));
    body[0] = g_ai.channel_id;
    body[1] = g_ai.channel_id == 2 ? VAXP_INPUT_CSI2 : VAXP_INPUT_CSI0;
    body[2] = VAXP_PIXFMT_RGB888;
    body[3] = VAXP_ROTATE_0;
    vaxp_write_le16(body + 4, g_ai.source_width);
    vaxp_write_le16(body + 6, g_ai.source_height);
    vaxp_write_le16(body + 8, 30);
    vaxp_write_le16(body + 10, 1);
}

static void handle_request(const VaxpHeader *header, const uint8_t *payload,
                           uint16_t payload_length)
{
    ai_cached_response_t *cached;
    if (header->sequence == 0 || header->source != VAXP_ADDR_HOST ||
        header->destination != AI_DEVICE_ADDRESS)
        return;
    if ((header->flags & ~(VAXP_FLAG_ACK_REQUIRED | VAXP_FLAG_URGENT)) != 0) {
        send_response(header, VAXP_ERR_NOT_SUPPORTED, header->flags,
                      NULL, 0);
        return;
    }
    if (header->command == VAXP_CMD_HELLO && header->session_id != 0) {
        send_response(header, VAXP_ERR_SESSION_INVALID, 0, NULL, 0);
        return;
    }
    if (header->command != VAXP_CMD_HELLO &&
        (!g_ai.host_ready || header->session_id != g_ai.session_id)) {
        send_response(header, VAXP_ERR_SESSION_INVALID, 0, NULL, 0);
        return;
    }
    cached = find_cached_response(header);
    if (cached != NULL) {
        replay_cached_response(cached);
        return;
    }
    switch (header->command) {
    case VAXP_CMD_HELLO:
        if (payload_length != sizeof(VaxpHelloRequest))
            goto invalid_length;
        if (payload[0] > payload[1]) {
            send_response(header, VAXP_ERR_INVALID_PARAMETER, 0, NULL, 0);
            return;
        }
        if (payload[0] > VAXP_PROTOCOL_VERSION ||
            payload[1] < VAXP_PROTOCOL_VERSION) {
            send_response(header, VAXP_ERR_VERSION_NOT_SUPPORTED,
                          VAXP_PROTOCOL_VERSION,
                          NULL, 0);
            return;
        }
        if (vaxp_read_le16(payload + 2) <
            sizeof(VaxpResponseHeader) + sizeof(VaxpHelloResponse)) {
            send_response(header, VAXP_ERR_INVALID_PARAMETER,
                          sizeof(VaxpResponseHeader) +
                              sizeof(VaxpHelloResponse),
                          NULL, 0);
            return;
        }
        write_hello_response(g_response_body);
        memset(g_response_cache, 0, sizeof(g_response_cache));
        g_response_cache_next = 0;
        g_ai.host_ready = 1;
        g_ai.peer_max_rx_payload = vaxp_read_le16(payload + 2);
        if (g_ai.peer_max_rx_payload > VAXP_DEFAULT_MAX_PAYLOAD)
            g_ai.peer_max_rx_payload = VAXP_DEFAULT_MAX_PAYLOAD;
        g_ai.stream_enabled = 0;
        g_ai.subscribed_pipeline_id = 0;
        memset(g_ai.last_result_ms, 0, sizeof(g_ai.last_result_ms));
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpHelloResponse));
        break;
    case VAXP_CMD_PING:
        if (payload_length != 0) goto invalid_length;
        send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
        break;
    case VAXP_CMD_GET_CAPABILITIES:
        if (payload_length != 0) goto invalid_length;
        write_capabilities(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpCapabilities));
        break;
    case VAXP_CMD_GET_DEVICE_INFO:
        if (payload_length != 0) goto invalid_length;
        write_device_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpDeviceInfo));
        break;
    case VAXP_CMD_GET_STATUS:
        if (payload_length != 0) goto invalid_length;
        write_device_status(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpDeviceStatus));
        break;
    case VAXP_CMD_GET_HEALTH:
        if (payload_length != 0) goto invalid_length;
        write_health(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpHealthStatus));
        break;
    case VAXP_CMD_TIME_SYNC:
        if (payload_length != sizeof(VaxpTimeSyncRequest))
            goto invalid_length;
        memcpy(g_response_body, payload, sizeof(uint64_t));
        vaxp_write_le64(g_response_body + 8, monotonic_us());
        vaxp_write_le64(g_response_body + 16, monotonic_us());
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpTimeSyncResponse));
        break;
    case VAXP_CMD_GET_INPUT_LIST:
        if (payload_length != 0) goto invalid_length;
        write_input_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpInputConfig));
        break;
    case VAXP_CMD_GET_INPUT_STATUS:
        if (payload_length != sizeof(VaxpInputSelector))
            goto invalid_length;
        if (payload[0] != g_ai.channel_id) {
            send_response(header, VAXP_ERR_CAMERA_NOT_FOUND, 0, NULL, 0);
            break;
        }
        write_input_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpInputConfig));
        break;
    case VAXP_CMD_GET_MODEL_LIST:
        if (payload_length != 0) goto invalid_length;
        if (g_ai.current_model_id == 0) {
            send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
            break;
        }
        write_model_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpModelInfo));
        break;
    case VAXP_CMD_GET_MODEL_INFO:
        if (payload_length != sizeof(VaxpModelSelector))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_model_id) {
            send_response(header, VAXP_ERR_MODEL_NOT_FOUND, 0, NULL, 0);
            break;
        }
        write_model_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpModelInfo));
        break;
    case VAXP_CMD_GET_CLASS_LIST:
        if (payload_length != sizeof(VaxpModelSelector))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_model_id) {
            send_response(header, VAXP_ERR_MODEL_NOT_FOUND, 0, NULL, 0);
            break;
        }
        {
            uint16_t class_list_length = write_class_list(
                g_response_body, sizeof(g_response_body),
                g_ai.current_model_id);
            send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                          class_list_length);
        }
        break;
    case VAXP_CMD_PIPELINE_GET_STATUS:
        if (payload_length != sizeof(VaxpPipelineSelector))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_pipeline_id) {
            send_response(header, VAXP_ERR_PIPELINE_NOT_FOUND, 0,
                          NULL, 0);
            break;
        }
        write_pipeline_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpPipelineDescriptor));
        break;
    case VAXP_CMD_PIPELINE_GET_LIST:
        if (payload_length != 0) goto invalid_length;
        if (g_ai.current_pipeline_id == 0) {
            send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
            break;
        }
        write_pipeline_info(g_response_body);
        send_response(header, VAXP_STATUS_OK, 0, g_response_body,
                      sizeof(VaxpPipelineDescriptor));
        break;
    case VAXP_CMD_RESULT_SUBSCRIBE:
        if (payload_length != sizeof(VaxpResultSubscription))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_pipeline_id) {
            send_response(header, VAXP_ERR_PIPELINE_NOT_FOUND, 0,
                          NULL, 0);
            break;
        }
        if (payload[4] != VAXP_RESULT_MODE_ALL ||
            vaxp_read_le16(payload + 8) != 0) {
            send_response(header, VAXP_ERR_NOT_SUPPORTED, 0, NULL, 0);
            break;
        }
        g_ai.result_mask = vaxp_read_le16(payload + 2);
        g_ai.max_objects = payload[5] == 0 ? g_ai.object_limit
                                          : payload[5];
        if (g_ai.max_objects > g_ai.object_limit)
            g_ai.max_objects = g_ai.object_limit;
        g_ai.result_fps = vaxp_read_le16(payload + 6);
        if (g_ai.result_fps == 0 ||
            g_ai.result_fps > g_ai.result_fps_limit)
            g_ai.result_fps = g_ai.result_fps_limit;
        g_ai.subscribed_pipeline_id = g_ai.current_pipeline_id;
        memset(g_ai.last_result_ms, 0, sizeof(g_ai.last_result_ms));
        g_ai.stream_enabled = 1;
        send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
        break;
    case VAXP_CMD_RESULT_UNSUBSCRIBE:
        if (payload_length != sizeof(VaxpPipelineSelector))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_pipeline_id) {
            send_response(header, VAXP_ERR_PIPELINE_NOT_FOUND, 0,
                          NULL, 0);
            break;
        }
        g_ai.stream_enabled = 0;
        g_ai.subscribed_pipeline_id = 0;
        send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
        break;
    case VAXP_CMD_SET_RESULT_RATE:
        if (payload_length != sizeof(VaxpResultRateConfig))
            goto invalid_length;
        if (vaxp_read_le16(payload) != g_ai.current_pipeline_id) {
            send_response(header, VAXP_ERR_PIPELINE_NOT_FOUND, 0,
                          NULL, 0);
            break;
        }
        if (vaxp_read_le16(payload + 4) == 0 ||
            vaxp_read_le16(payload + 6) != 0) {
            send_response(header, VAXP_ERR_INVALID_PARAMETER, 0,
                          NULL, 0);
            break;
        }
        {
            uint32_t numerator = vaxp_read_le16(payload + 2);
            uint32_t denominator = vaxp_read_le16(payload + 4);
            uint32_t fps = (numerator + denominator - 1u) / denominator;
            g_ai.result_fps = fps > g_ai.result_fps_limit
                                  ? g_ai.result_fps_limit
                                  : (uint16_t)fps;
            memset(g_ai.last_result_ms, 0, sizeof(g_ai.last_result_ms));
        }
        send_response(header, VAXP_STATUS_OK, 0, NULL, 0);
        break;
    default:
        send_response(header, VAXP_ERR_UNKNOWN_COMMAND, 0, NULL, 0);
        break;
    }
    return;

invalid_length:
    send_response(header, VAXP_ERR_INVALID_LENGTH, 0, NULL, 0);
}

static void handle_ack(const VaxpHeader *header, const uint8_t *payload,
                       uint16_t payload_length)
{
    if (payload_length != sizeof(VaxpAckPayload) ||
        header->source != VAXP_ADDR_HOST ||
        header->destination != AI_DEVICE_ADDRESS ||
        header->session_id != g_ai.session_id)
        return;
    if (g_ai.boot_pending &&
        vaxp_read_le16(payload) == g_ai.boot_sequence &&
        vaxp_read_le16(payload + 2) == VAXP_EVENT_DEVICE_BOOT &&
        (int16_t)vaxp_read_le16(payload + 4) == VAXP_STATUS_OK)
        g_ai.boot_pending = 0;
}

static void received_frame(void *context, const VaxpHeader *header,
                           const uint8_t *payload,
                           uint16_t payload_length)
{
    (void)context;
    ++g_ai.rx_packets;
    if (header->message_type == VAXP_MSG_REQUEST)
        handle_request(header, payload, payload_length);
    else if (header->message_type == VAXP_MSG_ACK)
        handle_ack(header, payload, payload_length);
}

static void parse_error(void *context, vaxp_lab_parse_error_t error)
{
    (void)context;
    (void)error;
    ++g_ai.parser_errors;
}

static void service_rx(void)
{
    uint8_t bytes[512];
    int loops = 0;
    while (g_ai.uart != NULL && loops++ < 8 &&
           dshanpi_uart_lab_poll(g_ai.uart, 0) > 0) {
        size_t size = dshanpi_uart_lab_read(g_ai.uart, bytes, sizeof(bytes));
        if (size == 0)
            break;
        vaxp_lab_parser_feed(&g_ai.parser, bytes, size, monotonic_ms(),
                             received_frame, parse_error, NULL);
    }
    vaxp_lab_parser_tick(&g_ai.parser, monotonic_ms(),
                         assembly_timeout_ms(),
                         parse_error, NULL);
}

static void send_boot(void)
{
    uint8_t body[sizeof(VaxpDeviceBootEvent)] = {0};
    body[0] = VAXP_PROTOCOL_VERSION;
    body[1] = VAXP_BOOT_SOFTWARE;
    vaxp_write_le16(body + 2, g_ai.session_id);
    vaxp_write_le32(body + 8, monotonic_ms() - g_ai.started_ms);
    if (g_ai.boot_sequence == 0)
        g_ai.boot_sequence = next_sequence();
    if (send_frame(VAXP_MSG_EVENT,
                   VAXP_FLAG_ACK_REQUIRED | VAXP_FLAG_URGENT,
                   g_ai.boot_sequence, VAXP_EVENT_DEVICE_BOOT,
                   VAXP_ADDR_HOST, body, sizeof(body)) == 0) {
        g_ai.boot_pending = 1;
        g_ai.boot_sent_ms = monotonic_ms();
    }
}

static void send_heartbeat(void)
{
    uint8_t body[sizeof(VaxpHeartbeat)] = {0};
    vaxp_write_le32(body, (monotonic_ms() - g_ai.started_ms) / 1000u);
    body[4] = VAXP_DEVICE_RUNNING;
    body[5] = 1;
    body[6] = g_ai.current_model_id != 0 ? 1 : 0;
    body[7] = g_ai.current_pipeline_id != 0 ? 1 : 0;
    vaxp_write_le32(body + 16, g_ai.rx_packets);
    vaxp_write_le32(body + 20, g_ai.tx_packets);
    vaxp_write_le32(body + 24, g_ai.parser.crc_errors);
    vaxp_write_le32(body + 28, g_ai.parser_errors);
    vaxp_write_le32(body + 32, g_ai.dropped_results);
    send_frame(VAXP_MSG_EVENT, 0, next_sequence(), VAXP_EVENT_HEARTBEAT,
               VAXP_ADDR_HOST, body, sizeof(body));
    g_ai.last_heartbeat_ms = monotonic_ms();
}

static void service_protocol(void)
{
    uint32_t now;
    if (!g_ai.started)
        return;
    service_rx();
    now = monotonic_ms();
    if (g_ai.boot_pending &&
        (uint32_t)(now - g_ai.boot_sent_ms) >= AI_BOOT_ACK_TIMEOUT_MS) {
        if (g_ai.boot_retries < AI_BOOT_MAX_RETRIES) {
            ++g_ai.boot_retries;
            send_boot();
        } else {
            g_ai.boot_pending = 0;
        }
    }
    if (g_ai.host_ready &&
        (uint32_t)(now - g_ai.last_heartbeat_ms) >=
        VAXP_DEFAULT_HEARTBEAT_MS)
        send_heartbeat();
}

static void configure_current(uint16_t pipeline_id, uint16_t model_id,
                              uint8_t task_type, const char *mode_name)
{
    g_ai.current_pipeline_id = pipeline_id;
    g_ai.current_model_id = model_id;
    g_ai.current_task_type = task_type;
    snprintf(g_ai.mode_name, sizeof(g_ai.mode_name), "%s",
             mode_name != NULL ? mode_name : "AI Result");
}

int dshanpi_vaxp_ai_start(const dshanpi_vaxp_ai_config_t *config)
{
    dshanpi_system_settings_t settings;
    uint32_t seed;
    if (config == NULL || config->source_width == 0 ||
        config->source_height == 0)
        return -1;
    if (g_ai.started)
        return 0;
    memset(&g_ai, 0, sizeof(g_ai));
    memset(g_response_cache, 0, sizeof(g_response_cache));
    g_response_cache_next = 0;
    g_ai.channel_id = config->channel_id;
    g_ai.source_width = config->source_width;
    g_ai.source_height = config->source_height;
    g_ai.capabilities = config->capabilities |
                        VAXP_CAP_PIPELINE | VAXP_CAP_HEALTH;
    dshanpi_system_settings_load(&settings);
    g_ai.baud_rate = dshanpi_vaxp_baud_is_supported(
                         settings.vaxp_baud_rate)
                         ? settings.vaxp_baud_rate
                         : DSHANPI_VAXP_BAUD_DEFAULT;
    g_ai.object_limit = object_limit_for_baud(g_ai.baud_rate);
    g_ai.result_fps_limit = result_fps_limit_for_baud(g_ai.baud_rate);
    g_ai.max_objects = g_ai.object_limit;
    g_ai.result_fps = g_ai.result_fps_limit;
    g_ai.peer_max_rx_payload = VAXP_DEFAULT_MAX_PAYLOAD;
    g_ai.result_mask = 0xFFFFu;
    g_ai.stream_enabled = 0;
    snprintf(g_ai.application_name, sizeof(g_ai.application_name), "%s",
             config->application_name != NULL
                 ? config->application_name : "AI Application");
    seed = monotonic_ms() ^ (uint32_t)getpid() ^
           (uint32_t)(uintptr_t)&g_ai;
    g_ai.session_id = (uint16_t)(seed ^ (seed >> 16));
    if (g_ai.session_id == 0)
        g_ai.session_id = 1;
    vaxp_lab_parser_init(&g_ai.parser, VAXP_DEFAULT_MAX_PAYLOAD);
    if (dshanpi_uart_lab_open(&g_ai.uart, g_ai.baud_rate, PARITY_NONE,
                              STOP_BITS_1) != 0) {
        memset(&g_ai, 0, sizeof(g_ai));
        return -2;
    }
    g_ai.started = 1;
    g_ai.started_ms = monotonic_ms();
    g_ai.last_heartbeat_ms = g_ai.started_ms;
    atexit(dshanpi_vaxp_ai_stop);
    send_boot();
    printf("[vaxp-ai] %s ready on UART2 at %lu 8N1, session=0x%04X, "
           "limit=%u fps/%u objects\n",
           g_ai.application_name, (unsigned long)g_ai.baud_rate,
           g_ai.session_id, g_ai.result_fps_limit, g_ai.object_limit);
    return 0;
}

void dshanpi_vaxp_ai_register_classes(uint16_t model_id,
                                      const char *const *labels,
                                      size_t label_count)
{
    if (!g_ai.started || labels == NULL)
        return;
    for (size_t index = 0; index < label_count && index <= UINT16_MAX;
         ++index)
        remember_class(model_id, (uint16_t)index, labels[index]);
}

void dshanpi_vaxp_ai_announce(uint16_t pipeline_id, uint16_t model_id,
                              uint8_t task_type, const char *mode_name)
{
    if (!g_ai.started)
        return;
    configure_current(pipeline_id, model_id, task_type, mode_name);
    service_protocol();
}

int dshanpi_vaxp_ai_wait_for_subscription(uint16_t pipeline_id,
                                          uint32_t timeout_ms)
{
    uint32_t started = monotonic_ms();
    if (!g_ai.started)
        return 0;
    do {
        service_protocol();
        if (g_ai.host_ready && g_ai.stream_enabled &&
            g_ai.subscribed_pipeline_id == pipeline_id)
            return 1;
        usleep(2000);
    } while ((uint32_t)(monotonic_ms() - started) < timeout_ms);
    return 0;
}

void dshanpi_vaxp_ai_stop(void)
{
    if (!g_ai.started || g_ai.stopping)
        return;
    g_ai.stopping = 1;
    dshanpi_uart_lab_close(&g_ai.uart);
    g_ai.started = 0;
}

static int begin_result(uint16_t pipeline_id, uint16_t model_id,
                        uint8_t task_type, const char *mode_name,
                        uint32_t inference_time_us, uint16_t result_count)
{
    if (!g_ai.started)
        return -1;
    configure_current(pipeline_id, model_id, task_type, mode_name);
    service_protocol();
    memset(g_payload, 0, sizeof(VaxpVisionResultHeader));
    g_payload[0] = g_ai.channel_id;
    g_payload[1] = task_type;
    vaxp_write_le16(g_payload + 2, pipeline_id);
    vaxp_write_le16(g_payload + 4, model_id);
    vaxp_write_le32(g_payload + 8, ++g_ai.frame_id);
    vaxp_write_le64(g_payload + 12, monotonic_us());
    vaxp_write_le32(g_payload + 20, inference_time_us);
    vaxp_write_le16(g_payload + 24, result_count);
    return 0;
}

static int result_allowed(uint16_t mask)
{
    uint8_t slot = 0;
    uint32_t now;
    uint32_t minimum_interval;
    uint16_t bit = mask;
    if (!g_ai.started || !g_ai.host_ready || !g_ai.stream_enabled ||
        g_ai.subscribed_pipeline_id != g_ai.current_pipeline_id ||
        (g_ai.result_mask & mask) == 0)
        return 0;
    while (slot < 7 && (bit & 1u) == 0) {
        bit >>= 1;
        ++slot;
    }
    if (g_ai.result_fps == 0)
        return 1;
    now = monotonic_ms();
    minimum_interval = (1000u + g_ai.result_fps - 1u) /
                       g_ai.result_fps;
    if (g_ai.last_result_ms[slot] != 0 &&
        (uint32_t)(now - g_ai.last_result_ms[slot]) < minimum_interval)
        return 0;
    g_ai.last_result_ms[slot] = now;
    return 1;
}

static int finish_result(uint16_t command, size_t payload_length,
                         size_t original_count, size_t encoded_count,
                         uint16_t extra_flags)
{
    uint16_t result_flags = extra_flags;
    if (encoded_count < original_count)
        result_flags |= VAXP_RESULT_FLAG_TRUNCATED;
    vaxp_write_le16(g_payload + 6, result_flags);
    vaxp_write_le16(g_payload + 24, (uint16_t)encoded_count);
    if (send_frame(VAXP_MSG_EVENT, 0, next_sequence(), command,
                   VAXP_ADDR_HOST, g_payload,
                   (uint16_t)payload_length) != 0) {
        ++g_ai.dropped_results;
        return -1;
    }
    return 0;
}

static size_t append_text_tlv(uint8_t *destination, size_t capacity,
                              uint16_t type, const char *text)
{
    size_t length = bounded_text_length(text, 255);
    if (length == 0 || capacity < sizeof(VaxpTlvHeader) + length)
        return 0;
    vaxp_write_le16(destination, type);
    vaxp_write_le16(destination + 2, (uint16_t)length);
    memcpy(destination + sizeof(VaxpTlvHeader), text, length);
    return sizeof(VaxpTlvHeader) + length;
}

int dshanpi_vaxp_ai_publish_detections(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name, uint32_t inference_time_us,
    const dshanpi_vaxp_ai_detection_t *objects, size_t object_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    uint16_t result_mask = task_type == VAXP_TASK_TRACKING
        ? VAXP_RESULT_TRACK : VAXP_RESULT_DETECTION;
    if (objects == NULL && object_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, task_type, mode_name);
    for (size_t index = 0; index < object_count; ++index)
        remember_class(model_id, objects[index].class_id,
                       objects[index].label);
    service_protocol();
    if (!result_allowed(result_mask))
        return 0;
    begin_result(pipeline_id, model_id, task_type, mode_name,
                 inference_time_us, 0);
    while (encoded < object_count && encoded < g_ai.max_objects) {
        const dshanpi_vaxp_ai_detection_t *source = objects + encoded;
        size_t text_length = bounded_text_length(source->text, 255);
        size_t attributes = text_length == 0 ? 0
            : sizeof(VaxpTlvHeader) + text_length;
        size_t record_length = sizeof(VaxpDetectObject) + attributes;
        uint8_t *record;
        if (offset + record_length > sizeof(g_payload))
            break;
        record = g_payload + offset;
        memset(record, 0, sizeof(VaxpDetectObject));
        vaxp_write_le16(record, (uint16_t)record_length);
        vaxp_write_le16(record + 2, source->class_id);
        vaxp_write_le32(record + 4, source->track_id);
        vaxp_write_le16(record + 8, confidence_wire(source->confidence));
        vaxp_write_le16(record + 10,
                        coordinate_wire(source->x, g_ai.source_width));
        vaxp_write_le16(record + 12,
                        coordinate_wire(source->y, g_ai.source_height));
        vaxp_write_le16(record + 14,
                        dimension_wire(source->x, source->width,
                                       g_ai.source_width));
        vaxp_write_le16(record + 16,
                        dimension_wire(source->y, source->height,
                                       g_ai.source_height));
        vaxp_write_le16(record + 18, source->object_flags);
        vaxp_write_le16(record + 20, (uint16_t)attributes);
        if (attributes != 0)
            append_text_tlv(record + sizeof(VaxpDetectObject), attributes,
                            DSHANPI_VAXP_ATTR_RESULT_TEXT, source->text);
        offset += record_length;
        ++encoded;
    }
    return finish_result(VAXP_EVENT_DETECTION_RESULT, offset,
                         object_count, encoded,
                         task_type == VAXP_TASK_TRACKING
                             ? VAXP_RESULT_FLAG_TRACKING : 0);
}

int dshanpi_vaxp_ai_publish_classifications(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_classification_t *items, size_t item_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    if (items == NULL && item_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, VAXP_TASK_CLASSIFICATION,
                      mode_name);
    for (size_t index = 0; index < item_count; ++index)
        remember_class(model_id, items[index].class_id, items[index].label);
    service_protocol();
    if (!result_allowed(VAXP_RESULT_CLASSIFICATION))
        return 0;
    begin_result(pipeline_id, model_id, VAXP_TASK_CLASSIFICATION,
                 mode_name, inference_time_us, 0);
    while (encoded < item_count && encoded < g_ai.max_objects &&
           offset + sizeof(VaxpClassifyItem) <= sizeof(g_payload)) {
        vaxp_write_le16(g_payload + offset, items[encoded].class_id);
        vaxp_write_le16(g_payload + offset + 2,
                        confidence_wire(items[encoded].confidence));
        offset += sizeof(VaxpClassifyItem);
        ++encoded;
    }
    return finish_result(VAXP_EVENT_CLASSIFICATION_RESULT, offset,
                         item_count, encoded, 0);
}

int dshanpi_vaxp_ai_publish_poses(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_pose_t *objects, size_t object_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    uint16_t flags = 0;
    if (objects == NULL && object_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, VAXP_TASK_POSE, mode_name);
    service_protocol();
    if (!result_allowed(VAXP_RESULT_POSE))
        return 0;
    begin_result(pipeline_id, model_id, VAXP_TASK_POSE, mode_name,
                 inference_time_us, 0);
    while (encoded < object_count && encoded < g_ai.max_objects) {
        const dshanpi_vaxp_ai_pose_t *source = objects + encoded;
        size_t keypoints = source->keypoint_count > 255
                               ? 255 : source->keypoint_count;
        size_t text_length = bounded_text_length(source->label, 255);
        size_t attributes = text_length == 0 ? 0
            : sizeof(VaxpTlvHeader) + text_length;
        size_t record_length = sizeof(VaxpPoseObject) +
                               keypoints * sizeof(VaxpKeypoint) + attributes;
        uint8_t *record;
        if (offset + record_length > sizeof(g_payload))
            break;
        if (keypoints != source->keypoint_count)
            flags |= VAXP_RESULT_FLAG_PARTIAL;
        record = g_payload + offset;
        memset(record, 0, sizeof(VaxpPoseObject));
        vaxp_write_le16(record, (uint16_t)record_length);
        vaxp_write_le32(record + 2, source->track_id);
        vaxp_write_le16(record + 6, confidence_wire(source->confidence));
        vaxp_write_le16(record + 8,
                        coordinate_wire(source->x, g_ai.source_width));
        vaxp_write_le16(record + 10,
                        coordinate_wire(source->y, g_ai.source_height));
        vaxp_write_le16(record + 12,
                        dimension_wire(source->x, source->width,
                                       g_ai.source_width));
        vaxp_write_le16(record + 14,
                        dimension_wire(source->y, source->height,
                                       g_ai.source_height));
        record[16] = (uint8_t)keypoints;
        for (size_t point = 0; point < keypoints; ++point) {
            uint8_t *wire = record + sizeof(VaxpPoseObject) +
                            point * sizeof(VaxpKeypoint);
            vaxp_write_le16(wire, coordinate_wire(
                source->keypoints[point].x, g_ai.source_width));
            vaxp_write_le16(wire + 2, coordinate_wire(
                source->keypoints[point].y, g_ai.source_height));
            vaxp_write_le16(wire + 4, confidence_wire(
                source->keypoints[point].confidence));
        }
        if (attributes != 0)
            append_text_tlv(record + sizeof(VaxpPoseObject) +
                            keypoints * sizeof(VaxpKeypoint), attributes,
                            DSHANPI_VAXP_ATTR_RESULT_TEXT, source->label);
        offset += record_length;
        ++encoded;
    }
    return finish_result(VAXP_EVENT_POSE_RESULT, offset,
                         object_count, encoded, flags);
}

int dshanpi_vaxp_ai_publish_segments(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_detection_t *objects, size_t object_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    if (objects == NULL && object_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, VAXP_TASK_SEGMENTATION,
                      mode_name);
    for (size_t index = 0; index < object_count; ++index)
        remember_class(model_id, objects[index].class_id,
                       objects[index].label);
    service_protocol();
    if (!result_allowed(VAXP_RESULT_SEGMENTATION))
        return 0;
    begin_result(pipeline_id, model_id, VAXP_TASK_SEGMENTATION,
                 mode_name, inference_time_us, 0);
    while (encoded < object_count && encoded < g_ai.max_objects) {
        const dshanpi_vaxp_ai_detection_t *source = objects + encoded;
        enum { POLYGON_BYTES = 2 + 4 * sizeof(VaxpPoint) };
        size_t record_length = sizeof(VaxpSegmentationObject) +
                               POLYGON_BYTES;
        uint8_t *record;
        uint8_t *polygon;
        uint16_t x0, y0, x1, y1;
        if (offset + record_length > sizeof(g_payload))
            break;
        record = g_payload + offset;
        memset(record, 0, sizeof(VaxpSegmentationObject));
        vaxp_write_le16(record, (uint16_t)record_length);
        vaxp_write_le16(record + 2, source->class_id);
        vaxp_write_le32(record + 4, source->track_id);
        vaxp_write_le16(record + 8, confidence_wire(source->confidence));
        record[10] = VAXP_SEG_POLYGON;
        vaxp_write_le16(record + 12, POLYGON_BYTES);
        polygon = record + sizeof(VaxpSegmentationObject);
        vaxp_write_le16(polygon, 4);
        x0 = coordinate_wire(source->x, g_ai.source_width);
        y0 = coordinate_wire(source->y, g_ai.source_height);
        x1 = coordinate_wire(source->x + source->width, g_ai.source_width);
        y1 = coordinate_wire(source->y + source->height, g_ai.source_height);
        vaxp_write_le16(polygon + 2, x0); vaxp_write_le16(polygon + 4, y0);
        vaxp_write_le16(polygon + 6, x1); vaxp_write_le16(polygon + 8, y0);
        vaxp_write_le16(polygon + 10, x1); vaxp_write_le16(polygon + 12, y1);
        vaxp_write_le16(polygon + 14, x0); vaxp_write_le16(polygon + 16, y1);
        offset += record_length;
        ++encoded;
    }
    return finish_result(VAXP_EVENT_SEGMENTATION_RESULT, offset,
                         object_count, encoded, VAXP_RESULT_FLAG_PARTIAL);
}

int dshanpi_vaxp_ai_publish_ocr_task(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_ocr_t *objects, size_t object_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    if (objects == NULL && object_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, task_type, mode_name);
    service_protocol();
    if (!result_allowed(VAXP_RESULT_OCR))
        return 0;
    begin_result(pipeline_id, model_id, task_type, mode_name,
                 inference_time_us, 0);
    while (encoded < object_count && encoded < g_ai.max_objects) {
        const dshanpi_vaxp_ai_ocr_t *source = objects + encoded;
        size_t text_length = bounded_text_length(source->text, 1024);
        size_t record_length = sizeof(VaxpOcrObject) + text_length;
        uint8_t *record;
        if (offset + record_length > sizeof(g_payload))
            break;
        record = g_payload + offset;
        memset(record, 0, sizeof(VaxpOcrObject));
        vaxp_write_le16(record, (uint16_t)record_length);
        vaxp_write_le32(record + 2, source->result_id);
        for (unsigned point = 0; point < 4; ++point) {
            vaxp_write_le16(record + 6 + point * 4,
                            coordinate_wire(source->points[point * 2],
                                            g_ai.source_width));
            vaxp_write_le16(record + 8 + point * 4,
                            coordinate_wire(source->points[point * 2 + 1],
                                            g_ai.source_height));
        }
        vaxp_write_le16(record + 22,
                        confidence_wire(source->confidence));
        vaxp_write_le16(record + 24, (uint16_t)text_length);
        if (text_length != 0)
            memcpy(record + sizeof(VaxpOcrObject), source->text,
                   text_length);
        offset += record_length;
        ++encoded;
    }
    return finish_result(VAXP_EVENT_OCR_RESULT, offset,
                         object_count, encoded, 0);
}

int dshanpi_vaxp_ai_publish_ocr(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_ocr_t *objects, size_t object_count)
{
    return dshanpi_vaxp_ai_publish_ocr_task(
        pipeline_id, model_id, VAXP_TASK_OCR_RECOGNIZE, mode_name,
        inference_time_us, objects, object_count);
}

int dshanpi_vaxp_ai_publish_faces(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name, uint32_t inference_time_us,
    const dshanpi_vaxp_ai_face_t *objects, size_t object_count)
{
    size_t offset = sizeof(VaxpVisionResultHeader);
    size_t encoded = 0;
    if (objects == NULL && object_count != 0)
        return -1;
    configure_current(pipeline_id, model_id, task_type, mode_name);
    service_protocol();
    if (!result_allowed(VAXP_RESULT_FACE))
        return 0;
    begin_result(pipeline_id, model_id, task_type, mode_name,
                 inference_time_us, 0);
    while (encoded < object_count && encoded < g_ai.max_objects) {
        const dshanpi_vaxp_ai_face_t *source = objects + encoded;
        size_t label_length = bounded_text_length(source->label, 255);
        size_t attributes = label_length == 0 ? 0
            : sizeof(VaxpTlvHeader) + label_length;
        size_t record_length = sizeof(VaxpFaceObject) + attributes;
        uint8_t *record;
        if (offset + record_length > sizeof(g_payload))
            break;
        record = g_payload + offset;
        memset(record, 0, sizeof(VaxpFaceObject));
        vaxp_write_le16(record, (uint16_t)record_length);
        vaxp_write_le32(record + 2, source->track_id);
        vaxp_write_le32(record + 6, source->person_id);
        vaxp_write_le16(record + 10,
                        confidence_wire(source->detection_confidence));
        vaxp_write_le16(record + 12,
                        confidence_wire(source->recognition_similarity));
        vaxp_write_le16(record + 14,
                        coordinate_wire(source->x, g_ai.source_width));
        vaxp_write_le16(record + 16,
                        coordinate_wire(source->y, g_ai.source_height));
        vaxp_write_le16(record + 18,
                        dimension_wire(source->x, source->width,
                                       g_ai.source_width));
        vaxp_write_le16(record + 20,
                        dimension_wire(source->y, source->height,
                                       g_ai.source_height));
        vaxp_write_le16(record + 22, (uint16_t)attributes);
        if (attributes != 0)
            append_text_tlv(record + sizeof(VaxpFaceObject), attributes,
                            DSHANPI_VAXP_ATTR_RESULT_TEXT, source->label);
        offset += record_length;
        ++encoded;
    }
    return finish_result(VAXP_EVENT_FACE_RESULT, offset,
                         object_count, encoded, 0);
}

uint32_t dshanpi_vaxp_ai_dropped_results(void)
{
    return g_ai.dropped_results;
}
