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

#include "crosswalk_detect.h" 

// for video
crosswalkDetect::crosswalkDetect(char *kmodel_file, float obj_thresh,float nms_thresh, FrameCHWSize image_size,int debug_mode) 
: obj_thresh_(obj_thresh),nms_thresh_(nms_thresh), AIBase(kmodel_file,"smokeDetect", debug_mode)
{
    model_name_ = "crosswalkDetect";
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::padding_resize_two_side_set(image_size_,input_size_,ai2d_builder_, cv::Scalar(114, 114, 114));
}

crosswalkDetect::~crosswalkDetect()
{

}

// ai2d for image
void crosswalkDetect::pre_process(runtime_tensor &input_tensor)
{
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void crosswalkDetect::inference()
{
    this->run();
    this->get_output();
}

static float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

void crosswalkDetect::post_process(FrameCHWSize frame_size,std::vector<BoxInfo> &result)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    result.clear();
    int net_len = input_shapes_[0][2];
    // first output
    {
        float *output_0 = p_outputs_[0];
        auto boxes0 = decode_infer(output_0, net_len, 8, classes_num_, frame_size, anchors_0_, obj_thresh_);
        result.insert(result.begin(), boxes0.begin(), boxes0.end());
    }
    // second output
    {
        float *output_1 = p_outputs_[1];
        auto boxes1 = decode_infer(output_1, net_len, 16, classes_num_, frame_size, anchors_1_, obj_thresh_);
        result.insert(result.begin(), boxes1.begin(), boxes1.end());
    }
    // third output
    {
        float *output_2 = p_outputs_[2];
        auto boxes2 = decode_infer(output_2, net_len, 32, classes_num_, frame_size, anchors_2_, obj_thresh_);
        result.insert(result.begin(), boxes2.begin(), boxes2.end());
    }

    nms(result, nms_thresh_);
}

void crosswalkDetect::draw_result(cv::Mat& draw_img,vector<BoxInfo>& results){
    int osd_width = draw_img.cols;
    int osd_height = draw_img.rows;
    for (auto r : results)
    {
        ScopedTiming st("draw boxes", debug_mode_);
        if(r.label == 1){
            continue;
        }
        int x =  r.x1 / image_size_.width*osd_width;
        int y =  r.y1 / image_size_.height*osd_height;
        int w = (r.x2-r.x1) / image_size_.width*osd_width;
        int h = (r.y2-r.y1) / image_size_.height*osd_height;
        string text = labels[r.label] + ":" + std::to_string(round(r.score * 100) / 100).substr(0,4);
        cv::rectangle(draw_img, cv::Rect( x,y,w,h ), cv::Scalar(255, 0,255, 255), 6, 2, 0);
        cv::putText(draw_img, text, {x,y-20}, cv::FONT_HERSHEY_COMPLEX, 2, cv::Scalar(255,0,255,255), 1, 8, 0);
    }
}


// for NHWC
std::vector<BoxInfo> crosswalkDetect::decode_infer(float *data, int net_size, int stride, int num_classes, FrameCHWSize frame_size, float anchors[][2], float threshold)
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


void crosswalkDetect::nms(std::vector<BoxInfo> &input_boxes, float NMS_THRESH)
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