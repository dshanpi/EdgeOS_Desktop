#include "smart_driving.h"
#include <iostream>
static std::string model(const std::string &d, const char *n) { return d + "/" + n; }
const char *driving_mode_name(DrivingMode mode) {
    static const char *names[] = {
        "Crosswalk", "Traffic Light", "Helmet Safety", "Smoke Alert"
    };
    unsigned i = static_cast<unsigned>(mode);
    return i < static_cast<unsigned>(DrivingMode::Count) ? names[i] : "Unknown";
}
SmartDriving::SmartDriving(const std::string &d, FrameCHWSize s, int debug,
                           DrivingMode initial_mode)
    : dir_(d), size_(s), debug_(debug), requested_(initial_mode),
      active_(DrivingMode::Count) {
    load_mode(initial_mode);
    std::cout << "[smart-driving] loaded initial model: "
              << driving_mode_name(active_) << std::endl;
}
void SmartDriving::request_mode(DrivingMode m) {
    if (m >= DrivingMode::Crosswalk && m < DrivingMode::Count) requested_ = m;
}
DrivingMode SmartDriving::requested_mode() const { return requested_.load(); }
DrivingMode SmartDriving::active_mode() const { return active_; }
void SmartDriving::load_mode(DrivingMode next) {
    crosswalk_.reset(); traffic_.reset(); helmet_.reset(); smoke_.reset();
    if (next == DrivingMode::Crosswalk)
        crosswalk_.reset(new CrosswalkRunner(model(dir_, "crosswalk.kmodel"), size_, debug_));
    else if (next == DrivingMode::TrafficLight)
        traffic_.reset(new TrafficRunner(model(dir_, "traffic.kmodel"), size_, debug_));
    else if (next == DrivingMode::Helmet)
        helmet_.reset(new HelmetRunner(model(dir_, "helmet.kmodel"), size_, debug_));
    else
        smoke_.reset(new SmokeRunner(model(dir_, "yolov5s_smoke_best.kmodel"), size_, debug_));
    active_ = next;
}
void SmartDriving::apply_mode() {
    DrivingMode next = requested_.load();
    if (next == active_) return;
    load_mode(next);
    std::cout << "[smart-driving] switched mode: " << driving_mode_name(active_) << std::endl;
}
int SmartDriving::process(runtime_tensor &input, cv::Mat &osd) {
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    if (active_ == DrivingMode::Crosswalk) crosswalk_->process(input, osd);
    else if (active_ == DrivingMode::TrafficLight) traffic_->process(input, osd);
    else if (active_ == DrivingMode::Helmet) helmet_->process(input, osd);
    else smoke_->process(input, osd);
    return 0;
}
