#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <unistd.h>
#include "drv_touch.h"
#include "ocr_box.h"
#include "ocr_reco.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "vaxp_ai_stream.h"

static std::atomic<bool> g_stop(false);
static void touch_proc() {
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) return;
    auto last = std::chrono::steady_clock::now() - std::chrono::milliseconds(500);
    while (!g_stop.load()) {
        drv_touch_data p[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, p, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) { usleep(10000); continue; }
        auto now = std::chrono::steady_clock::now();
        if (p[0].event != DRV_TOUCH_EVENT_DOWN ||
            now - last < std::chrono::milliseconds(250)) continue;
        last = now;
        int x = 639 - static_cast<int>(p[0].y_coordinate);
        int y = 479 - static_cast<int>(p[0].x_coordinate);
        if (y < ios_ui::kBackTouchExtent &&
            x < ios_ui::kBackTouchExtent) g_stop = true;
    }
    drv_touch_inst_destroy(&touch);
}
static void draw_controls(cv::Mat &osd) {
    ios_ui::rounded_rect(osd, cv::Rect(12, 12, 58, 58),
                  cv::Scalar(40, 40, 40, 220), cv::FILLED);
    cv::putText(osd, "<", cv::Point(32, 50),
                cv::FONT_HERSHEY_SIMPLEX, .65,
                cv::Scalar(255, 255, 255, 255), 2);
    dshanpi_ui::draw_mode_header(
        osd,
        dshanpi_ui::localized("OCR Detection", "OCR 文字识别",
                              "OCR 文字辨識", "OCR文字認識"),
        58);
}
int main(int argc, char **argv) {
    static const char *det_model =
        "/sdcard/app/ocr_detection/models/ocr_det_int16.kmodel";
    static const char *rec_model =
        "/sdcard/app/ocr_detection/models/ocr_rec_int16.kmodel";
    static const char *dictionary =
        "/sdcard/app/ocr_detection/dict_ocr.txt";
    static const char *font =
        "/sdcard/app/ocr_detection/SourceHanSansSC-Normal-Min.ttf";
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    int debug = argc > 2 ? atoi(argv[2]) : 0;
    if (csi != 0 && csi != 2) csi = 2;
    printf("[ocr-detection] starting on CSI%d\n", csi);
    fflush(stdout);
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "OCR Detection", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT, VAXP_CAP_OCR};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0)
        printf("[ocr-detection] UART2 VAXP output unavailable\n");
    if (access(det_model, R_OK) != 0 || access(rec_model, R_OK) != 0 ||
        access(dictionary, R_OK) != 0 || access(font, R_OK) != 0) {
        printf("[ocr-detection] resource missing: det=%d rec=%d dict=%d font=%d\n",
               access(det_model, R_OK), access(rec_model, R_OK),
               access(dictionary, R_OK), access(font, R_OK));
        fflush(stdout);
        return 2;
    }
    FrameCHWSize size{AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    std::unique_ptr<OCRBox> detector;
    std::unique_ptr<OCRReco> recognizer;
    std::atomic<bool> model_ready(false);
    std::thread model_thread([&]() {
        printf("[ocr-detection] loading detector\n");
        fflush(stdout);
        detector.reset(new OCRBox(const_cast<char *>(det_model),
                                  .25f, .4f, size, debug));
        printf("[ocr-detection] loading recognizer\n");
        fflush(stdout);
        recognizer.reset(new OCRReco(const_cast<char *>(rec_model), debug));
        printf("[ocr-detection] models and dictionary loaded\n");
        fflush(stdout);
        model_ready = true;
    });
    CameraManager camera(debug, csi);
    if (camera.Create() != 0) return 1;
    cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    camera.InsertFrame(osd.data, false);
    model_thread.join();
    std::thread touch(touch_proc);
    dims_t shape{1, AI_FRAME_CHANNEL, AI_FRAME_HEIGHT, AI_FRAME_WIDTH};
    runtime_tensor input = host_runtime_tensor::create(
        typecode_t::dt_uint8, shape, hrt::pool_shared).expect("ocr tensor");
    auto buf = input.impl()->to_host().unwrap()->buffer().as_host().unwrap()
        .map(map_access_::map_write).unwrap().buffer();
    const size_t plane = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
    while (!g_stop.load()) {
        DumpRes frame;
        if (camera.GetFrame(frame) != 0) { usleep(10000); continue; }
        const uint8_t *src = reinterpret_cast<const uint8_t *>(frame.virt_addr);
        uint8_t *dst = reinterpret_cast<uint8_t *>(buf.data());
        for (int c = 0; c < AI_FRAME_CHANNEL; ++c)
            std::reverse_copy(src + c * plane, src + (c + 1) * plane,
                              dst + c * plane);
        hrt::sync(input, sync_op_t::sync_write_back, true).expect("ocr sync");
        std::vector<ocr_det_res> boxes;
        std::vector<std::string> texts;
        detector->pre_process(input);
        detector->inference();
        detector->post_process(boxes);
        cv::Mat r(size.height, size.width, CV_8UC1, dst);
        cv::Mat g(size.height, size.width, CV_8UC1, dst + plane);
        cv::Mat b(size.height, size.width, CV_8UC1, dst + plane * 2);
        cv::Mat image;
        std::vector<cv::Mat> channels{b, g, r};
        cv::merge(channels, image);
        for (auto &box : boxes) {
            std::vector<cv::Point2f> sorted(4);
            cv::Mat crop;
            detector->warppersp(image, crop, box, sorted);
            recognizer->pre_process(crop);
            recognizer->inference();
            std::string text;
            recognizer->post_process(text);
            texts.push_back(text);
        }
        std::vector<dshanpi_vaxp_ai_ocr_t> wire;
        wire.reserve(boxes.size());
        for (size_t index = 0; index < boxes.size(); ++index) {
            dshanpi_vaxp_ai_ocr_t result{};
            result.result_id = static_cast<uint32_t>(index + 1u);
            result.confidence = boxes[index].score;
            result.text = index < texts.size() ? texts[index].c_str() : "";
            for (size_t point = 0; point < 4; ++point) {
                result.points[point * 2] = boxes[index].vertices[point].x;
                result.points[point * 2 + 1] = boxes[index].vertices[point].y;
            }
            wire.push_back(result);
        }
        dshanpi_vaxp_ai_publish_ocr(
            0x0601, 0x0601, "OCR Detect + Recognize", 0,
            wire.data(), wire.size());
        osd.setTo(cv::Scalar(0, 0, 0, 0));
        detector->draw_result(osd, boxes, texts);
        draw_controls(osd);
        camera.InsertFrame(osd.data);
        camera.ReleaseFrame(frame);
    }
    touch.join();
    dshanpi_vaxp_ai_stop();
    camera.Destroy();
    usleep(200000);
    return 0;
}
