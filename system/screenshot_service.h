#pragma once

#define DSHANPI_SCREENSHOT_NOTICE_PATH "/data/.dshanpi_screenshot_saved"

typedef void (*dshanpi_screenshot_saved_cb)(const char *image_path);

int dshanpi_screenshot_service_start(dshanpi_screenshot_saved_cb callback);
