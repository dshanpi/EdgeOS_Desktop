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

#include "nanotrack_tracker.h"

NanoTrackTracker::NanoTrackTracker(char *kmodel_file,FrameCHWSize image_size,float thresh,int debug_mode)
: AIBase(kmodel_file,"nanotracker_tracker", debug_mode)
{
    model_name_ = "nanotracker_tracker";
    image_size_=image_size;
    thresh_=thresh;
	// hanning窗初始化和平滑points初始化
    float stride = 8.0;
    float ori = - (output_size / 2) * stride;

    for (int i = 0; i < output_size; i++) {
        float y = ori + i * stride;
        for (int j = 0; j < output_size; j++) {
            float x = ori + j * stride;

            // 1. 生成 window
            window[i * output_size + j] = hhanning[i] * hhanning[j];

            // 2. 生成 anchor points (x, y)
            points[i * output_size + j][0] = x;  // X 坐标
            points[i * output_size + j][1] = y;  // Y 坐标
        }
    }
    int min_len=std::min(image_size_.height,image_size_.width);
    int w=min_len*crop_ratio;
    int h=min_len*crop_ratio;
    center[0]=image_size_.width*0.5;
    center[1]=image_size_.height*0.5;
    rect_size[0]=w;
    rect_size[1]=h;
}

NanoTrackTracker::~NanoTrackTracker()
{
}

void NanoTrackTracker::pre_process(vector<float> &input_0,vector<float> &input_1){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);

    dims_t in_shape_0 { 1, input_shape_0.channel, input_shape_0.height, input_shape_0.width };
    runtime_tensor input_tensor_0 = host_runtime_tensor::create(typecode_t::dt_float32, in_shape_0, hrt::pool_shared).expect("cannot create input tensor");
    auto input_buf_0 = input_tensor_0.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
    memcpy(reinterpret_cast<void *>(input_buf_0.data()), reinterpret_cast<void *>(input_0.data()), input_0.size()*sizeof(float));
    hrt::sync(input_tensor_0, sync_op_t::sync_write_back, true).expect("write back input failed");
    set_input_tensor(0,input_tensor_0);

    dims_t in_shape_1 { 1, input_shape_1.channel, input_shape_1.height, input_shape_1.width };
    runtime_tensor input_tensor_1 = host_runtime_tensor::create(typecode_t::dt_float32, in_shape_1, hrt::pool_shared).expect("cannot create input tensor");
    auto input_buf_1 = input_tensor_1.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
    memcpy(reinterpret_cast<void *>(input_buf_1.data()), reinterpret_cast<void *>(input_1.data()), input_1.size()*sizeof(float));
    hrt::sync(input_tensor_1, sync_op_t::sync_write_back, true).expect("write back input failed");  
    set_input_tensor(1,input_tensor_1);
}

void NanoTrackTracker::inference()
{
    this->run();
    this->get_output();
}

void NanoTrackTracker::post_process(Bbox &result)
{
    // 使用 ScopedTiming 记录后处理阶段的耗时（用于调试）
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    // 计算crop上下文宽度
    int w_=rect_size[0]+context_amount_*(rect_size[0]+rect_size[1]);
    int h_=rect_size[1]+context_amount_*(rect_size[0]+rect_size[1]);
    int s_z=round(sqrt(w_*h_));
    // 获取输出数据指针：
    // score 指向分类得分输出（可能是目标与背景的概率）
    // box 指向边界框回归输出（预测框坐标或偏移量）
    float* score = p_outputs_[0];
    float* box = p_outputs_[1];

    // 根据输入缩放因子调整比例因子 scale_z
    float scale_z = 1.0*crop_input_size / s_z;

    // 对分类得分进行 softmax 归一化（前景和背景）
    for (int i = 0; i < output_grid_size; i++) {
        float s0 = score[i];                        // 类别0
        float s1 = score[output_grid_size + i];     // 类别1
        float exp0 = exp(s0);
        float exp1 = exp(s1);
        float denom = exp0 + exp1;
        score[output_grid_size + i] = exp1 / denom;  // 类别1的 softmax 结果，赋值回去
    }

    // 定义 penalty_array 和 pscore 数组，分别用于存储惩罚系数和加权后的得分
    float penalty_array[output_grid_size];
    float pscore[output_grid_size];

    // 遍历每个网格点，计算最终预测框并应用惩罚项
    for (int i = 0; i < output_grid_size; i++)
    {
        // 调整预测框坐标：将相对中心点的偏移量转换为绝对坐标
        box[i]              = points[i][0] - box[i];  // 左上角 x 坐标
        box[output_grid_size + i] = points[i][1] - box[output_grid_size + i]; // 左上角 y 坐标
        box[output_grid_size * 2 + i] = points[i][0] + box[output_grid_size * 2 + i]; // 右下角 x 坐标
        box[output_grid_size * 3 + i] = points[i][1] + box[output_grid_size * 3 + i]; // 右下角 y 坐标

        // 将左上右下坐标转换为中心点、宽高的形式 (cx, cy, w, h)
        float x1 = box[i];
        float y1 = box[output_grid_size + i];
        float x2 = box[output_grid_size * 2 + i];
        float y2 = box[output_grid_size * 3 + i];
        box[i] = (x1 + x2) * 0.5;  // cx
        box[output_grid_size + i] = (y1 + y2) * 0.5;  // cy
        box[output_grid_size * 2 + i] = x2 - x1;  // width
        box[output_grid_size * 3 + i] = y2 - y1;  // height

        // 提取当前框的宽高
        float w = box[output_grid_size * 2 + i];
        float h = box[output_grid_size * 3 + i];

        // 计算尺度惩罚项（基于当前框大小与原始目标大小的比值）
        float l_1 = (w + h) * 0.5;
        float s_1 = sqrt((w + l_1) * (h + l_1));  // 当前框的等效尺寸
        float l_2 = (rect_size[0] * scale_z + rect_size[1] * scale_z) * 0.5;  // 原始目标尺寸乘以缩放因子
        float s_2 = sqrt((rect_size[0] * scale_z + l_2) * (rect_size[1] * scale_z + l_2));  // 等效尺寸
        float s = s_1 / s_2;
        float sc = std::max(s, (float)(1.0 / s));

        // 计算长宽比惩罚项（基于当前框与原始目标的长宽比）
        float r = (rect_size[0] / rect_size[1]) / (w / h);  // 长宽比差异
        float rc = std::max(r, (float)(1.0 / r));
        float penalty = exp(-(rc * sc - 1) * penalty_k);  // 结合惩罚系数
        penalty_array[i] = penalty;

        // 加权得分 = 分类得分 × 惩罚项 + 窗口影响因子（平滑结果）
        float pscore_tmp = penalty * score[output_grid_size + i];
        pscore[i] = pscore_tmp * (1 - window_influence) + window[i] * window_influence;
    }

    // 找到最大得分的索引（即最优预测框）
    int best_index = 0;
    float max_score = pscore[0];
    for (int i = 1; i < output_grid_size; i++) {
        if (pscore[i] > max_score) {
            max_score = pscore[i];
            best_index = i;
        }
    }

    // 根据最佳索引提取预测框参数，并根据 scale_z 进行反归一化
    float cx = box[best_index] / scale_z;
    float cy = box[best_index + output_grid_size] / scale_z;
    float cw = box[best_index + output_grid_size * 2] / scale_z;
    float ch = box[best_index + output_grid_size * 3] / scale_z;

    // 计算学习率 lr（结合了惩罚项和分类得分）
    float lr = penalty_array[best_index] * score[output_grid_size + best_index] * LR;
    // 更新目标中心位置（加上原图上的偏移）
    cx = cx + center[0];
    cy = cy + center[1];

    // 平滑更新目标尺寸（使用学习率对旧尺寸和新尺寸进行插值）
    cw = rect_size[0] * (1 - lr) + cw * lr;
    ch = rect_size[1] * (1 - lr) + ch * lr;

    // 边界检查：确保坐标和尺寸在图像范围内
    cx = max((float)0.0, min(cx, (float)(image_size_.width * 1.0)));
    cy = max((float)0.0, min(cy, (float)(image_size_.height * 1.0)));
    cw = max((float)10.0, min(cw, (float)(image_size_.width * 1.0)));
    ch = max((float)10.0, min(ch, (float)(image_size_.height * 1.0)));

    // 更新跟踪器内部状态
    center[0] = cx;
    center[1] = cy;
    rect_size[0] = cw;
    rect_size[1] = ch;

    // 设置输出结果结构体
    result.score = score[output_grid_size + best_index];  // 最终得分
    result.x = std::max(0, int(cx - cw / 2));  // 左上角 x 坐标
    result.y = std::max(0, int(cy - ch / 2));  // 左上角 y 坐标
    result.w = int(cw);  // 宽度
    result.h = int(ch);  // 高度
}

float* NanoTrackTracker:: get_center(){
    return center;
}

float* NanoTrackTracker::get_rect_size(){
    return rect_size;
}

void NanoTrackTracker::set_center(float* center){
    memcpy(this->center,center,2*sizeof(float));
}

void NanoTrackTracker::set_rect_size(float* rect_size){
    memcpy(this->rect_size,rect_size,2*sizeof(float));
}

void NanoTrackTracker::draw_result(cv::Mat &draw_frame,Bbox &result) {
    int w_ = draw_frame.cols;
    int h_ = draw_frame.rows;
    int x=result.x/image_size_.width*w_;
    int y=result.y/image_size_.height*h_;
    int w=result.w/image_size_.width*w_;
    int h=result.h/image_size_.height*h_;
    cv::rectangle(draw_frame, cv::Point(x, y), cv::Point(x + w, y + h), cv::Scalar(0, 255, 0, 255), 2);
}



