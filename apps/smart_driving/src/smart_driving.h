#pragma once
#include <atomic>
#include "driving_runners.h"
enum class DrivingMode { Crosswalk = 0, TrafficLight, Helmet, Smoke, Count };
const char *driving_mode_name(DrivingMode);
class SmartDriving {
public:
    SmartDriving(const std::string &, FrameCHWSize, int,
                 DrivingMode initial_mode = DrivingMode::Crosswalk);
    void request_mode(DrivingMode);
    DrivingMode requested_mode() const;
    DrivingMode active_mode() const;
    void apply_mode();
    int process(runtime_tensor &, cv::Mat &);
private:
    void load_mode(DrivingMode);
    std::string dir_;
    FrameCHWSize size_;
    int debug_;
    std::atomic<DrivingMode> requested_;
    DrivingMode active_;
    std::unique_ptr<CrosswalkRunner> crosswalk_;
    std::unique_ptr<TrafficRunner> traffic_;
    std::unique_ptr<HelmetRunner> helmet_;
    std::unique_ptr<SmokeRunner> smoke_;
};
