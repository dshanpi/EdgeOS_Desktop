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
#include "video_pipeline.h"
#include "face_detection.h"
#include "face_pose.h"

using std::cerr;
using std::cout;
using std::endl;

std::atomic<bool> isp_stop(false);

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<kmodel_det> <obj_thres> <nms_thres> <kmodel_fp> <warning_amount> <warning_angle_roll> <warning_angle_yaw> <warning_angle_pitch> <debug_mode>" << endl
         << "Options:" << endl
         << "  kmodel_det           人脸检测kmodel路径\n"
         << "  obj_thres            人脸检测阈值\n"
         << "  nms_thres            人脸检测nms阈值\n"
         << "  kmodel_fp            人脸姿态估计kmodel路径\n"
         << "  warning_amount       达到需要提醒注意的帧数\n"
         << "  warning_angle_roll   达到需要提醒注意的滚转角偏离角度\n"
         << "  warning_angle_yaw    达到需要提醒注意的偏航角偏离角度\n"
         << "  warning_angle_pitch  达到需要提醒注意的俯仰角偏离角度\n"
         << "  debug_mode           是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
         << "\n"
         << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[9]);
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
    FacePose fp(argv[4], image_size, debug_mode);
    vector<FaceDetectionInfo> det_results;
    FacePoseInfo pose_result;
    int warning_amount = atoi(argv[5]);
    float warning_angle_roll = atof(argv[6]);
    float warning_angle_yaw = atof(argv[7]);
    float warning_angle_pitch = atof(argv[8]);
    int warning_count = 0;

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
        fd.post_process(image_size,det_results);
        draw_frame.setTo(cv::Scalar(0, 0, 0, 0));
        float max_area_face = 0;
        int max_id_face = -1;
        for (int i = 0; i < det_results.size(); ++i)
        {
            float area_i = det_results[i].bbox.w * det_results[i].bbox.h;
            if (area_i > max_area_face)
            {
                max_area_face = area_i;
                max_id_face = i;
            }
        }
        if (max_id_face != -1)
        {
            fp.pre_process(input_tensor,det_results[max_id_face].bbox);
            fp.inference();
            fp.post_process(pose_result);
            fp.draw_result(draw_frame,det_results[max_id_face].bbox,pose_result,false);
            if (std::abs(pose_result.roll) > warning_angle_roll || std::abs(pose_result.yaw) > warning_angle_yaw || std::abs(pose_result.pitch) > warning_angle_pitch)
            {
                warning_count += 1; 
            }
            else
            {
                warning_count = 0;
            }
            if (warning_count > warning_amount)
            {
                cv::putText(draw_frame, " Please look straight ahead ! ", {0, 100}, cv::FONT_HERSHEY_COMPLEX, 2, cv::Scalar(255, 255, 0, 0), 2, 4, 0);
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
    if (argc != 10)
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