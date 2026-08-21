/* Copyright (c) 2022, Canaan Bright Sight Co., Ltd
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

#ifndef _SEG_H
#define _SEG_H

#include "ai_utils.h"
#include "ai_base.h"

typedef struct SegBbox{
	cv::Rect box;
	float confidence;
	int index;
    cv::Mat mask;
}SegBbox;

/**
 * @brief 多目标分割
 * 主要封装了对于每一帧图片，从预处理、运行到后处理给出结果的过程
 */
class Seg : public AIBase
{
    public:
        /**
        * @brief Seg构造函数，加载kmodel,并初始化kmodel输入、输出和多目标分割阈值
        * @param kmodel_file kmodel文件路径
        * @param conf_thres  分割框的置信度阈值
        * @param nms_thres   分割非极大值抑制阈值
        * @param mask_thres  分割掩码阈值
        * @param image_size   图片大小
        * @param debug_mode  0（不调试）、 1（只显示时间）、2（显示所有打印信息）
        * @return None
        */    
        Seg(char *kmodel_file, float conf_thres, float nms_thres, float mask_thres, FrameCHWSize image_size,int debug_mode);
        
        /**
        * @brief Seg析构函数
        * @return None
        */
        ~Seg();

        void pre_process(runtime_tensor &input_tensor);

        /**
        * @brief kmodel推理
        * @return None
        */
        void inference();

        void post_process(std::vector<SegBbox> &yolo_results);

        void draw_result(cv::Mat &draw_frame,std::vector<SegBbox> &yolo_results);

    private:
        void yolov8_nms(std::vector<SegBbox> &bboxes,  float confThreshold, float nmsThreshold, std::vector<int> &indices);
    
        float yolov8_iou_calculate(cv::Rect &rect1, cv::Rect &rect2);
        
        std::unique_ptr<ai2d_builder> ai2d_builder_; // ai2d构建器
        runtime_tensor ai2d_in_tensor_;              // ai2d输入tensor
        runtime_tensor ai2d_out_tensor_;             // ai2d输出tensor
        FrameCHWSize image_size_;
        FrameCHWSize input_size_;

        // 标签列表
        std::vector<std::string> labels_{"person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"};
        int label_num_=80;
        std::vector<cv::Scalar> colors;
        // 置信度阈值
        float conf_thres_;
        // nms阈值
        float nms_thres_;
        // 分割任务使用的mask阈值
        float mask_thres_;
        // 检测框的总数
        int box_num_;
        int max_box_num_;
        // 每个检测框的特征维度
        int box_feature_len_;
};
#endif
