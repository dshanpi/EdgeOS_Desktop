#include "camera_settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int valid_csi(int csi)
{
    return csi == DSHANPI_CAMERA_REAR_CSI ||
           csi == DSHANPI_CAMERA_FRONT_CSI;
}

static int valid_resolution(int resolution)
{
    return resolution >= DSHANPI_CAMERA_RESOLUTION_640P &&
           resolution < DSHANPI_CAMERA_RESOLUTION_COUNT;
}

static void load_settings(int *csi, int *resolution)
{
    FILE *file = fopen(DSHANPI_CAMERA_CONFIG_PATH, "r");
    char line[64];

    *csi = DSHANPI_CAMERA_REAR_CSI;
    *resolution = DSHANPI_CAMERA_RESOLUTION_1080P;
    if (file == NULL)
        return;
    while (fgets(line, sizeof(line), file) != NULL) {
        int value;
        if (sscanf(line, "csi=%d", &value) == 1 && valid_csi(value))
            *csi = value;
        else if (sscanf(line, "resolution=%d", &value) == 1 &&
                 valid_resolution(value))
            *resolution = value;
    }
    fclose(file);
}

static int save_settings(int csi, int resolution)
{
    char temporary[128];
    FILE *file;
    int failed = 0;

    if (!valid_csi(csi) || !valid_resolution(resolution))
        return -1;
    snprintf(temporary, sizeof(temporary), "%s.tmp",
             DSHANPI_CAMERA_CONFIG_PATH);
    file = fopen(temporary, "w");
    if (file == NULL) {
        printf("[camera-setting] open %s failed: %s\n", temporary,
               strerror(errno));
        return -1;
    }
    if (fprintf(file, "csi=%d\nresolution=%d\n", csi, resolution) < 0)
        failed = 1;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0)
        failed = 1;
    if (fclose(file) != 0)
        failed = 1;
    if (failed) {
        unlink(temporary);
        return -1;
    }
    /* RT-Smart rename() does not replace an existing target. */
    if (unlink(DSHANPI_CAMERA_CONFIG_PATH) != 0 && errno != ENOENT) {
        printf("[camera-setting] remove old setting failed: %s\n",
               strerror(errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, DSHANPI_CAMERA_CONFIG_PATH) != 0) {
        printf("[camera-setting] commit failed: %s\n", strerror(errno));
        unlink(temporary);
        return -1;
    }
    return 0;
}

int dshanpi_camera_setting_load(void)
{
    int csi;
    int resolution;
    load_settings(&csi, &resolution);
    return csi;
}

int dshanpi_camera_setting_save(int csi)
{
    int saved_csi;
    int resolution;

    if (!valid_csi(csi))
        return -1;
    load_settings(&saved_csi, &resolution);
    if (save_settings(csi, resolution) != 0)
        return -1;
    printf("[camera-setting] next boot sensor: CSI%d (%s)\n", csi,
           dshanpi_camera_setting_name(csi));
    return 0;
}

const char *dshanpi_camera_setting_name(int csi)
{
    return csi == DSHANPI_CAMERA_FRONT_CSI ? "Front" : "Rear";
}

int dshanpi_camera_resolution_load(void)
{
    int csi;
    int resolution;
    load_settings(&csi, &resolution);
    return resolution;
}

int dshanpi_camera_resolution_save(int resolution)
{
    int csi;
    int saved_resolution;

    if (!valid_resolution(resolution))
        return -1;
    load_settings(&csi, &saved_resolution);
    if (save_settings(csi, resolution) != 0)
        return -1;
    printf("[camera-setting] capture resolution: %s\n",
           dshanpi_camera_resolution_name(resolution));
    return 0;
}

const char *dshanpi_camera_resolution_name(int resolution)
{
    static const char *const names[] = { "640P", "720P", "1080P" };
    return valid_resolution(resolution) ? names[resolution] : "1080P";
}

int dshanpi_camera_resolution_dimensions(int resolution,
                                         unsigned *width,
                                         unsigned *height)
{
    static const unsigned widths[] = { 640, 1280, 1920 };
    static const unsigned heights[] = { 480, 720, 1080 };

    if (!valid_resolution(resolution) || width == NULL || height == NULL)
        return -1;
    *width = widths[resolution];
    *height = heights[resolution];
    return 0;
}
