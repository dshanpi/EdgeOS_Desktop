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

#include "licence_det.h"

LicenceDetect::LicenceDetect(char *kmodel_file, float obj_thresh, float nms_thresh, FrameCHWSize image_size, int debug_mode)
:obj_thresh(obj_thresh), nms_thresh(nms_thresh), AIBase(kmodel_file,"LicenceDetect", debug_mode)
{
    model_name_ = "LicenceDetect";
    anchors = (input_shapes_[0][2] == 320 ? anchors320 : anchors640);
    min_size = (input_shapes_[0][2] == 320 ? 200 : 800);
	image_size_=image_size;
    input_size_={input_shapes_[0][1], input_shapes_[0][2],input_shapes_[0][3]};
    ai2d_out_tensor_=get_input_tensor(0);
    Utils::resize_set(image_size_,input_size_,ai2d_builder_);
}

LicenceDetect::~LicenceDetect()
{
}

void LicenceDetect::pre_process(runtime_tensor &input_tensor){
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void LicenceDetect::inference()
{
    this->run();
    this->get_output();
}

int nms_comparator(const void *pa, const void *pb)
{
	sortable_obj_t a = *(sortable_obj_t*)pa;
	sortable_obj_t b = *(sortable_obj_t*)pb;
	float diff = a.probs[a.index] - b.probs[b.index];

	if (diff < 0)
		return 1;
	else if (diff > 0)
		return -1;
	return 0;
}

void LicenceDetect::post_process(vector<BoxPoint> &results)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);

    float* loc0 = p_outputs_[0];
    float* loc1 = p_outputs_[1];
    float* loc2 = p_outputs_[2];
    float* conf0 = p_outputs_[3];
    float* conf1 = p_outputs_[4];
    float* conf2 = p_outputs_[5];
    float* landms0 = p_outputs_[6];
    float* landms1 = p_outputs_[7];
    float* landms2 = p_outputs_[8];

    float box[LOC_SIZE] = { 0.0 };
	float landms[LAND_SIZE] = { 0.0 };
	int objs_num = min_size * (1 + 4 + 16);
	sortable_obj_t* s = (sortable_obj_t*)malloc(objs_num * sizeof(sortable_obj_t));
	float* s_probs = (float*)malloc(objs_num * sizeof(float));
	int obj_cnt = 0;
	deal_conf(conf0, s_probs, s, 16 * min_size / 2, obj_cnt);
	deal_conf(conf1, s_probs, s, 4 * min_size / 2, obj_cnt);
	deal_conf(conf2, s_probs, s, 1 * min_size / 2, obj_cnt);
	float* boxes = (float*)malloc(objs_num * LOC_SIZE * sizeof(float));
	obj_cnt = 0;
	deal_loc(loc0, boxes, 16 * min_size / 2, obj_cnt);
	deal_loc(loc1, boxes, 4 * min_size / 2, obj_cnt);
	deal_loc(loc2, boxes, 1 * min_size / 2, obj_cnt);
	float* landmarks = (float*)malloc(objs_num * LAND_SIZE * sizeof(float));
	obj_cnt = 0;
	deal_landms(landms0, landmarks, 16 * min_size / 2, obj_cnt);
	deal_landms(landms1, landmarks, 4 * min_size / 2, obj_cnt);
	deal_landms(landms2, landmarks, 1 * min_size / 2, obj_cnt);
	for (uint32_t oo = 0; oo < objs_num; oo++)
	{
		s[oo].probs = s_probs;
	}
	qsort(s, objs_num, sizeof(sortable_obj_t), nms_comparator);

	std::vector<Bbox> valid_box;
	std::vector<landmarks_t> valid_landmarks;
	int iou_cal_times = 0;
	int i, j, k, obj_index;
	for (i = 0; i < objs_num; ++i)
	{
		obj_index = s[i].index;
		if (s_probs[obj_index] < obj_thresh)
			continue;
		Bbox a = get_box(boxes, obj_index);
		landmarks_t l = get_landmark(landmarks, obj_index);
		valid_box.push_back(a);
		valid_landmarks.push_back(l);

		for (j = i + 1; j < objs_num; ++j)
		{
			obj_index = s[j].index;
			if (s_probs[obj_index] < obj_thresh)
				continue;
			Bbox b = get_box(boxes, obj_index);
			iou_cal_times += 1;
			if (box_iou(a, b) >= nms_thresh)
				s_probs[obj_index] = 0;
		}
	}

	float x1, x2, y1, y2;
	BoxPoint boxPoint;
	for (auto l : valid_landmarks)
	{
		boxPoint.vertices[0].x = l.points[2 * 0 + 0] * image_size_.width;
        boxPoint.vertices[0].y = l.points[2 * 0 + 1] * image_size_.height;
        boxPoint.vertices[1].x = l.points[2 * 1 + 0] * image_size_.width;
        boxPoint.vertices[1].y = l.points[2 * 1 + 1] * image_size_.height;
        boxPoint.vertices[2].x = l.points[2 * 2 + 0] * image_size_.width;
        boxPoint.vertices[2].y = l.points[2 * 2 + 1] * image_size_.height;
        boxPoint.vertices[3].x = l.points[2 * 3 + 0] * image_size_.width;
        boxPoint.vertices[3].y = l.points[2 * 3 + 1] * image_size_.height;
        results.push_back(boxPoint);
	}
    free(s_probs);
	free(boxes);
	free(landmarks);
	free(s);
}

void LicenceDetect::draw_result(cv::Mat& draw_frame, vector<BoxPoint>& results)
{
	int w_=draw_frame.cols;
    int h_=draw_frame.rows;
    for(int i = 0; i < results.size(); i++)
    {   
        std::vector<cv::Point> vec;
        vec.clear();
        for(int j = 0; j < 4; j++)
        {
            vec.push_back(results[i].vertices[j]);
        }
        cv::RotatedRect rect = minAreaRect(vec);
        cv::Point2f ver[4];
        rect.points(ver);
        for(int i = 0; i < 4; i++){
			int x1 = int(ver[i].x * w_ / image_size_.width);
            int y1 = int(ver[i].y * h_ / image_size_.height);
			int x2 = int(ver[(i + 1) % 4].x * w_ / image_size_.width);
            int y2 = int(ver[(i + 1) % 4].y * h_ / image_size_.height);
            cv::line(draw_frame,cv::Point2d(x1,y1),cv::Point2d(x2,y2), cv::Scalar(255, 0, 0,255), 3);
		}
			
    }
}

void LicenceDetect::local_softmax(float* x, float* dx, uint32_t len)
{
	float max_value = x[0];
	for (uint32_t i = 0; i < len; i++)
	{
		if (max_value < x[i])
		{
			max_value = x[i];
		}
	}
	for (uint32_t i = 0; i < len; i++)
	{
		x[i] -= max_value;
		x[i] = expf(x[i]);
	}
	float sum_value = 0.0f;
	for (uint32_t i = 0; i < len; i++)
	{
		sum_value += x[i];
	}
	for (uint32_t i = 0; i < len; i++)
	{
		dx[i] = x[i] / sum_value;
	}
}

int LicenceDetect::argmax(float* x, uint32_t len)
{
	float max_value = x[0];
	int max_index = 0;
	for (uint32_t ll = 1; ll < len; ll++)
	{
		if (max_value < x[ll])
		{
			max_value = x[ll];
			max_index = ll;
		}
	}
	return max_index;
}

void LicenceDetect::deal_conf(float* conf, float* s_probs, sortable_obj_t* s, int size, int& obj_cnt)
{
	float prob[CONF_SIZE] = { 0.0 };
	for (uint32_t ww = 0; ww < size; ww++)
	{
		for (uint32_t hh = 0; hh < 2; hh++)
		{
			for (uint32_t cc = 0; cc < CONF_SIZE; cc++)
			{
				prob[cc] = conf[(hh * CONF_SIZE + cc) * size + ww];
			}
			local_softmax(prob, prob, 2);
			s[obj_cnt].index = obj_cnt;
			s_probs[obj_cnt] = prob[1];
			obj_cnt += 1;
		}
	}
}

void LicenceDetect::deal_loc(float* loc, float* boxes, int size, int& obj_cnt)
{
	for (uint32_t ww = 0; ww < size; ww++)
	{
		for (uint32_t hh = 0; hh < 2; hh++)
		{
			for (uint32_t cc = 0; cc < LOC_SIZE; cc++)
			{
				boxes[obj_cnt * LOC_SIZE + cc] = loc[(hh * LOC_SIZE + cc) * size + ww];
			}
			obj_cnt += 1;
		}
	}
}

void LicenceDetect::deal_landms(float* landms, float* landmarks, int size, int& obj_cnt)
{
	for (uint32_t ww = 0; ww < size; ww++)
	{
		for (uint32_t hh = 0; hh < 2; hh++)
		{
			for (uint32_t cc = 0; cc < LAND_SIZE; cc++)
			{
				landmarks[obj_cnt * LAND_SIZE + cc] = landms[(hh * LAND_SIZE + cc) * size + ww];
			}
			obj_cnt += 1;
		}
	}
}

Bbox LicenceDetect::get_box(float* boxes, int obj_index)
{
	float x, y, w, h;
	x = boxes[obj_index * LOC_SIZE + 0];
	y = boxes[obj_index * LOC_SIZE + 1];
	w = boxes[obj_index * LOC_SIZE + 2];
	h = boxes[obj_index * LOC_SIZE + 3];
	x = anchors[obj_index][0] + x * 0.1 * anchors[obj_index][2];
	y = anchors[obj_index][1] + y * 0.1 * anchors[obj_index][3];
	w = anchors[obj_index][2] * expf(w * 0.2);
	h = anchors[obj_index][3] * expf(h * 0.2);
	Bbox box;
	box.x = x;
	box.y = y;
	box.w = w;
	box.h = h;
	return box;
}

landmarks_t LicenceDetect::get_landmark(float* landmarks, int obj_index)
{
	landmarks_t landmark;
	for (uint32_t ll = 0; ll < 4; ll++)
	{
		landmark.points[2 * ll + 0] = anchors[obj_index][0] + landmarks[obj_index * LAND_SIZE + 2 * ll + 0] * 0.1 * anchors[obj_index][2];
		landmark.points[2 * ll + 1] = anchors[obj_index][1] + landmarks[obj_index * LAND_SIZE + 2 * ll + 1] * 0.1 * anchors[obj_index][3];
	}
	return landmark;
}

float LicenceDetect::overlap(float x1, float w1, float x2, float w2)
{
	float l1 = x1 - w1 / 2;
	float l2 = x2 - w2 / 2;
	float left = l1 > l2 ? l1 : l2;
	float r1 = x1 + w1 / 2;
	float r2 = x2 + w2 / 2;
	float right = r1 < r2 ? r1 : r2;

	return right - left;
}

float LicenceDetect::box_intersection(Bbox a, Bbox b)
{
	float w = overlap(a.x, a.w, b.x, b.w);
	float h = overlap(a.y, a.h, b.y, b.h);

	if (w < 0 || h < 0)
		return 0;
	return w * h;
}

float LicenceDetect::box_union(Bbox a, Bbox b)
{
	float i = box_intersection(a, b);
	float u = a.w * a.h + b.w * b.h - i;

	return u;
}

float LicenceDetect::box_iou(Bbox a, Bbox b)
{
	return box_intersection(a, b) / box_union(a, b);
}


