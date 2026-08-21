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

#include "licence_reco.h"

LicenceReco::LicenceReco(char *kmodel_file, int debug_mode)
: AIBase(kmodel_file,"LicenceReco", debug_mode)
{
    model_name_ = "LicenceReco";
}

LicenceReco::~LicenceReco()
{
}

void LicenceReco::pre_process(cv::Mat &input_img){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    image_size_={input_img.channels(),input_img.rows,input_img.cols};
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::resize_set(image_size_,input_size_,ai2d_builder_);
    // 创建tensor
    dims_t rec_in_shape { 1, 1, input_img.rows, input_img.cols };
    runtime_tensor rec_input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, rec_in_shape, hrt::pool_shared).expect("cannot create input tensor");
    auto rec_input_buf = rec_input_tensor.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
    memcpy(reinterpret_cast<char *>(rec_input_buf.data()), input_img.data, input_img.rows * input_img.cols * input_img.channels());
    hrt::sync(rec_input_tensor, sync_op_t::sync_write_back, true).expect("write back input failed");
    ai2d_builder_->invoke(rec_input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void LicenceReco::inference()
{
    this->run();
    this->get_output();
}

void LicenceReco::post_process(std::string &results)
{
    float* output = p_outputs_[0];
	int size = input_shapes_[0][3] / 4;
	vector<int> result;
	for (int i = 0; i < size; i++)
	{
		float maxs = -10.f;
		int index = -1;
		for (int j = 0; j < dict_size; j++)
		{
			if (maxs < output[i * dict_size + j])
			{
				index = j;
				maxs = output[i * dict_size + j];
			}
		}
		result.push_back(index);
	}

	for (int i = 0; i < size; i++){
        if (result[i] >= 0 && result[i] != 0 && !(i > 0 && result[i-1] == result[i]))
        {
        	results+=dict[result[i]-1];
        }
    }
}