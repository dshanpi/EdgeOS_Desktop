#include "model_runner.h"
#include "segment.h"
#include "ai_label_tables.h"
#include "vaxp_ai_stream.h"
class SegmentationRunner final : public ModelRunner {
public:
    SegmentationRunner(const std::string &model, FrameCHWSize size, int debug)
        : model_(const_cast<char *>(model.c_str()), .1f, .5f, .5f,
                 size, debug) {}
    void process(runtime_tensor &input, cv::Mat &osd) override {
        std::vector<SegBbox> results;
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
        dshanpi_vaxp_ai_publish_segments(
            0x0702, 0x0702, "Instance Segmentation", 0,
            wire.data(), wire.size());
    }
private:
    Seg model_;
};
std::unique_ptr<ModelRunner> create_segmentation_runner(
    const std::string &model, FrameCHWSize size, int debug) {
    return std::unique_ptr<ModelRunner>(
        new SegmentationRunner(model, size, debug));
}
