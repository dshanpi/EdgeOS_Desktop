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

#include "person_detect.h"

personDetect::personDetect(char *kmodel_file, float obj_thresh,float nms_thresh, FrameCHWSize image_size, int debug_mode) 
: AIBase(kmodel_file,"personDetect", debug_mode)
{
    model_name_ = "personDetect";
    obj_thresh_ = obj_thresh;
    nms_thresh_ = nms_thresh;
    image_size_ = image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::padding_resize_two_side_set(image_size_,input_size_,ai2d_builder_, cv::Scalar(114, 114, 114));
}

personDetect::~personDetect()
{
}

void personDetect::pre_process(runtime_tensor &input_tensor){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void personDetect::inference()
{
    this->run();
    this->get_output();
}

void personDetect::post_process(std::vector<BoxInfo> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    int net_len = input_shapes_[0][2];
    
    float *output_0 = p_outputs_[0];
    int first_len = net_len / 8;
    int first_size = first_len * first_len;
    auto boxes0 = decode_infer(output_0, net_len, 8, classes_num_, image_size_, anchors_0_, obj_thresh_);
    results.insert(results.begin(), boxes0.begin(), boxes0.end());

    float *output_1 = p_outputs_[1];
    int second_len = net_len / 16;
    int second_size = second_len * second_len;
    auto boxes1 = decode_infer(output_1, net_len, 16, classes_num_, image_size_, anchors_1_, obj_thresh_);
    results.insert(results.begin(), boxes1.begin(), boxes1.end());

    float *output_2 = p_outputs_[2];
    int third_len = net_len / 32;
    int third_size = third_len * third_len;
    auto boxes2 = decode_infer(output_2, net_len, 32, classes_num_, image_size_, anchors_2_, obj_thresh_);
    results.insert(results.begin(), boxes2.begin(), boxes2.end());

    nms(results, nms_thresh_);
}

void personDetect::draw_result(cv::Mat &draw_frame, std::vector<BoxInfo> &results){
    ScopedTiming st(model_name_ + " draw_result", debug_mode_);
    int w_=draw_frame.cols;
    int h_=draw_frame.rows;
    for(auto &box:results){
        std::string text = labels[box.label] + ":" + std::to_string(round(box.score * 100) / 100).substr(0,4);
        int x =  int(box.x1 / image_size_.width * w_);
        int y =  int(box.y1 / image_size_.height  * h_);
        int w = int((box.x2-box.x1) / image_size_.width * w_);
        int h = int((box.y2-box.y1) / image_size_.height  * h_);
        if(draw_frame.channels()==3){
            cv::rectangle(draw_frame, cv::Rect( x,y,w,h ), cv::Scalar(0,255, 0), 6, 2, 0); 
            cv::putText(draw_frame, text, cv::Point(x,y-20), cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0,255,0), 3, 0);
        }
        else{
            cv::rectangle(draw_frame, cv::Rect( x,y,w,h ), cv::Scalar(0,255, 0,255), 6, 2, 0); 
            cv::putText(draw_frame, text, cv::Point(x,y-20), cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0, 255, 0,255), 3, 0);
        }
        
    }
}

std::vector<BoxInfo> personDetect::decode_infer(float *data, int net_size, int stride, int num_classes, FrameCHWSize frame_size, float anchors[][2], float threshold)
{
    float ratiow = (float)net_size / frame_size.width;
    float ratioh = (float)net_size / frame_size.height;
    float gain = ratiow < ratioh ? ratiow : ratioh;
    std::vector<BoxInfo> result;
    int grid_size = net_size / stride;
    int one_rsize = num_classes + 5;
    float cx, cy, w, h;
    for (int shift_y = 0; shift_y < grid_size; shift_y++)
    {
        for (int shift_x = 0; shift_x < grid_size; shift_x++)
        {
            int loc = shift_x + shift_y * grid_size;
            for (int i = 0; i < 3; i++)
            {
                float *record = data + (loc * 3 + i) * one_rsize;
                float *cls_ptr = record + 5;
                for (int cls = 0; cls < num_classes; cls++)
                {
                    float score = cls_ptr[cls] * record[4];
                    if (score > threshold)
                    {
                        cx = (record[0] * 2.f - 0.5f + (float)shift_x) * (float)stride;
                        cy = (record[1] * 2.f - 0.5f + (float)shift_y) * (float)stride;
                        w = pow(record[2] * 2.f, 2) * anchors[i][0];
                        h = pow(record[3] * 2.f, 2) * anchors[i][1];
                        cx -= ((net_size - frame_size.width * gain) / 2);
                        cy -= ((net_size - frame_size.height * gain) / 2);
                        cx /= gain;
                        cy /= gain;
                        w /= gain;
                        h /= gain;
                        BoxInfo box;
                        box.x1 = std::max(0, std::min<int>(frame_size.width, int(cx - w / 2.f)));
                        box.y1 = std::max(0, std::min<int>(frame_size.height, int(cy - h / 2.f)));
                        box.x2 = std::max(0, std::min<int>(frame_size.width, int(cx + w / 2.f)));
                        box.y2 = std::max(0, std::min<int>(frame_size.height, int(cy + h / 2.f)));
                        box.score = score;
                        box.label = cls;
                        result.push_back(box);
                    }
                }
            }
        }
    }
    return result;
}


void personDetect::nms(std::vector<BoxInfo> &input_boxes, float NMS_THRESH)
{
    std::sort(input_boxes.begin(), input_boxes.end(), [](BoxInfo a, BoxInfo b) { return a.score > b.score; });
    std::vector<float> vArea(input_boxes.size());
    for (int i = 0; i < int(input_boxes.size()); ++i)
    {
        vArea[i] = (input_boxes.at(i).x2 - input_boxes.at(i).x1 + 1)
            * (input_boxes.at(i).y2 - input_boxes.at(i).y1 + 1);
    }
    for (int i = 0; i < int(input_boxes.size()); ++i)
    {
        for (int j = i + 1; j < int(input_boxes.size());)
        {
            float xx1 = std::max(input_boxes[i].x1, input_boxes[j].x1);
            float yy1 = std::max(input_boxes[i].y1, input_boxes[j].y1);
            float xx2 = std::min(input_boxes[i].x2, input_boxes[j].x2);
            float yy2 = std::min(input_boxes[i].y2, input_boxes[j].y2);
            float w = std::max(float(0), xx2 - xx1 + 1);
            float h = std::max(float(0), yy2 - yy1 + 1);
            float inter = w * h;
            float ovr = inter / (vArea[i] + vArea[j] - inter);
            if (ovr >= NMS_THRESH)
            {
                input_boxes.erase(input_boxes.begin() + j);
                vArea.erase(vArea.begin() + j);
            }
            else
            {
                j++;
            }
        }
    }
}
