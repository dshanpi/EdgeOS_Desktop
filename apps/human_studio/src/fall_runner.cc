#include "fall_runner.h"
#include "falldown_detect.h"
#include "vaxp_ai_stream.h"
struct FallRunner::Impl {
    FalldownDetect model;
    FrameCHWSize size;
    std::vector<BoxInfo> results;
    Impl(const std::string &path, FrameCHWSize s, int debug)
        : model(const_cast<char *>(path.c_str()), .35f, .45f, s, debug),
          size(s) {}
};
FallRunner::FallRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
FallRunner::~FallRunner() = default;
void FallRunner::process(runtime_tensor &input, cv::Mat &osd) {
    impl_->results.clear();
    impl_->model.pre_process(input);
    impl_->model.inference();
    impl_->model.post_process(impl_->size, impl_->results);
    impl_->model.draw_result(osd, impl_->results);
    static const char *const labels[] = {"Fall", "NoFall"};
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(impl_->results.size());
    for (const auto &result : impl_->results) {
        const int label = result.label >= 0 && result.label < 2
                              ? result.label : 1;
        wire.push_back({
            static_cast<uint16_t>(label), 0, result.score,
            result.x1, result.y1, result.x2 - result.x1,
            result.y2 - result.y1, 0, labels[label]});
    }
    dshanpi_vaxp_ai_publish_detections(
        0x0404, 0x0404, VAXP_TASK_DETECTION, "Fall Safety", 0,
        wire.data(), wire.size());
}
