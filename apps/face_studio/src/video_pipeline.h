#ifndef _PIPELINE_H
#define _PIPELINE_H

// ==============================
// C++ 标准库头文件
// ==============================
#include <chrono>      // 计时、时间点，用于性能统计
#include <fstream>     // 文件读写（日志/数据输出等）
#include <iostream>    // 标准输入输出
#include <thread>      // 线程支持
#include <atomic>      // 原子变量（多线程安全）

// ==============================
// C 标准库 / 系统调用
// ==============================
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>  // mmap/munmap，用于物理地址映射到虚拟地址

// ==============================
// K230 / MPP 多媒体相关头文件
// ==============================
#include "k_module.h"
#include "k_type.h"
#include "k_gsdma_comm.h"
#include "k_vb_comm.h"
#include "k_video_comm.h"
#include "k_sys_comm.h"
#include "mpi_vb_api.h"
#include "mpi_vicap_api.h"
#include "mpi_isp_api.h"
#include "mpi_sys_api.h"
#include "k_vo_comm.h"
#include "k_vicap_comm.h"
#include "mpi_vo_api.h"
#include "k_connector_comm.h"
#include "mpi_connector_api.h"
#include "k_autoconf_comm.h"
#include "mpi_sensor_api.h"
#include "mpi_gsdma_api.h"

// ==============================
// 项目内工具头文件
// ==============================
#include "scoped_timing.h"   // 作用域计时工具，用于性能分析
#include "setting.h"         // 项目配置宏（分辨率、通道号、是否开启 OSD 等）

// ========================================================================================
// DumpRes：用于保存从 VICAP dump 出来的一帧图像的地址信息
// ========================================================================================
typedef struct DumpRes
{
    uintptr_t virt_addr;   // 用户态虚拟地址（CPU 访问，用于 AI 推理/图像处理）
    uintptr_t phy_addr;    // 物理地址（供 DMA/硬件模块使用）
} DumpRes;


// ========================================================================================
// PipeLine：视频采集 + 显示 + AI 推理 + OSD 叠加 的完整管线封装类
//
// 数据流向：
//   Sensor --> VICAP
//                 |--> 通道0 --> VO（实时显示）
//                 |--> 通道1 --> Dump --> AI 推理
//                                   |
//                                   +--> OSD 绘制 --> VO OSD Layer
// ========================================================================================
class CameraManager
{
public:

    // 构造函数
    // @param debug_mode: 是否开启调试模式（影响 ScopedTiming 打印）
    CameraManager(int debug_mode, int csi_num);

    // 析构函数
    ~CameraManager();

    // 创建整个视频管线：
    // 包括 VB、Connector、VO、OSD、Sensor、VICAP、绑定关系等初始化
    int Create();

    // 从 VICAP 的 AI 通道获取一帧图像
    // @param dump_res: 返回该帧的物理地址和虚拟地址
    int GetFrame(DumpRes &dump_res);

    // 释放通过 GetFrame() 获取的帧资源
    // @param dump_res: 需要释放的帧地址信息
    int ReleaseFrame(DumpRes &dump_res);

    // 向 OSD layer 插入一帧（用于叠加 AI 检测结果）
    // @param osd_data: 外部准备好的 OSD 像素数据指针
    // @param show_metrics: 是否绘制推理帧率；启动/清屏帧应关闭
    int InsertFrame(void* osd_data, bool show_metrics = true);

    // 销毁管线，释放所有资源：
    // 包括 OSD、VICAP、VO、VB、内存池、映射地址等
    int Destroy();

private:
    // ============================
    // Video Buffer（VB）相关
    // ============================
    k_vb_config config;   // VB 全局配置结构体（内存池数量、大小等）

    // ============================
    // 屏幕 / Connector 相关
    // ============================
    k_connector_type connector_type; // 显示接口类型（MIPI、HDMI、LCD 等）

    // ============================
    // VO（Video Output）相关
    // ============================
    k_vo_layer_id vi_vo_id;     // 显示摄像头视频的 VO layer ID
    k_vo_layer_attr vi_vo_attr;// VO layer 属性（分辨率、像素格式、旋转等）
    k_s32 vo_dev_id;            // VO 设备 ID

    // ============================
    // OSD（叠加显示层）相关
    // ============================
    k_vo_layer_id osd_vo_id;        // OSD layer ID（覆盖在视频之上）
    k_vo_layer_attr osd_vo_attr;   // OSD layer 属性（ARGB、分辨率等）
    k_u32 osd_pool_id;             // OSD 使用的 VB 内存池 ID
    k_vb_blk_handle handle;        // 从 VB 池申请的内存块句柄
    k_video_frame_info osd_frame_info; // OSD 帧信息（描述物理地址、格式等）
    void *insert_osd_vaddr;        // OSD 内存块的虚拟地址（CPU 写入绘制结果）

    // ============================
    // Sensor & VICAP 采集相关
    // ============================
    k_vicap_sensor_type sensor_type; // 传感器类型（GC2093 等）
    k_vicap_dev vicap_dev;           // VICAP 设备 ID
    k_vicap_chn vicap_chn_to_vo;     // VICAP 通道0：输出到 VO 显示
    k_vicap_chn vicap_chn_to_ai;     // VICAP 通道1：输出给 AI 推理
    k_vicap_chn vicap_chn_to_capture;// VICAP 通道2：固定拍照/录像输入
    k_video_frame_info dump_info;    // dump 帧的元信息（物理地址、尺寸等）

    // ============================
    // 模块绑定（MPP 通道）相关
    // ============================
    k_mpp_chn vicap_mpp_chn;  // VICAP 模块通道描述（源）
    k_mpp_chn vo_mpp_chn;     // VO 模块通道描述（目的）

    // ============================
    // 调试控制
    // ============================
    int debug_mode_ = 0;      // 是否启用调试/计时打印
    int csi_num_ = 2;         // Session-latched sensor: CSI0 or CSI2
};

#endif
