#include "ai_registry.h"

#include <string.h>

/*
 * The launch script names are the upstream ISP/interactive defaults copied
 * beside each ELF and its resources under /sdcard/app/ai/<id>.
 */
static const dshanpi_ai_app_t g_ai_apps[] = {
    {"anomaly_det", "Anomaly", "Vision", "anomaly_det_image.sh", 0xFFE0B2},
    {"bytetrack", "ByteTrack", "Tracking", "bytetrack_isp.sh", 0xBBDEFB},
    {"crosswalk_detect", "Crosswalk", "Driving", "crosswalk_detect_isp.sh", 0xC8E6C9},
    {"distraction_reminder", "Distraction", "Driving", "distraction_reminder_isp.sh", 0xFFCDD2},
    {"dms_system", "Driver Monitor", "Driving", "dms_isp.sh", 0xD1C4E9},
    {"dynamic_gesture", "Dynamic Gesture", "Hand", "gesture.sh", 0xB2DFDB},
    {"eye_gaze", "Eye Gaze", "Face", "eye_gaze_isp.sh", 0xF8BBD0},
    {"face_alignment", "Face Align", "Face", "face_alignment_isp.sh", 0xC5CAE9},
    {"face_detection", "Face Detect", "Face", "face_detect_isp.sh", 0xBBDEFB},
    {"face_emotion", "Emotion", "Face", "face_emotion_isp.sh", 0xFFCCBC},
    {"face_gender", "Gender", "Face", "face_gender_isp.sh", 0xD7CCC8},
    {"face_glasses", "Glasses", "Face", "face_glasses_isp.sh", 0xB3E5FC},
    {"face_landmark", "Landmark", "Face", "face_landmark_isp.sh", 0xDCEDC8},
    {"face_mask", "Face Mask", "Face", "face_mask_isp.sh", 0xFFF9C4},
    {"face_mesh", "Face Mesh", "Face", "face_mesh_isp.sh", 0xD1C4E9},
    {"face_parse", "Face Parse", "Face", "face_parse_isp.sh", 0xF8BBD0},
    {"face_pose", "Face Pose", "Face", "face_pose_isp.sh", 0xB2EBF2},
    {"face_verification", "Face Verify", "Face", "face_verification_image.sh", 0xC8E6C9},
    {"falldown_detect", "Fall Detect", "Person", "falldown_detect_isp.sh", 0xFFCDD2},
    {"finger_guessing", "Finger Guess", "Hand", "finger_guessing_0_isp.sh", 0xFFE0B2},
    {"fitness", "Fitness", "Person", "fitness_isp.sh", 0xC5CAE9},
    {"head_detection", "Head Detect", "Person", "head_detect_isp.sh", 0xBBDEFB},
    {"helmet_detect", "Helmet", "Driving", "helmet_detect_isp.sh", 0xFFF9C4},
    {"kws", "Voice Commands", "Audio", "kws.sh", 0xD7CCC8},
    {"licence_det", "Plate Detect", "Driving", "licence_detect_isp.sh", 0xB2DFDB},
    {"licence_det_rec", "Plate OCR", "Driving", "licence_detect_rec_isp.sh", 0xB3E5FC},
    {"nanotracker", "Nano Tracker", "Tracking", "nanotracker_isp.sh", 0xDCEDC8},
    {"object_detect_yolov8n", "Object Detect", "Vision", "ob_detect_isp_320.sh", 0xFFCCBC},
    {"ocr", "OCR", "Text", "ocr_isp.sh", 0xC8E6C9},
    {"person_attr", "Person Attr", "Person", "person_attr_isp.sh", 0xD1C4E9},
    {"person_detect", "Person Detect", "Person", "person_detect_isp.sh", 0xBBDEFB},
    {"person_distance", "Distance", "Person", "person_distance_isp.sh", 0xB2EBF2},
    {"pose_detect", "Pose", "Person", "pose_detect_isp.sh", 0xF8BBD0},
    {"pphumanseg", "Human Seg", "Segmentation", "pphumanseg_isp.sh", 0xB2DFDB},
    {"puzzle_game", "Puzzle", "Games", "puzzle_game_isp.sh", 0xFFE0B2},
    {"segment_yolov8n", "YOLO Segment", "Segmentation", "segment_isp_320.sh", 0xC5CAE9},
    {"self_learning", "Self Learning", "Vision", "self_learning.sh", 0xFFF9C4},
    {"smoke_detect", "Smoke", "Safety", "smoke_detect_isp.sh", 0xFFCDD2},
    {"space_resize", "Space Resize", "Games", "space_resize_isp.sh", 0xD1C4E9},
    {"sq_hand_det", "Hand Detect", "Hand", "handdet_cpp_isp.sh", 0xBBDEFB},
    {"sq_handkp_class", "Hand Class", "Hand", "handkpclass_cpp_isp.sh", 0xC8E6C9},
    {"sq_handkp_det", "Hand Keypoint", "Hand", "handkpdet_cpp_isp.sh", 0xB2EBF2},
    {"sq_handkp_flower", "Flower Class", "Hand", "handkpflower_isp.sh", 0xF8BBD0},
    {"sq_handkp_ocr", "Hand OCR", "Hand", "handkpocr_cpp_isp.sh", 0xFFE0B2},
    {"sq_handreco", "Hand Gesture", "Hand", "handreco_cpp_isp.sh", 0xD7CCC8},
    {"traffic_light_detect", "Traffic Light", "Driving", "traffic_light_detect_isp.sh", 0xFFCDD2},
    {"translate_en_ch", "Translator", "Language", "translate_en_ch.sh", 0xB3E5FC},
    {"tts_zh", "Chinese TTS", "Audio", "tts_zh.sh", 0xDCEDC8},
    {"vehicle_attr", "Vehicle Attr", "Driving", "vehicle_attr_isp.sh", 0xC5CAE9},
    {"virtual_keyboard", "Air Keyboard", "Hand", "virtual_keyboard.sh", 0xD1C4E9},
    {"yolop_lane_seg", "Lane Segment", "Driving", "yolop_isp.sh", 0xB2DFDB},
};

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static const char *const g_face_studio[] = {
    "face_detection", "face_emotion", "face_gender", "face_glasses",
    "face_mask", "eye_gaze",
};
static const char *const g_face_geometry[] = {
    "face_pose", "face_mesh", "face_parse", "face_alignment",
};
static const char *const g_face_identity[] = {
    "face_landmark", "face_verification",
};
static const char *const g_hand_studio[] = {
    "sq_hand_det", "sq_handkp_det", "sq_handkp_class", "sq_handreco",
    "dynamic_gesture", "finger_guessing", "sq_handkp_flower",
    "space_resize",
};
static const char *const g_human_studio[] = {
    "person_detect", "pose_detect", "fitness", "falldown_detect",
};
static const char *const g_smart_driving[] = {
    "crosswalk_detect", "traffic_light_detect", "helmet_detect",
    "smoke_detect",
};
static const char *const g_vision_ocr[] = {
    "ocr", "sq_handkp_ocr", "licence_det", "licence_det_rec",
};
static const char *const g_object_vision[] = {
    "object_detect_yolov8n", "segment_yolov8n", "anomaly_det",
    "self_learning", "bytetrack", "nanotracker",
};
static const char *const g_ai_playground[] = {
    "puzzle_game", "finger_guessing", "sq_handkp_flower", "space_resize",
};
static const char *const g_air_control[] = {
    "virtual_keyboard", "space_resize", "dynamic_gesture",
    "sq_handkp_class",
};
static const char *const g_voice_assistant[] = {
    "kws", "tts_zh", "translate_en_ch",
};

static const dshanpi_ai_scene_t g_ai_scenes[] = {
    {"face_studio", "Face Studio", "Face attributes and gaze",
     "F", 0xFCE4EC, g_face_studio, ARRAY_SIZE(g_face_studio)},
    {"hand_studio", "Hand Studio", "Hand, keypoints and gestures",
     "H", 0xE0F2F1, g_hand_studio, ARRAY_SIZE(g_hand_studio)},
    {"face_geometry", "Face Geometry", "Pose, mesh and parsing",
     "3D", 0xEDE7F6, g_face_geometry, ARRAY_SIZE(g_face_geometry)},
    {"face_identity", "Face Identity", "Landmarks and verification",
     "ID", 0xE3F2FD, g_face_identity, ARRAY_SIZE(g_face_identity)},
    {"human_studio", "Human Studio", "Person, pose and safety",
     "P", 0xE8EAF6, g_human_studio, ARRAY_SIZE(g_human_studio)},
    {"smart_driving", "Smart Driving", "Road and driver intelligence",
     "D", 0xFFF3E0, g_smart_driving, ARRAY_SIZE(g_smart_driving)},
    {"vision_ocr", "Vision OCR", "Text and licence recognition",
     "T", 0xE8F5E9, g_vision_ocr, ARRAY_SIZE(g_vision_ocr)},
    {"object_vision", "Object Vision", "Detection, segment and track",
     "O", 0xE1F5FE, g_object_vision, ARRAY_SIZE(g_object_vision)},
    {"ai_playground", "AI Playground", "Gesture-powered games",
     "G", 0xFFF8E1, g_ai_playground, ARRAY_SIZE(g_ai_playground)},
    {"air_control", "Air Control", "Touch-free interaction",
     "A", 0xF3E5F5, g_air_control, ARRAY_SIZE(g_air_control)},
    {"voice_assistant", "Voice Assistant", "Keyword, translate and TTS",
     "V", 0xEFEBE9, g_voice_assistant, ARRAY_SIZE(g_voice_assistant)},
};

const dshanpi_ai_app_t *dshanpi_ai_apps(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(g_ai_apps) / sizeof(g_ai_apps[0]);
    }
    return g_ai_apps;
}

const dshanpi_ai_app_t *dshanpi_ai_app_find(const char *id)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_ai_apps); ++i) {
        if (strcmp(g_ai_apps[i].id, id) == 0) {
            return &g_ai_apps[i];
        }
    }
    return NULL;
}

const dshanpi_ai_scene_t *dshanpi_ai_scenes(size_t *count)
{
    if (count != NULL) {
        *count = ARRAY_SIZE(g_ai_scenes);
    }
    return g_ai_scenes;
}
