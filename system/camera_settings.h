#ifndef DSHANPI_CAMERA_SETTINGS_H
#define DSHANPI_CAMERA_SETTINGS_H

#define DSHANPI_CAMERA_REAR_CSI  0
#define DSHANPI_CAMERA_FRONT_CSI 2
#define DSHANPI_CAMERA_CONFIG_PATH "/data/dshanpi_camera.conf"

typedef enum {
    DSHANPI_CAMERA_RESOLUTION_640P = 0,
    DSHANPI_CAMERA_RESOLUTION_720P,
    DSHANPI_CAMERA_RESOLUTION_1080P,
    DSHANPI_CAMERA_RESOLUTION_COUNT,
} dshanpi_camera_resolution_t;

int dshanpi_camera_setting_load(void);
int dshanpi_camera_setting_save(int csi);
const char *dshanpi_camera_setting_name(int csi);
int dshanpi_camera_resolution_load(void);
int dshanpi_camera_resolution_save(int resolution);
const char *dshanpi_camera_resolution_name(int resolution);
int dshanpi_camera_resolution_dimensions(int resolution,
                                         unsigned *width,
                                         unsigned *height);

#endif
