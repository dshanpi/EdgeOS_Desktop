#ifndef DSHANPI_SYSTEM_SETTINGS_H
#define DSHANPI_SYSTEM_SETTINGS_H

#include <stdint.h>

#define DSHANPI_SYSTEM_CONFIG_PATH "/data/dshanpi_system.conf"
#define DSHANPI_WIFI_PASSWORD_MAX 64
#define DSHANPI_VAXP_BAUD_DEFAULT 115200u
#define DSHANPI_SLEEP_TIMEOUT_DEFAULT 300u

typedef enum {
    DSHANPI_LANG_ZH_CN = 0,
    DSHANPI_LANG_ZH_TW,
    DSHANPI_LANG_EN,
    DSHANPI_LANG_JA,
    DSHANPI_LANG_COUNT
} dshanpi_language_t;

typedef enum {
    DSHANPI_AUTOSTART_NONE = 0,
    DSHANPI_AUTOSTART_FACE_STUDIO,
    DSHANPI_AUTOSTART_FACE_GEOMETRY,
    DSHANPI_AUTOSTART_HAND_STUDIO,
    DSHANPI_AUTOSTART_HUMAN_STUDIO,
    DSHANPI_AUTOSTART_SMART_DRIVING,
    DSHANPI_AUTOSTART_OCR_DETECTION,
    DSHANPI_AUTOSTART_OBJECT_DETECTION,
    DSHANPI_AUTOSTART_YOLO_MODELS,
    DSHANPI_AUTOSTART_RTSP_STREAM,
    DSHANPI_AUTOSTART_CV_LITE,
    DSHANPI_AUTOSTART_PLATE_OCR,
    DSHANPI_AUTOSTART_CODE_SCANNER,
    DSHANPI_AUTOSTART_SELF_LEARNING,
    DSHANPI_AUTOSTART_UVC_CAMERA,
    DSHANPI_AUTOSTART_SETTINGS,
    DSHANPI_AUTOSTART_CAMERA,
    DSHANPI_AUTOSTART_GALLERY,
    DSHANPI_AUTOSTART_DRAWING,
    DSHANPI_AUTOSTART_UART_LAB,
    DSHANPI_AUTOSTART_DUAL_CAMERA,
    DSHANPI_AUTOSTART_RTMP_STREAM,
    DSHANPI_AUTOSTART_COUNT
} dshanpi_autostart_t;

typedef struct {
    dshanpi_language_t language;
    dshanpi_language_t pending_language;
    int timezone_index;
    dshanpi_autostart_t autostart;
    char wifi_ssid[33];
    char wifi_password[DSHANPI_WIFI_PASSWORD_MAX + 1];
    int wifi_auto_connect;
    uint32_t vaxp_baud_rate;
    uint32_t sleep_timeout_seconds;
} dshanpi_system_settings_t;

void dshanpi_system_settings_defaults(dshanpi_system_settings_t *settings);
int dshanpi_system_settings_load(dshanpi_system_settings_t *settings);
int dshanpi_system_settings_save(const dshanpi_system_settings_t *settings);
int dshanpi_system_settings_commit_pending_language(
    dshanpi_system_settings_t *settings);
void dshanpi_system_settings_apply_timezone(
    const dshanpi_system_settings_t *settings);

int dshanpi_timezone_count(void);
const char *dshanpi_timezone_name(int index);
const char *dshanpi_language_name(dshanpi_language_t language);
const char *dshanpi_autostart_name(dshanpi_autostart_t app);
int dshanpi_vaxp_baud_is_supported(uint32_t baud_rate);
int dshanpi_sleep_timeout_is_supported(uint32_t timeout_seconds);

#endif
