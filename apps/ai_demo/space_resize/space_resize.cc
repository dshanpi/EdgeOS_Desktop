#include <cmath>
#include <opencv2/opencv.hpp>
#include "space_resize.h"

CropResizer::CropResizer(FrameCHWSize image_size){
    first_start_=true;
    image_size_=image_size;
}

void CropResizer::crop_and_draw(cv::Mat& draw_frame, runtime_tensor& input_tensor,std::vector<int> &two_point) {
    if (first_start_) {
        int x1 = two_point[0], y1 = two_point[1];
        int x2 = two_point[2], y2 = two_point[3];
        if (x1 > 0 && x1 < image_size_.width && x2 > 0 && x2 < image_size_.height && y1 > 0 && y1 < image_size_.width && y2 > 0 && y2 < image_size_.height) {
            float dist = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
            two_point_mean_w_ = dist * 0.8f;
            two_point_mean_h_ = dist * 0.8f;
            first_start_ = false;
        }
    }else{
        int w_=draw_frame.cols;
        int h_=draw_frame.rows;

        int x1 = two_point[0], y1 = two_point[1];
        int x2 = two_point[2], y2 = two_point[3];
        if (!(x1 > 0 && x1 < image_size_.width && x2 > 0 && x2 < image_size_.height && y1 > 0 && y1 < image_size_.width && y2 > 0 && y2 < image_size_.height)) {
            return;
        }

        float center_x = (x1 + x2) / 2.0f;
        float center_y = (y1 + y2) / 2.0f;

        // 计算裁剪位置和宽高
        float left_x = std::max(center_x - two_point_mean_w_ / 2.0f, 0.0f);
        float top_y = std::max(center_y - two_point_mean_h_ / 2.0f, 0.0f);
        float crop_w = std::min(two_point_mean_w_, static_cast<float>(image_size_.width) - left_x);
        float crop_h = std::min(two_point_mean_h_, static_cast<float>(image_size_.height) - top_y);

        // 计算当前两指之间的距离，对比初始距离计算比例，即缩放比例
        float dist_now = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
        float ori_new_ratio = dist_now * 0.8f / two_point_mean_w_;

        // 计算最终显示分辨率下的宽高，不超过256
        int new_resize_w = std::min(static_cast<int>(crop_w * ori_new_ratio / image_size_.width * w_), (int)max_new_resize_w_);
        int new_resize_h = std::min(static_cast<int>(crop_h * ori_new_ratio / image_size_.height * h_), (int)max_new_resize_h_);

        dims_t out_shape_crop{1, 3, new_resize_h, new_resize_w};
        FrameCHWSize out_shape={3,new_resize_h,new_resize_w};
        ai2d_out_tensor_ = hrt::create(typecode_t::dt_uint8, out_shape_crop, hrt::pool_shared).expect("create ai2d output tensor failed");
        Utils::crop_resize_set(image_size_,out_shape,left_x,top_y,crop_w,crop_h,ai2d_builder_);
        ai2d_builder_->invoke(input_tensor,ai2d_out_tensor_).expect("error occurred in ai2d running");   
        
        auto out_buf = ai2d_out_tensor_.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_read).unwrap().buffer();
        const uint8_t* output = reinterpret_cast<const uint8_t*>(out_buf.data());
        int crop_area = new_resize_w * new_resize_h;
        std::vector<uint8_t> bgr_buffer(crop_area * 3);

        for (int i = 0; i < crop_area; ++i) {
            uint8_t r = output[i + crop_area * 2];
            uint8_t g = output[i + crop_area];
            uint8_t b = output[i];
            
            bgr_buffer[i * 3 + 0] = r;
            bgr_buffer[i * 3 + 1] = g;
            bgr_buffer[i * 3 + 2] = b;
        }
        cv::Mat bgr_img(new_resize_h, new_resize_w, CV_8UC3, bgr_buffer.data());
        int dst_x = static_cast<int>(left_x / image_size_.width * w_);
        int dst_y = static_cast<int>(top_y / image_size_.height * h_);

        // 取出 draw_frame 中的目标区域（ROI）
        if (dst_x >= 0 && dst_y >= 0 && dst_x + new_resize_w <= w_ && dst_y + new_resize_h <= h_) 
        {
            cv::Mat roi = draw_frame(cv::Rect(dst_x, dst_y, new_resize_w, new_resize_h));
            // 把 BGR 转成 BGRA
            cv::Mat bgra_img;
            cv::cvtColor(bgr_img, bgra_img, cv::COLOR_BGR2BGRA);
            // 拷贝
            bgra_img.copyTo(roi);
        }
    }
}
