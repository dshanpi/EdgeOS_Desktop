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
#include "hand_recognition.h"

HandRecognition::HandRecognition(char *kmodel_file, FrameCHWSize image_size, int debug_mode)
: AIBase(kmodel_file,"HandRecognition", debug_mode)
{
    model_name_ = "HandRecognition";
    image_size_ = image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_ = get_input_tensor(0);
}
HandRecognition::~HandRecognition()
{
}

void HandRecognition::pre_process(runtime_tensor& input_tensor,Bbox &bbox){
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    Utils::crop_resize_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");   
}

void HandRecognition::inference()
{
    this->run();
    this->get_output();
}


void HandRecognition::post_process(string &result)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float *output = p_outputs_[0];
    float pred[4] = {0};
    softmax(output, pred, output_shapes_[0][1] );
    auto it = std::max_element(pred, pred + output_shapes_[0][1]);
    size_t idx = it - pred;
    result = labels[idx] + ":" + std::to_string(round(*it * 100) / 100.0);
}

void HandRecognition::draw_result(cv::Mat &draw_frame, string &result,Bbox &bbox)
{
    int w_=draw_frame.cols;
    int h_=draw_frame.rows;
    int x =  int(bbox.x / image_size_.width * w_);
    int y =  int(bbox.y / image_size_.height  * h_);
    int w = int((bbox.w) / image_size_.width * w_);
    int h = int((bbox.h) / image_size_.height  * h_);
    if(draw_frame.channels()==3){
        cv::rectangle(draw_frame, cv::Point(x, y), cv::Point(x + w, y + h), cv::Scalar(0, 0, 255), 2);
        cv::putText(draw_frame, result, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
    }
    else{
        cv::rectangle(draw_frame, cv::Point(x, y), cv::Point(x + w, y + h), cv::Scalar(0, 0, 255,255), 2);
        cv::putText(draw_frame, result, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255,255), 2);
    }
}

void HandRecognition::softmax(float* x, float* dx, uint32_t len)
{
    float max_value = x[0];
    for (uint32_t i = 0; i < len; i++)
    {
        if (max_value < x[i])
        {
            max_value = x[i];
        }
    }
    for (uint32_t i = 0; i < len; i++)
    {
        x[i] -= max_value;
        x[i] = expf(x[i]);
    }
    float sum_value = 0.0f;
    for (uint32_t i = 0; i < len; i++)
    {
        sum_value += x[i];
    }
    for (uint32_t i = 0; i < len; i++)
    {
        dx[i] = x[i] / sum_value;
    }
}


