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

#include "pphumanseg.h"

SEG::SEG(char *kmodel_file, FrameCHWSize image_size, int debug_mode):AIBase(kmodel_file,"pphumanseg", debug_mode)
{
    model_name_ = "pphumanseg";
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::resize_set(image_size_,input_size_,ai2d_builder_);
}


SEG::~SEG()
{
}

void SEG::pre_process(runtime_tensor& input_tensor){
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
    float *output = p_outputs_[0];
    int net_len_w = input_shapes_[0][2];
    int net_len_h = input_shapes_[0][3];
    cv::Mat mask = cv::Mat::ones(net_len_w, net_len_h, CV_8UC1) * 255;
	{
        // for NHWC
		for (int i = 0; i < net_len_h; i++)
		{
			for (int j = 0; j < net_len_w; j++)
			{
				int idx = j + i * net_len_w;
                mask.at<uchar>(i, j) = (output[2 * (j + net_len_w * i)] > output[2 * (j + net_len_w * i) + 1] ? 255 : 0);
			}
		}
	}

    cv::resize(mask, mask, cv::Size(draw_frame.cols, draw_frame.rows), 0, 0); 
    draw_frame.setTo(cv::Scalar(255, 255, 255, 255), mask);

}


