#pragma once
#include <memory>
#include "ai_utils.h"
class ModelRunner {
public:
    virtual ~ModelRunner() = default;
    virtual void process(runtime_tensor &input, cv::Mat &osd) = 0;
};
std::unique_ptr<ModelRunner> create_detection_runner(
    const std::string &model, FrameCHWSize size, int debug);
std::unique_ptr<ModelRunner> create_segmentation_runner(
    const std::string &model, FrameCHWSize size, int debug);
