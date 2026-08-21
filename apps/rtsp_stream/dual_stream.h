#ifndef DSHANPI_RTSP_DUAL_STREAM_H
#define DSHANPI_RTSP_DUAL_STREAM_H

#include <memory>

#include "media.h"

/* Two-camera H.264 source for streaming. Each 16:9 camera occupies one half
 * of the output canvas, preserving aspect ratio with neutral letterbox space
 * instead of stretching either sensor image. */
class DualRtspPipeline {
public:
    DualRtspPipeline();
    ~DualRtspPipeline();

    int Init(IOnVEncData *sink, unsigned output_width = 1280,
             unsigned output_height = 720,
             unsigned bitrate_kbps = 4000);
    int Start();
    void DeInit();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
