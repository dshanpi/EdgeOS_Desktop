#include "driving_runners.h"
#include "helmet_detect.h"
#include "vaxp_ai_stream.h"
struct HelmetRunner::Impl {
    helmetDetect model; std::vector<BoxInfo> results;
    Impl(const std::string &p, FrameCHWSize s, int d)
        : model(const_cast<char *>(p.c_str()), .35f, .45f, s, d) {}
};
HelmetRunner::HelmetRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
HelmetRunner::~HelmetRunner() = default;
void HelmetRunner::process(runtime_tensor &t, cv::Mat &m) {
    impl_->results.clear(); impl_->model.pre_process(t); impl_->model.inference();
    impl_->model.post_process(impl_->results);
    impl_->model.draw_result(m, impl_->results);
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &r : impl_->results)
        wire.push_back({0, 0, r.score, r.x1, r.y1,
                        r.x2 - r.x1, r.y2 - r.y1, 0, "helmet"});
    dshanpi_vaxp_ai_publish_detections(
        0x0503, 0x0503, VAXP_TASK_DETECTION, "Helmet Safety", 0,
        wire.data(), wire.size());
}
