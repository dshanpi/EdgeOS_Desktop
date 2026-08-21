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
#include "face_plugin.h"
#include "setting.h"
#include "video_pipeline.h"

std::atomic<bool> isp_stop(false);
AiPluginManager g_ai_plugins;

static void return_to_home_session(int csi_num)
{
    static constexpr long kRtSmartExecSyscall = 13;
    const char *home = "/sdcard/app/dshanpi_aimodel";
    char csi_text[4];
    snprintf(csi_text, sizeof(csi_text), "%d", csi_num);
    char *const home_argv[] = {
        const_cast<char *>(home), csi_text, nullptr
    };
    extern char **environ;
    long pid = syscall(kRtSmartExecSyscall, home, 2, home_argv, environ);
    if (pid <= 0) {
        std::cerr << "[session-manager] failed to return home, ret=" << pid
                  << std::endl;
    } else {
        std::cout << "[session-manager] home session pid=" << pid << std::endl;
    }
}

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<kmodel_det> <obj_thres> <nms_thres> <input_mode> <debug_mode>" << endl
         << "Options:" << endl
         << "  kmodel_det      人脸检测kmodel路径\n"
         << "  obj_thres       人脸检测kmodel阈值\n"
         << "  nms_thres       人脸检测kmodel nms阈值\n"
         << "  input_mode      本地图片(图片路径)/ 摄像头(None) \n"
         << "  debug_mode      是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
         << "  csi_num         系统活动摄像头，只允许 0 或 2\n"
         << "\n"
         << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[5]);
    int csi_num = atoi(argv[6]);
    FrameCHWSize image_size={AI_FRAME_CHANNEL,AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    // 创建一个空的Mat对象，用于存储绘制的帧
    cv::Mat draw_frame(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    // 创建一个空的runtime_tensor对象，用于存储输入数据
    runtime_tensor input_tensor;
    dims_t in_shape { 1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH };

    // One Camera Manager exclusively owns Sensor/VICAP/VO for this session.
    CameraManager camera_manager(debug_mode, csi_num);
    int pipeline_ret = camera_manager.Create();
    if (pipeline_ret != 0) {
        std::cerr << "Face Detection camera pipeline creation failed, ret="
                  << pipeline_ret << std::endl;
        isp_stop = true;
        return;
    }
    // 创建一个DumpRes对象，用于存储帧数据
    DumpRes dump_res;
    auto face = std::make_shared<FaceDetectionPlugin>(
        argv[1], atof(argv[2]), atof(argv[3]), image_size, debug_mode);
    g_ai_plugins.add(face);
    g_ai_plugins.activate("face_detection");
    std::cout << "[ai-manager] active plugin: face_detection" << std::endl;

    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        if (camera_manager.GetFrame(dump_res) != 0) {
            usleep(10000);
            continue;
        }
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        // AI plugin can change without restarting Sensor/VICAP/VO.
        g_ai_plugins.process(input_tensor, draw_frame);
        // 将绘制的帧插入到PipeLine中
        camera_manager.InsertFrame(draw_frame.data);
        // 释放帧数据
        camera_manager.ReleaseFrame(dump_res);
    }
    camera_manager.Destroy();
}

int main(int argc, char *argv[])
{
    std::cout << "case " << argv[0] << " built at " << __DATE__ << " " << __TIME__ << std::endl;
    if (argc != 7)
    {
        print_usage(argv[0]);
        return -1;
    }
    if (strcmp(argv[4], "None") == 0)
    {
        int session_csi = atoi(argv[6]);
        std::thread thread_isp(video_proc, argv);
        int command;
        std::cout << "[ai-manager] commands: 1=face, 0=preview only, q=quit"
                  << std::endl;
        while ((command = getchar()) != 'q') {
            if (command == '1') {
                if (g_ai_plugins.activate("face_detection")) {
                    std::cout << "[ai-manager] face_detection activated"
                              << std::endl;
                }
            } else if (command == '0') {
                g_ai_plugins.deactivate();
                std::cout << "[ai-manager] preview-only mode" << std::endl;
            }
            usleep(10000);
        }
        isp_stop = true;
        thread_isp.join();
        return_to_home_session(session_csi);
    }
    else
    {
        int debug_mode = atoi(argv[5]);
        // 读取图片
        cv::Mat ori_img = cv::imread(argv[4]);
        FrameCHWSize image_size={ori_img.channels(),ori_img.rows,ori_img.cols};
         // 创建一个空的向量，用于存储chw图像数据,将读入的hwc数据转换成chw数据
        std::vector<uint8_t> chw_vec;
        std::vector<cv::Mat> bgrChannels(3);
        cv::split(ori_img, bgrChannels);
        for (auto i = 2; i > -1; i--)
        {
            std::vector<uint8_t> data = std::vector<uint8_t>(bgrChannels[i].reshape(1, 1));
            chw_vec.insert(chw_vec.end(), data.begin(), data.end());
        }
        // 创建tensor
        dims_t in_shape { 1, 3, ori_img.rows, ori_img.cols };
        runtime_tensor input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, hrt::pool_shared).expect("cannot create input tensor");
        auto input_buf = input_tensor.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
        memcpy(reinterpret_cast<char *>(input_buf.data()), chw_vec.data(), chw_vec.size());
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("write back input failed");
        FaceDetection fd(argv[1], atof(argv[2]),atof(argv[3]), image_size, debug_mode);
        vector<FaceDetectionInfo> results;
        fd.pre_process(input_tensor);
        fd.inference();
        fd.post_process(image_size,results);
        fd.draw_result(ori_img,results,true);
        cv::imwrite("face_detection_result.jpg", ori_img);
    }
    return 0;
}
