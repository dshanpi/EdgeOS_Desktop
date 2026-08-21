#ifndef DSHANPI_HAND_STUDIO_H
#define DSHANPI_HAND_STUDIO_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "hand_detection.h"
#include "hand_keypoint.h"
#include "recognition_runner.h"

enum class HandStudioMode {
    Detection = 0,
    Keypoints,
    StaticGesture,
    ModelGesture,
    Count,
};

class HandStudio {
public:
    HandStudio(const std::string &model_dir, FrameCHWSize frame_size,
               int debug_mode,
               HandStudioMode initial_mode = HandStudioMode::Detection);

    void request_mode(HandStudioMode mode);
    HandStudioMode requested_mode() const;
    HandStudioMode active_mode() const;
    const char *active_mode_name() const;
    void apply_requested_mode();
    int process(runtime_tensor &input, cv::Mat &osd);

private:
    void load_mode(HandStudioMode mode);
    Bbox keypoint_roi(const BoxInfo &hand) const;
    void draw_detection(cv::Mat &osd, const BoxInfo &hand) const;

    std::string model_dir_;
    FrameCHWSize frame_size_;
    int debug_mode_;
    HandDetection detector_;
    std::atomic<HandStudioMode> requested_mode_;
    HandStudioMode active_mode_;
    std::vector<BoxInfo> hands_;
    std::unique_ptr<HandKeypoint> keypoint_;
    std::unique_ptr<RecognitionRunner> recognition_;
};

const char *hand_studio_mode_name(HandStudioMode mode);

#endif
