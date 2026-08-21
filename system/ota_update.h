#pragma once

#include <stddef.h>

typedef enum {
    DSHANPI_OTA_IDLE = 0,
    DSHANPI_OTA_CHECKING,
    DSHANPI_OTA_VERIFYING_MANIFEST,
    DSHANPI_OTA_DOWNLOADING,
    DSHANPI_OTA_VERIFYING_PACKAGE,
    DSHANPI_OTA_INSTALLING,
    DSHANPI_OTA_REBOOTING,
    DSHANPI_OTA_READY,
    DSHANPI_OTA_AVAILABLE,
    DSHANPI_OTA_UP_TO_DATE,
    DSHANPI_OTA_FAILED,
} dshanpi_ota_state_t;

typedef struct {
    dshanpi_ota_state_t state;
    unsigned progress;
    int busy;
    char detail[160];
} dshanpi_ota_snapshot_t;

int dshanpi_ota_start_network(void);
void dshanpi_ota_get_snapshot(dshanpi_ota_snapshot_t *snapshot);
void dshanpi_ota_get_url(char *url, size_t size);
int dshanpi_ota_set_url(const char *url);
int dshanpi_ota_check_network(void);
int dshanpi_ota_boot_is_pending(void);
void dshanpi_ota_report_ui_heartbeat(void);
void dshanpi_ota_confirm_boot_after_health_delay(void);
