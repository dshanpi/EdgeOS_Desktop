#include "driving_runners.h"
#include "smoke_detect.h"
#include "vaxp_ai_stream.h"
struct SmokeRunner::Impl {
    smokeDetect model; std::vector<BoxInfo> results;
    Impl(const std::string &p, FrameCHWSize s, int d)
        : model(const_cast<char *>(p.c_str()), .35f, .45f, s, d) {}
};
SmokeRunner::SmokeRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
SmokeRunner::~SmokeRunner() = default;
void SmokeRunner::process(runtime_tensor &t, cv::Mat &m) {
    impl_->results.clear(); impl_->model.pre_process(t); impl_->model.inference();
    impl_->model.post_process(impl_->results);
    impl_->model.draw_result(m, impl_->results);
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &r : impl_->results)
        wire.push_back({0, 0, r.score, r.x1, r.y1,
                        r.x2 - r.x1, r.y2 - r.y1, 0, "smoke"});
    dshanpi_vaxp_ai_publish_detections(
        0x0504, 0x0504, VAXP_TASK_DETECTION, "Smoke Alert", 0,
        wire.data(), wire.size());
}
