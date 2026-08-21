#include "person_runner.h"
#include "person_detect.h"
#include "vaxp_ai_stream.h"
struct PersonRunner::Impl {
    personDetect model;
    std::vector<BoxInfo> results;
    Impl(const std::string &path, FrameCHWSize size, int debug)
        : model(const_cast<char *>(path.c_str()), .35f, .45f, size, debug) {}
};
PersonRunner::PersonRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
PersonRunner::~PersonRunner() = default;
void PersonRunner::process(runtime_tensor &input, cv::Mat &osd) {
    impl_->results.clear();
    impl_->model.pre_process(input);
    impl_->model.inference();
    impl_->model.post_process(impl_->results);
    impl_->model.draw_result(osd, impl_->results);
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &result : impl_->results) {
        wire.push_back({
            static_cast<uint16_t>(result.label), 0, result.score,
            result.x1, result.y1, result.x2 - result.x1,
            result.y2 - result.y1, 0, "person"});
    }
    dshanpi_vaxp_ai_publish_detections(
        0x0401, 0x0401, VAXP_TASK_DETECTION, "Person Detection", 0,
        wire.data(), wire.size());
}
