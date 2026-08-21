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

Pulc::Pulc(char *kmodel_file, FrameCHWSize image_size,float pulc_thresh, float glasses_thresh, float hold_thresh, int debug_mode)
: AIBase(kmodel_file,"personAttr", debug_mode)
{
    model_name_ = "personAttr";
    pulc_thresh_ = pulc_thresh;
    glasses_thresh_ = glasses_thresh;
    hold_thresh_ = hold_thresh;
    image_size_ = image_size;
    input_size_ = {input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_ = get_input_tensor(0);
}

Pulc::~Pulc()
{
}

void Pulc::pre_process(runtime_tensor& input_tensor,Bbox &bbox)
{
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    Utils::crop_resize_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");   
}

void Pulc::inference()
{
    this->run();
    this->get_output();
}

void Pulc::post_process(vector<string> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    // float* output_0 = p_outputs_[0];
    // string gender = (string)(output_0[22] < pulc_thresh_ ? "Female" : "Male") + "\n";
    // string age = (string)(output_0[19] > output_0[20] ? "AgeLess18" : (output_0[20] > output_0[21] ? "Age18-60" : "AgeOver60")) + "\n";
    // string direction = (string)(output_0[23] > output_0[24] ? "Front" : (output_0[24] > output_0[25] ? "Side" : "Back")) + "\n";
    // string glasses = (string)"Glasses: " + (output_0[1] > glasses_thresh_ ? "True" : "False") + "\n";
    // string hat = "Hat: " + (string)(output_0[0] > pulc_thresh_ ? "True" : "False") + "\n";
    // string hold_obj = "HoldObjectsInFront: " + (string)(output_0[18] > hold_thresh_ ? "True" : "False") + "\n";
    // string bag = (string)(output_0[15] > output_0[16] ? (output_0[15] > pulc_thresh_ ? "HandBag" : "No bag") : 
    //             (output_0[16] > output_0[17] ? (output_0[17] > pulc_thresh_ ? "ShoulderBag" : "No bag") :
    //             (output_0[17] > pulc_thresh_ ? "Backpack" : "No bag"))) + "\n";
    // string upper = "Upper: " + (string)(output_0[3] > output_0[2] ? "LongSleeve " : "ShortSleeve ") + 
    //                 (output_0[4] > output_0[5] ? "UpperStride" : (output_0[5] > output_0[6] ? "UpperLogo" :
    //                 (output_0[6] > output_0[7] ? "UpperPlaid" : "UpperSplice"))) + "\n";
    // string lower = "Lower: " + (string)(output_0[8] > pulc_thresh_ ? "LowerStripe" : (output_0[9] > pulc_thresh_ ? "LowerPattern" :
    //                            (output_0[10] > pulc_thresh_ ? "LongCoat" : (output_0[11] > pulc_thresh_ ? "Trousers" : 
    //                            (output_0[12] > pulc_thresh_ ? "Shorts" : "Skirt&Dress"))))) + "\n";
    // string shoe = (string)(output_0[14] > pulc_thresh_ ? "Boots" : "No boots") + "\n";
    // results = gender + age + direction + glasses + hat + hold_obj + bag + upper + lower + shoe;

    string gender = GetGender();
    string age = GetAge();
    string direction = GetDirection();
    results.push_back(gender);
    results.push_back(age);
    results.push_back(direction);
           
}

void Pulc::draw_result(cv::Mat &draw_frame,Bbox &box,vector<string> &results){
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

string Pulc::GetGender()
{
    float* output_0 = p_outputs_[0];
    return (string)(output_0[22] > pulc_thresh_ ? "Male" : "Female");
}

string Pulc::GetAge()
{
    float* output_0 = p_outputs_[0];
    return (string)(output_0[19] > output_0[20] ? "AgeLess18" : (output_0[20] > output_0[21] ? "Age18-60" : "AgeOver60"));
}

string Pulc::GetDirection()
{
    float* output_0 = p_outputs_[0];
    return (string)(output_0[23] > output_0[24] ? "Front" : (output_0[24] > output_0[25] ? "Side" : "Back"));
}

string Pulc::GetGlasses()
{
    float* output_0 = p_outputs_[0];
    return (string)"Glasses: " + (output_0[1] > glasses_thresh_ ? "True" : "False");
}

string Pulc::GetHat()
{
    float* output_0 = p_outputs_[0];
    return "Hat: " + (string)(output_0[0] > pulc_thresh_ ? "True" : "False");
}

string Pulc::GetHoldObj()
{
    float* output_0 = p_outputs_[0];
    return "HoldObjectsInFront: " + (string)(output_0[18] > hold_thresh_ ? "True" : "False");
}

string Pulc::GetBag()
{
    float* output_0 = p_outputs_[0];
    return (string)(output_0[15] > output_0[16] ? (output_0[15] > pulc_thresh_ ? "HandBag" : "No bag") : 
                (output_0[16] > output_0[17] ? (output_0[17] > pulc_thresh_ ? "ShoulderBag" : "No bag") :
                (output_0[17] > pulc_thresh_ ? "Backpack" : "No bag")));
}

string Pulc::GetUpper()
{
    float* output_0 = p_outputs_[0];
    return "Upper: " + (string)(output_0[3] > output_0[2] ? "LongSleeve " : "ShortSleeve ") + 
                    (output_0[4] > output_0[5] ? "UpperStride" : (output_0[5] > output_0[6] ? "UpperLogo" :
                    (output_0[6] > output_0[7] ? "UpperPlaid" : "UpperSplice")));
}

string Pulc::GetLower()
{
    float* output_0 = p_outputs_[0];
    return "Lower: " + (string)(output_0[8] > pulc_thresh_ ? "LowerStripe" : (output_0[9] > pulc_thresh_ ? "LowerPattern" :
                               (output_0[10] > pulc_thresh_ ? "LongCoat" : (output_0[11] > pulc_thresh_ ? "Trousers" : 
                               (output_0[12] > pulc_thresh_ ? "Shorts" : "Skirt&Dress")))));
}

string Pulc::GetShoe()
{
    float* output_0 = p_outputs_[0];
    return (string)(output_0[14] > pulc_thresh_ ? "Boots" : "No boots");
}