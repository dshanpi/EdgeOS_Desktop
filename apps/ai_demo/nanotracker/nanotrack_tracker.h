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

#ifndef NANOTRACK_TRACKER_H_
#define NANOTRACK_TRACKER_H_

#include <iostream>
#include <vector>
#include "ai_utils.h"
#include "ai_base.h"

using namespace std;

typedef struct Bbox
{
    float x; 
    float y; 
    float w;
    float h;
    float score;
} Bbox;

/**
 * @brief 目标图片特征提取
 * 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class NanoTrackTracker: public AIBase
{
    public:
        /** 
        * for video
        * @brief Src 构造函数，加载kmodel,并初始化kmodel输入、输出
        * @param kmodel_file kmodel文件路径
        * @param net_len   Src模型输入尺寸
        * @param image_size   图片大小
        * @param debug_mode 0（不调试）、 1（只显示时间）、2（显示所有打印信息）
        * @return None
        */
        NanoTrackTracker(char *kmodel_file,FrameCHWSize image_size, float thresh,int debug_mode);
        
        /** 
        * @brief  Src 析构函数
        * @return None
        */
        ~NanoTrackTracker();

        void pre_process(vector<float> &input_0,vector<float> &input_1);

        /**
         * @brief kmodel推理
         * @return None
         */
        void inference();

        /** 
        * @brief postprocess 函数
        * @return None
        */
        void post_process(Bbox &result);

        void draw_result(cv::Mat &draw_frame,Bbox &result);

        float* get_center();

        float* get_rect_size();

        void set_center(float* center);

        void set_rect_size(float* rect_size);
       
    private:
        float* output;  //模型输出结果
        float thresh_;
        FrameCHWSize image_size_;
        FrameCHWSize input_shape_0={48,8,8};
        FrameCHWSize input_shape_1={48,16,16};

        float context_amount_ = 0.5;
        float crop_ratio=0.2;

        static const int output_size=16;
        static const int output_grid_size=output_size*output_size;
        float window[output_size*output_size];
        float points[output_size*output_size][2];
        inline static float hhanning[] = { 0., 0.04322727, 0.1654347, 0.3454915, 0.55226423, 0.75, 0.9045085, 0.9890738, 0.9890738 , 0.9045085, 0.75, 0.55226423, 0.3454915, 0.1654347, 0.04322727, 0. };
        float window_influence=0.46;
        float LR=0.34;
        float penalty_k=0.16;

        int crop_input_size=127;
        int src_input_size=255;

        float center[2]={0.0,0.0};
        float rect_size[2]={0.0,0.0};

};
#endif
