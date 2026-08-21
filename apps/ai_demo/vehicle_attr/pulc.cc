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

#include "pulc.h"
#include <vector>
#include "ai_utils.h"

Pulc::Pulc(char *kmodel_file, FrameCHWSize image_size,float color_thresh, float type_thresh, int debug_mode)
: AIBase(kmodel_file,"personAttr", debug_mode)
{
    model_name_ = "vehicleAttr";
    color_thresh_ = color_thresh;
    type_thresh_ = type_thresh;
    image_size_ = image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_ = get_input_tensor(0);
}

Pulc::~Pulc()
{
}

void Pulc::pre_process(runtime_tensor& input_tensor,Bbox &bbox){
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    Utils::crop_resize_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");   
}

void Pulc::inference()
{
    this->run();
    this->get_output();
}

void Pulc::post_process(std::vector<string> &results)
{
    // float* output_0 = p_outputs_[0];
    // std::string color = "Color: " + (string)(output_0[0] > color_thresh_ ? "Yellow" : (output_0[1] > color_thresh_ ? "orange" : (output_0[2] > color_thresh_ ? "green" : (output_0[3] > color_thresh_ ? "gray" : 
    //                 (output_0[4] > color_thresh_ ? "read" : (output_0[5] > color_thresh_ ? "blue" : (output_0[6] > color_thresh_ ? "white" : (output_0[7] > color_thresh_ ? "golden": 
    //                 (output_0[8] > color_thresh_ ? "brown" : (output_0[9] > color_thresh_ ? "black" : "Color unknown"))))))))));
    
    // std::string type = "Type: " + (string)(output_0[10] > type_thresh_ ? "sedan" : (output_0[11] > type_thresh_ ? "suv" : (output_0[12] > type_thresh_ ? "van" : (output_0[13] > type_thresh_ ? "hatchback" : 
    //                 (output_0[14] > type_thresh_ ? "mpv" : (output_0[15] > type_thresh_ ? "pickup" : (output_0[16] > type_thresh_ ? "bus" : (output_0[17] > type_thresh_ ? "truck" :
    //                 (output_0[18] > type_thresh_ ? "estate" : "Type unknown")))))))))));
    string color=GetColor();
    string type=GetType();
    results.push_back(color);
    results.push_back(type);
}

 void Pulc::draw_result(cv::Mat &draw_frame, Bbox &box,std::vector<string> &results){
    ScopedTiming st(model_name_ + " draw_result", debug_mode_);
    int w_=draw_frame.cols;
    int h_=draw_frame.rows;
    int x=int(box.x/image_size_.width*w_);
    int y=int(box.y/image_size_.height*h_);
    int w=int(box.w/image_size_.width*w_);
    int h=int(box.h/image_size_.height*h_);
    for(int i=0;i<results.size();i++){
        cv::putText(draw_frame, results[i], cv::Point(x+30, y+i*30+30), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,255, 0, 255), 1);
    }
 }

string Pulc::GetColor()
{
    float* output_0 = p_outputs_[0];
    return "Color: " + (string)(output_0[0] > color_thresh_ ? "yellow" : (output_0[1] > color_thresh_ ? "orange" : (output_0[2] > color_thresh_ ? "green" : (output_0[3] > color_thresh_ ? "gray" : 
                    (output_0[4] > color_thresh_ ? "red" : (output_0[5] > color_thresh_ ? "blue" : (output_0[6] > color_thresh_ ? "white" : (output_0[7] > color_thresh_ ? "golden": 
                    (output_0[8] > color_thresh_ ? "brown" : (output_0[9] > color_thresh_ ? "black" : "Color unknown"))))))))));
}

string Pulc::GetType()
{
    float* output_0 = p_outputs_[0];
    return "Type: " + (string)(output_0[10] > type_thresh_ ? "sedan" : (output_0[11] > type_thresh_ ? "suv" : (output_0[12] > type_thresh_ ? "van" : (output_0[13] > type_thresh_ ? "hatchback" : 
                    (output_0[14] > type_thresh_ ? "mpv" : (output_0[15] > type_thresh_ ? "pickup" : (output_0[16] > type_thresh_ ? "bus" : (output_0[17] > type_thresh_ ? "truck" :
                    (output_0[18] > type_thresh_ ? "estate" : "Type unknown")))))))));;
}