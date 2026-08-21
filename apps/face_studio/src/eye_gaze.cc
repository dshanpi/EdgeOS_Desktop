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
#include "eye_gaze.h"
#include <vector>

EyeGaze::EyeGaze(char *kmodel_file, FrameCHWSize image_size, int debug_mode) : AIBase(kmodel_file,"EyeGaze", debug_mode)
{
    model_name_ = "EyeGaze";
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_ = get_input_tensor(0);
}

EyeGaze::~EyeGaze()
{
}

void EyeGaze::pre_process(runtime_tensor& input_tensor, Bbox &bbox){
    ScopedTiming st(model_name_ + " pre_process_video", debug_mode_);
	Utils::crop_resize_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void EyeGaze::inference()
{
    this->run();
    this->get_output();
}

void EyeGaze::post_process(EyeGazeInfo& result)
{
	ScopedTiming st(model_name_ + " post_process", debug_mode_);
	for(int out_index = 0;out_index < output_shapes_.size(); ++out_index)
	{
		vector<float> pred(p_outputs_[out_index],p_outputs_[out_index]+output_shapes_[out_index][0]*output_shapes_[out_index][1]);
		vector<float> softmax_pred;
		softmax(pred,softmax_pred);
		float pred_sum = 0;
		for(int i = 0;i<softmax_pred.size();++i)
		{
			pred_sum += softmax_pred[i] * i;
		}
		pred_sum = pred_sum * 4 - 180;
		pred_sum = pred_sum * PI / 180.0;
		if(out_index == 0)
			result.pitch = pred_sum;
		else
			result.yaw = pred_sum;

	}
}

void EyeGaze::draw_result(cv::Mat& src_img,Bbox& bbox,EyeGazeInfo& result, bool pic_mode)
{
    int src_w = src_img.cols;
    int src_h = src_img.rows;
	
	if(pic_mode)
    {
        float length = float(src_w) / 2;
		int center_x = int(bbox.x + bbox.w/2.0);
		int center_y = int(bbox.y + bbox.h/2.0);
		double dx = -length * sin(result.pitch) * cos(result.yaw);
		double dy = -length * sin(result.yaw);
		cv::arrowedLine(src_img, cv::Point(cvRound(center_x), cvRound(center_y)),
					cv::Point(cvRound(center_x + dx), cvRound(center_y + dy)), cv::Scalar(255, 255, 0),
					2, cv::LINE_AA, 0, 0.18);
    }
    else
    {
		float length = float(src_w) / 2;
		int center_x = float(bbox.x + bbox.w/2.0) / image_size_.width * src_w;
		int center_y = float(bbox.y + bbox.h/2.0) / image_size_.height * src_h;

        double dx = -length * sin(result.pitch) * cos(result.yaw);
		double dy = -length * sin(result.yaw);
		cv::arrowedLine(src_img, cv::Point(cvRound(center_x), cvRound(center_y)),
					cv::Point(cvRound(center_x + dx), cvRound(center_y + dy)), cv::Scalar(255, 255, 0,255),
					2, cv::LINE_AA, 0, 0.18);
	} 
}

void EyeGaze::softmax(vector<float>& input,vector<float>& output)
{
    //e_x = np.exp(x - np.max(x))
    //return e_x / e_x.sum()
    std::vector<float>::iterator p_input_max = std::max_element(input.begin(), input.end());
    float input_max = *p_input_max;
    float input_total = 0;
    
    for(auto x:input)
	{
		input_total+=exp( x- input_max);
	}

    output.resize(input.size());
	for(int i=0;i<input.size();++i)
	{
		output[i] = exp(input[i] - input_max)/input_total;
	}
}