#ifndef VAXP_COMMANDS_H
#define VAXP_COMMANDS_H

/*
 * VAXP 1.0 - standard commands, events, capabilities, and command payloads.
 */

#include "vaxp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- Command IDs ----------------------------- */

typedef enum VaxpCommandId {
    VAXP_CMD_HELLO                 = 0x0001,
    VAXP_CMD_GET_CAPABILITIES      = 0x0010,
    VAXP_CMD_AUTH_START            = 0x0080,
    VAXP_CMD_AUTH_VERIFY           = 0x0081,

    VAXP_CMD_PING                  = 0x0101,
    VAXP_CMD_GET_DEVICE_INFO       = 0x0102,
    VAXP_CMD_GET_STATUS            = 0x0103,
    VAXP_CMD_SET_TIME              = 0x0104,
    VAXP_CMD_REBOOT                = 0x0105,
    VAXP_CMD_FACTORY_RESET         = 0x0106,
    VAXP_CMD_GET_CONFIG            = 0x0107,
    VAXP_CMD_SAVE_CONFIG           = 0x0108,
    VAXP_CMD_TIME_SYNC             = 0x0109,
    VAXP_CMD_GET_HEALTH            = 0x0110,

    VAXP_CMD_GET_INPUT_LIST        = 0x0201,
    VAXP_CMD_SET_INPUT_CONFIG      = 0x0202,
    VAXP_CMD_GET_INPUT_STATUS      = 0x0203,
    VAXP_CMD_INPUT_START           = 0x0204,
    VAXP_CMD_INPUT_STOP            = 0x0205,

    VAXP_CMD_GET_MODEL_LIST        = 0x0301,
    VAXP_CMD_GET_MODEL_INFO        = 0x0302,
    VAXP_CMD_LOAD_MODEL            = 0x0303,
    VAXP_CMD_UNLOAD_MODEL          = 0x0304,
    VAXP_CMD_GET_MODEL_STATUS      = 0x0305,
    VAXP_CMD_GET_CLASS_LIST        = 0x0306,
    VAXP_CMD_REGISTER_MODEL        = 0x0307,
    VAXP_CMD_DELETE_MODEL          = 0x0308,

    VAXP_CMD_PIPELINE_CREATE       = 0x0401,
    VAXP_CMD_PIPELINE_DELETE       = 0x0402,
    VAXP_CMD_PIPELINE_START        = 0x0403,
    VAXP_CMD_PIPELINE_STOP         = 0x0404,
    VAXP_CMD_PIPELINE_GET_STATUS   = 0x0405,
    VAXP_CMD_PIPELINE_GET_LIST     = 0x0406,
    VAXP_CMD_PIPELINE_UPDATE       = 0x0407,
    VAXP_CMD_RESULT_SUBSCRIBE      = 0x0410,
    VAXP_CMD_RESULT_UNSUBSCRIBE    = 0x0411,
    VAXP_CMD_SET_THRESHOLD         = 0x0420,
    VAXP_CMD_SET_RESULT_RATE       = 0x0421,
    VAXP_CMD_SET_TRACK_CONFIG      = 0x0422,
    VAXP_CMD_SET_CLASS_FILTER      = 0x0423,

    VAXP_CMD_RULE_CREATE           = 0x0501,
    VAXP_CMD_RULE_DELETE           = 0x0502,
    VAXP_CMD_RULE_ENABLE           = 0x0503,
    VAXP_CMD_RULE_DISABLE          = 0x0504,
    VAXP_CMD_RULE_GET              = 0x0505,
    VAXP_CMD_RULE_GET_LIST         = 0x0506,
    VAXP_CMD_ROI_SET               = 0x0510,
    VAXP_CMD_ROI_DELETE            = 0x0511,
    VAXP_CMD_ROI_GET               = 0x0512,
    VAXP_CMD_ROI_GET_LIST          = 0x0513,

    VAXP_CMD_UPGRADE_BEGIN         = 0x0601,
    VAXP_CMD_UPGRADE_VERIFY        = 0x0602,
    VAXP_CMD_UPGRADE_COMMIT        = 0x0603,
    VAXP_CMD_UPGRADE_ABORT         = 0x0604,
    VAXP_CMD_UPGRADE_STATUS        = 0x0605,

    VAXP_CMD_FILE_BEGIN            = 0x0620,
    VAXP_CMD_FILE_DATA             = 0x0621,
    VAXP_CMD_FILE_END              = 0x0622,
    VAXP_CMD_FILE_ABORT            = 0x0623,
    VAXP_CMD_FILE_STATUS           = 0x0624,

    VAXP_CMD_GET_STATISTICS        = 0x0701,
    VAXP_CMD_CLEAR_STATISTICS      = 0x0702,
    VAXP_CMD_GET_PERFORMANCE       = 0x0710,
    VAXP_CMD_SET_LOG_LEVEL         = 0x0720,
    VAXP_CMD_GET_ERROR_LOG         = 0x0730
} VaxpCommandId;

/* ------------------------------ Event IDs ------------------------------ */

typedef enum VaxpEventId {
    VAXP_EVENT_DEVICE_BOOT           = 0x8001,
    VAXP_EVENT_HEARTBEAT             = 0x8002,
    VAXP_EVENT_STATE_CHANGED         = 0x8003,
    VAXP_EVENT_OPERATION_PROGRESS    = 0x8004,
    VAXP_EVENT_OPERATION_COMPLETE    = 0x8005,

    VAXP_EVENT_DETECTION_RESULT      = 0x8101,
    VAXP_EVENT_CLASSIFICATION_RESULT = 0x8102,
    VAXP_EVENT_POSE_RESULT           = 0x8103,
    VAXP_EVENT_SEGMENTATION_RESULT   = 0x8104,
    VAXP_EVENT_OCR_RESULT            = 0x8105,
    VAXP_EVENT_FACE_RESULT           = 0x8106,

    VAXP_EVENT_ALARM_START           = 0x8201,
    VAXP_EVENT_ALARM_UPDATE          = 0x8202,
    VAXP_EVENT_ALARM_END             = 0x8203,

    VAXP_EVENT_TRANSFER_PROGRESS     = 0x8301,

    VAXP_EVENT_DEVICE_ERROR          = 0x8F01,
    VAXP_EVENT_LOG                   = 0x8F02
} VaxpEventId;

/* ----------------------------- Capabilities ---------------------------- */

#define VAXP_CAP_DETECTION          (UINT64_C(1) << 0)
#define VAXP_CAP_CLASSIFICATION     (UINT64_C(1) << 1)
#define VAXP_CAP_TRACKING           (UINT64_C(1) << 2)
#define VAXP_CAP_SEGMENTATION       (UINT64_C(1) << 3)
#define VAXP_CAP_POSE               (UINT64_C(1) << 4)
#define VAXP_CAP_OCR                (UINT64_C(1) << 5)
#define VAXP_CAP_FACE_DETECT        (UINT64_C(1) << 6)
#define VAXP_CAP_FACE_RECOGNIZE     (UINT64_C(1) << 7)
#define VAXP_CAP_MULTI_CAMERA       (UINT64_C(1) << 8)
#define VAXP_CAP_MULTI_MODEL        (UINT64_C(1) << 9)
#define VAXP_CAP_PIPELINE           (UINT64_C(1) << 10)
#define VAXP_CAP_ROI                (UINT64_C(1) << 11)
#define VAXP_CAP_RULE_ENGINE        (UINT64_C(1) << 12)
#define VAXP_CAP_FILE_TRANSFER      (UINT64_C(1) << 13)
#define VAXP_CAP_MODEL_UPLOAD       (UINT64_C(1) << 14)
#define VAXP_CAP_FW_UPGRADE         (UINT64_C(1) << 15)
#define VAXP_CAP_AUTH               (UINT64_C(1) << 16)
#define VAXP_CAP_ENCRYPTION         (UINT64_C(1) << 17)
#define VAXP_CAP_PERFORMANCE        (UINT64_C(1) << 18)
#define VAXP_CAP_HEALTH             (UINT64_C(1) << 19)

/* ---------------------------- Device/System ---------------------------- */

typedef enum VaxpDeviceState {
    VAXP_DEVICE_BOOTING    = 0,
    VAXP_DEVICE_READY      = 1,
    VAXP_DEVICE_CONFIGURED = 2,
    VAXP_DEVICE_RUNNING    = 3,
    VAXP_DEVICE_UPGRADING  = 4,
    VAXP_DEVICE_ERROR      = 5,
    VAXP_DEVICE_RECOVERY   = 6
} VaxpDeviceState;

typedef enum VaxpBootReason {
    VAXP_BOOT_POWER_ON = 0,
    VAXP_BOOT_SOFTWARE = 1,
    VAXP_BOOT_WATCHDOG = 2,
    VAXP_BOOT_BROWNOUT = 3,
    VAXP_BOOT_UPGRADE  = 4,
    VAXP_BOOT_RECOVERY = 5,
    VAXP_BOOT_UNKNOWN  = 255
} VaxpBootReason;

typedef enum VaxpLogLevel {
    VAXP_LOG_ERROR = 1,
    VAXP_LOG_WARN  = 2,
    VAXP_LOG_INFO  = 3,
    VAXP_LOG_DEBUG = 4,
    VAXP_LOG_TRACE = 5
} VaxpLogLevel;

typedef enum VaxpErrorSeverity {
    VAXP_ERROR_INFO    = 0,
    VAXP_ERROR_WARNING = 1,
    VAXP_ERROR_ERROR   = 2,
    VAXP_ERROR_FATAL   = 3
} VaxpErrorSeverity;

typedef enum VaxpErrorSubsystem {
    VAXP_SUBSYS_CORE     = 0,
    VAXP_SUBSYS_UART     = 1,
    VAXP_SUBSYS_CAMERA   = 2,
    VAXP_SUBSYS_MEMORY   = 3,
    VAXP_SUBSYS_KPU      = 4,
    VAXP_SUBSYS_MODEL    = 5,
    VAXP_SUBSYS_PIPELINE = 6,
    VAXP_SUBSYS_RULE     = 7,
    VAXP_SUBSYS_STORAGE  = 8,
    VAXP_SUBSYS_UPGRADE  = 9,
    VAXP_SUBSYS_SECURITY = 10
} VaxpErrorSubsystem;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpHelloRequest {
    uint8_t  min_version;
    uint8_t  max_version;
    uint16_t max_rx_payload;
    uint32_t host_features;
} VaxpHelloRequest;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpHelloResponse {
    uint8_t  selected_version;
    uint8_t  reserved;
    uint16_t session_id;
    uint16_t max_rx_payload;
    uint16_t max_tx_payload;
    uint16_t max_pending_requests;
    uint16_t heartbeat_interval_ms;
    uint64_t capabilities;
} VaxpHelloResponse;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpCapabilities {
    uint64_t capability_bits;
    uint16_t max_payload;
    uint16_t max_objects;
    uint8_t  max_channels;
    uint8_t  max_models;
    uint8_t  max_pipelines;
    uint8_t  max_roi;
    uint8_t  max_rules;
    uint8_t  max_async_operations;
    uint16_t reserved;
} VaxpCapabilities;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpDeviceInfo {
    char     product_name[32];
    char     manufacturer[32];
    char     serial_number[32];
    char     hardware_version[16];
    char     firmware_version[16];
    char     sdk_version[16];
    char     chip_name[16];
    char     profile_name[24];
    uint32_t build_timestamp;
} VaxpDeviceInfo;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpDeviceStatus {
    uint8_t  state;
    uint8_t  active_channels;
    uint8_t  active_models;
    uint8_t  active_pipelines;
    uint32_t uptime_s;
    int16_t  temperature_centi;
    uint16_t cpu_usage_permille;
    uint16_t kpu_usage_permille;
    uint16_t memory_usage_permille;
    int16_t  last_error;
    uint16_t reserved;
} VaxpDeviceStatus;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpHeartbeat {
    uint32_t uptime_s;
    uint8_t  state;
    uint8_t  active_channels;
    uint8_t  active_models;
    uint8_t  active_pipelines;
    int16_t  temperature_centi;
    uint16_t cpu_usage_permille;
    uint16_t kpu_usage_permille;
    uint16_t memory_usage_permille;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t crc_errors;
    uint32_t parser_errors;
    uint32_t dropped_results;
    int16_t  last_error;
    uint16_t reserved;
} VaxpHeartbeat;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpDeviceBootEvent {
    uint8_t  protocol_version;
    uint8_t  boot_reason;
    uint16_t session_id;
    uint32_t boot_count;
    uint32_t startup_time_ms;
    uint32_t firmware_crc32;
} VaxpDeviceBootEvent;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpSetTimeRequest {
    uint64_t utc_timestamp_ms;
    int16_t  timezone_offset_min;
    uint16_t reserved;
} VaxpSetTimeRequest;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpTimeSyncRequest {
    uint64_t host_t1_us;
} VaxpTimeSyncRequest;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpTimeSyncResponse {
    uint64_t host_t1_us;
    uint64_t device_t2_us;
    uint64_t device_t3_us;
} VaxpTimeSyncResponse;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpOperationAccepted {
    uint32_t operation_id;
} VaxpOperationAccepted;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpOperationEvent {
    uint32_t operation_id;
    uint16_t command;
    uint16_t progress_permille;
    int16_t  status;
    uint16_t detail;
} VaxpOperationEvent;
VAXP_PACKED_END

/* ------------------------------- Inputs -------------------------------- */

typedef enum VaxpInputSource {
    VAXP_INPUT_CSI0     = 0,
    VAXP_INPUT_CSI1     = 1,
    VAXP_INPUT_CSI2     = 2,
    VAXP_INPUT_USB      = 16,
    VAXP_INPUT_MEMORY   = 32,
    VAXP_INPUT_EXTERNAL = 64
} VaxpInputSource;

typedef enum VaxpPixelFormat {
    VAXP_PIXFMT_NV12   = 0,
    VAXP_PIXFMT_NV21   = 1,
    VAXP_PIXFMT_YUYV   = 2,
    VAXP_PIXFMT_RGB888 = 3,
    VAXP_PIXFMT_BGR888 = 4
} VaxpPixelFormat;

typedef enum VaxpRotation {
    VAXP_ROTATE_0   = 0,
    VAXP_ROTATE_90  = 1,
    VAXP_ROTATE_180 = 2,
    VAXP_ROTATE_270 = 3
} VaxpRotation;

enum {
    VAXP_INPUT_FLAG_MIRROR = 0x0001u,
    VAXP_INPUT_FLAG_FLIP   = 0x0002u
};

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpInputConfig {
    uint8_t  channel_id;
    uint8_t  source;
    uint8_t  pixel_format;
    uint8_t  rotation;
    uint16_t width;
    uint16_t height;
    uint16_t fps_num;
    uint16_t fps_den;
    uint16_t flags;
    uint16_t reserved;
} VaxpInputConfig;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpInputSelector {
    uint8_t channel_id;
    uint8_t reserved[3];
} VaxpInputSelector;
VAXP_PACKED_END

/* -------------------------------- Model -------------------------------- */

typedef uint16_t VaxpModelId;

typedef enum VaxpTaskType {
    VAXP_TASK_DETECTION      = 0x01,
    VAXP_TASK_CLASSIFICATION = 0x02,
    VAXP_TASK_SEGMENTATION   = 0x03,
    VAXP_TASK_POSE           = 0x04,
    VAXP_TASK_OCR_DETECT     = 0x05,
    VAXP_TASK_OCR_RECOGNIZE  = 0x06,
    VAXP_TASK_FACE_DETECT    = 0x07,
    VAXP_TASK_FACE_RECOGNIZE = 0x08,
    VAXP_TASK_TRACKING       = 0x09,
    VAXP_TASK_CUSTOM         = 0xFF
} VaxpTaskType;

typedef enum VaxpModelState {
    VAXP_MODEL_REGISTERED = 0,
    VAXP_MODEL_LOADING    = 1,
    VAXP_MODEL_READY      = 2,
    VAXP_MODEL_IN_USE     = 3,
    VAXP_MODEL_ERROR      = 4
} VaxpModelState;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpModelSelector {
    uint16_t model_id;
} VaxpModelSelector;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpModelInfo {
    uint16_t model_id;
    uint8_t  model_uuid[16];
    uint8_t  task_type;
    uint8_t  state;
    char     model_name[32];
    char     model_version[16];
    uint16_t input_width;
    uint16_t input_height;
    uint16_t class_count;
    uint16_t max_result_count;
    uint32_t model_size;
    uint32_t model_crc32;
    uint32_t flags;
} VaxpModelInfo;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpClassEntryHeader {
    uint16_t class_id;
    uint16_t name_length;
} VaxpClassEntryHeader;
VAXP_PACKED_END

/* ------------------------------- Pipeline ------------------------------- */

typedef uint16_t VaxpPipelineId;

typedef enum VaxpPipelineState {
    VAXP_PIPELINE_CREATED  = 0,
    VAXP_PIPELINE_STARTING = 1,
    VAXP_PIPELINE_RUNNING  = 2,
    VAXP_PIPELINE_STOPPING = 3,
    VAXP_PIPELINE_STOPPED  = 4,
    VAXP_PIPELINE_ERROR    = 5
} VaxpPipelineState;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpPipelineSelector {
    uint16_t pipeline_id;
} VaxpPipelineSelector;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpPipelineDescriptor {
    uint16_t pipeline_id;
    uint16_t model_id;
    uint8_t  channel_id;
    uint8_t  task_type;
    uint8_t  enabled;
    uint8_t  state;
    uint16_t input_width;
    uint16_t input_height;
    uint16_t inference_fps;
    uint16_t result_fps;
    uint32_t flags;
} VaxpPipelineDescriptor;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpThresholdConfig {
    uint16_t pipeline_id;
    uint16_t confidence_threshold;
    uint16_t nms_threshold;
    uint16_t reserved;
} VaxpThresholdConfig;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpResultRateConfig {
    uint16_t pipeline_id;
    uint16_t result_fps_num;
    uint16_t result_fps_den;
    uint16_t reserved;
} VaxpResultRateConfig;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpTrackConfig {
    uint16_t pipeline_id;
    uint8_t  enabled;
    uint8_t  reserved0;
    uint16_t max_lost_frames;
    uint16_t min_confirm_frames;
    uint16_t match_threshold;
    uint16_t reserved1;
} VaxpTrackConfig;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpClassFilterHeader {
    uint16_t pipeline_id;
    uint16_t class_count;
    /* followed by uint16_t class_ids[class_count] */
} VaxpClassFilterHeader;
VAXP_PACKED_END

/* ------------------------------- Result Subscription ------------------------------- */

#define VAXP_RESULT_DETECTION       (1u << 0)
#define VAXP_RESULT_CLASSIFICATION  (1u << 1)
#define VAXP_RESULT_TRACK           (1u << 2)
#define VAXP_RESULT_POSE            (1u << 3)
#define VAXP_RESULT_SEGMENTATION    (1u << 4)
#define VAXP_RESULT_OCR             (1u << 5)
#define VAXP_RESULT_FACE            (1u << 6)
#define VAXP_RESULT_STATS           (1u << 7)

typedef enum VaxpResultMode {
    VAXP_RESULT_MODE_ALL         = 0,
    VAXP_RESULT_MODE_CHANGE_ONLY = 1,
    VAXP_RESULT_MODE_EVENT_ONLY  = 2
} VaxpResultMode;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpResultSubscription {
    uint16_t pipeline_id;
    uint16_t result_mask;
    uint8_t  result_mode;
    uint8_t  max_objects;
    uint16_t result_fps;
    uint16_t flags;
} VaxpResultSubscription;
VAXP_PACKED_END

/* -------------------------------- ROI / Rules -------------------------------- */

#define VAXP_MAX_ROI_POINTS 16u

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpPoint {
    uint16_t x;
    uint16_t y;
} VaxpPoint;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpRoiConfig {
    uint16_t pipeline_id;
    uint8_t  roi_id;
    uint8_t  enabled;
    uint8_t  point_count;
    uint8_t  reserved;
    uint16_t flags;
    VaxpPoint points[VAXP_MAX_ROI_POINTS];
} VaxpRoiConfig;
VAXP_PACKED_END

typedef enum VaxpRuleType {
    VAXP_RULE_INTRUSION    = 1,
    VAXP_RULE_CROSS_LINE   = 2,
    VAXP_RULE_LOITERING    = 3,
    VAXP_RULE_CROWD        = 4,
    VAXP_RULE_OBJECT_ENTER = 5,
    VAXP_RULE_OBJECT_LEAVE = 6,
    VAXP_RULE_COUNTING     = 7
} VaxpRuleType;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpRuleBase {
    uint16_t rule_id;
    uint16_t pipeline_id;
    uint8_t  rule_type;
    uint8_t  enabled;
    uint8_t  alarm_level;
    uint8_t  reserved;
    uint32_t class_mask;
    /* followed by rule-specific TLVs */
} VaxpRuleBase;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpRuleSelector {
    uint16_t rule_id;
} VaxpRuleSelector;
VAXP_PACKED_END

/* ---------------------------- File / Upgrade ---------------------------- */

typedef enum VaxpFileType {
    VAXP_FILE_MODEL        = 1,
    VAXP_FILE_MODEL_CONFIG = 2,
    VAXP_FILE_LABELS       = 3,
    VAXP_FILE_FIRMWARE     = 4,
    VAXP_FILE_RESOURCE     = 5
} VaxpFileType;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpFileBegin {
    uint32_t transfer_id;
    uint8_t  file_type;
    uint8_t  reserved[3];
    uint64_t file_size;
    uint8_t  sha256[32];
    uint16_t name_length;
    /* followed by uint8_t name[name_length] (UTF-8, no NUL required) */
} VaxpFileBegin;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpFileDataHeader {
    uint32_t transfer_id;
    uint64_t offset;
    uint16_t data_length;
    uint16_t reserved;
    /* followed by uint8_t data[data_length] */
} VaxpFileDataHeader;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpTransferSelector {
    uint32_t transfer_id;
} VaxpTransferSelector;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpFileStatus {
    uint32_t transfer_id;
    uint64_t accepted_offset;
    uint64_t total_size;
    uint16_t progress_permille;
    int16_t  status;
} VaxpFileStatus;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpUpgradeBegin {
    uint32_t transfer_id;
    uint8_t  image_type;
    uint8_t  reserved[3];
    uint64_t image_size;
    uint8_t  sha256[32];
    char     version[16];
} VaxpUpgradeBegin;
VAXP_PACKED_END

/* ------------------------------ Diagnostics ----------------------------- */

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpHealthStatus {
    uint16_t camera_status;
    uint16_t kpu_status;
    uint16_t memory_status;
    uint16_t storage_status;
    uint16_t uart_status;
    uint16_t thermal_status;
    uint16_t model_status;
    uint16_t pipeline_status;
} VaxpHealthStatus;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpPerformance {
    uint16_t pipeline_id;
    uint16_t input_fps;
    uint16_t inference_fps;
    uint16_t result_fps;
    uint32_t preprocess_us;
    uint32_t inference_us;
    uint32_t postprocess_us;
    uint32_t total_latency_us;
    uint32_t processed_frames;
    uint32_t dropped_frames;
} VaxpPerformance;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpSetLogLevelRequest {
    uint8_t level;
    uint8_t reserved[3];
} VaxpSetLogLevelRequest;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpErrorEvent {
    int16_t  error_code;
    uint8_t  severity;
    uint8_t  subsystem;
    uint32_t context_id;
    uint32_t timestamp_ms;
    uint16_t detail_length;
    /* followed by detail bytes / TLVs */
} VaxpErrorEvent;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpLogEventHeader {
    uint8_t  level;
    uint8_t  subsystem;
    uint16_t message_length;
    uint32_t timestamp_ms;
    /* followed by UTF-8 message[message_length] */
} VaxpLogEventHeader;
VAXP_PACKED_END

/* ------------------------------- ABI checks ------------------------------ */

VAXP_STATIC_ASSERT(sizeof(VaxpHelloRequest) == 8u, "VaxpHelloRequest size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpHelloResponse) == 20u, "VaxpHelloResponse size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpCapabilities) == 20u, "VaxpCapabilities size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpDeviceInfo) == 188u, "VaxpDeviceInfo size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpDeviceStatus) == 20u, "VaxpDeviceStatus size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpHeartbeat) == 40u, "VaxpHeartbeat size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpDeviceBootEvent) == 16u, "VaxpDeviceBootEvent size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpSetTimeRequest) == 12u, "VaxpSetTimeRequest size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpTimeSyncRequest) == 8u, "VaxpTimeSyncRequest size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpTimeSyncResponse) == 24u, "VaxpTimeSyncResponse size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpOperationAccepted) == 4u, "VaxpOperationAccepted size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpOperationEvent) == 12u, "VaxpOperationEvent size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpInputConfig) == 16u, "VaxpInputConfig size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpModelInfo) == 88u, "VaxpModelInfo size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpPipelineDescriptor) == 20u, "VaxpPipelineDescriptor size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpThresholdConfig) == 8u, "VaxpThresholdConfig size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpTrackConfig) == 12u, "VaxpTrackConfig size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpResultSubscription) == 10u, "VaxpResultSubscription size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpPoint) == 4u, "VaxpPoint size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpRoiConfig) == 72u, "VaxpRoiConfig size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpRuleBase) == 12u, "VaxpRuleBase size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpFileBegin) == 50u, "VaxpFileBegin size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpFileDataHeader) == 16u, "VaxpFileDataHeader size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpFileStatus) == 24u, "VaxpFileStatus size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpUpgradeBegin) == 64u, "VaxpUpgradeBegin size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpHealthStatus) == 16u, "VaxpHealthStatus size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpPerformance) == 32u, "VaxpPerformance size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpErrorEvent) == 14u, "VaxpErrorEvent size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpLogEventHeader) == 8u, "VaxpLogEventHeader size mismatch");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VAXP_COMMANDS_H */
