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

#ifndef _PULC_H_
#define _PULC_H_

#include <vector>
#include "ai_utils.h"
#include "ai_base.h"
#include "object_detect.h"

using std::vector;

typedef struct Bbox
{
    float x; 
    float y; 
    float w;
    float h;
} Bbox;


/**
 * @brief  Pulc 车辆属性识别模型
 * 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class Pulc: public AIBase
{
    public:
        /**
        * @brief Pulc 构造函数，加载kmodel,并初始化kmodel输入、输出、类阈值和NMS阈值
        * @param kmodel_file kmodel文件路径
        * @param image_size  图片大小
        * @param color_thresh 颜色检测阈值
        * @param type_thresh  车型识别阈值
        * @param debug_mode 0（不调试）、 1（只显示时间）、2（显示所有打印信息）
        * @return None
        */
        Pulc(char *kmodel_file, FrameCHWSize image_size,float color_thresh, float type_thresh, int debug_mode);

        /** 
        * @brief  personDetect 析构函数
        * @return None
        */
        ~Pulc();

        void pre_process(runtime_tensor& input_tensor,Bbox &bbox);

        /**
        * @brief kmodel推理
        * @return None
        */
        void inference();

        void post_process(std::vector<string> &results);

        void draw_result(cv::Mat &draw_frame,Bbox &box, std::vector<string> &results);

        /** 
        * @brief GetColor 函数，获取颜色
        * @return string ( "Color: " + "yellow" , "orange" , "green" , "gray" ,  "red" , "blue" , "white" , "golden" , "brown" , "black" or "Color unknown")
        */
        string GetColor();

        /** 
        * @brief GetType 函数，获取车型
        * @return string ("car", "truck" or "bus")
        */
        string GetType();

    private:

        void get_affine_matrix(Bbox &bbox);

        float color_thresh_;  // 颜色检测阈值
        float type_thresh_;   // 车型识别阈值

        std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
        runtime_tensor ai2d_in_tensor_;              // ai2d输入tensor
        runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor
        
        FrameCHWSize image_size_;
        FrameCHWSize input_size_;
        cv::Mat matrix_dst_;                         // affine的变换矩阵
};
#endif
