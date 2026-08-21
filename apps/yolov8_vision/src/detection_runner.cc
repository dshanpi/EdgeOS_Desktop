#include "model_runner.h"
#include "ob_det.h"
#include "ai_label_tables.h"
#include "vaxp_ai_stream.h"
class DetectionRunner final : public ModelRunner {
public:
    DetectionRunner(const std::string &model, FrameCHWSize size, int debug)
        : model_(const_cast<char *>(model.c_str()), .5f, .6f, size, debug) {}
    void process(runtime_tensor &input, cv::Mat &osd) override {
        std::vector<YOLOBbox> results;
        model_.pre_process(input);
        model_.inference();
        model_.post_process(results);
        model_.draw_result(osd, results);
        std::vector<dshanpi_vaxp_ai_detection_t> wire;
        wire.reserve(results.size());
        for (const auto &result : results) {
            const unsigned label = result.index >= 0 &&
                                   result.index < DSHANPI_COCO80_COUNT
                                       ? static_cast<unsigned>(result.index)
                                       : 0u;
            wire.push_back({
                static_cast<uint16_t>(label), 0, result.confidence,
                static_cast<float>(result.box.x),
                static_cast<float>(result.box.y),
                static_cast<float>(result.box.width),
                static_cast<float>(result.box.height), 0,
                dshanpi_coco80_labels[label]});
        }
        dshanpi_vaxp_ai_publish_detections(
            0x0701, 0x0701, VAXP_TASK_DETECTION, "Object Detection", 0,
            wire.data(), wire.size());
    }
private:
    OBDet model_;
};
std::unique_ptr<ModelRunner> create_detection_runner(
    const std::string &model, FrameCHWSize size, int debug) {
    return std::unique_ptr<ModelRunner>(
        new DetectionRunner(model, size, debug));
}
