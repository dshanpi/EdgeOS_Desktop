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

#include "nanotrack_src.h"

NanoTrackSrc::NanoTrackSrc(char *kmodel_file,FrameCHWSize image_size, int debug_mode)
: AIBase(kmodel_file,"nanotracker_src", debug_mode)
{
    model_name_ = "nanotracker_src";
    image_size_ = image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);

    // 初始化裁剪框大小和裁剪中心
    int min_len=std::min(image_size_.height,image_size_.width);
    int w=min_len*crop_ratio;
    int h=min_len*crop_ratio;
    center[0]=image_size_.width*0.5;
    center[1]=image_size_.height*0.5;
    rect_size[0]=w;
    rect_size[1]=h;
}

NanoTrackSrc::~NanoTrackSrc()
{
}

float* NanoTrackSrc:: get_center(){
    return center;
}

float* NanoTrackSrc::get_rect_size(){
    return rect_size;
}

void NanoTrackSrc::set_center(float* center){
    memcpy(this->center,center,2*sizeof(float));
}

void NanoTrackSrc::set_rect_size(float* rect_size){
    memcpy(this->rect_size,rect_size,2*sizeof(float));
}

void NanoTrackSrc::pre_process(runtime_tensor& input_tensor){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    float w_=rect_size[0]+context_amount*(rect_size[0]+rect_size[1]);
    float h_=rect_size[1]+context_amount*(rect_size[0]+rect_size[1]);
    float z_=round(sqrt(w_*h_))*src_input_size/crop_input_size;
    crop_w=z_;
    crop_h=z_;
    crop_x=int(center[0]-crop_w*0.5);
    crop_y=int(center[1]-crop_h*0.5);
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
    Utils::crop_resize_padding_one_side_set(image_size_,input_size_,crop_x,crop_y,crop_w,crop_h,ai2d_builder_,cv::Scalar(114, 114, 114));
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void NanoTrackSrc::inference()
{
    this->run();
    this->get_output();
}

void NanoTrackSrc::post_process(std::vector<float> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float* output = p_outputs_[0];
    int size=output_shapes_[0][1]*output_shapes_[0][2]*output_shapes_[0][3];
    results.resize(size);
    memcpy(results.data(),output,size*sizeof(float));
}

