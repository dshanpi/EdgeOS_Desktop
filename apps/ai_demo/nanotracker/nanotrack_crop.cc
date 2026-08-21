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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.f
 */

#include "nanotrack_crop.h"

NanoTrackCrop::NanoTrackCrop(char *kmodel_file,FrameCHWSize image_size, int debug_mode)
: AIBase(kmodel_file,"nanotracker_crop", debug_mode)
{
    model_name_ = "nanotracker_crop";
    image_size_ = image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);

    // 初始化裁剪框和裁剪中心
    int min_len=std::min(image_size_.height,image_size_.width);
    int w=min_len*crop_ratio;
    int h=min_len*crop_ratio;
    center[0]=image_size_.width*0.5;
    center[1]=image_size_.height*0.5;
    rect_size[0]=w;
    rect_size[1]=h;
    // 计算上下文扩展宽度
    float w_=rect_size[0]+context_amount*(rect_size[0]+rect_size[1]);
    float h_=rect_size[1]+context_amount*(rect_size[0]+rect_size[1]);
    float z_=round(sqrt(w_*h_));
    // 计算裁剪参数
    crop_w=z_;
    crop_h=z_;
    crop_x=center[0]-crop_w/2;
    crop_y=center[1]-crop_h/2;
    if(crop_x<0)
    {
        crop_x=0;
    }
    if(crop_y<0)
    {
        crop_y=0;
    }
    if(crop_x+crop_w>image_size_.width)
    {
        crop_w=image_size_.width-crop_x;
    }
    if(crop_y+crop_h>image_size_.height)
    {
        crop_h=image_size_.height-crop_y;
    }
    // 配置ai2d裁剪方法
    Utils::crop_resize_padding_one_side_set(image_size_,input_size_,crop_x,crop_y,crop_w,crop_h,ai2d_builder_,cv::Scalar(114, 114, 114));
}

NanoTrackCrop::~NanoTrackCrop()
{
}

void NanoTrackCrop::set_target(float x, float y, float width, float height)
{
    rect_size[0] = std::max(10.0f, width);
    rect_size[1] = std::max(10.0f, height);
    center[0] = x + rect_size[0] * 0.5f;
    center[1] = y + rect_size[1] * 0.5f;

    float context = context_amount * (rect_size[0] + rect_size[1]);
    crop_w = std::max(1, (int)round(sqrt((rect_size[0] + context) *
                                        (rect_size[1] + context))));
    crop_h = crop_w;
    crop_x = std::max(0, (int)round(center[0] - crop_w * 0.5f));
    crop_y = std::max(0, (int)round(center[1] - crop_h * 0.5f));
    if (crop_x + crop_w > image_size_.width)
        crop_w = image_size_.width - crop_x;
    if (crop_y + crop_h > image_size_.height)
        crop_h = image_size_.height - crop_y;
    Utils::crop_resize_padding_one_side_set(
        image_size_, input_size_, crop_x, crop_y, crop_w, crop_h,
        ai2d_builder_, cv::Scalar(114, 114, 114));
}

void NanoTrackCrop::pre_process(runtime_tensor& input_tensor){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void NanoTrackCrop::inference()
{
    this->run();
    this->get_output();
}

void NanoTrackCrop::post_process(std::vector<float> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float* output = p_outputs_[0];
    int size=output_shapes_[0][1]*output_shapes_[0][2]*output_shapes_[0][3];
    results.resize(size);
    memcpy(results.data(),output,size*sizeof(float));
}

void NanoTrackCrop::draw_box(cv::Mat &draw_frame){
    int w_=draw_frame.cols;
    int h_=draw_frame.rows;

    int draw_w=rect_size[0]*w_/image_size_.width;
    int draw_h=rect_size[1]*h_/image_size_.height;
    int draw_x=int(center[0]*w_/image_size_.width-draw_w*0.5);
    int draw_y=int(center[1]*h_/image_size_.height-draw_h*0.5);
    cv::rectangle(draw_frame,cv::Point(draw_x,draw_y),cv::Point(draw_x+draw_w,draw_y+draw_h),cv::Scalar(0,255,0,255),2);
}
