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
#include "dynamic_gesture.h"
#include "ai_utils.h"

DynamicGesture::DynamicGesture(char *kmodel_file,int debug_mode) : AIBase(kmodel_file,"DynamicGesture", debug_mode)
{
    model_name_ = "DynamicGesture";
   
    for (int i=0;i<input_shapes_.size();i++)
    {
        runtime_tensor in_tensor_= get_input_tensor(i);
        in_tensors_.push_back(in_tensor_);
    }
    for (int i=0;i<input_shapes_.size();i++)
    {
        float* data_ = new float[input_shapes_[i][0]*input_shapes_[i][1]*input_shapes_[i][2]*input_shapes_[i][3]];
        memset(data_, 0, sizeof(float) * input_shapes_[i][0]*input_shapes_[i][1]*input_shapes_[i][2]*input_shapes_[i][3]);
        input_bins.push_back(data_);
    }
}

DynamicGesture::~DynamicGesture()
{
    for (int i=0;i<input_shapes_.size();i++)
    {
        delete[] input_bins[i];
    }
}

void DynamicGesture::pre_process(cv::Mat &ori_img)
{
    ScopedTiming st(model_name_ + " pre_process_video", debug_mode_);
    float ratiow = (float)256 / ori_img.cols;
    float ratioh = (float)256 / ori_img.rows;
    float ratio = ratiow > ratioh ? ratiow : ratioh;
    int new_w = (int)(ratio * ori_img.cols);
    int new_h = (int)(ratio * ori_img.rows);
    int top = (int)(new_h - input_shapes_[0][2]) / 2;
    int bottom = (int)(input_shapes_[0][2] + top);
    int left = (int)(new_w - input_shapes_[0][3]) / 2;
    int right = (int)(input_shapes_[0][3] + left);
    cv::Mat resized_img;
    cv::Mat cropped_img;
    cv::resize(ori_img, resized_img, cv::Size(new_w, new_h), cv::INTER_AREA);
    cropped_img = resized_img(cv::Range(top, bottom), cv::Range(left, right));

    std::vector<cv::Mat> image_channels(3);
    cv::split(cropped_img, image_channels);

    std::vector<float> mean_value{0.485f, 0.456f, 0.406f};
    std::vector<float> std_value{0.229f, 0.224f, 0.225f};

    int height = input_shapes_[0][2];
    int width = input_shapes_[0][3];
    float* input_data = input_bins[0];

    for (int c = 0; c < 3; ++c) {
        cv::Mat float_img;
        image_channels[c].convertTo(float_img, CV_32FC1, 1.0 / 255.0);  // 归一化到 0~1
        float_img = (float_img - mean_value[c]) / std_value[c];  // 标准化
        memcpy(input_data + c * height * width, float_img.ptr<float>(), height * width * sizeof(float));
    }

    // 数据拷贝 & 同步
    for (size_t i = 0; i < input_shapes_.size(); ++i) {
        auto buf = in_tensors_[i].impl()->to_host().unwrap()->buffer().as_host().unwrap()
                    .map(map_access_::map_write).unwrap().buffer();
        memcpy(buf.data(), input_bins[i], sizeof(float) * input_shapes_[i][0] * input_shapes_[i][1] * input_shapes_[i][2] * input_shapes_[i][3]);
        hrt::sync(in_tensors_[i], sync_op_t::sync_write_back, true).expect("sync write_back failed");
    }
    
}

void DynamicGesture::inference()
{
    this->run();
    this->get_output();
}

void DynamicGesture::post_process()
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float *output = p_outputs_[0];

    for (int i=0;i<10;i++)
    {
        memcpy(reinterpret_cast<char*>(input_bins[i + 1]), reinterpret_cast<char*>(p_outputs_[i+1]), sizeof(float)*(input_shapes_[i + 1][0]*input_shapes_[i+1][1]*input_shapes_[i+1][2]*input_shapes_[i+1][3]));
    }
}

void DynamicGesture::get_out(vector<float> &output)
{
    softmax(p_outputs_[0], p_outputs_[0], output_shapes_[0][1] );
    output.resize(output_shapes_[0][1]);

    for (int i=0;i<output_shapes_[0][1];i++)
    {
        output[i] = p_outputs_[0][i];
    }
}

void DynamicGesture::softmax(float* x, float* dx, uint32_t len)
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

int DynamicGesture::process_output(int pred, std::vector<int>& history) {
    if (pred == 7 || pred == 8 || pred == 21 || pred == 22 || pred == 3 ) {
        pred = history.back();
    }
    if (pred == 0 || pred == 4 || pred == 6 || pred == 9|| pred == 14 || pred == 1|| pred == 19|| pred == 20|| pred == 23|| pred == 24) 
    {
        pred = history.back();
    }

    if (pred == 0) {
        pred = 2;
    }
    if (pred != history.back()) {
        if (history.size() >= 2) {
            if (!(history.back() == history[history.size() - 2])) {
                pred = history.back();
            }
        }
    }
    history.push_back(pred);
    if (history.size() > max_hist_len) {
        history.erase(history.begin(), history.begin() + (history.size() - max_hist_len));
    }
    
    return history.back();
}
