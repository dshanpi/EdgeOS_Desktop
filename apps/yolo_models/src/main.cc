#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include "drv_touch.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/mode_persistence.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "utils.h"
#include "yolov5.h"
#include "yolov8.h"
#include "yolo11.h"
#include "yolo26.h"
#include "vaxp_ai_stream.h"

enum class Mode { Yolo5, Yolo8, Yolo11, Yolo26, Count };
static const char *names[] = {"YOLOv5", "YOLOv8", "YOLO11", "YOLO26"};
static std::atomic<bool> stop_flag(false), menu_flag(false), switch_flag(false);
static std::atomic<Mode> requested(Mode::Yolo8);
static Mode active = Mode::Yolo8;

static void publish_results(const std::vector<YOLOBbox> &results,
                            const std::vector<std::string> &labels)
{
    std::vector<dshanpi_vaxp_ai_detection_t> wire;
    wire.reserve(results.size());
    for (const auto &result : results) {
        const size_t label = result.index >= 0 &&
                             static_cast<size_t>(result.index) < labels.size()
                                 ? static_cast<size_t>(result.index) : 0u;
        wire.push_back({
            static_cast<uint16_t>(label), 0, result.confidence,
            static_cast<float>(result.box.x),
            static_cast<float>(result.box.y),
            static_cast<float>(result.box.width),
            static_cast<float>(result.box.height), 0,
            label < labels.size() ? labels[label].c_str() : "unknown"});
    }
    const uint16_t mode_id = static_cast<uint16_t>(active) + 1u;
    dshanpi_vaxp_ai_publish_detections(
        static_cast<uint16_t>(0x0800u + mode_id),
        static_cast<uint16_t>(0x0800u + mode_id),
        VAXP_TASK_DETECTION, names[static_cast<int>(active)], 0,
        wire.data(), wire.size());
}

class Runner {
public:
    virtual ~Runner() = default;
    virtual void process(runtime_tensor &, cv::Mat &) = 0;
};

template <typename T> class StandardRunner final : public Runner {
public:
    StandardRunner(const std::string &path, FrameSize size,
                   const std::vector<std::string> &labels, int debug)
        : model_(const_cast<char *>("detect"), const_cast<char *>("video"),
                 const_cast<char *>(path.c_str()), .35f, .65f, .5f,
                 labels, size, 17, 3, debug), labels_(labels) {}
    void process(runtime_tensor &input, cv::Mat &osd) override {
        std::vector<YOLOBbox> results;
        model_.pre_process(input);
        model_.inference();
        model_.post_process(results);
        model_.draw_results(osd, results);
        publish_results(results, labels_);
    }
private:
    T model_;
    std::vector<std::string> labels_;
};

class Yolo5Runner final : public Runner {
public:
    Yolo5Runner(const std::string &path, FrameSize size,
                const std::vector<std::string> &labels, int debug)
        : model_(const_cast<char *>("detect"), const_cast<char *>("video"),
                 const_cast<char *>(path.c_str()), .35f, .65f, .5f,
                 labels, size, debug), labels_(labels) {}
    void process(runtime_tensor &input, cv::Mat &osd) override {
        std::vector<YOLOBbox> results;
        model_.pre_process(input); model_.inference();
        model_.post_process(results); model_.draw_results(osd, results);
        publish_results(results, labels_);
    }
private:
    Yolov5 model_;
    std::vector<std::string> labels_;
};

class Yolo26Runner final : public Runner {
public:
    Yolo26Runner(const std::string &path, FrameSize size,
                 const std::vector<std::string> &labels, int debug)
        : model_(const_cast<char *>("detect"), const_cast<char *>("video"),
                 const_cast<char *>(path.c_str()), .35f, .5f,
                 labels, size, 17, 3, debug), labels_(labels) {}
    void process(runtime_tensor &input, cv::Mat &osd) override {
        std::vector<YOLOBbox> results;
        model_.pre_process(input); model_.inference();
        model_.post_process(results); model_.draw_results(osd, results);
        publish_results(results, labels_);
    }
private:
    Yolo26 model_;
    std::vector<std::string> labels_;
};

static std::unique_ptr<Runner> load(Mode mode, FrameSize size,
                                    const std::vector<std::string> &labels,
                                    int debug) {
    std::string dir = "/sdcard/app/yolo_models/models/";
    if (mode == Mode::Yolo5)
        return std::unique_ptr<Runner>(new Yolo5Runner(dir+"yolov5n.kmodel",size,labels,debug));
    if (mode == Mode::Yolo11)
        return std::unique_ptr<Runner>(new StandardRunner<Yolo11>(dir+"yolo11n.kmodel",size,labels,debug));
    if (mode == Mode::Yolo26)
        return std::unique_ptr<Runner>(new Yolo26Runner(dir+"yolo26n.kmodel",size,labels,debug));
    return std::unique_ptr<Runner>(new StandardRunner<Yolov8>(dir+"yolov8n.kmodel",size,labels,debug));
}

static void touch_proc() {
    drv_touch_inst_t *touch=nullptr;
    if (drv_touch_inst_create(0,&touch)!=0) return;
    auto last=std::chrono::steady_clock::now()-std::chrono::milliseconds(500);
    while (!stop_flag.load()) {
        drv_touch_data p[DRV_TOUCH_POINT_NUMBER_MAX];
        if (drv_touch_read(touch,p,DRV_TOUCH_POINT_NUMBER_MAX)<=0) { usleep(10000); continue; }
        auto now=std::chrono::steady_clock::now();
        if (p[0].event!=DRV_TOUCH_EVENT_DOWN || now-last<std::chrono::milliseconds(250)) continue;
        last=now;
        int x=639-(int)p[0].y_coordinate, y=479-(int)p[0].x_coordinate;
        if (y < ios_ui::kBackTouchExtent &&
            x < ios_ui::kBackTouchExtent) stop_flag=true;
        else if (!switch_flag.load() && y<88 && x>500)
            menu_flag=!menu_flag.load();
        else if (!switch_flag.load() && menu_flag.load() &&
                 x>=390 && x<=630 && y>=82 && y<322) {
            Mode next=(Mode)((y-82)/60);
            if (next!=requested.load()) {
                requested=next;
                menu_flag=false;
                switch_flag=true;
                dshanpi_mode_state::save(
                    "yolo_models", static_cast<int>(next),
                    static_cast<int>(Mode::Count));
            }
            menu_flag=false;
        }
    }
    drv_touch_inst_destroy(&touch);
}

static void controls(cv::Mat &osd) {
    ios_ui::rounded_rect(osd,{12,12,58,58},{40,40,40,230},cv::FILLED);
    cv::putText(osd,"<",{32,50},cv::FONT_HERSHEY_SIMPLEX,.65,{255,255,255,255},2);
    dshanpi_ui::draw_mode_header(
        osd,names[static_cast<int>(requested.load())],58);
    ios_ui::rounded_rect(osd,{500,12,128,58},{40,40,40,230},cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),
        cv::Rect(500,12,128,58), 21, {255,255,255,255});
    if (!menu_flag.load()) return;
    ios_ui::rounded_rect(osd,{386,76,242,252},{28,32,40,240},cv::FILLED);
    for (int i=0;i<(int)Mode::Count;++i) {
        int top=82+i*60;
        if ((Mode)i==requested.load()) ios_ui::rounded_rect(osd,{394,top,226,52},ios_ui::accent(),cv::FILLED);
        cv::putText(osd,names[i],{415,top+35},cv::FONT_HERSHEY_SIMPLEX,.65,{255,255,255,255},2);
    }
}

int main(int argc,char **argv) {
    int csi=argc>1?atoi(argv[1]):2, debug=argc>2?atoi(argv[2]):0;
    if (csi!=0 && csi!=2) csi=2;
    active=static_cast<Mode>(dshanpi_mode_state::load(
        "yolo_models",static_cast<int>(Mode::Yolo8),
        static_cast<int>(Mode::Count)));
    requested=active;
    FrameSize size{AI_FRAME_WIDTH,AI_FRAME_HEIGHT};
    auto labels=readLabelsFromTxt("/sdcard/app/yolo_models/coco_labels.txt");
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "YOLO Models", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION | VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0) {
        std::cerr << "[yolo-models] UART2 VAXP output unavailable"
                  << std::endl;
    } else {
        std::vector<const char *> class_names;
        class_names.reserve(labels.size());
        for (const auto &label : labels) class_names.push_back(label.c_str());
        for (uint16_t model_id = 0x0801; model_id <= 0x0804; ++model_id)
            dshanpi_vaxp_ai_register_classes(
                model_id, class_names.data(), class_names.size());
    }
    std::unique_ptr<Runner> runner;
    std::atomic<bool> model_ready(false);
    std::thread model_thread([&]() {
        runner=load(active,size,labels,debug);
        std::cout << "[yolo-models] loaded initial model: "
                  << names[static_cast<int>(active)] << std::endl;
        model_ready=true;
    });
    CameraManager camera(debug,csi);
    if (camera.Create()!=0) return 1;
    cv::Mat osd(OSD_HEIGHT,OSD_WIDTH,CV_8UC4,cv::Scalar(0,0,0,0));
    dshanpi_ui::show_model_loading_until_ready(
        camera,osd,model_ready,names[static_cast<int>(active)]);
    model_thread.join();
    std::thread touch(touch_proc);
    dims_t shape{1,AI_FRAME_CHANNEL,AI_FRAME_HEIGHT,AI_FRAME_WIDTH};
    runtime_tensor input=host_runtime_tensor::create(typecode_t::dt_uint8,shape,hrt::pool_shared).expect("yolo tensor");
    auto buf=input.impl()->to_host().unwrap()->buffer().as_host().unwrap().map(map_access_::map_write).unwrap().buffer();
    const size_t plane=AI_FRAME_WIDTH*AI_FRAME_HEIGHT;
    while (!stop_flag.load()) {
        DumpRes frame;
        if (camera.GetFrame(frame)!=0) { usleep(10000); continue; }
        if (switch_flag.load()) {
            const Mode next=requested.load();
            camera.ReleaseFrame(frame);
            dshanpi_ui::load_model_with_feedback(
                camera,osd,names[static_cast<int>(next)],
                [&]() {
                    runner.reset();
                    runner=load(next,size,labels,debug);
                },
                [&](cv::Mat &loading_osd) { controls(loading_osd); });
            active=next;
            switch_flag=false;
            std::cout << "[yolo-models] switched mode: "
                      << names[static_cast<int>(active)] << std::endl;
            continue;
        }
        const uint8_t *src=(const uint8_t*)frame.virt_addr;
        uint8_t *dst=(uint8_t*)buf.data();
        for(int c=0;c<AI_FRAME_CHANNEL;++c)
            std::reverse_copy(src+c*plane,src+(c+1)*plane,dst+c*plane);
        hrt::sync(input,sync_op_t::sync_write_back,true).expect("sync");
        osd.setTo(cv::Scalar(0,0,0,0));
        runner->process(input,osd); controls(osd); camera.InsertFrame(osd.data);
        camera.ReleaseFrame(frame);
    }
    touch.join(); runner.reset(); dshanpi_vaxp_ai_stop();
    camera.Destroy(); usleep(200000); return 0;
}
