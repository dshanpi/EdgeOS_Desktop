#include <stddef.h>
#include <stdint.h>

#include "material_app_icons.h"

/*
 * Generated Material 3 application artwork, downsampled to the exact desktop
 * render size.  Each source is stored as little-endian BGRA, which is the
 * in-memory layout of LV_COLOR_FORMAT_ARGB8888 on this target.  The generated
 * alpha channel supplies a consistent 30 px rounded mask and removes the
 * source artwork's outer corners without a runtime clipping pass.
 */
#include "generated/material_icon_settings.inc"
#include "generated/material_icon_camera.inc"
#include "generated/material_icon_gallery.inc"
#include "generated/material_icon_face_studio.inc"
#include "generated/material_icon_face_geometry.inc"
#include "generated/material_icon_hand_studio.inc"
#include "generated/material_icon_human_studio.inc"
#include "generated/material_icon_smart_driving.inc"
#include "generated/material_icon_ocr_detection.inc"
#include "generated/material_icon_object_detection.inc"
#include "generated/material_icon_yolo_models.inc"
#include "generated/material_icon_rtsp_stream.inc"
#include "generated/material_icon_drawing.inc"
#include "generated/material_icon_cv_lite.inc"
#include "generated/material_icon_plate_ocr.inc"
#include "generated/material_icon_code_scanner.inc"
#include "generated/material_icon_ai_learning.inc"
#include "generated/material_icon_cloud_model.inc"
#include "generated/material_icon_usb_camera.inc"
#include "generated/material_icon_uart_lab.inc"
#include "generated/material_icon_dual_camera.inc"

#define MATERIAL_ICON_DSC(map_name)                  \
    {                                                \
        .header = {                                  \
            .magic = LV_IMAGE_HEADER_MAGIC,          \
            .cf = LV_COLOR_FORMAT_ARGB8888,          \
            .w = 108,                                \
            .h = 108,                                \
            .stride = 108 * 4,                       \
        },                                           \
        .data_size = sizeof(map_name),               \
        .data = map_name,                            \
    }

static const lv_image_dsc_t g_material_app_icons[] = {
    MATERIAL_ICON_DSC(material_icon_settings_map),
    MATERIAL_ICON_DSC(material_icon_camera_map),
    MATERIAL_ICON_DSC(material_icon_gallery_map),
    MATERIAL_ICON_DSC(material_icon_face_studio_map),
    MATERIAL_ICON_DSC(material_icon_face_geometry_map),
    MATERIAL_ICON_DSC(material_icon_hand_studio_map),
    MATERIAL_ICON_DSC(material_icon_human_studio_map),
    MATERIAL_ICON_DSC(material_icon_smart_driving_map),
    MATERIAL_ICON_DSC(material_icon_ocr_detection_map),
    MATERIAL_ICON_DSC(material_icon_object_detection_map),
    MATERIAL_ICON_DSC(material_icon_yolo_models_map),
    MATERIAL_ICON_DSC(material_icon_rtsp_stream_map),
    MATERIAL_ICON_DSC(material_icon_drawing_map),
    MATERIAL_ICON_DSC(material_icon_cv_lite_map),
    MATERIAL_ICON_DSC(material_icon_plate_ocr_map),
    MATERIAL_ICON_DSC(material_icon_code_scanner_map),
    MATERIAL_ICON_DSC(material_icon_ai_learning_map),
    MATERIAL_ICON_DSC(material_icon_cloud_model_map),
    MATERIAL_ICON_DSC(material_icon_usb_camera_map),
    MATERIAL_ICON_DSC(material_icon_uart_lab_map),
    MATERIAL_ICON_DSC(material_icon_dual_camera_map),
    /* Keep RTMP visually aligned with the existing broadcast family. */
    MATERIAL_ICON_DSC(material_icon_rtsp_stream_map),
};

const lv_image_dsc_t *material_app_icon_get(unsigned int icon_id)
{
    if (icon_id >= sizeof(g_material_app_icons) /
                       sizeof(g_material_app_icons[0])) {
        icon_id = 0;
    }
    return &g_material_app_icons[icon_id];
}
