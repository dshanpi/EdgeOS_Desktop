#ifndef VAXP_RESULT_H
#define VAXP_RESULT_H

/*
 * VAXP 1.0 - vision result and AI alarm payload definitions.
 */

#include "vaxp_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VAXP_COORD_MIN          0u
#define VAXP_COORD_MAX          10000u
#define VAXP_CONFIDENCE_MIN     0u
#define VAXP_CONFIDENCE_MAX     10000u
#define VAXP_K230_MAX_OBJECTS   64u

/* Result flags carried by VaxpVisionResultHeader.result_flags. */
enum {
    VAXP_RESULT_FLAG_TRUNCATED = 0x0001u,
    VAXP_RESULT_FLAG_TRACKING  = 0x0002u,
    VAXP_RESULT_FLAG_ROI       = 0x0004u,
    VAXP_RESULT_FLAG_RULE      = 0x0008u,
    VAXP_RESULT_FLAG_PARTIAL   = 0x0010u
};

/* Per-object flags. */
enum {
    VAXP_OBJECT_TRACKED    = 0x0001u,
    VAXP_OBJECT_NEW        = 0x0002u,
    VAXP_OBJECT_LOST       = 0x0004u,
    VAXP_OBJECT_OCCLUDED   = 0x0008u,
    VAXP_OBJECT_IN_ROI     = 0x0010u,
    VAXP_OBJECT_CROSS_LINE = 0x0020u,
    VAXP_OBJECT_TRUNCATED  = 0x0040u
};

/* Standard object-attribute TLV base types. */
typedef enum VaxpObjectAttributeType {
    VAXP_ATTR_COLOR        = 0x0101,
    VAXP_ATTR_VEHICLE_TYPE = 0x0102,
    VAXP_ATTR_PLATE        = 0x0103,
    VAXP_ATTR_HELMET       = 0x0201,
    VAXP_ATTR_BAG          = 0x0202,
    VAXP_ATTR_DIRECTION    = 0x0301,
    VAXP_ATTR_SPEED        = 0x0302
} VaxpObjectAttributeType;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpVisionResultHeader {
    uint8_t  channel_id;
    uint8_t  task_type;
    uint16_t pipeline_id;
    uint16_t model_id;
    uint16_t result_flags;
    uint32_t frame_id;
    uint64_t capture_timestamp_us;
    uint32_t inference_time_us;
    uint16_t result_count;
    uint16_t reserved;
} VaxpVisionResultHeader;
VAXP_PACKED_END

/*
 * Detection record layout:
 *   VaxpDetectObject
 *   uint8_t attribute_tlvs[attributes_length]
 *
 * record_length MUST equal sizeof(VaxpDetectObject) + attributes_length for VAXP 1.0.
 */
VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpDetectObject {
    uint16_t record_length;
    uint16_t class_id;
    uint32_t track_id;
    uint16_t confidence;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t object_flags;
    uint16_t attributes_length;
    uint16_t reserved;
} VaxpDetectObject;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpClassifyItem {
    uint16_t class_id;
    uint16_t confidence;
} VaxpClassifyItem;
VAXP_PACKED_END

/*
 * Pose record layout:
 *   VaxpPoseObject
 *   VaxpKeypoint keypoints[keypoint_count]
 *
 * record_length MUST equal sizeof(VaxpPoseObject) + keypoint_count * sizeof(VaxpKeypoint)
 * unless future compatible extension bytes are appended.
 */
VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpPoseObject {
    uint16_t record_length;
    uint32_t track_id;
    uint16_t confidence;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t  keypoint_count;
    uint8_t  reserved;
} VaxpPoseObject;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpKeypoint {
    uint16_t x;
    uint16_t y;
    uint16_t confidence;
} VaxpKeypoint;
VAXP_PACKED_END

/*
 * OCR record layout:
 *   VaxpOcrObject
 *   uint8_t utf8_text[text_length]
 */
VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpOcrObject {
    uint16_t record_length;
    uint32_t result_id;
    VaxpPoint points[4];
    uint16_t confidence;
    uint16_t text_length;
} VaxpOcrObject;
VAXP_PACKED_END

typedef enum VaxpSegmentationFormat {
    VAXP_SEG_POLYGON = 1,
    VAXP_SEG_RLE     = 2,
    VAXP_SEG_BITMAP  = 3
} VaxpSegmentationFormat;

/* Generic segmentation record. Encoded data follows this header. */
VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpSegmentationObject {
    uint16_t record_length;
    uint16_t class_id;
    uint32_t track_id;
    uint16_t confidence;
    uint8_t  format;
    uint8_t  reserved0;
    uint16_t data_length;
    uint16_t reserved1;
    /* followed by uint8_t data[data_length] */
} VaxpSegmentationObject;
VAXP_PACKED_END

/* Optional face result record. No embedding is included in the high-rate result stream. */
VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpFaceObject {
    uint16_t record_length;
    uint32_t track_id;
    uint32_t person_id;
    uint16_t detection_confidence;
    uint16_t recognition_similarity;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t attributes_length;
    uint16_t reserved;
    /* followed by optional attribute TLVs */
} VaxpFaceObject;
VAXP_PACKED_END

/* ------------------------------- Alarm events ------------------------------- */

typedef enum VaxpAlarmLevel {
    VAXP_ALARM_INFO     = 0,
    VAXP_ALARM_MINOR    = 1,
    VAXP_ALARM_MAJOR    = 2,
    VAXP_ALARM_CRITICAL = 3
} VaxpAlarmLevel;

typedef enum VaxpAlarmState {
    VAXP_ALARM_STATE_START  = 1,
    VAXP_ALARM_STATE_UPDATE = 2,
    VAXP_ALARM_STATE_END    = 3
} VaxpAlarmState;

typedef enum VaxpAlarmType {
    VAXP_ALARM_PERSON       = 1,
    VAXP_ALARM_VEHICLE      = 2,
    VAXP_ALARM_CROSS_LINE   = 3,
    VAXP_ALARM_INTRUSION    = 4,
    VAXP_ALARM_LOITERING    = 5,
    VAXP_ALARM_CROWD        = 6,
    VAXP_ALARM_FACE_MATCH   = 7,
    VAXP_ALARM_CAMERA_BLOCK = 8,
    VAXP_ALARM_CUSTOM       = 255
} VaxpAlarmType;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpAlarmEvent {
    uint32_t event_id;
    uint16_t rule_id;
    uint16_t pipeline_id;
    uint8_t  channel_id;
    uint8_t  alarm_type;
    uint8_t  alarm_level;
    uint8_t  state;
    uint32_t frame_id;
    uint64_t timestamp_us;
    uint32_t track_id;
} VaxpAlarmEvent;
VAXP_PACKED_END

/* -------------------------- Safe traversal helpers ------------------------- */

static inline int vaxp_coord_valid(uint16_t v)
{
    return v <= VAXP_COORD_MAX;
}

static inline int vaxp_confidence_valid(uint16_t v)
{
    return v <= VAXP_CONFIDENCE_MAX;
}

static inline const uint8_t *vaxp_detect_object_attributes(const VaxpDetectObject *obj)
{
    return ((const uint8_t *)obj) + sizeof(VaxpDetectObject);
}

static inline const VaxpDetectObject *vaxp_detect_object_next(const VaxpDetectObject *obj)
{
    if (obj == NULL || obj->record_length < sizeof(VaxpDetectObject)) {
        return NULL;
    }
    return (const VaxpDetectObject *)(((const uint8_t *)obj) + obj->record_length);
}

static inline const VaxpKeypoint *vaxp_pose_object_keypoints(const VaxpPoseObject *obj)
{
    return (const VaxpKeypoint *)(((const uint8_t *)obj) + sizeof(VaxpPoseObject));
}

static inline const uint8_t *vaxp_ocr_object_text(const VaxpOcrObject *obj)
{
    return ((const uint8_t *)obj) + sizeof(VaxpOcrObject);
}

VAXP_STATIC_ASSERT(sizeof(VaxpVisionResultHeader) == 28u, "VaxpVisionResultHeader size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpDetectObject) == 24u, "VaxpDetectObject size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpClassifyItem) == 4u, "VaxpClassifyItem size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpPoseObject) == 18u, "VaxpPoseObject size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpKeypoint) == 6u, "VaxpKeypoint size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpOcrObject) == 26u, "VaxpOcrObject size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpSegmentationObject) == 16u, "VaxpSegmentationObject size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpFaceObject) == 26u, "VaxpFaceObject size mismatch");
VAXP_STATIC_ASSERT(sizeof(VaxpAlarmEvent) == 28u, "VaxpAlarmEvent size mismatch");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VAXP_RESULT_H */
