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

#include "ocr_box.h"

OCRBox::OCRBox(char *kmodel_file, float threshold, float box_thresh, FrameCHWSize image_size, int debug_mode)
:threshold(threshold), box_thresh(box_thresh), AIBase(kmodel_file,"OCRBox", debug_mode)
{
    model_name_ = "OCRBox";
    renderer.init("SourceHanSansSC-Normal-Min.ttf",25);
    threshold=threshold;
    box_thresh=box_thresh;
    image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
}


OCRBox::~OCRBox()
{
}

void OCRBox::pre_process(runtime_tensor &input_tensor,Bbox &bbox){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    crop_x=bbox.x;
    crop_y=bbox.y;
    crop_w=bbox.w;
    crop_h=bbox.h;
    Utils::crop_resize_padding_one_side_set(image_size_,input_size_,bbox.x,bbox.y,bbox.w,bbox.h,ai2d_builder_, cv::Scalar(114, 114, 114));
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void OCRBox::inference()
{
    this->run();
    this->get_output();
}

void OCRBox::post_process(vector<ocr_det_res> &results)
{   
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    float* output=new float[input_size_.width*input_size_.height];
    for(int i = 0; i < input_size_.width*input_size_.height; i++)
      output[i] = p_outputs_[0][2*i];
    Mat src(input_size_.height, input_size_.width, CV_32FC1, output);
    Mat mask(src > threshold);
    vector<vector<cv::Point>> contours;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    int num = contours.size();
    for(int i = 0; i < num; i++)
    {
        if(contours[i].size() < 4)
            continue;
        ocr_det_res b;
        getBox(b, contours[i]);
        vector<cv::Point> con;
        unclip(contours[i], con);
        getBox(b, con);
        float score = boxScore(src, contours[i], b);
        if (score < box_thresh)
            continue;
        b.score = score;
        float ratiow = 1.0 * input_size_.width / crop_w;
        float ratioh = 1.0 * input_size_.height / crop_h;
        float ratio = ratiow < ratioh ? ratiow : ratioh;
        for(int i = 0; i < 4; i++)
        {
            b.vertices[i].x = max(min((int)b.vertices[i].x, (int)input_size_.width), 0);
            b.vertices[i].y = max(min((int)b.vertices[i].y, (int)input_size_.height), 0);
            b.vertices[i].x = (b.vertices[i].x) / ratio*1.0;
            b.vertices[i].y = (b.vertices[i].y) / ratio*1.0;
            b.ver_src[i].x = b.vertices[i].x;
            b.ver_src[i].y = b.vertices[i].y;
            b.vertices[i].x = max((float)0, min(b.vertices[i].x, (float)crop_w))+crop_x;
            b.vertices[i].y = max((float)0, min(b.vertices[i].y, (float)crop_h))+crop_y;
        }
        results.push_back(b);
    }
    delete [] output;
}

void OCRBox::draw_result(Mat &img, vector<ocr_det_res> &results,vector<std::string> &results_str){
    int w_=img.cols;
    int h_=img.rows;
    for(int i = 0; i < results.size(); i++)
    {   
        for(int j = 0; j < 4; j++){
			int x1 = int(results[i].vertices[j].x * (float)w_ / image_size_.width);
            int y1 = int(results[i].vertices[j].y * (float)h_ / image_size_.height);
			int x2 = int(results[i].vertices[(j + 1) % 4].x * (float)w_ / image_size_.width);
            int y2 = int(results[i].vertices[(j + 1) % 4].y * (float)h_ / image_size_.height);
            cv::line(img,cv::Point2d(x1,y1),cv::Point2d(x2,y2), cv::Scalar(255, 0, 0,255), 3);
			if(j==0){
                renderer.putText(img, results_str[i], {x1, y1-20}, cv::Scalar(255, 0, 0,255));
			}
		}
    }
}

void OCRBox::getBox(ocr_det_res& b, vector<cv::Point> contours)
{
    cv::RotatedRect minrect = minAreaRect(contours);
    cv::Point2f vtx[4];
    minrect.points(vtx);
    for(int i = 0; i < 4; i++)
    {
        b.vertices[i].x = vtx[i].x;
        b.vertices[i].y = vtx[i].y;
    }
}

double OCRBox::distance(cv::Point p0, cv::Point p1)
{
    return sqrt((p0.x - p1.x) * (p0.x - p1.x) + (p1.y - p0.y) * (p1.y - p0.y));
}

void OCRBox::unclip(vector<cv::Point> contours, vector<cv::Point>& con)
{
    Path subj;
    Paths solution;
    double dis = 0.0;
    for(int i = 0; i < contours.size(); i++)
        subj << IntPoint(contours[i].x, contours[i].y);
    for(int i = 0; i < contours.size() - 1; i++)
        dis += distance(contours[i], contours[i+1]);
    double dis1 = (-1 * Area(subj)) * 1.5 / dis;
    ClipperOffset co;
    co.AddPath(subj, jtSquare, etClosedPolygon);
    co.Execute(solution, dis1);
    Path tmp = solution[0];
    for(int i = 0; i < tmp.size(); i++)
    {
        cv::Point p(tmp[i].X, tmp[i].Y);
        con.push_back(p);
    }
    for(int i = 0; i < con.size(); i++)
        subj << IntPoint(con[i].x, con[i].y);
}

float OCRBox::boxScore(Mat src, vector<cv::Point> contours, ocr_det_res& b)
{
    int xmin = input_shapes_[0][3];
    int xmax = 0;
    int ymin = input_shapes_[0][2];
    int ymax = 0;
    for(int i = 0; i < contours.size(); i++)
    {
        xmin = floor((contours[i].x < xmin ? contours[i].x : xmin));
        xmax = ceil((contours[i].x > xmax ? contours[i].x : xmax));
        ymin = floor((contours[i].y < ymin ? contours[i].y : ymin));
        ymax = ceil((contours[i].y > ymax ? contours[i].y : ymax));
    }
    for(int i = 0; i < contours.size(); i++)
    {
        contours[i].x = contours[i].x - xmin;
        contours[i].y = contours[i].y - ymin;
    }
    vector<vector<cv::Point>> vec;
    vec.clear();
    vec.push_back(contours);
    b.meanx = ((1.0 * xmin + xmax) / 2) / (input_shapes_[0][3]) * image_size_.width;
    b.meany = ((1.0 * ymin + ymax) / 2) / (input_shapes_[0][2]) * image_size_.height;
    Mat img = Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1);
    cv::fillPoly(img, vec, cv::Scalar(1));
    return (float)cv::mean(src(cv::Rect(xmin, ymin, xmax-xmin+1, ymax-ymin+1)), img)[0];  
}


std::vector<size_t> sort_indices(std::vector<cv::Point2f>& vec)
{
    std::vector<size_t> indices(vec.size());
    std::iota(indices.begin(), indices.end(), 0); // 生成 0 到 vec.size()-1

    std::sort(indices.begin(), indices.end(),
        [&vec](size_t i1, size_t i2) {
            return vec[i1].x < vec[i2].x;
        });

    return indices;
}

void find_rectangle_vertices(std::vector<cv::Point2f>& points, 
                             cv::Point2f& topLeft, cv::Point2f& topRight, 
                             cv::Point2f& bottomRight, cv::Point2f& bottomLeft) 
{
    auto sorted_x_id = sort_indices(points);

    auto& left1  = points[sorted_x_id[0]];
    auto& left2  = points[sorted_x_id[1]];
    auto& right1 = points[sorted_x_id[2]];
    auto& right2 = points[sorted_x_id[3]];

    // 左边两点中y小的是左上，大的是左下
    if (left1.y < left2.y) {
        topLeft = left1;
        bottomLeft = left2;
    } else {
        topLeft = left2;
        bottomLeft = left1;
    }

    // 右边两点中y小的是右上，大的是右下
    if (right1.y < right2.y) {
        topRight = right1;
        bottomRight = right2;
    } else {
        topRight = right2;
        bottomRight = right1;
    }
}

void OCRBox::warppersp(cv::Mat& src, cv::Mat& dst, ocr_det_res& b, std::vector<cv::Point2f>& vtd)
{
    // Step 1: 构造最小外接矩形
    std::vector<cv::Point2f> con(b.vertices, b.vertices+4);
    cv::RotatedRect minrect = cv::minAreaRect(con);

    std::vector<cv::Point2f> vtx(4);
    minrect.points(vtx.data());

    // Step 2: 提取四个角点（左上、右上、右下、左下）
    find_rectangle_vertices(vtx, vtd[0], vtd[1], vtd[2], vtd[3]);

    // Step 3: 计算宽高（注意可能是横着或竖着）
    float tmp_w = cv::norm(vtd[1] - vtd[0]);
    float tmp_h = cv::norm(vtd[2] - vtd[1]);
    float w = std::max(tmp_w, tmp_h);
    float h = std::min(tmp_w, tmp_h);

    // Step 4: 构造目标四边形坐标（0,0 到 w,h）
    std::array<cv::Point2f, 4> vt = {
        cv::Point2f(0, 0),
        cv::Point2f(w, 0),
        cv::Point2f(w, h),
        cv::Point2f(0, h)
    };

    // Step 5: 获取透视矩阵并变换
    cv::Mat transform = cv::getPerspectiveTransform(vtd, vt);
    cv::warpPerspective(src, dst, transform, cv::Size(w, h));
}
