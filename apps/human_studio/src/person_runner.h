#pragma once
#include <memory>
#include <string>
#include "ai_utils.h"
class PersonRunner {
public:
    PersonRunner(const std::string &, FrameCHWSize, int);
    ~PersonRunner();
    void process(runtime_tensor &, cv::Mat &);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
