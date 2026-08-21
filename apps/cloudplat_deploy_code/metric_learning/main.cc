/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
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
#include "text_paint.h"
#include "video_pipeline.h"
#include "live_ui.h"
#include "metriclearning.h"
#include "vaxp_ai_stream.h"

using std::cerr;
using std::cout;
using std::endl;

std::atomic<bool> isp_stop(false);

namespace {
constexpr uint16_t kPipelineId = 0x0B07;
constexpr uint16_t kModelId = 0x0B07;
const char *const kClasses[] = {"embedding"};
} // namespace

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<config_path> <image_path/None> <debug_mode>" << endl
         << "Options:" << endl
         << "  config_path     部署配置文件deploy_config.json路径\n"
         << "  image_path/None 推理图片路径，当使用视频推理时设置为None"
         << "  debug_mode      是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
         << "\n"
         << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[3]);
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
    TextRenderer writepen;
    writepen.init("/sdcard/app/cloudplat/SourceHanSansSC-Normal-Min.ttf", 25);
     // 参数解析
    std::string config_path(argv[1]);
    config_args args;
    parse_args(config_path,args,debug_mode);
    dshanpi_vaxp_ai_config_t vaxp_config{
        "Cloud Metric Learning", 0, AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION};
    dshanpi_vaxp_ai_start(&vaxp_config);
    dshanpi_vaxp_ai_register_classes(kModelId, kClasses, 1);
    Metriclearning ml(args,image_size,debug_mode);
    writepen.putText(draw_frame, "特征值正在保存成文件在程序运行的同级目录，您可以用它完成下游任务！", cv::Point(20,50), cv::Scalar(255,0, 0, 255));

    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        ml.pre_process(input_tensor);
        ml.inference();
        std::string saved_path;
        const int dimensions = ml.post_process(&saved_path);
        char metadata[160];
        std::snprintf(metadata, sizeof(metadata),
                      "dimensions=%d,file=%s", dimensions,
                      saved_path.c_str());
        dshanpi_vaxp_ai_detection_t result{
            0, 0, 1.0f, 0.0f, 0.0f,
            static_cast<float>(AI_FRAME_WIDTH),
            static_cast<float>(AI_FRAME_HEIGHT), 0,
            "embedding", metadata};
        dshanpi_vaxp_ai_publish_detections(
            kPipelineId, kModelId, VAXP_TASK_CUSTOM,
            "Cloud Metric Learning", 0, &result, 1);
        // 将绘制的帧插入到PipeLine中
        pl.InsertFrame(draw_frame.data);
        // 释放帧数据
        pl.ReleaseFrame(dump_res);
    }
    dshanpi_vaxp_ai_stop();
    pl.Destroy();
}

int main(int argc, char *argv[])
{
    std::cout << "case " << argv[0] << " built at " << __DATE__ << " " << __TIME__ << std::endl;
    if (argc != 4)
    {
        print_usage(argv[0]);
        return -1;
    }

    if (strcmp(argv[2], "None") == 0)
    {
        std::thread thread_isp(video_proc, argv);
        cloud_live_wait_for_exit(isp_stop);
        thread_isp.join();
    }
    else
    {
        // 参数解析
        std::string config_path(argv[1]);
        std::string image_path(argv[2]);
        int debug_mode = atoi(argv[3]);
        // deploy_config.json参数解析
        config_args args;
        parse_args(config_path,args,debug_mode);
        // 读取图片
        cv::Mat ori_img = cv::imread(image_path);
        FrameCHWSize image_size={ori_img.channels(),ori_img.rows,ori_img.cols};
        dshanpi_vaxp_ai_config_t vaxp_config{
            "Cloud Metric Learning", 0,
            static_cast<uint16_t>(image_size.width),
            static_cast<uint16_t>(image_size.height), VAXP_CAP_DETECTION};
        dshanpi_vaxp_ai_start(&vaxp_config);
        dshanpi_vaxp_ai_register_classes(kModelId, kClasses, 1);
        dshanpi_vaxp_ai_announce(kPipelineId, kModelId,
                                 VAXP_TASK_CUSTOM,
                                 "Cloud Metric Learning");
        dshanpi_vaxp_ai_wait_for_subscription(kPipelineId, 500);
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

        Metriclearning ml(args, image_size,debug_mode);
        ml.pre_process(input_tensor);
        ml.inference();
        std::string saved_path;
        const int dimensions = ml.post_process(&saved_path);
        char metadata[160];
        std::snprintf(metadata, sizeof(metadata),
                      "dimensions=%d,file=%s", dimensions,
                      saved_path.c_str());
        dshanpi_vaxp_ai_detection_t result{
            0, 0, 1.0f, 0.0f, 0.0f,
            static_cast<float>(image_size.width),
            static_cast<float>(image_size.height), 0,
            "embedding", metadata};
        dshanpi_vaxp_ai_publish_detections(
            kPipelineId, kModelId, VAXP_TASK_CUSTOM,
            "Cloud Metric Learning", 0, &result, 1);
        std::cout<<"特征已经保存在result_0.bin中"<<std::endl;
        dshanpi_vaxp_ai_stop();
    }
    return 0;
}
