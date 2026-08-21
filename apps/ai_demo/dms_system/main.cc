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
#include <thread>
#include "ai_utils.h"
#include "face_detection.h"
#include "hand_detection.h"
#include "video_pipeline.h"

using std::cerr;
using std::cout;
using std::endl;

std::atomic<bool> isp_stop(false);

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<face_kmodel_det> <face_obj_thres> <face_nms_thres> <hand_kmodel_det> <hand_obj_thresh> <hand_nms_thresh> <init_area> <init_len> <warning_amount> <debug_mode>" << endl
         << "Options:" << endl
         << "  face_kmodel_det      人脸检测kmodel路径\n"
         << "  face_obj_thres       人脸检测kmodel阈值\n"
         << "  face_nms_thres       人脸检测kmodel nms阈值\n"
		 << "  hand_kmodel_det      手掌检测kmodel路径\n"
         << "  hand_obj_thresh      手掌检测阈值\n"
         << "  hand_nms_thresh      手掌检测非极大值抑制阈值\n"
         << "  init_area            定义基准人脸面积\n"
         << "  init_len             定义基准距离长度\n"
         << "  warning_amount       达到需要提醒注意的帧数\n"
         << "  debug_mode      是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
         << "\n"
         << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[10]);
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
    // 创建FaceDetection实例
    FaceDetection fd(argv[1], atof(argv[2]),atof(argv[3]), image_size, debug_mode);
    HandDetection hd(argv[4], atof(argv[5]), atof(argv[6]), image_size,debug_mode);
    vector<FaceDetectionInfo> face_results;
    vector<BoxInfo> hand_results;
    float init_area = atof(argv[7]);
    float init_len = atof(argv[8]);
    float now_area;
    float now_len;
    int warning_amount = atoi(argv[9]);
    int warning_count_smk_drk = 0;
    int warning_count_drk = 0;
    int warning_count_call = 0;

    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        fd.pre_process(input_tensor);
        fd.inference();
        fd.post_process(image_size,face_results);

        hd.pre_process(input_tensor);
        hd.inference();
        hd.post_process(image_size,hand_results);
        draw_frame.setTo(cv::Scalar(0, 0, 0, 0));
        float max_area_face = 0;
        int max_id_face = -1;
        for (int i = 0; i < face_results.size(); ++i)
        {
            float area_i = face_results[i].bbox.w * face_results[i].bbox.h;
            if (area_i > max_area_face)
            {
                max_area_face = area_i;
                max_id_face = i;
            }
        }

        float max_area_hand = 0;
        int max_id_hand = -1;
        for (int i = 0; i < hand_results.size(); ++i)
        {
            float area_i = (hand_results[i].x2 - hand_results[i].x1) * (hand_results[i].y2 - hand_results[i].y1);
            if (area_i > max_area_hand)
            {
                max_area_hand = area_i;
                max_id_hand = i;
            }
        }

        string text_dms_call = "Calling : No ";
        string text_dms_smoke_drink = "Smoking or Drinking : No ";
        if (max_id_face != -1 && max_id_hand != -1)
        {
            float face_x = face_results[max_id_face].bbox.x + (face_results[max_id_face].bbox.w / 2.0);
            float face_y = face_results[max_id_face].bbox.y + (face_results[max_id_face].bbox.h / 2.0);
            float hand_x = (hand_results[max_id_hand].x1 + hand_results[max_id_hand].x2) / 2.0;
            float hand_y = (hand_results[max_id_hand].y1 + hand_results[max_id_hand].y2) / 2.0;

            now_area = max_area_face;
            now_len = sqrt(pow(face_x - hand_x, 2) + pow(face_y - hand_y, 2));

            if (now_len < (now_area / init_area) * init_len)
            {
                if (face_y < hand_y && (abs(face_y - hand_y) / abs(face_x - hand_x)) > 1)
                {
                    warning_count_smk_drk += 1;
                    warning_count_drk = 0;
                    warning_count_call = 0;
                }
                else if (face_y > hand_y && (abs(face_y - hand_y) / abs(face_x - hand_x)) > 0.8)
                {
                    warning_count_smk_drk = 0;
                    warning_count_drk += 1;
                    warning_count_call = 0;
                }
                else
                {
                    warning_count_smk_drk = 0;
                    warning_count_drk = 0;
                    warning_count_call += 1;
                }

                if (warning_count_smk_drk > warning_amount)
                {
                    text_dms_smoke_drink = "Smoking or Drinking : Yes ";
                    cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
                    cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,0, 255, 255), 2, 4, 0);
                }
                else if (warning_count_drk > warning_amount)
                {
                    text_dms_smoke_drink = "Drinking : Yes ";
                    cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255, 0, 255), 2, 4, 0);
                    cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,0, 255, 255), 2, 4, 0);
                }
                else if (warning_count_call > warning_amount)
                {
                    text_dms_call = "Calling : Yes ";
                    cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,0, 255, 255), 2, 4, 0);
                    cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255, 0, 255), 2, 4, 0);
                }
                else
                {
                    cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
                    cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
                }
            }
            else
            {
                warning_count_smk_drk = 0;
                warning_count_drk = 0;
                warning_count_call = 0;
                cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
                cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
            }


            int w = hand_results[max_id_hand].x2 - hand_results[max_id_hand].x1 + 1;
            int h = hand_results[max_id_hand].y2 - hand_results[max_id_hand].y1 + 1;
            
            int rect_x = hand_results[max_id_hand].x1/ AI_FRAME_WIDTH * OSD_WIDTH;
            int rect_y = hand_results[max_id_hand].y1/ AI_FRAME_HEIGHT * OSD_HEIGHT;
            int rect_w = (float)w / AI_FRAME_WIDTH * OSD_WIDTH;
            int rect_h = (float)h / AI_FRAME_HEIGHT  * OSD_HEIGHT;
            cv::rectangle(draw_frame, cv::Rect(rect_x, rect_y , rect_w, rect_h), cv::Scalar( 255, 255, 0, 255), 6, 2, 0);
        }
        else
        {
            warning_count_smk_drk = 0;
            warning_count_drk = 0;
            warning_count_call = 0;
            cv::putText(draw_frame, text_dms_call, cv::Point(20,50),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
            cv::putText(draw_frame, text_dms_smoke_drink, cv::Point(20,100),cv::FONT_HERSHEY_COMPLEX, 1.5, cv::Scalar(0,255,0, 255), 2, 4, 0);
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
    if (argc != 11)
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