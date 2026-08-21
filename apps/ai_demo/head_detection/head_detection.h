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

#ifndef _HEAD_DETECTION_H
#define _HEAD_DETECTION_H

#include "ai_utils.h"
#include "ai_base.h"

/**
 * @brief 多目标检测后处理后集合
 */
typedef struct {
	cv::Rect box;
	float confidence;
	int index;
}HeadDetBox;

/**
 * @brief 人头检测
 * 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class HeadDetection: public AIBase
{
    public:
    /**
    * @brief HeadDetection构造函数，加载kmodel,并初始化kmodel输入、输出和人头检测阈值
    * @param kmodel_file kmodel文件路径
    * @param score_thres 人头检测阈值
    * @param nms_thres   人头检测nms阈值
    * @param image_size  图像分辨率
    * @param debug_mode  0（不调试）、 1（只显示时间）、2（显示所有打印信息）
    * @return None
    */    
    HeadDetection(char *kmodel_file, float score_thres, float nms_thres, FrameCHWSize image_size, int debug_mode);
    
    /**
    * @brief HeadDetection析构函数
    * @return None
    */
    ~HeadDetection();

    void pre_process(runtime_tensor &input_tensor);

    /**
    * @brief kmodel推理
    * @return None
    */
    void inference();

    /**
    * @brief kmodel推理结果后处理
    * @param frame_size 原始图像/帧宽高，用于将结果放到原始图像大小
    * @param detections 后处理之后的基于原始图像的检测结果集合
    * @param pic_mode    ture(原图片)，false(osd)
    * @return None
    */
    void post_process(vector<HeadDetBox> &results);

    /**
     * @brief 将处理好的人头检测结果显示到原图或osd上
     * @param src_img     原图
     * @param results     人头检测结果
     * @param pic_mode    ture(原图片)，false(osd)
     * @return None
     */
    void draw_result(cv::Mat& img,vector<HeadDetBox> &results);
    
    private:

    void nms(std::vector<HeadDetBox> &bboxes,  float confThreshold, float nmsThreshold, std::vector<int> &indices);
    
    float iou_calculate(cv::Rect &rect1, cv::Rect &rect2);

    std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
    runtime_tensor ai2d_in_tensor_;              // ai2d输入tensor
    runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor
    FrameCHWSize image_size_;
    FrameCHWSize input_size_;

    // 人头检测类别名字
    std::vector<std::string> classes{"head", "person"};
    int label_num_ = 2;

    // 人头检测分数阈值
    float conf_thresh_;

    // 人头检测nms阈值
    float nms_thresh_;

    // kmodel的输出初步处理结果
    float *output_det;
};
#endif