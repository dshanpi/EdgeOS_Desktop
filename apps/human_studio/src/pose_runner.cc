#include "pose_runner.h"
#include "pose_detect.h"
#include <algorithm>
#include "vaxp_ai_stream.h"
struct PoseRunner::Impl {
    poseDetect model;
    FrameCHWSize size;
    std::vector<OutputPose> results;
    int reps = 0;
    bool down = false;
    Impl(const std::string &path, FrameCHWSize s, int debug)
        : model(const_cast<char *>(path.c_str()), .25f, .45f, s, debug),
          size(s) {}
};
PoseRunner::PoseRunner(const std::string &p, FrameCHWSize s, int d)
    : impl_(new Impl(p, s, d)) {}
PoseRunner::~PoseRunner() = default;
void PoseRunner::process(runtime_tensor &input, cv::Mat &osd, bool fitness) {
    impl_->results.clear();
    impl_->model.pre_process(input);
    impl_->model.inference();
    impl_->model.post_process(impl_->results);
    impl_->model.draw_result(osd, impl_->size, impl_->results);
    if (fitness && !impl_->results.empty() &&
        impl_->results[0].kps.size() >= 51) {
        const auto &k = impl_->results[0].kps;
        float hip = (k[11 * 3 + 1] + k[12 * 3 + 1]) * .5f;
        float knee = (k[13 * 3 + 1] + k[14 * 3 + 1]) * .5f;
        bool now_down = hip > knee - impl_->size.height * .08f;
        if (impl_->down && !now_down) ++impl_->reps;
        impl_->down = now_down;
    }
    if (fitness) {
        cv::rectangle(osd, cv::Rect(18, 390, 190, 66),
                      cv::Scalar(28, 32, 40, 230), cv::FILLED);
        cv::putText(osd, "Squats: " + std::to_string(impl_->reps),
                    cv::Point(32, 434), cv::FONT_HERSHEY_SIMPLEX, .8,
                    cv::Scalar(255, 255, 255, 255), 2);
    }

    std::vector<dshanpi_vaxp_ai_pose_t> wire;
    std::vector<std::vector<dshanpi_vaxp_ai_keypoint_t>> points;
    std::vector<std::string> labels;
    wire.reserve(impl_->results.size());
    points.reserve(impl_->results.size());
    labels.reserve(impl_->results.size());
    for (const auto &result : impl_->results) {
        std::vector<dshanpi_vaxp_ai_keypoint_t> object_points;
        object_points.reserve(result.kps.size() / 3u);
        for (size_t point = 0; point + 2 < result.kps.size(); point += 3) {
            object_points.push_back({
                result.kps[point], result.kps[point + 1],
                result.kps[point + 2]});
        }
        points.push_back(std::move(object_points));
        labels.push_back(fitness ? "squats=" + std::to_string(impl_->reps)
                                 : "person pose");
        wire.push_back({
            0, result.confidence,
            static_cast<float>(result.box.x),
            static_cast<float>(result.box.y),
            static_cast<float>(result.box.width),
            static_cast<float>(result.box.height),
            points.back().data(), points.back().size(),
            labels.back().c_str()});
    }
    dshanpi_vaxp_ai_publish_poses(
        fitness ? 0x0403 : 0x0402, fitness ? 0x0403 : 0x0402,
        fitness ? "Fitness Counter" : "Body Pose", 0,
        wire.data(), wire.size());
}
