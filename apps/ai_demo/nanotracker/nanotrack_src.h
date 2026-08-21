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

#ifndef NANOTRACK_SRC_H_
#define NANOTRACK_SRC_H_

#include <iostream>
#include <vector>
#include "ai_utils.h"
#include "ai_base.h"

/**
 * @brief NanoTrackSrc 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class NanoTrackSrc: public AIBase
{
    public:
        /** 
        * @brief NanoTrackerSrc 构造函数，加载kmodel,并初始化kmodel输入、输出
        * @param kmodel_file kmodel文件路径
        * @param image_size   图片大小
        * @param debug_mode 0（不调试）、 1（只显示时间）、2（显示所有打印信息）
        * @return None
        */
        NanoTrackSrc(char *kmodel_file,FrameCHWSize image_size, int debug_mode);
        
        /** 
        * @brief  NanoTrackerSrc 析构函数
        * @return None
        */
        ~NanoTrackSrc();

        float* get_center();

        float* get_rect_size();

        /**
         * @brief set_center 更新目标中心坐标
         * @param center 目标中心坐标
         * @return None
         */
        void set_center(float* center);

        /**
         * @brief set_rect_size 更新目标框大小
         * @param rect_size 目标大小
         * @return None
         */
        void set_rect_size(float* rect_size);

        /**
         * @brief pre_process 预处理
         * @return None
         */
        void pre_process(runtime_tensor &input_tensor);

        /**
         * @brief kmodel推理
         * @return None
         */
        void inference();

        /** 
        * @brief postprocess 函数
        * @return None
        */
        void post_process(std::vector<float> &results);
       
    private:
        int net_len_;
        float* output;  // Src模型输出结果
        std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
        runtime_tensor ai2d_in_tensor_;              // ai2d输入tensor
        runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor
        FrameCHWSize image_size_;
        FrameCHWSize input_size_;
        int crop_x;                                 // 裁剪框左上角x坐标
        int crop_y;                                 // 裁剪框左上角y坐标
        int crop_w;                                 // 裁剪框宽度
        int crop_h;                                 // 裁剪框高度

        float context_amount=0.5;                   // 裁剪框上下左右扩展比例
        float crop_ratio=0.2;                       // 裁剪框长宽比
        float center[2]={0.0,0.0};                  // 目标中心坐标
        float rect_size[2]={0.0,0.0};               // 目标大小

        int crop_input_size=127;                    // 裁剪模型输入尺寸
        int src_input_size=256;                     // 搜索模型输入尺寸
};
#endif
