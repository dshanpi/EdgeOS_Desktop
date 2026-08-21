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
#include <iostream>
#include <chrono>
#include <fstream>
#include <thread>

#include "ai_utils.h"
#include "hand_detection.h"
#include "hand_keypoint.h"
#include "dynamic_gesture.h"
#include "video_pipeline.h"

std::atomic<bool> isp_stop(false);

void print_usage(const char *name)
{
	cout << "Usage: " << name << "<kmodel_det> <obj_thresh> <nms_thresh> <kmodel_kp> <kmodel_gesture> <debug_mode>" << endl
		 << "Options:" << endl
         << "  kmodel_det       手掌检测kmodel路径\n"
         << "  obj_thresh       手掌检测阈值\n"
         << "  nms_thresh       手掌检测非极大值抑制阈值\n"
		 << "  kmodel_kp        手势关键点检测kmodel路径\n"
		 << "  kmodel_gesture   动态手势识别kmodel路径\n"
		 << "  debug_mode       是否需要调试, 0、1、2分别表示不调试、简单调试、详细调试\n"
		 << "\n"
		 << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[6]);
    FrameCHWSize image_size={AI_FRAME_CHANNEL,AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    // 创建一个空的Mat对象，用于存储绘制的帧
    cv::Mat draw_frame(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    // 创建一个空的runtime_tensor对象，用于存储输入数据
    runtime_tensor input_tensor;
    dims_t in_shape { 1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH };
    // 创建一个PipeLine对象，用于处理视频流
    PipeLine pl(debug_mode);
    // 初始化PipeLine对象
    pl.Create();
    // 创建一个DumpRes对象，用于存储帧数据
    DumpRes dump_res;
    HandDetection hd(argv[1], atof(argv[2]), atof(argv[3]), image_size, debug_mode);
    HandKeypoint hk(argv[4], image_size,debug_mode);
    DynamicGesture Dag(argv[5], debug_mode);
    std::vector<BoxInfo> results;
    enum state {TRIGGER,UP,RIGHT,DOWN,LEFT,MIDDLE} cur_state_ = TRIGGER, pre_state_ = TRIGGER, draw_state_ = TRIGGER;

    int idx_ = 0;
    int idx = 0;
    std::vector<int> history ={2};
    std::vector<vector<float>> history_logit ;
    std::vector<int> vec_flag;
    std::chrono::steady_clock::time_point m_start; // 计时开始时间
	std::chrono::steady_clock::time_point m_stop;  // 计时结束时间
    std::chrono::steady_clock::time_point s_start; // 图片显示计时开始时间
    std::chrono::steady_clock::time_point s_stop;  // 图片显示计时结束时间
    int bin_width = 150;
    int bin_height = 216;
    cv::Mat shang_argb;
    Utils::bin_2_mat("shang.bin", bin_width, bin_height, shang_argb);
    cv::Mat xia_argb;
    Utils::bin_2_mat("xia.bin", bin_width, bin_height, xia_argb);
    cv::Mat zuo_argb;
    Utils::bin_2_mat("zuo.bin", bin_height, bin_width, zuo_argb);
    cv::Mat you_argb;
    Utils::bin_2_mat("you.bin", bin_height, bin_width, you_argb);
    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        draw_frame.setTo(cv::Scalar(0, 0, 0, 0));
        if (cur_state_== TRIGGER)
        {
            ScopedTiming st("trigger time", atoi(argv[6]));
            results.clear();

            hd.pre_process(input_tensor);
            hd.inference();
            hd.post_process(image_size,results);
            for (auto r: results)
            {
                int w = r.x2 - r.x1 + 1;
                int h = r.y2 - r.y1 + 1;
 
                int length = std::max(w,h)/2;
                int cx = (r.x1+r.x2)/2;
                int cy = (r.y1+r.y2)/2;
                int ratio_num = 1.26*length;

                int x1_1 = std::max(0,cx-ratio_num);
                int y1_1 = std::max(0,cy-ratio_num);
                int x2_1 = std::min(AI_FRAME_WIDTH-1, cx+ratio_num);
                int y2_1 = std::min(AI_FRAME_HEIGHT-1, cy+ratio_num);
                int w_1 = x2_1 - x1_1 + 1;
                int h_1 = y2_1 - y1_1 + 1;
                struct Bbox bbox = {x:x1_1,y:y1_1,w:w_1,h:h_1};
                hk.pre_process(input_tensor,bbox);
                hk.inference();
                hk.post_process(bbox);

                std::vector<double> angle_list = hk.hand_angle();
                std::string gesture = hk.h_gesture(angle_list);

                if ((gesture == "five") ||(gesture == "yeah"))
                {
                    double v1_x = hk.results[24] - hk.results[0];
                    double v1_y = hk.results[25] - hk.results[1];
                    double v2_x = 1.0; 
                    double v2_y = 0.0;

                    // 掌根到中指指尖向量和（1，0）向量的夹角
                    double v1_norm = std::sqrt(v1_x * v1_x + v1_y * v1_y);
                    double v2_norm = std::sqrt(v2_x * v2_x + v2_y * v2_y);
                    double dot_product = v1_x * v2_x + v1_y * v2_y;
                    double cos_angle = dot_product / (v1_norm * v2_norm);
                    double angle = std::acos(cos_angle) * 180 / M_PI;

                    if (v1_y>0)
                    {
                        angle = 360-angle;
                    }

                    if ((70.0<=angle) && (angle<110.0))
                    {
                        if ((pre_state_ != UP) || (pre_state_ != MIDDLE))
                        {
                            vec_flag.push_back(pre_state_);
                        }
                        if ((vec_flag.size()>10)||(pre_state_ == UP) || (pre_state_ == MIDDLE) ||(pre_state_ == TRIGGER))
                        {
                            cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_width,bin_height));
                            shang_argb.copyTo(copy_image); 
                            cur_state_ = UP;
                        }
                    }
                    else if ((110.0<=angle) && (angle<225.0))
                    {
                        // 中指向右(实际方向)
                        if (pre_state_ != RIGHT)
                        {
                            vec_flag.push_back(pre_state_);
                        }
                        if ((vec_flag.size()>10)||(pre_state_ == RIGHT)||(pre_state_ == TRIGGER))
                        {
                            cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_height,bin_width));
                            you_argb.copyTo(copy_image); 
                            cur_state_ = RIGHT;
                        }
                    }
                    else if((225.0<=angle) && (angle<315.0))
                    {
                        if (pre_state_ != DOWN)
                        {
                            vec_flag.push_back(pre_state_);
                        }
                        if ((vec_flag.size()>10)||(pre_state_ == DOWN)||(pre_state_ == TRIGGER))
                        {
                            cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_width,bin_height));
                            xia_argb.copyTo(copy_image); 
                            cur_state_ = DOWN;
                        }
                    }
                    else
                    {
                        if (pre_state_ != LEFT)
                        {
                            vec_flag.push_back(pre_state_);
                        }
                        if ((vec_flag.size()>10)||(pre_state_ == LEFT)||(pre_state_ == TRIGGER))
                        {
                            cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_height,bin_width));
                            zuo_argb.copyTo(copy_image);
                            cur_state_ = LEFT;
                        }
                    }
                    m_start = std::chrono::steady_clock::now();
                }
            }
            history_logit.clear();
        }
        else if (cur_state_ != TRIGGER)
        {
            ScopedTiming st("swipe time", atoi(argv[6]));
            {
                int matsize = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
                void* ptr = reinterpret_cast<void*>(dump_res.virt_addr);
                cv::Mat ori_img;
                {
                    cv::Mat channels[] = {
                        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, reinterpret_cast<void*>(dump_res.virt_addr)),
                        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, reinterpret_cast<void*>(dump_res.virt_addr) + matsize),
                        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, reinterpret_cast<void*>(dump_res.virt_addr) + 2 * matsize)
                    };
                    cv::merge(channels, 3, ori_img);
                }
                Dag.pre_process(ori_img);
                Dag.inference();
                Dag.post_process();
                vector<float> avg_logit;
                {
                    vector<float> output;
                    Dag.get_out(output);
                    history_logit.push_back(output);

                    for(int j=0;j<27;j++)
                    {
                        float sum = 0.0;
                        for (int i=0;i<history_logit.size();i++)
                        {
                            sum += history_logit[i][j];
                        }
                        avg_logit.push_back(sum / history_logit.size());
                    }
                    idx_ = std::distance(avg_logit.begin(), std::max_element(avg_logit.begin(), avg_logit.end()));
                }

                idx = Dag.process_output(idx_, history);
                if (idx!=idx_)
                {
                    vector<float> history_logit_last = history_logit.back();
                    history_logit.clear();
                    history_logit.push_back(history_logit_last);
                }

                if (cur_state_ == UP)
                {
                    cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_width,bin_height));
                    shang_argb.copyTo(copy_image); 
                    if ((idx==15) || (idx==10))
                    {
                        vec_flag.clear();
                        if (((avg_logit[idx] >= 0.7) && (history_logit.size() >= 2)) || ((avg_logit[idx] >= 0.3) && (history_logit.size() >= 4)))
                        {
                            s_start = std::chrono::steady_clock::now();
                            cur_state_ = TRIGGER;
                            draw_state_ = DOWN;
                            history.clear();
                        }
                        pre_state_ = UP;
                    }else if ((idx==25)||(idx==26)) 
                    {
                        vec_flag.clear();
                        if (((avg_logit[idx] >= 0.4) && (history_logit.size() >= 2)) || ((avg_logit[idx] >= 0.3) && (history_logit.size() >= 3)))
                        {
                            s_start = std::chrono::steady_clock::now();
                            cur_state_ = TRIGGER;
                            draw_state_ = MIDDLE;
                            history.clear();
                        }
                        pre_state_ = MIDDLE;
                    }else
                    {
                        history_logit.clear();
                    }
                }
                else if (cur_state_ == RIGHT)
                {
                    cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_height,bin_width));
                    you_argb.copyTo(copy_image); 
                    if  ((idx==16)||(idx==11)) 
                    {
                        vec_flag.clear();
                        if (((avg_logit[idx] >= 0.4) && (history_logit.size() >= 2)) || ((avg_logit[idx] >= 0.3) && (history_logit.size() >= 3)))
                        {
                            s_start = std::chrono::steady_clock::now();
                            cur_state_ = TRIGGER;
                            draw_state_ = RIGHT;
                            history.clear();
                        }
                        pre_state_ = RIGHT;
                    }else
                    {
                        history_logit.clear();
                    }
                }
                else if (cur_state_ == DOWN)
                {
                    cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_width,bin_height));
                    xia_argb.copyTo(copy_image); 
                    if  ((idx==18)||(idx==13))
                    {
                        vec_flag.clear();
                        if (((avg_logit[idx] >= 0.4) && (history_logit.size() >= 2)) || ((avg_logit[idx] >= 0.3) && (history_logit.size() >= 3)))
                        {
                            s_start = std::chrono::steady_clock::now();
                            cur_state_ = TRIGGER;
                            draw_state_ = UP;
                            history.clear();
                        }
                        pre_state_ = DOWN;
                    }else
                    {
                        history_logit.clear();
                    }
                }
                else if (cur_state_ == LEFT)
                {
                    cv::Mat copy_image = draw_frame(cv::Rect(0,0,bin_height,bin_width));
                    zuo_argb.copyTo(copy_image);
                    if ((idx==17)||(idx==12))
                    {
                        vec_flag.clear();
                        if (((avg_logit[idx] >= 0.4) && (history_logit.size() >= 2)) || ((avg_logit[idx] >= 0.3) && (history_logit.size() >= 3)))
                        {
                            s_start = std::chrono::steady_clock::now();
                            cur_state_ = TRIGGER;
                            draw_state_ = LEFT;
                            history.clear();
                        }
                        pre_state_ = LEFT;
                    }else
                    {
                        history_logit.clear();
                    }
                }
            }
            m_stop = std::chrono::steady_clock::now();
			double elapsed_ms = std::chrono::duration<double, std::milli>(m_stop - m_start).count();

            if ((cur_state_ != TRIGGER) &&(elapsed_ms>2000))
            {
                cur_state_ = TRIGGER;
                pre_state_ = TRIGGER;
            }
        }
        s_stop = std::chrono::steady_clock::now();
        double elapsed_ms_show = std::chrono::duration<double, std::milli>(s_stop - s_start).count();
        if (elapsed_ms_show<1000)
        {
            if (draw_state_ == UP)
            {
                cv::putText(draw_frame, "UP", cv::Point(OSD_WIDTH*3/7,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 5, cv::Scalar(0, 195,255, 255), 2);
            } else if (draw_state_ == RIGHT)
            {
                cv::putText(draw_frame, "LEFT", cv::Point(OSD_WIDTH*3/7,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 5, cv::Scalar(0, 195,255, 255), 2);
            }else if (draw_state_ == DOWN)
            {
                cv::putText(draw_frame, "DOWN", cv::Point(OSD_WIDTH*3/7,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 5, cv::Scalar(0, 195,255, 255), 2);
            }else if (draw_state_ == LEFT)
            {
                cv::putText(draw_frame, "RIGHT", cv::Point(OSD_WIDTH*3/7,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 5, cv::Scalar(0, 195,255, 255), 2);
            }else if (draw_state_ == MIDDLE)
            {
                cv::putText(draw_frame, "MIDDLE", cv::Point(OSD_WIDTH*3/7,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 5, cv::Scalar(0, 195,255, 255), 2);
            }

        }else
        {
            draw_state_ = TRIGGER;
        }
        
        // 将绘制的帧插入到PipeLine中
        pl.InsertFrame(draw_frame.data);
        // 释放帧数据
        pl.ReleaseFrame(dump_res);
    }
    pl.Destroy();
}

int main(int argc, char *argv[])
{
    std::cout << "case " << argv[0] << " built at " << __DATE__ << " " << __TIME__ << std::endl;
    if (argc != 7)
    {
        print_usage(argv[0]);
        return -1;
    }
    std::thread thread_isp(video_proc, argv);
    while (getchar() != 'q')
    {
        usleep(10000);
    }
    isp_stop = true;
    thread_isp.join();
    return 0;
}