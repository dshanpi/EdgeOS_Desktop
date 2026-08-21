#include "human_studio.h"
#include <iostream>
static std::string path(const std::string &d, const char *n) {
    return d + "/" + n;
}
const char *human_studio_mode_name(HumanStudioMode mode) {
    static const char *names[] = {
        "Person Detection", "Body Pose", "Fitness Counter", "Fall Safety"
    };
    unsigned i = static_cast<unsigned>(mode);
    return i < static_cast<unsigned>(HumanStudioMode::Count) ? names[i] : "Unknown";
}
HumanStudio::HumanStudio(const std::string &d, FrameCHWSize s, int debug,
                         HumanStudioMode initial_mode)
    : dir_(d), size_(s), debug_(debug), requested_(initial_mode),
      active_(HumanStudioMode::Count) {
    load_mode(initial_mode);
    std::cout << "[human-studio] loaded initial model: "
              << human_studio_mode_name(active_) << std::endl;
}
void HumanStudio::request_mode(HumanStudioMode m) {
    if (m >= HumanStudioMode::Detection && m < HumanStudioMode::Count)
        requested_.store(m);
}
HumanStudioMode HumanStudio::requested_mode() const { return requested_.load(); }
HumanStudioMode HumanStudio::active_mode() const { return active_; }
void HumanStudio::load_mode(HumanStudioMode next) {
    const bool keep_pose =
        (active_ == HumanStudioMode::Pose || active_ == HumanStudioMode::Fitness) &&
        (next == HumanStudioMode::Pose || next == HumanStudioMode::Fitness);
    if (!keep_pose) pose_.reset();
    if (next != HumanStudioMode::Detection) person_.reset();
    if (next != HumanStudioMode::FallSafety) fall_.reset();
    if (next == HumanStudioMode::Detection)
        person_.reset(new PersonRunner(path(dir_, "person_detect_yolov5n.kmodel"), size_, debug_));
    if ((next == HumanStudioMode::Pose || next == HumanStudioMode::Fitness) &&
        !pose_)
        pose_.reset(new PoseRunner(path(dir_, "yolov8n-pose.kmodel"), size_, debug_));
    if (next == HumanStudioMode::FallSafety)
        fall_.reset(new FallRunner(path(dir_, "yolov5n-falldown.kmodel"), size_, debug_));
    active_ = next;
}
void HumanStudio::apply_requested_mode() {
    HumanStudioMode next = requested_.load();
    if (next == active_) return;
    load_mode(next);
    std::cout << "[human-studio] switched mode: "
              << human_studio_mode_name(active_) << std::endl;
}
int HumanStudio::process(runtime_tensor &input, cv::Mat &osd) {
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    if (active_ == HumanStudioMode::Detection) person_->process(input, osd);
    else if (active_ == HumanStudioMode::Pose) pose_->process(input, osd, false);
    else if (active_ == HumanStudioMode::Fitness) pose_->process(input, osd, true);
    else fall_->process(input, osd);
    return 0;
}
