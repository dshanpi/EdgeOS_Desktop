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
#include "hand_keypoint.h"

HandKeypoint::HandKeypoint(char *kmodel_file, FrameCHWSize image_size, int debug_mode)
: AIBase(kmodel_file,"HandKeypoint", debug_mode)
{
    model_name_ = "HandKeypoint";
    image_size_ = image_size;
    input_size_ = {input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_ = get_input_tensor(0);
}

HandKeypoint::~HandKeypoint()
{
}

void HandKeypoint::pre_process(runtime_tensor& input_tensor,Bbox &bbox)
{
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    Utils::crop_resize_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");   
}

void HandKeypoint::inference()
{
    this->run();
    this->get_output();
}

void HandKeypoint::post_process(Bbox &bbox)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float *pred = p_outputs_[0];
    // 绘制关键点像素坐标
    int64_t output_tensor_size = output_shapes_[0][1];// 关键点输出 （x,y）*21= 42
    results.clear();

    for (unsigned i = 0; i < output_tensor_size / 2; i++)
    {
        float x_kp;
        float y_kp;
        x_kp = pred[i * 2] * bbox.w + bbox.x;
        y_kp = pred[i * 2 + 1] * bbox.h + bbox.y;

        results.push_back(static_cast<int>(x_kp));
        results.push_back(static_cast<int>(y_kp));

    }
}

void HandKeypoint::get_point(vector<float> &output){
    float* pred=p_outputs_[0];
    output.push_back(std::max(std::min(pred[16], 1.0f), 0.0f));
    output.push_back(std::max(std::min(pred[17], 1.0f), 0.0f));
}

void HandKeypoint::draw_result(cv::Mat &img,Bbox &bbox)
{
    ScopedTiming st(model_name_ + " draw_keypoints", debug_mode_);
    int img_w = img.cols;
    int img_h = img.rows;
    int64_t output_tensor_size = output_shapes_[0][1];// 关键点输出 （x,y）*21= 42
    std::vector<int>results_vd(output_tensor_size);

    int x =  int(bbox.x / image_size_.width * img_w);
    int y =  int(bbox.y / image_size_.height  * img_h);
    int w = int((bbox.w) / image_size_.width * img_w);
    int h = int((bbox.h) / image_size_.height  * img_h);
    if(img.channels()==3){
        cv::rectangle(img, cv::Rect( x,y,w,h ), cv::Scalar(0,0, 255), 4, 2, 0); 
    }
    else{
        cv::rectangle(img, cv::Rect( x,y,w,h ), cv::Scalar(0,0,255, 255), 4, 2, 0); 
    }

    for (unsigned i = 0; i < output_tensor_size / 2; i++)
    {
        results_vd[i * 2] = static_cast<float>(results[i*2]) / image_size_.width * img_w;
        results_vd[i * 2 + 1] = static_cast<float>(results[i*2+1]) / image_size_.height * img_h;
        if(img.channels()==3){
            cv::circle(img, cv::Point(results_vd[i * 2], results_vd[i * 2 + 1]), 4, cv::Scalar(155, 255, 255), 3);
        }else{
            cv::circle(img, cv::Point(results_vd[i * 2], results_vd[i * 2 + 1]), 4, cv::Scalar(155, 255, 255, 255), 4);
        }
        
    }

    for (unsigned k = 0; k < 5; k++)
    {
        int i = k*8;
        unsigned char R = 255, G = 0, B = 0;

        switch(k)
        {
            case 0:R = 255; G = 0; B = 0;break;
            case 1:R = 255; G = 0; B = 255;break;
            case 2:R = 255; G = 255; B = 0;break;
            case 3:R = 0; G = 255; B = 0;break;
            case 4:R = 0; G = 0; B = 255;break;
            default: std::cout << "error" << std::endl;
        }

        if(img.channels()==3){
            cv::line(img, cv::Point(results[0], results[1]), cv::Point(results[i + 2], results[i + 3]), cv::Scalar(B,G,R), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results[i + 2], results[i + 3]), cv::Point(results[i + 4], results[i + 5]), cv::Scalar(B, G, R), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results[i + 4], results[i + 5]), cv::Point(results[i + 6], results[i + 7]), cv::Scalar(B, G, R), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results[i + 6], results[i + 7]), cv::Point(results[i + 8], results[i + 9]), cv::Scalar(B, G, R), 2, cv::LINE_AA);
        }
        else{
            cv::line(img, cv::Point(results_vd[0], results_vd[1]), cv::Point(results_vd[i + 2], results_vd[i + 3]), cv::Scalar(B,G,R,255), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results_vd[i + 2], results_vd[i + 3]), cv::Point(results_vd[i + 4], results_vd[i + 5]), cv::Scalar(B, G, R,255), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results_vd[i + 4], results_vd[i + 5]), cv::Point(results_vd[i + 6], results_vd[i + 7]), cv::Scalar(B, G, R,255), 2, cv::LINE_AA);
            cv::line(img, cv::Point(results_vd[i + 6], results_vd[i + 7]), cv::Point(results_vd[i + 8], results_vd[i + 9]), cv::Scalar(B, G, R,255), 2, cv::LINE_AA);
        }
    }
}
