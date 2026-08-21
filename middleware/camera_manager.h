#ifndef DSHANPI_CAMERA_MANAGER_H
#define DSHANPI_CAMERA_MANAGER_H

#include <stddef.h>
#include "k_vicap_comm.h"

#define DSHANPI_PHOTO_DIR "/data/dshanpi_photos"
#define DSHANPI_VIDEO_DIR "/data/dshanpi_photos"

/*
 * Camera Manager's stable VICAP contract.  Algorithms may consume AI_CH
 * without changing the preview or still-capture paths.
 */
#define DSHANPI_CAMERA_PREVIEW_CH  VICAP_CHN_ID_0
#define DSHANPI_CAMERA_AI_CH       VICAP_CHN_ID_1
#define DSHANPI_CAMERA_CAPTURE_CH  VICAP_CHN_ID_2

int dshanpi_camera_start(int csi, int resolution);
void dshanpi_camera_stop(void);
int dshanpi_camera_capture_preview(const char *preview_path);

/*
 * Capture one JPEG using GC2093 on CSI0 (rear) or CSI2 (front).
 * Returns 0 on success and writes the absolute filename to output_path.
 */
int dshanpi_camera_capture_jpeg(int csi, char *output_path,
                                size_t output_path_size);
int dshanpi_camera_record_start(char *output_path, size_t output_path_size);
int dshanpi_camera_record_stop(void);
int dshanpi_camera_is_recording(void);

#endif
