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
#include "ocr_box.h"
#include "ocr_reco.h"
#include "vaxp_ai_stream.h"

using std::cerr;
using std::cout;
using std::endl;

std::atomic<bool> isp_stop(false);

namespace {
constexpr uint16_t kPipelineId = 0x0B06;
constexpr uint16_t kModelId = 0x0B06;

void publish_ocr_results(const vector<ocr_det_res> &boxes,
                         const vector<string> &texts)
{
    std::vector<dshanpi_vaxp_ai_ocr_t> wire;
    wire.reserve(boxes.size());
    for (size_t index = 0; index < boxes.size(); ++index) {
        dshanpi_vaxp_ai_ocr_t item{};
        item.result_id = static_cast<uint32_t>(index + 1);
        for (size_t point = 0; point < 4; ++point) {
            item.points[point * 2] = boxes[index].vertices[point].x;
            item.points[point * 2 + 1] = boxes[index].vertices[point].y;
        }
        item.confidence = boxes[index].score;
        item.text = index < texts.size() ? texts[index].c_str() : "";
        wire.push_back(item);
    }
    dshanpi_vaxp_ai_publish_ocr(kPipelineId, kModelId,
                                "Cloud OCR Pipeline", 0,
                                wire.data(), wire.size());
}
} // namespace

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<ocrdet_config_path> <ocrrec_config_path> <image_path> <debug_mode>" << endl
         << "Options:" << endl
         << "  ocrdet_config_path     OCR检测deploy_config.json路径\n"
         << "  ocrrec_config_path     OCR识别deploy_config.json路径\n"
         << "  image_path/None 推理图片路径，当使用视频推理时设置为None\n"
         << "  debug_mode      是否需要调试，0、1、2分别表示不调试、简单调试、详细调试\n"
         << "\n"
         << endl;
}

void video_proc(char *argv[])
{
    int debug_mode = atoi(argv[4]);
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
    std::string ocrdet_config_path(argv[1]);
    std::string ocrrec_config_path(argv[2]);
    config_args ocrdet_args;
    parse_args(ocrdet_config_path,ocrdet_args,debug_mode);
    config_args ocrrec_args;
    parse_args(ocrrec_config_path,ocrrec_args,debug_mode);
    dshanpi_vaxp_ai_config_t vaxp_config{
        "Cloud OCR Pipeline", 0, AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_OCR | VAXP_CAP_MULTI_MODEL};
    dshanpi_vaxp_ai_start(&vaxp_config);
    OCRBox ocr_det(ocrdet_args, image_size,debug_mode);
    OCRReco ocr_rec(ocrrec_args,image_size,debug_mode);
    // OCR检测结果
    vector<ocr_det_res> ocrdet_results;
    // OCR识别结果
    vector<string> ocrrec_results;
    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        // OCR检测得到检测框
        ocr_det.pre_process(input_tensor);
        ocr_det.inference();
        ocrdet_results.clear();
        ocrrec_results.clear();
        ocr_det.post_process(ocrdet_results);
        draw_frame = cv::Scalar(0, 0, 0, 0);
        cv::Mat ori_img;
        void* vaddr=reinterpret_cast<void*>(dump_res.virt_addr);
        cv::Mat ori_img_R = cv::Mat(image_size.height, image_size.width, CV_8UC1, vaddr);
        cv::Mat ori_img_G = cv::Mat(image_size.height, image_size.width, CV_8UC1, vaddr + image_size.width * image_size.height);
        cv::Mat ori_img_B = cv::Mat(image_size.height, image_size.width, CV_8UC1, vaddr + 2 * image_size.width * image_size.height);
        std::vector<cv::Mat> sensor_rgb;
        sensor_rgb.push_back(ori_img_B);
        sensor_rgb.push_back(ori_img_G);
        sensor_rgb.push_back(ori_img_R);
        cv::merge(sensor_rgb, ori_img);
        for(int i = 0; i < ocrdet_results.size(); i++)
        {
            vector<cv::Point> ori_vec;
            vector<cv::Point2f> sort_vtd(4);
            for(int j = 0; j < 4; j++)
            {
                ori_vec.push_back(ocrdet_results[i].vertices[j]);
            }
            cv::RotatedRect rect = cv::minAreaRect(ori_vec);
            cv::Point2f ver[4];
            rect.points(ver);
            cv::Mat crop;
            ocr_det.warppersp(ori_img, crop, ocrdet_results[i], sort_vtd);
            ocr_rec.pre_process(crop);
            ocr_rec.inference();
            std::string result_reco;
            ocr_rec.post_process(result_reco);
            ocrrec_results.push_back(result_reco);
        }
        publish_ocr_results(ocrdet_results, ocrrec_results);
        OCRBox::draw_result(draw_frame,ocrdet_results,ocrrec_results,image_size,writepen);
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
    if (argc != 5)
    {
        print_usage(argv[0]);
        return -1;
    }

    if (strcmp(argv[3], "None") == 0)
    {
        std::thread thread_isp(video_proc, argv);
        cloud_live_wait_for_exit(isp_stop);
        thread_isp.join();
    }
    else
    {
        TextRenderer writepen;
        writepen.init("/sdcard/app/cloudplat/SourceHanSansSC-Normal-Min.ttf", 25);
        // 参数解析
        std::string ocrdet_config_path(argv[1]);
        std::string ocrrec_config_path(argv[2]);
        std::string image_path(argv[3]);
        int debug_mode = atoi(argv[4]);
        // deploy_config.json参数解析
        config_args ocrdet_args;
        parse_args(ocrdet_config_path,ocrdet_args,debug_mode);
        config_args ocrrec_args;
        parse_args(ocrrec_config_path,ocrrec_args,debug_mode);

        // 读取图片
        cv::Mat ori_img = cv::imread(image_path);
        FrameCHWSize image_size={ori_img.channels(),ori_img.rows,ori_img.cols};
        dshanpi_vaxp_ai_config_t vaxp_config{
            "Cloud OCR Pipeline", 0,
            static_cast<uint16_t>(image_size.width),
            static_cast<uint16_t>(image_size.height),
            VAXP_CAP_OCR | VAXP_CAP_MULTI_MODEL};
        dshanpi_vaxp_ai_start(&vaxp_config);
        dshanpi_vaxp_ai_announce(kPipelineId, kModelId,
                                 VAXP_TASK_OCR_RECOGNIZE,
                                 "Cloud OCR Pipeline");
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

        OCRBox ocr_det(ocrdet_args, image_size,debug_mode);
        OCRReco ocr_rec(ocrrec_args, image_size,debug_mode);
        // OCR检测结果
        vector<ocr_det_res> ocrdet_results;
        // OCR识别结果
        vector<string> ocrrec_results;
        // OCR检测得到检测框
        ocr_det.pre_process(input_tensor);
        ocr_det.inference();
        ocrdet_results.clear();
        ocrrec_results.clear();
        ocr_det.post_process(ocrdet_results);

        for(int i = 0; i < ocrdet_results.size(); i++)
        {
            vector<cv::Point> ori_vec;
            vector<cv::Point2f> sort_vtd(4);
            for(int j = 0; j < 4; j++)
            {
                ori_vec.push_back(ocrdet_results[i].vertices[j]);
            }
            cv::RotatedRect rect = cv::minAreaRect(ori_vec);
            cv::Point2f ver[4];
            rect.points(ver);
            cv::Mat crop;
            ocr_det.warppersp(ori_img, crop, ocrdet_results[i], sort_vtd);
            ocr_rec.pre_process(crop);
            ocr_rec.inference();
            std::string result_reco;
            ocr_rec.post_process(result_reco);
            ocrrec_results.push_back(result_reco);
        }
        publish_ocr_results(ocrdet_results, ocrrec_results);
        OCRBox::draw_result(ori_img,ocrdet_results,ocrrec_results,image_size,writepen);
        cv::imwrite("/sdcard/ocr_result.jpg",ori_img);  
        dshanpi_vaxp_ai_stop();
    }
    return 0;
}
