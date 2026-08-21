#ifndef DSHANPI_DUAL_CAMERA_MANAGER_H
#define DSHANPI_DUAL_CAMERA_MANAGER_H

#include <stddef.h>

/*
 * Dual Camera owns CSI0/VICAP0 and CSI2/VICAP1 together.  The live rear
 * camera is rendered full screen on VIDEO1 while the front camera is rendered
 * as a VIDEO2 picture-in-picture.  Recording produces one composited
 * H.264 MP4 at the selected capture resolution and a same-stem JPEG
 * thumbnail for Gallery.
 */
int dshanpi_dual_camera_start(int resolution);
void dshanpi_dual_camera_stop(void);
/* Capture the current full-screen camera together with the current PIP
 * camera and placement as one JPEG at the selected capture resolution. */
int dshanpi_dual_camera_capture_jpeg(char *output_path,
                                     size_t output_path_size);
int dshanpi_dual_camera_record_start(char *output_path,
                                     size_t output_path_size);
int dshanpi_dual_camera_record_stop(void);
int dshanpi_dual_camera_is_recording(void);
/* Logical 640x480 UI coordinates. Position changes are rejected while the
 * recording pipeline is locked. */
int dshanpi_dual_camera_set_pip_position(unsigned x, unsigned y);
/* Swap the full-screen and PIP sensors without restarting either ISP. */
int dshanpi_dual_camera_swap_views(void);

#endif
