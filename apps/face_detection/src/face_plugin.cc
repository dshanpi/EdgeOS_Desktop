#include "face_plugin.h"

FaceDetectionPlugin::FaceDetectionPlugin(
    const char *model, float object_threshold, float nms_threshold,
    FrameCHWSize frame_size, int debug_mode)
    : detector_(model, object_threshold, nms_threshold, frame_size, debug_mode),
      frame_size_(frame_size)
{
}

const char *FaceDetectionPlugin::name() const
{
    return "face_detection";
}

int FaceDetectionPlugin::process(runtime_tensor &input, cv::Mat &osd)
{
    results_.clear();
    detector_.pre_process(input);
    detector_.inference();
    detector_.post_process(frame_size_, results_);
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    detector_.draw_result(osd, results_, false);
    return 0;
}
