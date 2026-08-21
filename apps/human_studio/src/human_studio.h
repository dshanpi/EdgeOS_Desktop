#pragma once
#include <atomic>
#include <memory>
#include <string>
#include "fall_runner.h"
#include "person_runner.h"
#include "pose_runner.h"
enum class HumanStudioMode { Detection = 0, Pose, Fitness, FallSafety, Count };
const char *human_studio_mode_name(HumanStudioMode);
class HumanStudio {
public:
    HumanStudio(const std::string &, FrameCHWSize, int,
                HumanStudioMode initial_mode = HumanStudioMode::Detection);
    void request_mode(HumanStudioMode);
    HumanStudioMode requested_mode() const;
    HumanStudioMode active_mode() const;
    void apply_requested_mode();
    int process(runtime_tensor &, cv::Mat &);
private:
    void load_mode(HumanStudioMode);
    std::string dir_;
    FrameCHWSize size_;
    int debug_;
    std::atomic<HumanStudioMode> requested_;
    HumanStudioMode active_;
    std::unique_ptr<PersonRunner> person_;
    std::unique_ptr<PoseRunner> pose_;
    std::unique_ptr<FallRunner> fall_;
};
