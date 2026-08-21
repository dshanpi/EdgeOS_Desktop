#pragma once
#include <memory>
#include <string>
#include "ai_utils.h"
#define DECLARE_RUNNER(name) \
class name { \
public: \
    name(const std::string &, FrameCHWSize, int); \
    ~name(); \
    void process(runtime_tensor &, cv::Mat &); \
private: \
    struct Impl; \
    std::unique_ptr<Impl> impl_; \
};
DECLARE_RUNNER(CrosswalkRunner)
DECLARE_RUNNER(TrafficRunner)
DECLARE_RUNNER(HelmetRunner)
DECLARE_RUNNER(SmokeRunner)
#undef DECLARE_RUNNER
