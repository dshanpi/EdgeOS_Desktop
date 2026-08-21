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
#include <cmath>
#include <algorithm>
#include "self_learning.h"
#include <fstream>


SelfLearning::SelfLearning(char *kmodel_file, float thres, int topk, FrameCHWSize image_size, int debug_mode)
: AIBase(kmodel_file,"self_learning", debug_mode)
{
    thres_=thres;
    topk_=topk;
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);

    // 计算裁剪参数
    int min_len=std::min(image_size_.height,image_size_.width);
    crop_w=min_len*0.4;
    crop_h=min_len*0.4;
    crop_x=image_size_.width/2-crop_w/2;
    crop_y=image_size_.height/2-crop_h/2;
    // 配置ai2d裁剪方法
    Utils::crop_resize_set(image_size_,input_size_,crop_x,crop_y,crop_w,crop_h,ai2d_builder_);
}

SelfLearning::~SelfLearning()
{
}

void SelfLearning::pre_process(runtime_tensor &input_tensor)
{
    ScopedTiming st(model_name_ + " pre_process", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void SelfLearning::inference()
{
    this->run();
    this->get_output();
}

void SelfLearning::register_object(string &name){
    float *output = p_outputs_[0];
    int length = output_shapes_[0][1];
    std::vector<float> output_feature(output,output+length);
    features_.push_back(output_feature);
    names_.push_back(name);
}

void SelfLearning::set_roi(int x, int y, int width, int height)
{
    crop_x = std::max(0, std::min(x, image_size_.width - 2));
    crop_y = std::max(0, std::min(y, image_size_.height - 2));
    crop_w = std::max(2, std::min(width, image_size_.width - crop_x));
    crop_h = std::max(2, std::min(height, image_size_.height - crop_y));
    Utils::crop_resize_set(image_size_, input_size_, crop_x, crop_y,
                           crop_w, crop_h, ai2d_builder_);
}

void SelfLearning::clear_objects()
{
    names_.clear();
    features_.clear();
}

bool SelfLearning::save_objects(const std::string &path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const uint32_t magic = 0x534C4442, count = features_.size();
    out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (size_t i = 0; i < features_.size(); ++i) {
        uint32_t name_len = names_[i].size(), feature_len = features_[i].size();
        out.write(reinterpret_cast<const char *>(&name_len), sizeof(name_len));
        out.write(names_[i].data(), name_len);
        out.write(reinterpret_cast<const char *>(&feature_len), sizeof(feature_len));
        out.write(reinterpret_cast<const char *>(features_[i].data()),
                  feature_len * sizeof(float));
    }
    return out.good();
}

bool SelfLearning::load_objects(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    uint32_t magic = 0, count = 0;
    if (!in.read(reinterpret_cast<char *>(&magic), sizeof(magic)) ||
        magic != 0x534C4442 ||
        !in.read(reinterpret_cast<char *>(&count), sizeof(count)) || count > 100)
        return false;
    std::vector<std::string> names;
    std::vector<std::vector<float>> features;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name_len = 0, feature_len = 0;
        if (!in.read(reinterpret_cast<char *>(&name_len), sizeof(name_len)) ||
            name_len > 128) return false;
        std::string name(name_len, '\0');
        if (!in.read(&name[0], name_len) ||
            !in.read(reinterpret_cast<char *>(&feature_len), sizeof(feature_len)) ||
            feature_len == 0 || feature_len > 65536) return false;
        std::vector<float> feature(feature_len);
        if (!in.read(reinterpret_cast<char *>(feature.data()),
                     feature_len * sizeof(float))) return false;
        names.push_back(std::move(name)); features.push_back(std::move(feature));
    }
    names_ = std::move(names); features_ = std::move(features);
    return true;
}

void SelfLearning::post_process(std::vector<ClassResult> &results){
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float* output = p_outputs_[0];
    int length = output_shapes_[0][1];
    std::vector<float> current_feature(output, output + length);
    // 计算与特征库的相似度
    std::vector<std::pair<int, float>> index_similarity;
    for (size_t i = 0; i < features_.size(); ++i) {
        std::vector<float>& registered = features_[i];

        // 计算余弦相似度：dot(a, b)
        float dot_ = 0.0f;
        float sum_0=0.0f;
        float sum_1=0.0f;
        for (int j = 0; j < length; ++j) {
            dot_ += current_feature[j] * registered[j]; 
            sum_0+=current_feature[j]*current_feature[j];
            sum_1+=registered[j]*registered[j];
        }
        sum_0=std::sqrt(sum_0);
        sum_1=std::sqrt(sum_1);
        float simularity=dot_/(sum_0*sum_1);

        index_similarity.emplace_back(i, simularity);
    }

    // 取 TopK 最大的相似度
    int k = std::min(topk_, static_cast<int>(index_similarity.size()));
    std::partial_sort(
        index_similarity.begin(), index_similarity.begin() + k, index_similarity.end(),
        [](const std::pair<int, float>& a, const std::pair<int, float>& b) {
            return a.second > b.second;
        }
    );

    // 打印 TopK 结果
    for (int i = 0; i < k; ++i) {
        ClassResult res_i;
        if(index_similarity[i].second>thres_){
            res_i.res=names_[index_similarity[i].first];
            res_i.score=index_similarity[i].second;
            results.push_back(res_i);
        }
    }
}

void SelfLearning::draw_result(cv::Mat &draw_frame,std::vector<ClassResult> &results){
    int w_=draw_frame.cols;
    int h_=draw_frame.rows;

    int draw_x=int((float)crop_x/image_size_.width*w_);
    int draw_y=int((float)crop_y/image_size_.height*h_);
    int draw_w=int((float)crop_w/image_size_.width*w_);
    int draw_h=int((float)crop_h/image_size_.height*h_);
    cv::rectangle(draw_frame,cv::Rect(draw_x,draw_y,draw_w,draw_h),cv::Scalar(0,255,0,255),2);

    int s=results.size();
    if(s>0){
        for(int i=0;i<results.size();i++){
            cv::putText(draw_frame,results[i].res+" "+std::to_string(results[i].score),cv::Point(draw_x,draw_y-30*(s-i)),cv::FONT_HERSHEY_SIMPLEX,1,cv::Scalar(0,255,0,255),3);
        }
    }
}

