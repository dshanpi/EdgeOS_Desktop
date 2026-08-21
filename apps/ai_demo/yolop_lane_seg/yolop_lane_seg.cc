/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "yolop_lane_seg.h"

SEG::SEG(char *kmodel_file, FrameCHWSize image_size, int debug_mode)
:AIBase(kmodel_file,"yolop", debug_mode)
{
    model_name_ = "yolop_lane_seg";
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::resize_set(image_size_,input_size_,ai2d_builder_);
}

SEG::~SEG()
{
}

void SEG::pre_process(runtime_tensor &input_tensor){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void SEG::inference()
{
    ScopedTiming st(model_name_ + " inference", debug_mode_);
    this->run();
    this->get_output();
}

void SEG::post_process(cv::Mat &draw_frame)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);

    const int net_len_w = input_shapes_[0][2];
    const int net_len_h = input_shapes_[0][3];
    const int area = net_len_w * net_len_h;

    float* pdata_drive = p_outputs_[1];
    float* pdata_lane_line = p_outputs_[2];

    // 创建与网络输出大小相同的mask，4通道BGRA
    cv::Mat mask(net_len_h, net_len_w, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    for (int y = 0; y < net_len_h; ++y)
    {
        cv::Vec4b* row_ptr = mask.ptr<cv::Vec4b>(y);
        for (int x = 0; x < net_len_w; ++x)
        {
            float drive_bg = pdata_drive[y * net_len_w + x];
            float drive_fg = pdata_drive[area + y * net_len_w + x];
            float lane_bg = pdata_lane_line[y * net_len_w + x];
            float lane_fg = pdata_lane_line[area + y * net_len_w + x];

            if (lane_bg - 0.1f <= lane_fg) {
                row_ptr[x] = cv::Vec4b(255, 255, 0, 127);
            } else if (drive_bg < drive_fg) {
                row_ptr[x] = cv::Vec4b(255, 0, 255, 127);
            }
        }
    }

    // Resize mask 到 draw_frame 尺寸
    cv::Mat resized_mask;
    cv::resize(mask, resized_mask, draw_frame.size(), 0, 0, cv::INTER_LINEAR);
    resized_mask.copyTo(draw_frame);

    // // 叠加（可选：使用 alpha 混合）
    // for (int i = 0; i < draw_frame.rows; ++i)
    // {
    //     cv::Vec4b* dst_ptr = draw_frame.ptr<cv::Vec4b>(i);
    //     const cv::Vec4b* mask_ptr = resized_mask.ptr<cv::Vec4b>(i);

    //     for (int j = 0; j < draw_frame.cols; ++j)
    //     {
    //         const cv::Vec4b& m = mask_ptr[j];
    //         if (m[3] == 0) continue; // alpha==0，不处理

    //         // alpha blending
    //         float alpha = m[3] / 255.0f;
    //         for (int c = 0; c < 3; ++c) {
    //             dst_ptr[j][c] = static_cast<uchar>(
    //                 dst_ptr[j][c] * (1.0f - alpha) + m[c] * alpha
    //             );
    //         }
    //     }
    // }
}