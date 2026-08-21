#include "driving_runners.h"
#include "traffic_light_detect.h"
#include "vaxp_ai_stream.h"
struct TrafficRunner::Impl {
    traffic_lightDetect model; std::vector<BoxInfo> results;
    Impl(const std::string &p, FrameCHWSize s, int d)
        : model(const_cast<char *>(p.c_str()), .3f, .45f, s, d) {}
};
TrafficRunner::TrafficRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
TrafficRunner::~TrafficRunner() = default;
void TrafficRunner::process(runtime_tensor &t, cv::Mat &m) {
    impl_->results.clear(); impl_->model.pre_process(t); impl_->model.inference();
    impl_->model.post_process(impl_->results);
    impl_->model.draw_result(m, impl_->results);
    static const char *const labels[] = {"red", "green", "yellow"};
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &r : impl_->results) {
        const int label = r.label >= 0 && r.label < 3 ? r.label : 0;
        wire.push_back({static_cast<uint16_t>(label), 0, r.score,
                        r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1,
                        0, labels[label]});
    }
    dshanpi_vaxp_ai_publish_detections(
        0x0502, 0x0502, VAXP_TASK_DETECTION, "Traffic Light", 0,
        wire.data(), wire.size());
}
