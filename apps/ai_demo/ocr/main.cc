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
#include "ocr_box.h"
#include "ocr_reco.h"

using std::cerr;
using std::cout;
using std::endl;

std::atomic<bool> isp_stop(false);

void print_usage(const char *name)
{
    cout << "Usage: " << name << "<kmodel_det> <threshold> <box_thresh> <input_mode> <kmodel_reco> <debug_mode>" << endl
         << "Options:" << endl
         << "  kmodel_det      ocr检测kmodel路径\n"
         << "  threshold       置信度阈值:影响检测框的大小，置信度阈值越小，检测框越大，也更容易检测到文字。\n"
         << "  box_thresh      Box阈值：影响检测框的多少，最后输出的检测框分数小于Box阈值的会被剔除，大于Box阈值的会保留，过大的Box阈值导致漏检，过小的Box阈值导致误检。\n" 
         << "  input_mode      本地图片(图片路径)/ 摄像头(None) \n"
         << "  kmodel_reco     ocr识别kmodel路径 \n"
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

    OCRBox ocr_det(argv[1], atof(argv[2]), atof(argv[3]), image_size, debug_mode);
    OCRReco ocr_rec(argv[5],atoi(argv[6]));
    vector<ocr_det_res> results;
    vector<std::string> results_str;
    while(!isp_stop){
        // 创建一个ScopedTiming对象，用于计算总时间
        ScopedTiming st("total time", 1);
        // 从PipeLine中获取一帧数据，并创建tensor
        pl.GetFrame(dump_res);
        input_tensor = host_runtime_tensor::create(typecode_t::dt_uint8, in_shape, { (gsl::byte *)dump_res.virt_addr, compute_size(in_shape) },false, hrt::pool_shared, dump_res.phy_addr).expect("cannot create input tensor");
        hrt::sync(input_tensor, sync_op_t::sync_write_back, true).expect("sync write_back failed");
        //前处理，推理，后处理
        results.clear();
        results_str.clear();
        ocr_det.pre_process(input_tensor);
        ocr_det.inference();
        ocr_det.post_process(results);
        draw_frame.setTo(cv::Scalar(0, 0, 0, 0));
        
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
        for(int i = 0; i < results.size(); i++)
        {
            vector<cv::Point> ori_vec;
            vector<cv::Point2f> sort_vtd(4);
            for(int j = 0; j < 4; j++)
            {
                ori_vec.push_back(results[i].vertices[j]);
            }
            cv::RotatedRect rect = cv::minAreaRect(ori_vec);
            cv::Point2f ver[4];
            rect.points(ver);
            cv::Mat crop;
            ocr_det.warppersp(ori_img, crop, results[i], sort_vtd);
            ocr_rec.pre_process(crop);
            ocr_rec.inference();
            std::string result_reco;
            ocr_rec.post_process(result_reco);
            results_str.push_back(result_reco);
        }
        ocr_det.draw_result(draw_frame, results, results_str);
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
    if (strcmp(argv[4], "None") == 0)
    {
        std::thread thread_isp(video_proc, argv);
        while (getchar() != 'q')
        {
            usleep(10000);
        }
        isp_stop = true;
        thread_isp.join();
    }
    else
    {
        int debug_mode = atoi(argv[6]);
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

        OCRBox ocr_det(argv[1], atof(argv[2]), atof(argv[3]), image_size, debug_mode);
        OCRReco ocr_rec(argv[5],atoi(argv[6]));
        vector<ocr_det_res> results;
        vector<std::string> results_str;
        ocr_det.pre_process(input_tensor);
        ocr_det.inference();
        ocr_det.post_process(results);
        for(int i = 0; i < results.size(); i++)
        {
            vector<cv::Point> ori_vec;
            vector<cv::Point2f> sort_vtd(4);
            for(int j = 0; j < 4; j++)
            {
                ori_vec.push_back(results[i].vertices[j]);
            }
            cv::RotatedRect rect = cv::minAreaRect(ori_vec);
            cv::Point2f ver[4];
            rect.points(ver);
            cv::Mat crop;
            ocr_det.warppersp(ori_img, crop, results[i], sort_vtd);
            ocr_rec.pre_process(crop);
            ocr_rec.inference();
            std::string result_reco;
            ocr_rec.post_process(result_reco);
            results_str.push_back(result_reco);
        }
        ocr_det.draw_result(ori_img, results, results_str);
        cv::imwrite("ocr_det_rec.jpg", ori_img);
    }
    return 0;
}