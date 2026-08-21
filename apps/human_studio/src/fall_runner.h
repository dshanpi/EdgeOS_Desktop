#pragma once
#include <memory>
#include <string>
#include "ai_utils.h"
class FallRunner {
public:
    FallRunner(const std::string &, FrameCHWSize, int);
    ~FallRunner();
    void process(runtime_tensor &, cv::Mat &);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
