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
#include "video_pipeline.h"

std::atomic<bool> isp_stop(false);


void print_usage(const char *name)
{
	cout << "Usage: " << name << "<kmodel_det> <obj_thresh> <nms_thresh> <kmodel_kp> <guess_mode> <debug_mode>" << endl
		 << "Options:" << endl
		 << "  kmodel_det      手掌检测kmodel路径\n"
         << "  obj_thresh      手掌检测阈值\n"
         << "  nms_thresh      手掌检测非极大值抑制阈值\n"
		 << "  kmodel_kp       手势关键点检测kmodel路径\n"
         << "  guess_mode      石头剪刀布的游戏模式 0(玩家稳赢) 1(玩家必输) 奇数n(n局定输赢)\n"
		 << "  debug_mode      是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
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

    // 读取石头剪刀布的bin文件数据 并且转换为mat类型
    int img_width = 400;
    int img_height = 400;
    cv::Mat image_bu_bgra;
    Utils::bin_2_mat_bgra("bu.bin", img_width, img_height, image_bu_bgra);
    cv::Mat image_shitou_bgra;
    Utils::bin_2_mat_bgra("shitou.bin", img_width, img_height, image_shitou_bgra);
    cv::Mat image_jiandao_bgra;
    Utils::bin_2_mat_bgra("jiandao.bin", img_width, img_height, image_jiandao_bgra);

    // 设置游戏模式
    int MODE = atoi(argv[5]);
    int counts_guess = -1;
    int player_win = 0;
    int k230_win = 0;
    bool sleep_end = false;
    bool set_stop_id = true;
    std::vector<std::string> LIBRARY = {"fist","yeah","five"};

    HandDetection hd(argv[1], atof(argv[2]), atof(argv[3]), image_size, debug_mode);
    HandKeypoint hk(argv[4],image_size, debug_mode);

    cv::Mat draw_frame_tmp(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    std::vector<BoxInfo> results;

    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        draw_frame.setTo(cv::Scalar(0, 0, 0, 0));
        
        cv::Mat osd_frame_out;
        cv::Mat osd_frame_vertical;
        cv::Mat osd_frame_horizontal;

        results.clear();
        hd.pre_process(input_tensor);
        hd.inference();
        hd.post_process(image_size,results);

        float max_area_hand = 0;
        int max_id_hand = -1;
        for (int i = 0; i < results.size(); ++i)
        {
            float area_i = (results[i].x2 - results[i].x1) * (results[i].y2 - results[i].y1);
            if (area_i > max_area_hand)
            {
                max_area_hand = area_i;
                max_id_hand = i;
            }
        }

        std::string gesture = "";
        if (max_id_hand != -1)
        {
            std::string text = hd.labels_[results[max_id_hand].label] + ":" + std::to_string(round(results[max_id_hand].score * 100) / 100.0);
            int w = results[max_id_hand].x2 - results[max_id_hand].x1 + 1;
            int h = results[max_id_hand].y2 - results[max_id_hand].y1 + 1;
            
            
            int length = std::max(w,h)/2;
            int cx = (results[max_id_hand].x1+results[max_id_hand].x2)/2;
            int cy = (results[max_id_hand].y1+results[max_id_hand].y2)/2;
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
            gesture = hk.h_gesture(angle_list);

            std::string text1 = "Gesture: " + gesture;
        }

        cv::Mat copy_ori_image = draw_frame(cv::Rect(20,20,img_width,img_height));
        // 玩家全赢
        if(MODE == 0)
        {
            {
                // 玩家出石头，控制k230出剪刀
                if(gesture == "fist")
                {
                    image_jiandao_bgra.copyTo(copy_ori_image,image_jiandao_bgra); 
                }
                // 玩家出布，控制k230出石头
                else if(gesture == "five")
                {
                    image_shitou_bgra.copyTo(copy_ori_image,image_shitou_bgra); 
                }
                // 玩家出剪刀，控制k230出布
                else if(gesture == "yeah")
                {
                    image_bu_bgra.copyTo(copy_ori_image,image_bu_bgra);  
                }
            }
        }
        // 玩家全输
        else if (MODE == 1)
        {
            {
                // 玩家出石头，控制k230出布
                if(gesture == "fist")
                {
                    image_bu_bgra.copyTo(copy_ori_image,image_bu_bgra); 
                }
                // 玩家出布，控制k230剪刀
                else if(gesture == "five")
                {
                    image_jiandao_bgra.copyTo(copy_ori_image,image_jiandao_bgra);  
                }
                // 玩家出2剪刀，控制k230出石头
                else if(gesture == "yeah")
                {
                    image_shitou_bgra.copyTo(copy_ori_image,image_shitou_bgra); 
                }
            }
        }
        // n局随机
        else
        {
            if(sleep_end)
            {
                usleep(2000000);
                sleep_end = false;
            }

            if(max_id_hand == -1)
            {
                set_stop_id = true;
            }
            // 启动划拳
            if(counts_guess == -1 && gesture != "fist" && gesture != "yeah" && gesture != "five")
            {
                std::string start_txt = " G A M E   S T A R T ";
                cv::putText(draw_frame, start_txt, cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2-50),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0, 0, 255, 255), 4);
                std::string oneset_txt = std::to_string(1) + "  S E T";
                cv::putText(draw_frame, oneset_txt, cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2+50),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0, 150, 255, 255), 4);
            }
            // 完成n轮划拳
            else if(counts_guess == MODE)
            {
                if(k230_win > player_win)
                {
                    cv::putText(draw_frame, "Y O U   L O S E", cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0,0,255,255), 4);
                }
                else if(k230_win < player_win)
                {
                    cv::putText(draw_frame, "Y O U   W I N", cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0,0,255,255), 4);
                }
                else
                {
                    cv::putText(draw_frame, "T I E   G A M E", cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(0,0,255,255), 4);
                }
                counts_guess = -1;
                player_win = 0;
                k230_win = 0;
                sleep_end = true;
            }
            else
            {
                if(set_stop_id)
                {
                    if(counts_guess == -1 && (gesture == "fist" || gesture == "yeah" || gesture == "five"))
                    {
                        counts_guess = 0;
                    }

                    if(counts_guess != -1 && (gesture == "fist" || gesture == "yeah" || gesture == "five"))
                    {
                        // 获取k230出拳的随机数，0,1,2
                        int k230_guess=rand()%3;
                        if(gesture == "fist" && LIBRARY[k230_guess] == "yeah")
                        {
                            player_win += 1;
                        }
                        else if(gesture == "fist" && LIBRARY[k230_guess] == "five")
                        {
                            k230_win += 1;
                        }
                        if(gesture == "yeah" && LIBRARY[k230_guess] == "fist")
                        {
                            k230_win += 1;
                        }
                        else if(gesture == "yeah" && LIBRARY[k230_guess] == "five")
                        {
                            player_win += 1;
                        }
                        if(gesture == "five" && LIBRARY[k230_guess] == "fist")
                        {
                            player_win += 1;
                        }
                        else if(gesture == "five" && LIBRARY[k230_guess] == "yeah")
                        {
                            k230_win += 1;
                        }

                        if(LIBRARY[k230_guess] == "fist")
                        {
                            image_shitou_bgra.copyTo(copy_ori_image,image_shitou_bgra);  
                        }
                        else if(LIBRARY[k230_guess] == "five")
                        {
                            image_bu_bgra.copyTo(copy_ori_image,image_bu_bgra);  
                        }
                        else if(LIBRARY[k230_guess] == "yeah")
                        {
                            image_jiandao_bgra.copyTo(copy_ori_image,image_jiandao_bgra);  
                        }
                        counts_guess += 1;

                        cv::putText(draw_frame, std::to_string(counts_guess) + "  S E T", cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(255,150,0, 255), 4);
                        draw_frame_tmp = draw_frame;
                        set_stop_id = false;
                        sleep_end = true;
                    }
                    else
                    {
                        cv::putText(draw_frame, std::to_string(counts_guess + 1) + "  S E T", cv::Point(OSD_WIDTH/2-100,OSD_HEIGHT/2),cv::FONT_HERSHEY_COMPLEX, 1, cv::Scalar(255,150,0, 255), 4);
                    }
                }
                else
                {
                    draw_frame = draw_frame_tmp;
                }
            }
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