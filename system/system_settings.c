#include "system_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *posix;
} timezone_entry_t;

static const timezone_entry_t g_timezones[] = {
    {"UTC-08:00 Pacific", "PST8PDT"},
    {"UTC-05:00 Eastern", "EST5EDT"},
    {"UTC+00:00 London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"UTC+01:00 Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"UTC+08:00 Beijing/Taipei", "CST-8"},
    {"UTC+09:00 Tokyo", "JST-9"},
};

static const char *const g_language_names[] = {
    "简体中文", "繁體中文", "English", "日本語"
};

static const char *const g_autostart_names[] = {
    "Desktop", "Face Studio", "Face Geometry", "Hand Studio",
    "Human Studio", "Smart Driving", "OCR Detection", "Object Detection",
    "YOLO Models", "Network Camera", "CV Lite", "Plate OCR", "Code Scanner",
    "AI Learning", "USB Camera", "Settings", "Camera", "Gallery", "Drawing",
    "UART Lab", "Dual Camera", "Network Camera"
};

void dshanpi_system_settings_defaults(dshanpi_system_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->language = DSHANPI_LANG_EN;
    settings->pending_language = DSHANPI_LANG_EN;
    settings->timezone_index = 4;
    settings->autostart = DSHANPI_AUTOSTART_NONE;
    settings->wifi_auto_connect = 1;
    settings->vaxp_baud_rate = DSHANPI_VAXP_BAUD_DEFAULT;
    settings->sleep_timeout_seconds = DSHANPI_SLEEP_TIMEOUT_DEFAULT;
}

int dshanpi_system_settings_load(dshanpi_system_settings_t *settings)
{
    char line[160];
    FILE *file;
    int migrated_autostart = 0;
    int pending_language_seen = 0;

    dshanpi_system_settings_defaults(settings);
    file = fopen(DSHANPI_SYSTEM_CONFIG_PATH, "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        int value;
        if (sscanf(line, "language=%d", &value) == 1 &&
            value >= 0 && value < DSHANPI_LANG_COUNT) {
            settings->language = (dshanpi_language_t)value;
        } else if (sscanf(line, "pending_language=%d", &value) == 1 &&
                   value >= 0 && value < DSHANPI_LANG_COUNT) {
            settings->pending_language = (dshanpi_language_t)value;
            pending_language_seen = 1;
        } else if (sscanf(line, "timezone=%d", &value) == 1 &&
                   value >= 0 && value < dshanpi_timezone_count()) {
            settings->timezone_index = value;
        } else if (sscanf(line, "autostart=%d", &value) == 1 &&
                   value >= 0 && value < DSHANPI_AUTOSTART_COUNT) {
            settings->autostart = (dshanpi_autostart_t)value;
            if (settings->autostart == DSHANPI_AUTOSTART_RTMP_STREAM) {
                settings->autostart = DSHANPI_AUTOSTART_RTSP_STREAM;
                migrated_autostart = 1;
            }
        } else if (strncmp(line, "wifi_ssid=", 10) == 0) {
            char *value_start = line + 10;
            size_t length;
            value_start[strcspn(value_start, "\r\n")] = '\0';
            length = strnlen(value_start, sizeof(settings->wifi_ssid) - 1);
            memcpy(settings->wifi_ssid, value_start, length);
            settings->wifi_ssid[length] = '\0';
        } else if (strncmp(line, "wifi_password=", 14) == 0) {
            char *value_start = line + 14;
            size_t length;
            value_start[strcspn(value_start, "\r\n")] = '\0';
            length = strnlen(value_start,
                             sizeof(settings->wifi_password) - 1);
            memcpy(settings->wifi_password, value_start, length);
            settings->wifi_password[length] = '\0';
        } else if (sscanf(line, "wifi_auto_connect=%d", &value) == 1 &&
                   (value == 0 || value == 1)) {
            settings->wifi_auto_connect = value;
        } else if (sscanf(line, "vaxp_baud=%d", &value) == 1 &&
                   value > 0 &&
                   dshanpi_vaxp_baud_is_supported((uint32_t)value)) {
            settings->vaxp_baud_rate = (uint32_t)value;
        } else if (sscanf(line, "sleep_timeout=%d", &value) == 1 &&
                   value >= 0 &&
                   dshanpi_sleep_timeout_is_supported((uint32_t)value)) {
            settings->sleep_timeout_seconds = (uint32_t)value;
        }
    }
    fclose(file);
    if (!pending_language_seen) {
        settings->pending_language = settings->language;
    }
    if (migrated_autostart) {
        dshanpi_system_settings_save(settings);
    }
    return 0;
}

int dshanpi_system_settings_save(const dshanpi_system_settings_t *settings)
{
    char temporary[128];
    FILE *file;
    int failed = 0;

    snprintf(temporary, sizeof(temporary), "%s.tmp",
             DSHANPI_SYSTEM_CONFIG_PATH);
    file = fopen(temporary, "w");
    if (file == NULL) {
        return -1;
    }
    if (fprintf(file, "language=%d\npending_language=%d\n"
                      "timezone=%d\nautostart=%d\n"
                      "wifi_ssid=%s\nwifi_password=%s\n"
                      "wifi_auto_connect=%d\nvaxp_baud=%lu\n"
                      "sleep_timeout=%lu\n",
                settings->language, settings->pending_language,
                settings->timezone_index, settings->autostart,
                settings->wifi_ssid,
                settings->wifi_password,
                settings->wifi_auto_connect,
                (unsigned long)(dshanpi_vaxp_baud_is_supported(
                                    settings->vaxp_baud_rate)
                                    ? settings->vaxp_baud_rate
                                    : DSHANPI_VAXP_BAUD_DEFAULT),
                (unsigned long)(dshanpi_sleep_timeout_is_supported(
                                    settings->sleep_timeout_seconds)
                                    ? settings->sleep_timeout_seconds
                                    : DSHANPI_SLEEP_TIMEOUT_DEFAULT)) < 0) {
        failed = 1;
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
        failed = 1;
    }
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        unlink(temporary);
        return -1;
    }
    if (unlink(DSHANPI_SYSTEM_CONFIG_PATH) != 0 && errno != ENOENT) {
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, DSHANPI_SYSTEM_CONFIG_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(DSHANPI_SYSTEM_CONFIG_PATH, S_IRUSR | S_IWUSR);
    return 0;
}

int dshanpi_system_settings_commit_pending_language(
    dshanpi_system_settings_t *settings)
{
    dshanpi_language_t previous_language;

    if (settings == NULL ||
        settings->pending_language < 0 ||
        settings->pending_language >= DSHANPI_LANG_COUNT) {
        return -1;
    }
    if (settings->language == settings->pending_language) {
        return 0;
    }

    previous_language = settings->language;
    settings->language = settings->pending_language;
    if (dshanpi_system_settings_save(settings) != 0) {
        settings->language = previous_language;
        return -1;
    }
    return 1;
}

void dshanpi_system_settings_apply_timezone(
    const dshanpi_system_settings_t *settings)
{
    int index = settings->timezone_index;
    if (index < 0 || index >= dshanpi_timezone_count()) {
        index = 4;
    }
    setenv("TZ", g_timezones[index].posix, 1);
    tzset();
}

int dshanpi_timezone_count(void)
{
    return (int)(sizeof(g_timezones) / sizeof(g_timezones[0]));
}

const char *dshanpi_timezone_name(int index)
{
    if (index < 0 || index >= dshanpi_timezone_count()) {
        index = 4;
    }
    return g_timezones[index].name;
}

const char *dshanpi_language_name(dshanpi_language_t language)
{
    if (language < 0 || language >= DSHANPI_LANG_COUNT) {
        language = DSHANPI_LANG_EN;
    }
    return g_language_names[language];
}

const char *dshanpi_autostart_name(dshanpi_autostart_t app)
{
    if (app < 0 || app >= DSHANPI_AUTOSTART_COUNT) {
        app = DSHANPI_AUTOSTART_NONE;
    }
    return g_autostart_names[app];
}

int dshanpi_vaxp_baud_is_supported(uint32_t baud_rate)
{
    return baud_rate == 115200u || baud_rate == 460800u ||
           baud_rate == 921600u;
}

int dshanpi_sleep_timeout_is_supported(uint32_t timeout_seconds)
{
    return timeout_seconds == 0u || timeout_seconds == 30u ||
           timeout_seconds == 60u || timeout_seconds == 120u ||
           timeout_seconds == 300u || timeout_seconds == 600u ||
           timeout_seconds == 1800u;
}
