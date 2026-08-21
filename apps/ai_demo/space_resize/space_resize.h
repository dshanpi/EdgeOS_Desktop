
#ifndef _SPACE_RESIZE_H
#define _SPACE_RESIZE_H

#include "ai_utils.h"
#include "ai_base.h"


class CropResizer {
public:
    CropResizer(FrameCHWSize image_size);

    // 执行裁剪并绘制到 draw_frame 上（BGRA）
    void crop_and_draw(cv::Mat& draw_frame, runtime_tensor& input_tensor,std::vector<int> &two_point);

private:
    bool first_start_;
    float two_point_mean_w_, two_point_mean_h_;

    float max_new_resize_w_ = 256;
    float max_new_resize_h_ = 256;

    std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
    runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor

    FrameCHWSize image_size_;
};
#endif