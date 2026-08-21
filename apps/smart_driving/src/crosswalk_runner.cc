#include "driving_runners.h"
#include "crosswalk_detect.h"
#include "vaxp_ai_stream.h"
struct CrosswalkRunner::Impl {
    crosswalkDetect model; FrameCHWSize size; std::vector<BoxInfo> results;
    Impl(const std::string &p, FrameCHWSize s, int d)
        : model(const_cast<char *>(p.c_str()), .35f, .45f, s, d), size(s) {}
};
CrosswalkRunner::CrosswalkRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
CrosswalkRunner::~CrosswalkRunner() = default;
void CrosswalkRunner::process(runtime_tensor &t, cv::Mat &m) {
    impl_->results.clear(); impl_->model.pre_process(t); impl_->model.inference();
    impl_->model.post_process(impl_->size, impl_->results);
    impl_->model.draw_result(m, impl_->results);
    static const char *const labels[] = {"crosswalk", "None"};
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &r : impl_->results) {
        const int label = r.label >= 0 && r.label < 2 ? r.label : 1;
        wire.push_back({static_cast<uint16_t>(label), 0, r.score,
                        r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1,
                        0, labels[label]});
    }
    dshanpi_vaxp_ai_publish_detections(
        0x0501, 0x0501, VAXP_TASK_DETECTION, "Crosswalk", 0,
        wire.data(), wire.size());
}
