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

#ifndef _SELF_LEARNING_H_
#define _SELF_LEARNING_H_

#include "ai_utils.h"
#include "ai_base.h"

using namespace std;

typedef struct ClassResult
{
    std::string res;
    float score; 
} ClassResult;

/**
 * @brief 自学习类
 * 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class SelfLearning : public AIBase
{
    public:

        /** 
        * @brief SelfLearning 构造函数，加载kmodel,并初始化kmodel输入、输出
        * @param kmodel_file kmodel文件路径
        * @param thres       识别阈值
        * @param topk        识别范围
        * @param image_size  图片大小
        * @param debug_mode 0（不调试）、 1（只显示时间）、2（显示所有打印信息）
        * @return None
        */
        SelfLearning(char *kmodel_file,float thres, int topk, FrameCHWSize image_size, int debug_mode);
       
        /** 
        * @brief  SelfLearning 析构函数
        * @return None
        */
        ~SelfLearning();

        void pre_process(runtime_tensor &input_tensor);


        /**
         * @brief kmodel推理
         * @return None
         */
        void inference();

        void register_object(string &name);

        /** Update the camera-space crop selected by the touch UI. */
        void set_roi(int x, int y, int width, int height);

        /** Remove all samples learned in the current session. */
        void clear_objects();

        bool save_objects(const std::string &path) const;
        bool load_objects(const std::string &path);

        size_t object_count() const { return names_.size(); }

        /** 
        * @brief postprocess 函数
        * @return None
        */
        void post_process(std::vector<ClassResult> &results);


        void draw_result(cv::Mat &draw_frame,std::vector<ClassResult> &results);

    private:
        std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
        runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor
        FrameCHWSize image_size_;
        FrameCHWSize input_size_;

        int topk_;
        float thres_;
        std::vector<string> names_;                        // 特征数据库名字
        std::vector<std::vector<float>> features_;              // 特征数据库特征

        int crop_x;
        int crop_y;
        int crop_w;
        int crop_h;
};
#endif
