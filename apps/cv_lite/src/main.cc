#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "drv_touch.h"
#include "setting.h"
#include "video_pipeline.h"
#include "../../face_studio/src/ios_overlay.h"
#include "../../face_studio/src/mode_persistence.h"
#include "../../face_studio/src/startup_spinner.h"
#include "../../face_studio/src/ui_localization.h"
#include "vaxp_ai_stream.h"

enum class Mode { Circle, Edge, Rectangle, Blob, RectangleCorners,
                  PnpBlob, PnpRectangle, Count };

static std::atomic<bool> g_stop(false);
static std::atomic<bool> g_menu_open(false);
static std::atomic<bool> g_tune_open(false);
static std::atomic<bool> g_tune_advanced(false);
static std::atomic<int> g_tune_page(0);
static std::atomic<int> g_parameter_notice(0);
static std::atomic<Mode> g_mode(Mode::Circle);

static constexpr const char *kParameterPath =
    "/data/dshanpi_cv_lite_params.conf";
static constexpr const char *kTuneLevelPath =
    "/data/dshanpi_cv_lite_tune_level.conf";
static constexpr int kParametersPerPage = 5;

struct Parameters {
    int edge_blur = 5;
    int edge_low = 60;
    int edge_high = 150;

    int circle_blur = 5;
    int circle_dp_x10 = 15;
    int circle_min_distance = 30;
    int circle_edge_threshold = 120;
    int circle_center_threshold = 35;
    int circle_min_radius = 8;
    int circle_max_radius = 180;

    int rectangle_blur = 5;
    int rectangle_edge_low = 60;
    int rectangle_edge_high = 150;
    int rectangle_epsilon_x10 = 30;
    int rectangle_min_area = 1200;

    int blob_h_min = 0;
    int blob_h_max = 25;
    int blob_s_min = 80;
    int blob_s_max = 255;
    int blob_v_min = 60;
    int blob_v_max = 255;
    int blob_morphology = 5;
    int blob_min_area = 500;

    int target_width_x10 = 600;
    int focal_length_px = 520;
};

enum class ParameterId {
    EdgeBlur, EdgeLow, EdgeHigh,
    CircleBlur, CircleDp, CircleMinDistance, CircleEdgeThreshold,
    CircleCenterThreshold, CircleMinRadius, CircleMaxRadius,
    RectangleBlur, RectangleEdgeLow, RectangleEdgeHigh,
    RectangleEpsilon, RectangleMinArea,
    BlobHMin, BlobHMax, BlobSMin, BlobSMax, BlobVMin, BlobVMax,
    BlobMorphology, BlobMinArea, TargetWidth, FocalLength,
};

static const char *localized_mode_name(Mode mode)
{
    switch (mode) {
    case Mode::Circle:
        return dshanpi_ui::localized("Circle Detection", "圆形检测",
                                     "圓形偵測", "円検出");
    case Mode::Edge:
        return dshanpi_ui::localized("Edge Detection", "边缘检测",
                                     "邊緣偵測", "エッジ検出");
    case Mode::Rectangle:
        return dshanpi_ui::localized("Rectangle Detection", "矩形检测",
                                     "矩形偵測", "矩形検出");
    case Mode::Blob:
        return dshanpi_ui::localized("Color Blob Detection", "颜色色块",
                                     "顏色色塊", "色領域検出");
    case Mode::RectangleCorners:
        return dshanpi_ui::localized("Rectangle Corners", "矩形角点",
                                     "矩形角點", "矩形コーナー");
    case Mode::PnpBlob:
        return dshanpi_ui::localized("PnP Blob Distance", "色块测距",
                                     "色塊測距", "色領域距離");
    case Mode::PnpRectangle:
        return dshanpi_ui::localized("PnP Rectangle Distance", "矩形测距",
                                     "矩形測距", "矩形距離");
    default: return "";
    }
}

static const char *localized_parameter_name(ParameterId id)
{
    switch (id) {
    case ParameterId::EdgeBlur:
    case ParameterId::CircleBlur:
    case ParameterId::RectangleBlur:
        return dshanpi_ui::localized("Blur kernel", "模糊核",
                                     "模糊核心", "ぼかしカーネル");
    case ParameterId::EdgeLow:
    case ParameterId::RectangleEdgeLow:
        return dshanpi_ui::localized("Low threshold", "低阈值",
                                     "低閾值", "下限しきい値");
    case ParameterId::EdgeHigh:
    case ParameterId::RectangleEdgeHigh:
        return dshanpi_ui::localized("High threshold", "高阈值",
                                     "高閾值", "上限しきい値");
    case ParameterId::CircleDp:
        return dshanpi_ui::localized("Accumulator ratio", "累加器比例",
                                     "累加器比例", "累積比率");
    case ParameterId::CircleMinDistance:
        return dshanpi_ui::localized("Center distance", "圆心距离",
                                     "圓心距離", "中心距離");
    case ParameterId::CircleEdgeThreshold:
        return dshanpi_ui::localized("Edge threshold", "边缘阈值",
                                     "邊緣閾值", "エッジしきい値");
    case ParameterId::CircleCenterThreshold:
        return dshanpi_ui::localized("Center threshold", "圆心阈值",
                                     "圓心閾值", "中心しきい値");
    case ParameterId::CircleMinRadius:
        return dshanpi_ui::localized("Minimum radius", "最小半径",
                                     "最小半徑", "最小半径");
    case ParameterId::CircleMaxRadius:
        return dshanpi_ui::localized("Maximum radius", "最大半径",
                                     "最大半徑", "最大半径");
    case ParameterId::RectangleEpsilon:
        return dshanpi_ui::localized("Corner precision", "角点精度",
                                     "角點精度", "コーナー精度");
    case ParameterId::RectangleMinArea:
    case ParameterId::BlobMinArea:
        return dshanpi_ui::localized("Minimum area", "最小面积",
                                     "最小面積", "最小面積");
    case ParameterId::BlobHMin:
        return dshanpi_ui::localized("Hue minimum", "色相下限",
                                     "色相下限", "色相下限");
    case ParameterId::BlobHMax:
        return dshanpi_ui::localized("Hue maximum", "色相上限",
                                     "色相上限", "色相上限");
    case ParameterId::BlobSMin:
        return dshanpi_ui::localized("Saturation minimum", "饱和度下限",
                                     "飽和度下限", "彩度下限");
    case ParameterId::BlobSMax:
        return dshanpi_ui::localized("Saturation maximum", "饱和度上限",
                                     "飽和度上限", "彩度上限");
    case ParameterId::BlobVMin:
        return dshanpi_ui::localized("Brightness minimum", "亮度下限",
                                     "亮度下限", "明度下限");
    case ParameterId::BlobVMax:
        return dshanpi_ui::localized("Brightness maximum", "亮度上限",
                                     "亮度上限", "明度上限");
    case ParameterId::BlobMorphology:
        return dshanpi_ui::localized("Noise filter", "去噪尺寸",
                                     "去噪尺寸", "ノイズ除去");
    case ParameterId::TargetWidth:
        return dshanpi_ui::localized("Target width", "目标宽度",
                                     "目標寬度", "対象幅");
    case ParameterId::FocalLength:
        return dshanpi_ui::localized("Focal length", "焦距",
                                     "焦距", "焦点距離");
    }
    return "";
}

struct ParameterSpec {
    ParameterId id;
    const char *label;
    int minimum;
    int maximum;
    int step;
    int scale;
    const char *suffix;
};

static const ParameterSpec kCircleParameters[] = {
    {ParameterId::CircleBlur, "Blur kernel", 3, 15, 2, 1, ""},
    {ParameterId::CircleDp, "Accumulator ratio", 10, 30, 1, 10, ""},
    {ParameterId::CircleMinDistance, "Center distance", 10, 240, 5, 1, " px"},
    {ParameterId::CircleEdgeThreshold, "Edge threshold", 10, 255, 5, 1, ""},
    {ParameterId::CircleCenterThreshold, "Center threshold", 5, 150, 5, 1, ""},
    {ParameterId::CircleMinRadius, "Minimum radius", 0, 220, 2, 1, " px"},
    {ParameterId::CircleMaxRadius, "Maximum radius", 2, 320, 2, 1, " px"},
};
static const ParameterSpec kEdgeParameters[] = {
    {ParameterId::EdgeBlur, "Blur kernel", 3, 15, 2, 1, ""},
    {ParameterId::EdgeLow, "Low threshold", 0, 250, 5, 1, ""},
    {ParameterId::EdgeHigh, "High threshold", 1, 255, 5, 1, ""},
};
static const ParameterSpec kRectangleParameters[] = {
    {ParameterId::RectangleBlur, "Blur kernel", 3, 15, 2, 1, ""},
    {ParameterId::RectangleEdgeLow, "Low threshold", 0, 250, 5, 1, ""},
    {ParameterId::RectangleEdgeHigh, "High threshold", 1, 255, 5, 1, ""},
    {ParameterId::RectangleEpsilon, "Corner precision", 5, 100, 5, 10, "%"},
    {ParameterId::RectangleMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
};
static const ParameterSpec kBlobParameters[] = {
    {ParameterId::BlobHMin, "Hue minimum", 0, 179, 1, 1, ""},
    {ParameterId::BlobHMax, "Hue maximum", 0, 179, 1, 1, ""},
    {ParameterId::BlobSMin, "Saturation minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobSMax, "Saturation maximum", 0, 255, 5, 1, ""},
    {ParameterId::BlobVMin, "Brightness minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobVMax, "Brightness maximum", 0, 255, 5, 1, ""},
    {ParameterId::BlobMorphology, "Noise filter", 1, 15, 2, 1, ""},
    {ParameterId::BlobMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
};
static const ParameterSpec kPnpBlobParameters[] = {
    {ParameterId::BlobHMin, "Hue minimum", 0, 179, 1, 1, ""},
    {ParameterId::BlobHMax, "Hue maximum", 0, 179, 1, 1, ""},
    {ParameterId::BlobSMin, "Saturation minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobSMax, "Saturation maximum", 0, 255, 5, 1, ""},
    {ParameterId::BlobVMin, "Brightness minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobVMax, "Brightness maximum", 0, 255, 5, 1, ""},
    {ParameterId::BlobMorphology, "Noise filter", 1, 15, 2, 1, ""},
    {ParameterId::BlobMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
    {ParameterId::TargetWidth, "Target width", 10, 5000, 10, 10, " mm"},
    {ParameterId::FocalLength, "Focal length", 100, 3000, 10, 1, " px"},
};
static const ParameterSpec kPnpRectangleParameters[] = {
    {ParameterId::RectangleBlur, "Blur kernel", 3, 15, 2, 1, ""},
    {ParameterId::RectangleEdgeLow, "Low threshold", 0, 250, 5, 1, ""},
    {ParameterId::RectangleEdgeHigh, "High threshold", 1, 255, 5, 1, ""},
    {ParameterId::RectangleEpsilon, "Corner precision", 5, 100, 5, 10, "%"},
    {ParameterId::RectangleMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
    {ParameterId::TargetWidth, "Target width", 10, 5000, 10, 10, " mm"},
    {ParameterId::FocalLength, "Focal length", 100, 3000, 10, 1, " px"},
};

/* The basic editor intentionally exposes only the controls that normally
 * need adjustment in the field. Advanced mode keeps every parameter above. */
static const ParameterSpec kBasicCircleParameters[] = {
    {ParameterId::CircleEdgeThreshold, "Edge threshold", 10, 255, 5, 1, ""},
    {ParameterId::CircleCenterThreshold, "Center threshold", 5, 150, 5, 1, ""},
    {ParameterId::CircleMinRadius, "Minimum radius", 0, 220, 2, 1, " px"},
    {ParameterId::CircleMaxRadius, "Maximum radius", 2, 320, 2, 1, " px"},
};
static const ParameterSpec kBasicEdgeParameters[] = {
    {ParameterId::EdgeLow, "Low threshold", 0, 250, 5, 1, ""},
    {ParameterId::EdgeHigh, "High threshold", 1, 255, 5, 1, ""},
};
static const ParameterSpec kBasicRectangleParameters[] = {
    {ParameterId::RectangleEpsilon, "Corner precision", 5, 100, 5, 10, "%"},
    {ParameterId::RectangleMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
};
static const ParameterSpec kBasicBlobParameters[] = {
    {ParameterId::BlobHMin, "Hue minimum", 0, 179, 1, 1, ""},
    {ParameterId::BlobHMax, "Hue maximum", 0, 179, 1, 1, ""},
    {ParameterId::BlobSMin, "Saturation minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobVMin, "Brightness minimum", 0, 255, 5, 1, ""},
    {ParameterId::BlobMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
};
static const ParameterSpec kBasicPnpBlobParameters[] = {
    {ParameterId::BlobHMin, "Hue minimum", 0, 179, 1, 1, ""},
    {ParameterId::BlobHMax, "Hue maximum", 0, 179, 1, 1, ""},
    {ParameterId::BlobSMin, "Saturation minimum", 0, 255, 5, 1, ""},
    {ParameterId::TargetWidth, "Target width", 10, 5000, 10, 10, " mm"},
    {ParameterId::FocalLength, "Focal length", 100, 3000, 10, 1, " px"},
};
static const ParameterSpec kBasicPnpRectangleParameters[] = {
    {ParameterId::RectangleEpsilon, "Corner precision", 5, 100, 5, 10, "%"},
    {ParameterId::RectangleMinArea, "Minimum area", 100, 50000, 100, 1, " px2"},
    {ParameterId::TargetWidth, "Target width", 10, 5000, 10, 10, " mm"},
    {ParameterId::FocalLength, "Focal length", 100, 3000, 10, 1, " px"},
};

static Parameters g_parameters;
static std::mutex g_parameters_mutex;

static const ParameterSpec *advanced_parameters_for_mode(Mode mode,
                                                          size_t &count)
{
    switch (mode) {
    case Mode::Circle:
        count = sizeof(kCircleParameters) / sizeof(kCircleParameters[0]);
        return kCircleParameters;
    case Mode::Edge:
        count = sizeof(kEdgeParameters) / sizeof(kEdgeParameters[0]);
        return kEdgeParameters;
    case Mode::Rectangle:
    case Mode::RectangleCorners:
        count = sizeof(kRectangleParameters) /
                sizeof(kRectangleParameters[0]);
        return kRectangleParameters;
    case Mode::Blob:
        count = sizeof(kBlobParameters) / sizeof(kBlobParameters[0]);
        return kBlobParameters;
    case Mode::PnpBlob:
        count = sizeof(kPnpBlobParameters) /
                sizeof(kPnpBlobParameters[0]);
        return kPnpBlobParameters;
    case Mode::PnpRectangle:
        count = sizeof(kPnpRectangleParameters) /
                sizeof(kPnpRectangleParameters[0]);
        return kPnpRectangleParameters;
    default:
        count = 0;
        return nullptr;
    }
}

static const ParameterSpec *basic_parameters_for_mode(Mode mode,
                                                       size_t &count)
{
    switch (mode) {
    case Mode::Circle:
        count = sizeof(kBasicCircleParameters) /
                sizeof(kBasicCircleParameters[0]);
        return kBasicCircleParameters;
    case Mode::Edge:
        count = sizeof(kBasicEdgeParameters) /
                sizeof(kBasicEdgeParameters[0]);
        return kBasicEdgeParameters;
    case Mode::Rectangle:
    case Mode::RectangleCorners:
        count = sizeof(kBasicRectangleParameters) /
                sizeof(kBasicRectangleParameters[0]);
        return kBasicRectangleParameters;
    case Mode::Blob:
        count = sizeof(kBasicBlobParameters) /
                sizeof(kBasicBlobParameters[0]);
        return kBasicBlobParameters;
    case Mode::PnpBlob:
        count = sizeof(kBasicPnpBlobParameters) /
                sizeof(kBasicPnpBlobParameters[0]);
        return kBasicPnpBlobParameters;
    case Mode::PnpRectangle:
        count = sizeof(kBasicPnpRectangleParameters) /
                sizeof(kBasicPnpRectangleParameters[0]);
        return kBasicPnpRectangleParameters;
    default:
        count = 0;
        return nullptr;
    }
}

static const ParameterSpec *parameters_for_mode(Mode mode, size_t &count)
{
    return g_tune_advanced.load()
               ? advanced_parameters_for_mode(mode, count)
               : basic_parameters_for_mode(mode, count);
}

static int *parameter_value(Parameters &parameters, ParameterId id)
{
    switch (id) {
    case ParameterId::EdgeBlur: return &parameters.edge_blur;
    case ParameterId::EdgeLow: return &parameters.edge_low;
    case ParameterId::EdgeHigh: return &parameters.edge_high;
    case ParameterId::CircleBlur: return &parameters.circle_blur;
    case ParameterId::CircleDp: return &parameters.circle_dp_x10;
    case ParameterId::CircleMinDistance: return &parameters.circle_min_distance;
    case ParameterId::CircleEdgeThreshold: return &parameters.circle_edge_threshold;
    case ParameterId::CircleCenterThreshold: return &parameters.circle_center_threshold;
    case ParameterId::CircleMinRadius: return &parameters.circle_min_radius;
    case ParameterId::CircleMaxRadius: return &parameters.circle_max_radius;
    case ParameterId::RectangleBlur: return &parameters.rectangle_blur;
    case ParameterId::RectangleEdgeLow: return &parameters.rectangle_edge_low;
    case ParameterId::RectangleEdgeHigh: return &parameters.rectangle_edge_high;
    case ParameterId::RectangleEpsilon: return &parameters.rectangle_epsilon_x10;
    case ParameterId::RectangleMinArea: return &parameters.rectangle_min_area;
    case ParameterId::BlobHMin: return &parameters.blob_h_min;
    case ParameterId::BlobHMax: return &parameters.blob_h_max;
    case ParameterId::BlobSMin: return &parameters.blob_s_min;
    case ParameterId::BlobSMax: return &parameters.blob_s_max;
    case ParameterId::BlobVMin: return &parameters.blob_v_min;
    case ParameterId::BlobVMax: return &parameters.blob_v_max;
    case ParameterId::BlobMorphology: return &parameters.blob_morphology;
    case ParameterId::BlobMinArea: return &parameters.blob_min_area;
    case ParameterId::TargetWidth: return &parameters.target_width_x10;
    case ParameterId::FocalLength: return &parameters.focal_length_px;
    }
    return nullptr;
}

static void normalize_parameters(Parameters &p)
{
    auto clamp = [](int value, int low, int high) {
        return std::max(low, std::min(value, high));
    };
    p.edge_blur = clamp(p.edge_blur | 1, 3, 15);
    p.edge_low = clamp(p.edge_low, 0, 250);
    p.edge_high = clamp(p.edge_high, p.edge_low + 1, 255);
    p.circle_blur = clamp(p.circle_blur | 1, 3, 15);
    p.circle_dp_x10 = clamp(p.circle_dp_x10, 10, 30);
    p.circle_min_distance = clamp(p.circle_min_distance, 10, 240);
    p.circle_edge_threshold = clamp(p.circle_edge_threshold, 10, 255);
    p.circle_center_threshold = clamp(p.circle_center_threshold, 5, 150);
    p.circle_min_radius = clamp(p.circle_min_radius, 0, 220);
    p.circle_max_radius = clamp(p.circle_max_radius,
                                p.circle_min_radius + 2, 320);
    p.rectangle_blur = clamp(p.rectangle_blur | 1, 3, 15);
    p.rectangle_edge_low = clamp(p.rectangle_edge_low, 0, 250);
    p.rectangle_edge_high = clamp(p.rectangle_edge_high,
                                   p.rectangle_edge_low + 1, 255);
    p.rectangle_epsilon_x10 = clamp(p.rectangle_epsilon_x10, 5, 100);
    p.rectangle_min_area = clamp(p.rectangle_min_area, 100, 50000);
    p.blob_h_min = clamp(p.blob_h_min, 0, 179);
    p.blob_h_max = clamp(p.blob_h_max, p.blob_h_min, 179);
    p.blob_s_min = clamp(p.blob_s_min, 0, 255);
    p.blob_s_max = clamp(p.blob_s_max, p.blob_s_min, 255);
    p.blob_v_min = clamp(p.blob_v_min, 0, 255);
    p.blob_v_max = clamp(p.blob_v_max, p.blob_v_min, 255);
    p.blob_morphology = clamp(p.blob_morphology | 1, 1, 15);
    p.blob_min_area = clamp(p.blob_min_area, 100, 50000);
    p.target_width_x10 = clamp(p.target_width_x10, 10, 5000);
    p.focal_length_px = clamp(p.focal_length_px, 100, 3000);
}

static Parameters parameter_snapshot()
{
    std::lock_guard<std::mutex> lock(g_parameters_mutex);
    return g_parameters;
}

static void assign_loaded_parameter(Parameters &p, const char *key, int value)
{
#define CV_LITE_LOAD(name, field) \
    if (strcmp(key, name) == 0) { p.field = value; return; }
    CV_LITE_LOAD("edge_blur", edge_blur)
    CV_LITE_LOAD("edge_low", edge_low)
    CV_LITE_LOAD("edge_high", edge_high)
    CV_LITE_LOAD("circle_blur", circle_blur)
    CV_LITE_LOAD("circle_dp_x10", circle_dp_x10)
    CV_LITE_LOAD("circle_min_distance", circle_min_distance)
    CV_LITE_LOAD("circle_edge_threshold", circle_edge_threshold)
    CV_LITE_LOAD("circle_center_threshold", circle_center_threshold)
    CV_LITE_LOAD("circle_min_radius", circle_min_radius)
    CV_LITE_LOAD("circle_max_radius", circle_max_radius)
    CV_LITE_LOAD("rectangle_blur", rectangle_blur)
    CV_LITE_LOAD("rectangle_edge_low", rectangle_edge_low)
    CV_LITE_LOAD("rectangle_edge_high", rectangle_edge_high)
    CV_LITE_LOAD("rectangle_epsilon_x10", rectangle_epsilon_x10)
    CV_LITE_LOAD("rectangle_min_area", rectangle_min_area)
    CV_LITE_LOAD("blob_h_min", blob_h_min)
    CV_LITE_LOAD("blob_h_max", blob_h_max)
    CV_LITE_LOAD("blob_s_min", blob_s_min)
    CV_LITE_LOAD("blob_s_max", blob_s_max)
    CV_LITE_LOAD("blob_v_min", blob_v_min)
    CV_LITE_LOAD("blob_v_max", blob_v_max)
    CV_LITE_LOAD("blob_morphology", blob_morphology)
    CV_LITE_LOAD("blob_min_area", blob_min_area)
    CV_LITE_LOAD("target_width_x10", target_width_x10)
    CV_LITE_LOAD("focal_length_px", focal_length_px)
#undef CV_LITE_LOAD
}

static void load_parameters()
{
    Parameters loaded;
    FILE *file = fopen(kParameterPath, "r");
    if (file == nullptr) {
        std::cout << "[cv-lite] using default parameters" << std::endl;
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char key[64];
        int value;
        if (sscanf(line, "%63[^=]=%d", key, &value) == 2)
            assign_loaded_parameter(loaded, key, value);
    }
    fclose(file);
    normalize_parameters(loaded);
    {
        std::lock_guard<std::mutex> lock(g_parameters_mutex);
        g_parameters = loaded;
    }
    std::cout << "[cv-lite] restored saved parameters" << std::endl;
}

static void load_tune_level()
{
    FILE *file = fopen(kTuneLevelPath, "r");
    int advanced = 0;
    if (file != nullptr) {
        if (fscanf(file, "%d", &advanced) != 1)
            advanced = 0;
        fclose(file);
    }
    g_tune_advanced = advanced != 0;
}

static void save_tune_level(bool advanced)
{
    char temporary[128];
    snprintf(temporary, sizeof(temporary), "%s.tmp", kTuneLevelPath);
    FILE *file = fopen(temporary, "w");
    if (file == nullptr)
        return;
    bool failed = fprintf(file, "%d\n", advanced ? 1 : 0) < 0 ||
                  fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (fclose(file) != 0)
        failed = true;
    if (failed || rename(temporary, kTuneLevelPath) != 0)
        unlink(temporary);
}

static int save_parameters()
{
    Parameters p = parameter_snapshot();
    char temporary[128];
    snprintf(temporary, sizeof(temporary), "%s.tmp", kParameterPath);
    FILE *file = fopen(temporary, "w");
    if (file == nullptr) return -1;
    int failed = 0;
#define CV_LITE_SAVE(name, field) \
    failed |= fprintf(file, name "=%d\n", p.field) < 0;
    CV_LITE_SAVE("edge_blur", edge_blur)
    CV_LITE_SAVE("edge_low", edge_low)
    CV_LITE_SAVE("edge_high", edge_high)
    CV_LITE_SAVE("circle_blur", circle_blur)
    CV_LITE_SAVE("circle_dp_x10", circle_dp_x10)
    CV_LITE_SAVE("circle_min_distance", circle_min_distance)
    CV_LITE_SAVE("circle_edge_threshold", circle_edge_threshold)
    CV_LITE_SAVE("circle_center_threshold", circle_center_threshold)
    CV_LITE_SAVE("circle_min_radius", circle_min_radius)
    CV_LITE_SAVE("circle_max_radius", circle_max_radius)
    CV_LITE_SAVE("rectangle_blur", rectangle_blur)
    CV_LITE_SAVE("rectangle_edge_low", rectangle_edge_low)
    CV_LITE_SAVE("rectangle_edge_high", rectangle_edge_high)
    CV_LITE_SAVE("rectangle_epsilon_x10", rectangle_epsilon_x10)
    CV_LITE_SAVE("rectangle_min_area", rectangle_min_area)
    CV_LITE_SAVE("blob_h_min", blob_h_min)
    CV_LITE_SAVE("blob_h_max", blob_h_max)
    CV_LITE_SAVE("blob_s_min", blob_s_min)
    CV_LITE_SAVE("blob_s_max", blob_s_max)
    CV_LITE_SAVE("blob_v_min", blob_v_min)
    CV_LITE_SAVE("blob_v_max", blob_v_max)
    CV_LITE_SAVE("blob_morphology", blob_morphology)
    CV_LITE_SAVE("blob_min_area", blob_min_area)
    CV_LITE_SAVE("target_width_x10", target_width_x10)
    CV_LITE_SAVE("focal_length_px", focal_length_px)
#undef CV_LITE_SAVE
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        unlink(temporary);
        return -1;
    }
    if (unlink(kParameterPath) != 0 && errno != ENOENT) {
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, kParameterPath) != 0) {
        unlink(temporary);
        return -1;
    }
    std::cout << "[cv-lite] parameters saved" << std::endl;
    return 0;
}

static void reset_mode_parameters(Mode mode)
{
    Parameters defaults;
    size_t count = 0;
    const ParameterSpec *specs = advanced_parameters_for_mode(mode, count);
    std::lock_guard<std::mutex> lock(g_parameters_mutex);
    for (size_t i = 0; i < count; ++i) {
        int *destination = parameter_value(g_parameters, specs[i].id);
        int *source = parameter_value(defaults, specs[i].id);
        if (destination != nullptr && source != nullptr)
            *destination = *source;
    }
    normalize_parameters(g_parameters);
}

static void adjust_parameter(const ParameterSpec &spec, int direction)
{
    std::lock_guard<std::mutex> lock(g_parameters_mutex);
    int *value = parameter_value(g_parameters, spec.id);
    if (value == nullptr) return;
    *value = std::max(spec.minimum,
                      std::min(*value + direction * spec.step,
                               spec.maximum));
    /* When a lower bound is moved past an upper bound (or vice versa), keep
     * the edited control responsive by moving the paired bound with it. */
    switch (spec.id) {
    case ParameterId::EdgeLow:
        g_parameters.edge_high = std::max(g_parameters.edge_high,
                                          g_parameters.edge_low + 1);
        break;
    case ParameterId::EdgeHigh:
        g_parameters.edge_low = std::min(g_parameters.edge_low,
                                         g_parameters.edge_high - 1);
        break;
    case ParameterId::RectangleEdgeLow:
        g_parameters.rectangle_edge_high =
            std::max(g_parameters.rectangle_edge_high,
                     g_parameters.rectangle_edge_low + 1);
        break;
    case ParameterId::RectangleEdgeHigh:
        g_parameters.rectangle_edge_low =
            std::min(g_parameters.rectangle_edge_low,
                     g_parameters.rectangle_edge_high - 1);
        break;
    case ParameterId::CircleMinRadius:
        g_parameters.circle_max_radius =
            std::max(g_parameters.circle_max_radius,
                     g_parameters.circle_min_radius + 2);
        break;
    case ParameterId::CircleMaxRadius:
        g_parameters.circle_min_radius =
            std::min(g_parameters.circle_min_radius,
                     g_parameters.circle_max_radius - 2);
        break;
    case ParameterId::BlobHMin:
        g_parameters.blob_h_max = std::max(g_parameters.blob_h_max,
                                           g_parameters.blob_h_min);
        break;
    case ParameterId::BlobHMax:
        g_parameters.blob_h_min = std::min(g_parameters.blob_h_min,
                                           g_parameters.blob_h_max);
        break;
    case ParameterId::BlobSMin:
        g_parameters.blob_s_max = std::max(g_parameters.blob_s_max,
                                           g_parameters.blob_s_min);
        break;
    case ParameterId::BlobSMax:
        g_parameters.blob_s_min = std::min(g_parameters.blob_s_min,
                                           g_parameters.blob_s_max);
        break;
    case ParameterId::BlobVMin:
        g_parameters.blob_v_max = std::max(g_parameters.blob_v_max,
                                           g_parameters.blob_v_min);
        break;
    case ParameterId::BlobVMax:
        g_parameters.blob_v_min = std::min(g_parameters.blob_v_min,
                                           g_parameters.blob_v_max);
        break;
    default:
        break;
    }
    normalize_parameters(g_parameters);
    g_parameter_notice = 0;
}

static void touch_to_screen(const drv_touch_data &p, int &x, int &y)
{
    x = 639 - static_cast<int>(p.y_coordinate);
    y = 479 - static_cast<int>(p.x_coordinate);
}

static void handle_parameter_touch(int x, int y)
{
    Mode mode = g_mode.load();
    size_t count = 0;
    const ParameterSpec *specs = parameters_for_mode(mode, count);
    int page_count = std::max(1, static_cast<int>(
        (count + kParametersPerPage - 1) / kParametersPerPage));
    int page = std::max(0, std::min(g_tune_page.load(), page_count - 1));
    g_tune_page = page;

    if (x >= 570 && y >= 72 && y <= 116) {
        g_tune_open = false;
        return;
    }
    if (y >= 72 && y <= 116) {
        bool level_changed = false;
        if (x >= 300 && x <= 410 && g_tune_advanced.load()) {
            g_tune_advanced = false;
            level_changed = true;
        } else if (x >= 416 && x <= 562 && !g_tune_advanced.load()) {
            g_tune_advanced = true;
            level_changed = true;
        }
        if (level_changed) {
            g_tune_page = 0;
            g_parameter_notice = 0;
            save_tune_level(g_tune_advanced.load());
        }
        return;
    }
    if (y >= 132 && y < 392) {
        int row = (y - 132) / 52;
        size_t index = static_cast<size_t>(page * kParametersPerPage + row);
        if (row >= 0 && row < kParametersPerPage && index < count) {
            if (x >= 376 && x <= 438)
                adjust_parameter(specs[index], -1);
            else if (x >= 542 && x <= 608)
                adjust_parameter(specs[index], 1);
        }
        return;
    }
    if (y >= 404 && y <= 460) {
        if (x >= 94 && x <= 224) {
            reset_mode_parameters(mode);
            g_parameter_notice = 2;
        } else if (x >= 236 && x <= 366) {
            g_parameter_notice = save_parameters() == 0 ? 1 : -1;
        } else if (x >= 388 && x <= 446 && page > 0) {
            g_tune_page = page - 1;
        } else if (x >= 458 && x <= 516 && page + 1 < page_count) {
            g_tune_page = page + 1;
        }
    }
}

static void touch_proc()
{
    drv_touch_inst_t *touch = nullptr;
    if (drv_touch_inst_create(0, &touch) != 0) return;
    auto last = std::chrono::steady_clock::now() - std::chrono::milliseconds(500);
    while (!g_stop.load()) {
        drv_touch_data points[DRV_TOUCH_POINT_NUMBER_MAX];
        int count = drv_touch_read(touch, points, DRV_TOUCH_POINT_NUMBER_MAX);
        if (count <= 0) { usleep(10000); continue; }
        auto now = std::chrono::steady_clock::now();
        if (points[0].event != DRV_TOUCH_EVENT_DOWN ||
            now - last < std::chrono::milliseconds(250)) continue;
        last = now;
        int x, y;
        touch_to_screen(points[0], x, y);
        if (g_tune_open.load()) {
            handle_parameter_touch(x, y);
        } else if (y < ios_ui::kBackTouchExtent &&
            x < ios_ui::kBackTouchExtent) g_stop = true;
        else if (y < 78 && x >= 500) {
            g_menu_open = !g_menu_open.load();
        } else if (y < 78 && x >= 104 && x < 194) {
            g_tune_open = true;
            g_menu_open = false;
            g_tune_page = 0;
            g_parameter_notice = 0;
        }
        else if (g_menu_open && x >= 310 && x <= 630 && y >= 74 && y < 424) {
            int index = (y - 74) / 50;
            if (index >= 0 && index < static_cast<int>(Mode::Count) &&
                index != static_cast<int>(g_mode.load())) {
                g_mode = static_cast<Mode>(index);
                dshanpi_mode_state::save(
                    "cv_lite", index, static_cast<int>(Mode::Count));
            }
            g_menu_open = false;
            g_tune_page = 0;
        }
    }
    drv_touch_inst_destroy(&touch);
}

static std::vector<std::vector<cv::Point>> contours_from_edges(
    const cv::Mat &gray, const Parameters &parameters)
{
    cv::Mat blur, edges;
    cv::GaussianBlur(gray, blur,
                     cv::Size(parameters.rectangle_blur,
                              parameters.rectangle_blur), 0);
    cv::Canny(blur, edges, parameters.rectangle_edge_low,
              parameters.rectangle_edge_high);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    return contours;
}

static bool largest_rectangle(const cv::Mat &gray,
                              std::vector<cv::Point> &best,
                              const Parameters &parameters)
{
    double best_area = 0;
    for (const auto &contour : contours_from_edges(gray, parameters)) {
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(
            contour, polygon,
            cv::arcLength(contour, true) *
                (static_cast<double>(parameters.rectangle_epsilon_x10) /
                 1000.0),
            true);
        double area = std::fabs(cv::contourArea(polygon));
        if (polygon.size() == 4 && cv::isContourConvex(polygon) &&
            area > parameters.rectangle_min_area && area > best_area) {
            best = polygon;
            best_area = area;
        }
    }
    return best_area > 0;
}

static bool largest_blob(const cv::Mat &rgb, cv::Rect &best,
                         const Parameters &parameters)
{
    cv::Mat hsv, mask;
    cv::cvtColor(rgb, hsv, cv::COLOR_RGB2HSV);
    cv::inRange(hsv,
                cv::Scalar(parameters.blob_h_min, parameters.blob_s_min,
                           parameters.blob_v_min),
                cv::Scalar(parameters.blob_h_max, parameters.blob_s_max,
                           parameters.blob_v_max), mask);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                     cv::getStructuringElement(
                         cv::MORPH_RECT,
                         cv::Size(parameters.blob_morphology,
                                  parameters.blob_morphology)));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int area = 0;
    for (const auto &c : contours) {
        cv::Rect r = cv::boundingRect(c);
        if (r.area() > area && r.area() > parameters.blob_min_area) {
            best = r;
            area = r.area();
        }
    }
    return area > 0;
}

static float distance_label(cv::Mat &osd,
                            const std::vector<cv::Point2f> &corners,
                            const Parameters &parameters)
{
    if (corners.size() != 4) return -1.0f;
    const float target_width = parameters.target_width_x10 / 10.0f;
    const float focal_length =
        static_cast<float>(parameters.focal_length_px);
    const float half = target_width * .5f;
    std::vector<cv::Point3f> object = {
        {-half, -half, 0}, {half, -half, 0},
        {half, half, 0}, {-half, half, 0}
    };
    cv::Mat camera = (cv::Mat_<double>(3, 3) <<
        focal_length, 0, AI_FRAME_WIDTH * .5,
        0, focal_length, AI_FRAME_HEIGHT * .5,
        0, 0, 1);
    cv::Mat distortion = cv::Mat::zeros(1, 5, CV_64F), rotation, translation;
    if (!cv::solvePnP(object, corners, camera, distortion, rotation, translation,
                      false, cv::SOLVEPNP_IPPE_SQUARE)) return -1.0f;
    float distance = static_cast<float>(cv::norm(translation));
    cv::Rect r = cv::boundingRect(corners);
    char text[48];
    snprintf(text, sizeof(text), "Distance: %.1f cm", distance / 10.0f);
    cv::putText(osd, text, cv::Point(r.x, std::max(24, r.y - 8)),
                cv::FONT_HERSHEY_SIMPLEX, .58, cv::Scalar(0, 255, 255, 255), 2);
    return distance / 10.0f;
}

static void process(const cv::Mat &rgb, cv::Mat &osd)
{
    osd.setTo(cv::Scalar(0, 0, 0, 0));
    cv::Mat gray;
    cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    Mode mode = g_mode.load();
    Parameters parameters = parameter_snapshot();
    if (mode == Mode::Edge) {
        cv::Mat blur, edges;
        cv::GaussianBlur(gray, blur,
                         cv::Size(parameters.edge_blur,
                                  parameters.edge_blur), 0);
        cv::Canny(blur, edges, parameters.edge_low, parameters.edge_high);
        cv::Mat rgba;
        cv::cvtColor(edges, rgba, cv::COLOR_GRAY2BGRA);
        rgba.copyTo(osd);
        const dshanpi_vaxp_ai_classification_t result = {
            0, static_cast<float>(cv::countNonZero(edges)) /
                   static_cast<float>(edges.total()), "edge pixels"};
        dshanpi_vaxp_ai_publish_classifications(
            0x0C02, 0x0C02, "Edge Detection", 0, &result, 1);
    } else if (mode == Mode::Circle) {
        cv::Mat blur;
        cv::medianBlur(gray, blur, parameters.circle_blur);
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(
            blur, circles, cv::HOUGH_GRADIENT,
            parameters.circle_dp_x10 / 10.0,
            parameters.circle_min_distance,
            parameters.circle_edge_threshold,
            parameters.circle_center_threshold,
            parameters.circle_min_radius,
            parameters.circle_max_radius);
        std::vector<dshanpi_vaxp_ai_detection_t> wire;
        for (size_t i = 0; i < circles.size() && i < 12; ++i) {
            cv::circle(osd, cv::Point(cvRound(circles[i][0]), cvRound(circles[i][1])),
                       cvRound(circles[i][2]), cv::Scalar(0, 255, 0, 255), 3);
            wire.push_back({
                0, 0, 1.0f, circles[i][0] - circles[i][2],
                circles[i][1] - circles[i][2], circles[i][2] * 2.0f,
                circles[i][2] * 2.0f, 0, "circle", nullptr});
        }
        dshanpi_vaxp_ai_publish_detections(
            0x0C01, 0x0C01, VAXP_TASK_DETECTION, "Circle Detection", 0,
            wire.data(), wire.size());
    } else if (mode == Mode::Blob || mode == Mode::PnpBlob) {
        cv::Rect blob;
        std::vector<dshanpi_vaxp_ai_detection_t> wire;
        std::string metrics;
        if (largest_blob(rgb, blob, parameters)) {
            ios_ui::rounded_rect(osd, blob, cv::Scalar(0, 165, 255, 255), 3);
            if (mode == Mode::PnpBlob) {
                const float distance = distance_label(osd, {
                    cv::Point2f(blob.x, blob.y),
                    cv::Point2f(blob.x + blob.width, blob.y),
                    cv::Point2f(blob.x + blob.width, blob.y + blob.height),
                    cv::Point2f(blob.x, blob.y + blob.height)}, parameters);
                if (distance >= 0.0f) {
                    char text[48];
                    snprintf(text, sizeof(text), "distance_cm=%.1f", distance);
                    metrics = text;
                }
            }
            wire.push_back({
                0, 0, 1.0f, static_cast<float>(blob.x),
                static_cast<float>(blob.y), static_cast<float>(blob.width),
                static_cast<float>(blob.height), 0, "color blob",
                metrics.empty() ? nullptr : metrics.c_str()});
        }
        const uint16_t id = mode == Mode::PnpBlob ? 0x0C06 : 0x0C04;
        dshanpi_vaxp_ai_publish_detections(
            id, id, VAXP_TASK_DETECTION,
            mode == Mode::PnpBlob ? "PnP Blob Distance"
                                  : "Color Blob Detection",
            0, wire.data(), wire.size());
    } else {
        std::vector<cv::Point> polygon;
        std::string metrics;
        if (largest_rectangle(gray, polygon, parameters)) {
            const cv::Point *p = polygon.data();
            int n = 4;
            cv::polylines(osd, &p, &n, 1, true, cv::Scalar(0, 255, 0, 255), 3);
            if (mode == Mode::RectangleCorners)
                for (size_t i = 0; i < polygon.size(); ++i) {
                    cv::circle(osd, polygon[i], 7, cv::Scalar(255, 0, 255, 255), -1);
                    cv::putText(osd, std::to_string(i + 1), polygon[i] + cv::Point(8, -8),
                                cv::FONT_HERSHEY_SIMPLEX, .55,
                                cv::Scalar(255, 255, 255, 255), 2);
                }
            if (mode == Mode::PnpRectangle) {
                cv::RotatedRect box = cv::minAreaRect(polygon);
                cv::Point2f points[4];
                box.points(points);
                const float distance = distance_label(
                    osd, {points[1], points[2], points[3], points[0]},
                    parameters);
                if (distance >= 0.0f) {
                    char text[48];
                    snprintf(text, sizeof(text), "distance_cm=%.1f", distance);
                    metrics = text;
                }
            }
        }
        if (mode == Mode::RectangleCorners) {
            std::vector<dshanpi_vaxp_ai_keypoint_t> points;
            for (const auto &point : polygon)
                points.push_back({static_cast<float>(point.x),
                                  static_cast<float>(point.y), 1.0f});
            const cv::Rect bounds = polygon.empty()
                                        ? cv::Rect() : cv::boundingRect(polygon);
            dshanpi_vaxp_ai_pose_t result = {
                0, polygon.empty() ? 0.0f : 1.0f,
                static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                static_cast<float>(bounds.width),
                static_cast<float>(bounds.height),
                points.data(), points.size(), "rectangle corners"};
            dshanpi_vaxp_ai_publish_poses(
                0x0C05, 0x0C05, "Rectangle Corners", 0,
                polygon.empty() ? nullptr : &result,
                polygon.empty() ? 0u : 1u);
        } else {
            std::vector<dshanpi_vaxp_ai_detection_t> wire;
            if (!polygon.empty()) {
                const cv::Rect bounds = cv::boundingRect(polygon);
                wire.push_back({
                    0, 0, 1.0f, static_cast<float>(bounds.x),
                    static_cast<float>(bounds.y),
                    static_cast<float>(bounds.width),
                    static_cast<float>(bounds.height), 0, "rectangle",
                    metrics.empty() ? nullptr : metrics.c_str()});
            }
            const uint16_t id = mode == Mode::PnpRectangle
                                    ? 0x0C07 : 0x0C03;
            dshanpi_vaxp_ai_publish_detections(
                id, id, VAXP_TASK_DETECTION,
                mode == Mode::PnpRectangle ? "PnP Rectangle Distance"
                                           : "Rectangle Detection",
                0, wire.data(), wire.size());
        }
    }
}

static void draw_centered_text(cv::Mat &osd, const char *text,
                               const cv::Rect &rect, double scale,
                               const cv::Scalar &color, int thickness = 2)
{
    int baseline = 0;
    cv::Size size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX,
                                    scale, thickness, &baseline);
    cv::putText(osd, text,
                cv::Point(rect.x + (rect.width - size.width) / 2,
                          rect.y + (rect.height + size.height) / 2),
                cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness,
                cv::LINE_AA);
}

static void format_parameter_value(const ParameterSpec &spec, int value,
                                   char *text, size_t text_size)
{
    if (spec.scale == 10) {
        snprintf(text, text_size, "%.1f%s", value / 10.0, spec.suffix);
    } else {
        snprintf(text, text_size, "%d%s", value, spec.suffix);
    }
}

static void draw_parameter_panel(cv::Mat &osd)
{
    Mode mode = g_mode.load();
    size_t count = 0;
    const ParameterSpec *specs = parameters_for_mode(mode, count);
    int page_count = std::max(1, static_cast<int>(
        (count + kParametersPerPage - 1) / kParametersPerPage));
    int page = std::max(0, std::min(g_tune_page.load(), page_count - 1));
    Parameters parameters = parameter_snapshot();

    ios_ui::rounded_rect(osd, cv::Rect(78, 68, 550, 400),
                         cv::Scalar(23, 25, 31, 250), cv::FILLED);
    dshanpi_ui::draw_text(
        osd,
        dshanpi_ui::localized("Live parameters", "实时参数",
                              "即時參數", "パラメーター"),
        cv::Point(100, 101), 21, ios_ui::white());
    const int notice = g_parameter_notice.load();
    const char *hint = g_tune_advanced.load()
        ? dshanpi_ui::localized("All controls; changes apply immediately",
                                "全部参数，调整后立即生效",
                                "全部參數，調整後立即生效",
                                "全項目・変更はすぐ反映")
        : dshanpi_ui::localized("Common controls only",
                                "仅显示常用参数", "僅顯示常用參數",
                                "よく使う項目のみ");
    if (notice == 0)
        dshanpi_ui::draw_text(osd, hint, cv::Point(100, 121), 14,
                              ios_ui::secondary());
    ios_ui::rounded_rect(osd, cv::Rect(300, 76, 110, 36),
                         !g_tune_advanced.load() ? ios_ui::accent()
                                                 : cv::Scalar(67, 69, 76, 255),
                         cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::localized("Basic", "基础", "基礎", "基本"),
        cv::Rect(300, 76, 110, 36), 16, ios_ui::white());
    ios_ui::rounded_rect(osd, cv::Rect(416, 76, 146, 36),
                         g_tune_advanced.load() ? ios_ui::accent()
                                               : cv::Scalar(67, 69, 76, 255),
                         cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::localized("Advanced", "高级", "進階", "詳細"),
        cv::Rect(416, 76, 146, 36), 16, ios_ui::white());
    ios_ui::rounded_rect(osd, cv::Rect(570, 76, 42, 36),
                         cv::Scalar(67, 69, 76, 255), cv::FILLED);
    draw_centered_text(osd, "X", cv::Rect(570, 76, 42, 36), .48,
                       ios_ui::white(), 2);

    for (int row = 0; row < kParametersPerPage; ++row) {
        size_t index = static_cast<size_t>(page * kParametersPerPage + row);
        if (index >= count) break;
        int top = 132 + row * 52;
        ios_ui::rounded_rect(osd, cv::Rect(94, top, 518, 44),
                             cv::Scalar(43, 46, 55, 246), cv::FILLED);
        dshanpi_ui::draw_text(osd,
                              localized_parameter_name(specs[index].id),
                              cv::Point(108, top + 30), 18,
                              ios_ui::white());
        ios_ui::rounded_rect(osd, cv::Rect(376, top + 2, 60, 40),
                             cv::Scalar(70, 73, 84, 255), cv::FILLED);
        draw_centered_text(osd, "-", cv::Rect(376, top + 2, 60, 40),
                           .65, ios_ui::white(), 2);
        ios_ui::rounded_rect(osd, cv::Rect(444, top + 2, 90, 40),
                             cv::Scalar(31, 33, 40, 255), cv::FILLED);
        int *value = parameter_value(parameters, specs[index].id);
        char value_text[40];
        format_parameter_value(specs[index], value ? *value : 0,
                               value_text, sizeof(value_text));
        draw_centered_text(osd, value_text, cv::Rect(444, top + 2, 90, 40),
                           .42, cv::Scalar(255, 205, 105, 255), 1);
        ios_ui::rounded_rect(osd, cv::Rect(542, top + 2, 66, 40),
                             ios_ui::accent(), cv::FILLED);
        draw_centered_text(osd, "+", cv::Rect(542, top + 2, 66, 40),
                           .62, ios_ui::white(), 2);
    }

    ios_ui::rounded_rect(osd, cv::Rect(94, 408, 130, 48),
                         cv::Scalar(70, 73, 84, 255), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Defaults),
        cv::Rect(94, 408, 130, 48), 17, ios_ui::white());
    ios_ui::rounded_rect(osd, cv::Rect(236, 408, 130, 48),
                         ios_ui::accent(), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Save),
        cv::Rect(236, 408, 130, 48), 18, ios_ui::white());
    ios_ui::rounded_rect(osd, cv::Rect(388, 408, 58, 48),
                         page > 0 ? cv::Scalar(70, 73, 84, 255)
                                  : cv::Scalar(45, 47, 54, 255),
                         cv::FILLED);
    draw_centered_text(osd, "<", cv::Rect(388, 408, 58, 48), .50,
                       page > 0 ? ios_ui::white() : ios_ui::secondary(), 1);
    ios_ui::rounded_rect(osd, cv::Rect(458, 408, 58, 48),
                         page + 1 < page_count
                             ? cv::Scalar(70, 73, 84, 255)
                             : cv::Scalar(45, 47, 54, 255),
                         cv::FILLED);
    draw_centered_text(osd, ">", cv::Rect(458, 408, 58, 48), .50,
                       page + 1 < page_count ? ios_ui::white()
                                             : ios_ui::secondary(), 1);
    char page_text[20];
    snprintf(page_text, sizeof(page_text), "%d / %d", page + 1, page_count);
    draw_centered_text(osd, page_text, cv::Rect(526, 408, 82, 48), .38,
                       ios_ui::secondary(), 1);

    if (notice != 0) {
        const char *message = notice == 1
            ? dshanpi_ui::common_text(dshanpi_ui::CommonText::Saved)
            : notice == 2
                  ? dshanpi_ui::localized(
                        "Defaults restored", "已恢复默认", "已恢復預設",
                        "初期値に戻しました")
                  : dshanpi_ui::common_text(
                        dshanpi_ui::CommonText::SaveFailed);
        dshanpi_ui::draw_text(
            osd, message, cv::Point(100, 121), 14,
            notice < 0 ? cv::Scalar(80, 100, 255, 255)
                       : cv::Scalar(110, 235, 160, 255));
    }
}

static void draw_controls(cv::Mat &osd)
{
    ios_ui::rounded_rect(osd, cv::Rect(12, 12, 52, 52), cv::Scalar(40, 40, 40, 230), cv::FILLED);
    cv::putText(osd, "<", cv::Point(29, 46), cv::FONT_HERSHEY_SIMPLEX,
                .62, cv::Scalar(255, 255, 255, 255), 2);
    ios_ui::rounded_rect(osd, cv::Rect(104, 12, 84, 52),
                         g_tune_open.load() ? ios_ui::accent()
                                            : cv::Scalar(40, 40, 40, 230),
                         cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Tune),
        cv::Rect(104, 12, 84, 52), 20,
        cv::Scalar(255, 255, 255, 255));
    dshanpi_ui::draw_mode_header(
        osd, localized_mode_name(g_mode.load()), 52);
    ios_ui::rounded_rect(osd, cv::Rect(500, 12, 128, 52), cv::Scalar(40, 40, 40, 230), cv::FILLED);
    dshanpi_ui::draw_text_centered(
        osd, dshanpi_ui::common_text(dshanpi_ui::CommonText::Mode),
        cv::Rect(500, 12, 128, 52), 20,
        cv::Scalar(255, 255, 255, 255));
    if (g_tune_open.load()) {
        draw_parameter_panel(osd);
        return;
    }
    if (!g_menu_open.load()) return;
    ios_ui::rounded_rect(osd, cv::Rect(306, 70, 322, 358), cv::Scalar(28, 32, 40, 245), cv::FILLED);
    for (int i = 0; i < static_cast<int>(Mode::Count); ++i) {
        int top = 74 + i * 50;
        if (i == static_cast<int>(g_mode.load()))
            ios_ui::rounded_rect(osd, cv::Rect(312, top, 310, 44), ios_ui::accent(), cv::FILLED);
        dshanpi_ui::draw_text(osd,
                              localized_mode_name(static_cast<Mode>(i)),
                              cv::Point(324, top + 30), 18,
                              cv::Scalar(255, 255, 255, 255));
    }
}

int main(int argc, char **argv)
{
    int csi = argc > 1 ? atoi(argv[1]) : 2;
    if (csi != 0 && csi != 2) csi = 2;
    load_tune_level();
    load_parameters();
    g_mode = static_cast<Mode>(dshanpi_mode_state::load(
        "cv_lite", static_cast<int>(Mode::Circle),
        static_cast<int>(Mode::Count)));
    const dshanpi_vaxp_ai_config_t vaxp_config = {
        "CV Lite", static_cast<uint8_t>(csi),
        AI_FRAME_WIDTH, AI_FRAME_HEIGHT,
        VAXP_CAP_DETECTION | VAXP_CAP_CLASSIFICATION |
            VAXP_CAP_POSE | VAXP_CAP_MULTI_MODEL};
    if (dshanpi_vaxp_ai_start(&vaxp_config) != 0) {
        std::cerr << "[cv-lite] UART2 VAXP output unavailable" << std::endl;
    } else {
        static const char *const circle[] = {"circle"};
        static const char *const edge[] = {"edge pixels"};
        static const char *const rectangle[] = {"rectangle"};
        static const char *const blob[] = {"color blob"};
        dshanpi_vaxp_ai_register_classes(0x0C01, circle, 1);
        dshanpi_vaxp_ai_register_classes(0x0C02, edge, 1);
        dshanpi_vaxp_ai_register_classes(0x0C03, rectangle, 1);
        dshanpi_vaxp_ai_register_classes(0x0C04, blob, 1);
        dshanpi_vaxp_ai_register_classes(0x0C06, blob, 1);
        dshanpi_vaxp_ai_register_classes(0x0C07, rectangle, 1);
    }
    CameraManager camera(0, csi);
    if (camera.Create() != 0) return 1;
    std::thread touch_thread(touch_proc);
    cv::Mat osd(OSD_HEIGHT, OSD_WIDTH, CV_8UC4);
    int startup_phase = 0;
    dshanpi_ui::draw_startup_spinner(osd, startup_phase);
    camera.InsertFrame(osd.data);
    const size_t plane = AI_FRAME_WIDTH * AI_FRAME_HEIGHT;
    std::vector<uint8_t> rotated(plane * 3);
    cv::Mat channels[3] = {
        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, rotated.data()),
        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, rotated.data() + plane),
        cv::Mat(AI_FRAME_HEIGHT, AI_FRAME_WIDTH, CV_8UC1, rotated.data() + plane * 2)
    };
    cv::Mat rgb;
    while (!g_stop.load()) {
        DumpRes frame;
        if (camera.GetFrame(frame) != 0) {
            startup_phase = (startup_phase + 18) % 360;
            dshanpi_ui::draw_startup_spinner(osd, startup_phase);
            camera.InsertFrame(osd.data);
            usleep(40000);
            continue;
        }
        const uint8_t *src = reinterpret_cast<const uint8_t *>(frame.virt_addr);
        for (int c = 0; c < 3; ++c)
            std::reverse_copy(src + c * plane, src + (c + 1) * plane,
                              rotated.data() + c * plane);
        cv::merge(channels, 3, rgb);
        process(rgb, osd);
        draw_controls(osd);
        camera.InsertFrame(osd.data);
        camera.ReleaseFrame(frame);
    }
    touch_thread.join();
    dshanpi_vaxp_ai_stop();
    camera.Destroy();
    usleep(200000);
    return 0;
}
