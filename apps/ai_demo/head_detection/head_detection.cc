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

#include "head_detection.h"

HeadDetection::HeadDetection(char *kmodel_file, float score_thres, float nms_thres, FrameCHWSize image_size, int debug_mode)
:AIBase(kmodel_file,"HeadDetection", debug_mode)
{
    model_name_ = "HeadDetection";
    conf_thresh_=score_thres;
    nms_thresh_=nms_thres;
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::padding_resize_one_side_set(image_size_,input_size_,ai2d_builder_, cv::Scalar(114, 114, 114));
}

HeadDetection::~HeadDetection()
{
}

void HeadDetection::pre_process(runtime_tensor &input_tensor)
{
    ScopedTiming st(model_name_ + " pre_process", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void HeadDetection::inference()
{
    this->run();
    this->get_output();
}

void HeadDetection::post_process(vector<HeadDetBox> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float ratiow = (float)input_size_.width / image_size_.width;
    float ratioh = (float)input_size_.height / image_size_.height;
    float ratio = ratiow < ratioh ? ratiow : ratioh;
    int box_num_=output_shapes_[0][2];
    int box_feature_len_=output_shapes_[0][1];
    float *output_det = new float[box_num_ * box_feature_len_];
    // 模型推理结束后，进行后处理
    float* output0= p_outputs_[0];
    // 将输出数据排布从[label_num_+4+51,(w/8)*(h/8)+(w/16)*(h/16)+(w/32)*(h/32)]调整为[(w/8)*(h/8)+(w/16)*(h/16)+(w/32)*(h/32),label_num_+4+51],方便后续处理
    for(int r = 0; r < box_num_; r++)
    {
        for(int c = 0; c < box_feature_len_; c++)
        {
            output_det[r*box_feature_len_ + c] = output0[c*box_num_ + r];
        }
    }
    for(int i=0;i<box_num_;i++){
        float* vec=output_det+i*box_feature_len_;
        float box[4]={vec[0],vec[1],vec[2],vec[3]};
        float* class_scores=vec+4;
        float* max_class_score_ptr=std::max_element(class_scores,class_scores+label_num_);
        float score=*max_class_score_ptr;
        int max_class_index = max_class_score_ptr - class_scores; // 计算索引
        if(score>conf_thresh_){
            HeadDetBox bbox;
            float x_=box[0]/ratio*1.0;
            float y_=box[1]/ratio*1.0;
            float w_=box[2]/ratio*1.0;
            float h_=box[3]/ratio*1.0;
            int x=int(MAX(x_-0.5*w_,0));
            int y=int(MAX(y_-0.5*h_,0));
            int w=int(w_);
            int h=int(h_);
            if (w <= 0 || h <= 0) { continue; }
            bbox.box=cv::Rect(x,y,w,h);
            bbox.confidence=score;
            bbox.index=max_class_index;
            results.push_back(bbox);
        }
    }
    //执行非最大抑制以消除具有较低置信度的冗余重叠框（NMS）
    std::vector<int> nms_result;
    nms(results, conf_thresh_, nms_thresh_, nms_result);
    delete[] output_det;
}


void HeadDetection::draw_result(cv::Mat& img,vector<HeadDetBox> &results)
{
    ScopedTiming st(model_name_ + " draw_result", debug_mode_);
    int head_count = 0;
    int w_=img.cols;
    int h_=img.rows;
    int res_size=results.size();
    for(int i=0;i<res_size;i++){
        HeadDetBox box_=results[i];
        cv::Rect box=box_.box;
        int idx=box_.index;
        float score=box_.confidence;
        int x=int(box.x*float(w_)/image_size_.width);
        int y=int(box.y*float(h_)/image_size_.height);
        int w=int(box.width*float(w_)/image_size_.width);
        int h=int(box.height*float(h_)/image_size_.height);
        int x_right = x + w;
        int y_bottom = y + h;
        if (x_right > w_)
        {
            w = w_ - x;
        }
        if (y_bottom > h_)
        {
            h = h_ - y;
        }
        cv::Rect new_box(x,y,w,h);
        if (idx == 0){
            head_count++;
            cv::rectangle(img, new_box, cv::Scalar(255, 0, 0,255), 2, 8);
        }else{
            continue;
        }
    }
    cv::putText(img, "head count:"+std::to_string(head_count), cv::Point(100,100), cv::FONT_HERSHEY_DUPLEX, 1, cv::Scalar(255, 0, 0,255), 4, 0);
}

void HeadDetection::nms(std::vector<HeadDetBox> &bboxes,  float confThreshold, float nmsThreshold, std::vector<int> &indices)
{	
    std::sort(bboxes.begin(), bboxes.end(), [](HeadDetBox &a, HeadDetBox &b) { return a.confidence > b.confidence; });
    int updated_size = bboxes.size();
    for (int i = 0; i < updated_size; i++) {
        if (bboxes[i].confidence < confThreshold)
            continue;
        indices.push_back(i);
        // 这里使用移除冗余框，而不是 erase 操作，减少内存移动的开销
        for (int j = i + 1; j < updated_size;) {
            float iou = iou_calculate(bboxes[i].box, bboxes[j].box);
            if (iou > nmsThreshold) {
                bboxes[j].confidence = -1;  // 设置为负值，后续不会再计算其IOU
            }
            j++;
        }
    }

    // 移除那些置信度小于0的框
    bboxes.erase(std::remove_if(bboxes.begin(), bboxes.end(), [](HeadDetBox &b) { return b.confidence < 0; }), bboxes.end());
}

float HeadDetection::iou_calculate(cv::Rect &rect1, cv::Rect &rect2)
{
    int xx1, yy1, xx2, yy2;
 
	xx1 = std::max(rect1.x, rect2.x);
	yy1 = std::max(rect1.y, rect2.y);
	xx2 = std::min(rect1.x + rect1.width - 1, rect2.x + rect2.width - 1);
	yy2 = std::min(rect1.y + rect1.height - 1, rect2.y + rect2.height - 1);
 
	int insection_width, insection_height;
	insection_width = std::max(0, xx2 - xx1 + 1);
	insection_height = std::max(0, yy2 - yy1 + 1);
 
	float insection_area, union_area, iou;
	insection_area = float(insection_width) * insection_height;
	union_area = float(rect1.width*rect1.height + rect2.width*rect2.height - insection_area);
	iou = insection_area / union_area;

	return iou;
}