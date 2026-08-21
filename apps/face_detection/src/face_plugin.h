#ifndef DSHANPI_FACE_PLUGIN_H
#define DSHANPI_FACE_PLUGIN_H

#include "ai_plugin.h"
#include "face_detection.h"

class FaceDetectionPlugin : public AiPlugin {
public:
    FaceDetectionPlugin(const char *model, float object_threshold,
                        float nms_threshold, FrameCHWSize frame_size,
                        int debug_mode);
    const char *name() const override;
    int process(runtime_tensor &input, cv::Mat &osd) override;

private:
    FaceDetection detector_;
    FrameCHWSize frame_size_;
    std::vector<FaceDetectionInfo> results_;
};

#endif
