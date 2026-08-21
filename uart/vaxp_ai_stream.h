#ifndef DSHANPI_VAXP_AI_STREAM_H
#define DSHANPI_VAXP_AI_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "vaxp_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DongshanPI VAXP 1.0 optional result attributes.  Bit 15 is deliberately
 * clear: a generic VAXP host MUST skip these TLVs when it does not understand
 * them.  UTF-8 values are not NUL terminated on the wire.  Ordinary class
 * names are exposed through the standard GET_CLASS_LIST command instead of
 * being repeated in every result event.
 */
#define DSHANPI_VAXP_ATTR_RESULT_TEXT 0x7E01u
#define DSHANPI_VAXP_ATTR_MODE_TEXT   0x7E02u

typedef struct dshanpi_vaxp_ai_config {
    const char *application_name;
    uint8_t channel_id;
    uint16_t source_width;
    uint16_t source_height;
    uint64_t capabilities;
} dshanpi_vaxp_ai_config_t;

typedef struct dshanpi_vaxp_ai_detection {
    uint16_t class_id;
    uint32_t track_id;
    float confidence;
    float x;
    float y;
    float width;
    float height;
    uint16_t object_flags;
    const char *label;
    /* Optional per-result metrics/text, encoded as a skippable vendor TLV. */
    const char *text;
} dshanpi_vaxp_ai_detection_t;

typedef struct dshanpi_vaxp_ai_classification {
    uint16_t class_id;
    float confidence;
    const char *label;
} dshanpi_vaxp_ai_classification_t;

typedef struct dshanpi_vaxp_ai_keypoint {
    float x;
    float y;
    float confidence;
} dshanpi_vaxp_ai_keypoint_t;

typedef struct dshanpi_vaxp_ai_pose {
    uint32_t track_id;
    float confidence;
    float x;
    float y;
    float width;
    float height;
    const dshanpi_vaxp_ai_keypoint_t *keypoints;
    size_t keypoint_count;
    const char *label;
} dshanpi_vaxp_ai_pose_t;

typedef struct dshanpi_vaxp_ai_ocr {
    uint32_t result_id;
    float points[8];
    float confidence;
    const char *text;
} dshanpi_vaxp_ai_ocr_t;

typedef struct dshanpi_vaxp_ai_face {
    uint32_t track_id;
    uint32_t person_id;
    float detection_confidence;
    float recognition_similarity;
    float x;
    float y;
    float width;
    float height;
    const char *label;
} dshanpi_vaxp_ai_face_t;

int dshanpi_vaxp_ai_start(const dshanpi_vaxp_ai_config_t *config);
void dshanpi_vaxp_ai_stop(void);

/* Register the complete model class table for standard GET_CLASS_LIST. */
void dshanpi_vaxp_ai_register_classes(uint16_t model_id,
                                      const char *const *labels,
                                      size_t label_count);

/* Declare the active mode before the first inference (used by one-shot apps). */
void dshanpi_vaxp_ai_announce(uint16_t pipeline_id, uint16_t model_id,
                              uint8_t task_type, const char *mode_name);

/* Service HELLO/query/subscribe traffic for a bounded startup interval. */
int dshanpi_vaxp_ai_wait_for_subscription(uint16_t pipeline_id,
                                          uint32_t timeout_ms);

int dshanpi_vaxp_ai_publish_detections(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name, uint32_t inference_time_us,
    const dshanpi_vaxp_ai_detection_t *objects, size_t object_count);

int dshanpi_vaxp_ai_publish_classifications(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_classification_t *items, size_t item_count);

int dshanpi_vaxp_ai_publish_poses(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_pose_t *objects, size_t object_count);

/* UART profile representation: bounding polygons, never bitmap masks. */
int dshanpi_vaxp_ai_publish_segments(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_detection_t *objects, size_t object_count);

int dshanpi_vaxp_ai_publish_ocr(
    uint16_t pipeline_id, uint16_t model_id, const char *mode_name,
    uint32_t inference_time_us,
    const dshanpi_vaxp_ai_ocr_t *objects, size_t object_count);

/* OCR detection and recognition share one result record but keep distinct tasks. */
int dshanpi_vaxp_ai_publish_ocr_task(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name, uint32_t inference_time_us,
    const dshanpi_vaxp_ai_ocr_t *objects, size_t object_count);

int dshanpi_vaxp_ai_publish_faces(
    uint16_t pipeline_id, uint16_t model_id, uint8_t task_type,
    const char *mode_name, uint32_t inference_time_us,
    const dshanpi_vaxp_ai_face_t *objects, size_t object_count);

uint32_t dshanpi_vaxp_ai_dropped_results(void);

#ifdef __cplusplus
}
#endif

#endif
