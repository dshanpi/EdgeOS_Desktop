#include "recognition_runner.h"

#include "../../ai_demo/sq_handreco/hand_recognition.h"

class RecognitionRunner::Impl {
public:
    Impl(const std::string &model, FrameCHWSize frame_size, int debug_mode)
        : model_(model),
          recognition_(const_cast<char *>(model_.c_str()), frame_size,
                       debug_mode)
    {
    }

    std::string model_;
    HandRecognition recognition_;
};

RecognitionRunner::RecognitionRunner(const std::string &model,
                                     FrameCHWSize frame_size, int debug_mode)
    : impl_(new Impl(model, frame_size, debug_mode))
{
}

RecognitionRunner::~RecognitionRunner() = default;

std::string RecognitionRunner::process(nncase::runtime::runtime_tensor &input,
                                       float x, float y, float width,
                                       float height, cv::Mat &osd)
{
    Bbox bbox{x, y, width, height};
    impl_->recognition_.pre_process(input, bbox);
    impl_->recognition_.inference();
    std::string result;
    impl_->recognition_.post_process(result);
    impl_->recognition_.draw_result(osd, result, bbox);
    return result;
}
