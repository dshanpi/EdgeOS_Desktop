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
#include "face_gender.h"
#include <vector>

FaceGender::FaceGender(char *kmodel_file, FrameCHWSize image_size, int debug_mode) : AIBase(kmodel_file,"FaceGender", debug_mode)
{
    model_name_ = "FaceGender";
	margin_ = 0.4;
    image_size_=image_size;
    input_size_={input_shapes_[0][3],input_shapes_[0][1],input_shapes_[0][2]};
    ai2d_out_tensor_ = get_input_tensor(0);
}

FaceGender::~FaceGender()
{
}

void FaceGender::pre_process(runtime_tensor& input_tensor, Bbox& bbox){
    float x1 = bbox.x, y1 = bbox.y;
    float x2 = bbox.x + bbox.w, y2 = bbox.y + bbox.h;
    int xw1 = std::max(int(x1 - margin_ * bbox.w), int(0));
    int yw1 = std::max(int(y1 - margin_ * bbox.h), int(0));
    int xw2 = std::min(int(x2 + margin_ * bbox.w), int(image_size_.width - 1));
    int yw2 = std::min(int(y2 + margin_ * bbox.h), int(image_size_.height - 1));
	Bbox crop_info = {xw1,yw1,xw2-xw1,yw2-yw1};
    Utils::crop_resize_out2RGBP_out2HWC_set(image_size_,input_size_,crop_info.x,crop_info.y,crop_info.w,crop_info.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void FaceGender::inference()
{
    this->run();
    this->get_output();
}

void FaceGender::post_process(FaceGenderInfo& result)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
	result.gender = (p_outputs_[0][0]>0.5) ? "F" : "M";
}

void FaceGender::draw_result(cv::Mat& src_img,Bbox& bbox,FaceGenderInfo& result, bool pic_mode)
{
    int src_w = src_img.cols;
    int src_h = src_img.rows;
    int max_src_size = std::max(src_w,src_h);

    char text[30];
	sprintf(text, "%s",result.gender.c_str());

    if(pic_mode)
    {
        cv::rectangle(src_img, cv::Rect(bbox.x, bbox.y , bbox.w, bbox.h), cv::Scalar(255, 255, 255), 2, 2, 0);
        cv::putText(src_img, text , {bbox.x,std::max(int(bbox.y-10),0)}, cv::FONT_HERSHEY_COMPLEX, 0.7, cv::Scalar(255, 0, 0), 1, 8, 0);
    }
    else
    {
		int x = bbox.x / image_size_.width * src_w;
        int y = bbox.y / image_size_.height * src_h;
        int w = bbox.w / image_size_.width * src_w;
        int h = bbox.h / image_size_.height * src_h;
        cv::rectangle(src_img, cv::Rect(x, y , w, h), cv::Scalar(255,255, 255, 255), 2, 2, 0);
        if(result.gender == "F")
			cv::putText(src_img,text,cv::Point(x,std::max(int(y-10),0)),cv::FONT_HERSHEY_COMPLEX,2,cv::Scalar(255,255, 0, 255), 2, 8, 0);
		else
			cv::putText(src_img,text,cv::Point(x,std::max(int(y-10),0)),cv::FONT_HERSHEY_COMPLEX,2,cv::Scalar(255,0, 255, 255), 2, 8, 0);
    }  
}