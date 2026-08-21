#pragma once
#include <memory>
#include <string>
#include "ai_utils.h"
class PoseRunner {
public:
    PoseRunner(const std::string &, FrameCHWSize, int);
    ~PoseRunner();
    void process(runtime_tensor &, cv::Mat &, bool fitness);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
