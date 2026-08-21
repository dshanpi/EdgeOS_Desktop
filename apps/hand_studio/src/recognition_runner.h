#ifndef DSHANPI_RECOGNITION_RUNNER_H
#define DSHANPI_RECOGNITION_RUNNER_H

#include <memory>
#include <string>

#include <nncase/runtime/runtime_tensor.h>
#include <opencv2/core.hpp>

#include "ai_utils.h"

class RecognitionRunner {
public:
    RecognitionRunner(const std::string &model, FrameCHWSize frame_size,
                      int debug_mode);
    ~RecognitionRunner();

    std::string process(nncase::runtime::runtime_tensor &input,
                        float x, float y, float width, float height,
                        cv::Mat &osd);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
