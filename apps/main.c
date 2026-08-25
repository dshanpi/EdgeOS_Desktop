#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* RT-Smart private syscall ABI: NRSYS(exec), see syscall_no.h. */
#define RTSMART_NRSYS_EXEC 13
#define RTSMART_NRSYS_WAITPID 110
#define DESKTOP_EXIT_FACE_STUDIO 42
#define DESKTOP_EXIT_FACE_GEOMETRY 43
#define DESKTOP_EXIT_HAND_STUDIO 44
#define DESKTOP_EXIT_HUMAN_STUDIO 45
#define DESKTOP_EXIT_SMART_DRIVING 46
#define DESKTOP_EXIT_OCR_DETECTION 47
#define DESKTOP_EXIT_YOLOV8_VISION 48
#define DESKTOP_EXIT_NETWORK_CAMERA 49
#define DESKTOP_EXIT_YOLO_MODELS 50
#define DESKTOP_EXIT_GALLERY_PLAYER 51
#define DESKTOP_EXIT_CV_LITE 52
#define DESKTOP_EXIT_PLATE_OCR 53
#define DESKTOP_EXIT_CODE_SCANNER 54
#define DESKTOP_EXIT_SELF_LEARNING 55
#define DESKTOP_EXIT_UVC_CAMERA 56
#define DESKTOP_EXIT_CLOUD_MODEL 57
#define DESKTOP_EXIT_RTMP_STREAM_LEGACY 58
#define DESKTOP_EXIT_RECOVER 100
#define GALLERY_PLAY_REQUEST "/data/dshanpi_gallery_video"
#define LAUNCH_ERROR_PATH "/data/dshanpi_launch_error"
#define CLOUD_MODEL_REQUEST_PATH "/tmp/dshanpi_cloud_model_request"
#define CLOUD_IMAGE_REQUEST_PATH "/tmp/dshanpi_cloud_image_request"
#define CLOUD_MODEL_RESULT_PATH "/tmp/dshanpi_cloud_model_result"
#define DESKTOP_STATUS_PATH "/tmp/dshanpi_desktop_status"

#include "camera_manager.h"
#include "dual_camera_manager.h"
#include "camera_settings.h"
#include "system_settings.h"
#include "power_control.h"
#include "ota_update.h"
#include "screenshot_service.h"
#include "ai_registry.h"
#include "ui_font.h"
#include "hal_netmgmt.h"
#include "lvgl.h"
#include "material_app_icons.h"
#include "screensaver_asset.h"
#include "uart_lab.h"
#include "vaxp_lab.h"
#include "drv_uart.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/libs/tjpgd/tjpgd.h"
#include "lv_k230_display.h"
#include "lv_k230_input_touch.h"
#include "kplayer.h"
#include "mp4_format.h"
#include "sdk_version.h"

#include "k_gsdma_comm.h"
#include "mpi_vb_api.h"
#include "mpi_uvc_api.h"

#define UI_WIDTH 640
#define UI_HEIGHT 480
#define MAX_DISPLAY_WIDTH 1920
#define MAX_DISPLAY_HEIGHT 1080
#define PAINT_CANVAS_WIDTH  600
#define PAINT_CANVAS_HEIGHT 350
#define PAINT_TOUCH_DEBUG 0
#define UI_TOUCH_DEBUG 0
#define WIFI_KEYBOARD_DEBUG 0
#define EDGEOS_SOFTWARE_RENDER_FAST_PATH 1
#define PAINT_RECONNECT_MS 100U
#define PAINT_RECONNECT_DISTANCE 80
#define TAP_GUARD_DISTANCE LV_K230_TOUCH_DRAG_THRESHOLD
#define DUAL_PIP_WIDTH 224
#define DUAL_PIP_HEIGHT 168
#define DUAL_PIP_MARGIN 12
#define DUAL_PIP_DEFAULT_X 392
#define DUAL_PIP_DEFAULT_Y 24
#define DUAL_PIP_DRAG_THRESHOLD 14
#define DESKTOP_SCROLL_STATE_PATH "/tmp/dshanpi_desktop_scroll"
#define UART_LOG_CAPACITY 8192
#define UART_MAX_PAYLOAD 256
#define UART_VAXP_PENDING_CAPACITY VAXP_DEFAULT_MAX_PENDING
#define UART_VAXP_RESPONSE_CACHE_CAPACITY 32u
#define UART_VAXP_MAX_RETRIES 3u
#define UART_VAXP_ACK_TIMEOUT_MS 500u
#define SETTINGS_SECTION_COUNT 10
#define SCREENSAVER_TOP_GESTURE_Y 56
#define SCREENSAVER_SWIPE_DISTANCE 80
#define UART_VAXP_SUPPORTED_FLAGS \
    (VAXP_FLAG_ACK_REQUIRED | VAXP_FLAG_URGENT)

typedef enum {
    MATERIAL_ICON_SETTINGS = 0,
    MATERIAL_ICON_CAMERA,
    MATERIAL_ICON_GALLERY,
    MATERIAL_ICON_FACE,
    MATERIAL_ICON_FACE_GEOMETRY,
    MATERIAL_ICON_HAND,
    MATERIAL_ICON_HUMAN,
    MATERIAL_ICON_DRIVING,
    MATERIAL_ICON_OCR,
    MATERIAL_ICON_DETECTION,
    MATERIAL_ICON_YOLO,
    MATERIAL_ICON_STREAM,
    MATERIAL_ICON_DRAWING,
    MATERIAL_ICON_CV,
    MATERIAL_ICON_PLATE,
    MATERIAL_ICON_SCANNER,
    MATERIAL_ICON_LEARNING,
    MATERIAL_ICON_CLOUD,
    MATERIAL_ICON_USB,
    MATERIAL_ICON_UART,
    MATERIAL_ICON_DUAL_CAMERA,
    MATERIAL_ICON_RTMP,
} material_icon_id_t;

typedef struct {
    const char *name;
    const char *symbol;
    const char *description;
    uint32_t color;
    uint32_t icon_color;
    material_icon_id_t icon_id;
} app_info_t;

typedef enum {
    APP_SETTINGS, APP_CAMERA, APP_GALLERY, APP_FACE_STUDIO, APP_FACE_GEOMETRY,
    APP_HAND_STUDIO, APP_HUMAN_STUDIO, APP_SMART_DRIVING,
    APP_OCR_DETECTION, APP_OBJECT_DETECTION, APP_YOLO_MODELS,
    APP_NETWORK_CAMERA, APP_DRAWING, APP_CV_LITE,
    APP_PLATE_OCR, APP_CODE_SCANNER, APP_SELF_LEARNING, APP_CLOUD_MODEL,
    APP_UVC_CAMERA, APP_UART_LAB, APP_DUAL_CAMERA,
    APP_RTMP_STREAM_LEGACY,
    APP_COUNT
} desktop_app_id_t;

typedef enum {
    CLOUD_TASK_CLASSIFICATION = 0,
    CLOUD_TASK_DETECTION,
    CLOUD_TASK_SEGMENTATION,
    CLOUD_TASK_OCR_DETECTION,
    CLOUD_TASK_OCR_RECOGNITION,
    CLOUD_TASK_OCR_PIPELINE,
    CLOUD_TASK_METRIC_LEARNING,
    CLOUD_TASK_MULTILABEL,
    CLOUD_TASK_COUNT
} cloud_task_id_t;

typedef struct {
    const char *name;
    const char *executable;
    const char *result_file;
    bool needs_dictionary;
    bool dual_config;
    bool supports_video;
} cloud_task_info_t;

static const cloud_task_info_t g_cloud_tasks[CLOUD_TASK_COUNT] = {
    {"Image Classification", "classification.elf", "cls_result.jpg", false, false, true},
    {"Object Detection", "detection.elf", "det_result.jpg", false, false, true},
    {"Semantic Segmentation", "segmentation.elf", "seg_result.jpg", false, false, true},
    {"OCR Detection", "ocr_detection.elf", "ocrdet_result.jpg", false, false, true},
    {"OCR Recognition", "ocr_recognition.elf", "ocrrec_result.txt", true, false, false},
    {"OCR Detect + Recognize", "ocr.elf", "ocr_result.jpg", true, true, true},
    {"Metric Learning", "metric_learning.elf", "result_0.bin", false, false, true},
    {"Multi-label Classification", "multilabel_cls.elf", "multl_result.jpg", false, false, true},
};

static dshanpi_system_settings_t g_system_settings;

static const app_info_t g_apps[] = {
    {"Settings", LV_SYMBOL_SETTINGS, "System preferences",
     0xEAE2F8, 0x65558F, MATERIAL_ICON_SETTINGS},
    {"Camera", LV_SYMBOL_IMAGE, "Camera preview and photos",
     0xE1E7FF, 0x415F91, MATERIAL_ICON_CAMERA},
    {"Gallery", LV_SYMBOL_IMAGE, "Photos captured by the AI module",
     0xDCEFE5, 0x356859, MATERIAL_ICON_GALLERY},
    {"Face Studio", LV_SYMBOL_EYE_OPEN, "Face detection and analysis",
     0xEEE3F7, 0x67507E, MATERIAL_ICON_FACE},
    {"Face Geometry", "3D", "Pose, mesh, parse and alignment",
     0xE1E8F8, 0x355F9E, MATERIAL_ICON_FACE_GEOMETRY},
    {"Hand Studio", "H", "Hand detection, keypoints and gestures",
     0xFCE4C5, 0xA84700, MATERIAL_ICON_HAND},
    {"Human Studio", "P", "Person, pose, fitness and safety",
     0xDDE8F8, 0x315DA8, MATERIAL_ICON_HUMAN},
    {"Smart Driving", "D", "Road and driving safety detection",
     0xDDEDE3, 0x346B57, MATERIAL_ICON_DRIVING},
    {"OCR Detection", "OCR", "Real-time text detection and recognition",
     0xE4EEE1, 0x496548, MATERIAL_ICON_OCR},
    {"Object Detection", "DET", "Detection and segmentation",
     0xDDEAF3, 0x35647B, MATERIAL_ICON_DETECTION},
    {"YOLO Models", "YOLO", "YOLOv5, YOLOv8, YOLO11 and YOLO26",
     0xE7E3F4, 0x5D5790, MATERIAL_ICON_YOLO},
    {"Network Camera", "NET", "RTSP and RTMP network camera",
     0xE4E8ED, 0x4F616E, MATERIAL_ICON_STREAM},
    {"Drawing", LV_SYMBOL_EDIT, "Touch drawing board",
     0xE7E3F4, 0x65558F, MATERIAL_ICON_DRAWING},
    {"CV Lite", "CV", "Classic real-time computer vision",
     0xDCEEEB, 0x246B61, MATERIAL_ICON_CV},
    {"Plate OCR", "LP", "Licence plate detection and recognition",
     0xDFE9F8, 0x3E5F8F, MATERIAL_ICON_PLATE},
    {"Code Scanner", "QR", "Barcode, QR code and AprilTag tools",
     0xF8E7CD, 0x8A5700, MATERIAL_ICON_SCANNER},
    {"AI Learning", "AI", "Draw, learn and classify objects",
     0xF4E8B8, 0x735C00, MATERIAL_ICON_LEARNING},
    {"Cloud Model", "K", "Deploy CanMV Cloud training models",
     0xEAE2F8, 0x65558F, MATERIAL_ICON_CLOUD},
    {"USB Camera", "USB", "Preview a connected UVC camera",
     0xDDEAF3, 0x35647B, MATERIAL_ICON_USB},
    {"UART Lab", "UART", "Serial terminal and loopback tester",
     0xDCEBFA, 0x285C85, MATERIAL_ICON_UART},
    {"Dual Camera", LV_SYMBOL_VIDEO, "Front + rear picture-in-picture video",
     0xE1E7FF, 0x415F91, MATERIAL_ICON_DUAL_CAMERA},
    {"Network Camera (legacy)", "NET", "Legacy migration entry",
     0xFBE8DA, 0x9A4A18, MATERIAL_ICON_RTMP},
};

/* Keep the stable application IDs used by launch/autostart code, while
 * presenting the two camera experiences next to one another on the desktop. */
static const desktop_app_id_t g_desktop_app_order[APP_COUNT - 1] = {
    APP_SETTINGS, APP_CAMERA, APP_DUAL_CAMERA, APP_GALLERY,
    APP_FACE_STUDIO, APP_FACE_GEOMETRY, APP_HAND_STUDIO, APP_HUMAN_STUDIO,
    APP_SMART_DRIVING, APP_OCR_DETECTION, APP_OBJECT_DETECTION,
    APP_YOLO_MODELS, APP_NETWORK_CAMERA, APP_DRAWING, APP_CV_LITE,
    APP_PLATE_OCR, APP_CODE_SCANNER, APP_SELF_LEARNING, APP_CLOUD_MODEL,
    APP_UVC_CAMERA, APP_UART_LAB,
};

static const char *const g_app_names[DSHANPI_LANG_COUNT][APP_COUNT] = {
    {"设置", "相机", "相册", "人脸工作室", "人脸几何", "手部工作室",
     "人体工作室", "智能驾驶", "OCR文字检测", "目标检测",
     "YOLO多版本", "网络摄像机", "画板", "CV Lite", "车牌OCR", "条码处理", "AI自学习", "云平台模型", "USB摄像头", "UART调试器", "双摄相机", "网络摄像机"},
    {"設定", "相機", "相簿", "人臉工作室", "人臉幾何", "手部工作室",
     "人體工作室", "智慧駕駛", "OCR文字辨識", "目標偵測",
     "YOLO多版本", "網路攝影機", "畫板", "CV Lite", "車牌OCR", "條碼處理", "AI自學習", "雲端平台模型", "USB攝影機", "UART調試器", "雙攝相機", "網路攝影機"},
    {"Settings", "Camera", "Gallery", "Face Studio", "Face Geometry", "Hand Studio",
     "Human Studio", "Smart Driving", "OCR Detection", "Object Detection",
     "YOLO Models", "Network Camera", "Drawing", "CV Lite", "Plate OCR", "Code Scanner", "AI Learning", "Cloud Model", "USB Camera", "UART Lab", "Dual Camera", "Network Camera"},
    {"設定", "カメラ", "ギャラリー", "顔スタジオ", "顔ジオメトリ", "手スタジオ",
     "人体スタジオ", "スマート運転", "OCR文字認識", "物体検出",
     "YOLOモデル", "ネットワークカメラ", "お絵描き", "CV Lite", "ナンバーOCR", "コード読取", "AI学習", "クラウドモデル", "USBカメラ", "UARTラボ", "デュアルカメラ", "ネットワークカメラ"},
};

static const char *localized_app_name(const app_info_t *app)
{
    size_t index = (size_t)(app - g_apps);
    dshanpi_language_t language = g_system_settings.language;
    if (index >= sizeof(g_apps) / sizeof(g_apps[0])) {
        return app->name;
    }
    if (language < 0 || language >= DSHANPI_LANG_COUNT) {
        language = DSHANPI_LANG_EN;
    }
    return g_app_names[language][index];
}

static const char *settings_text(const char *english)
{
    static const char *const translated[][4] = {
        {"Settings", "设置", "設定", "設定"},
        {"Wi-Fi", "无线网络", "無線網路", "Wi-Fi"},
        {"Language", "语言", "語言", "言語"},
        {"Date & Time", "日期和时间", "日期與時間", "日付と時刻"},
        {"Startup App", "开机自启", "開機自啟", "起動アプリ"},
        {"Default Camera", "默认相机", "預設相機", "デフォルトカメラ"},
        {"VAXP UART", "VAXP 串口", "VAXP 串口", "VAXP UART"},
        {"Sleep & Screen Saver", "休眠与屏保", "休眠與螢幕保護",
         "スリープとスクリーンセーバー"},
        {"Screen saver after inactivity", "无操作后自动进入屏保",
         "無操作後自動進入螢幕保護", "無操作時にスクリーンセーバーを表示"},
        {"Sleep timer saved", "休眠时间已保存", "休眠時間已儲存",
         "スリープ時間を保存しました"},
        {"Screenshot saved", "截屏已保存", "截圖已儲存",
         "スクリーンショットを保存しました"},
        {"Select", "选择", "選擇", "選択"},
        {"Selected: %u", "已选择：%u", "已選擇：%u", "%u 件選択中"},
        {"Delete", "删除", "刪除", "削除"},
        {"Delete %u selected items?", "删除选中的 %u 项？",
         "刪除選取的 %u 項？", "選択した %u 件を削除しますか？"},
        {"This action cannot be undone.", "此操作无法撤销。",
         "此操作無法復原。", "この操作は元に戻せません。"},
        {"Deleted %u items", "已删除 %u 项", "已刪除 %u 項",
         "%u 件を削除しました"},
        {"Deleted", "已删除", "已刪除", "削除しました"},
        {"Deleted %u items, %u failed",
         "已删除 %u 项，%u 项失败", "已刪除 %u 項，%u 項失敗",
         "%u 件を削除、%u 件失敗"},
        {"Delete failed", "删除失败", "刪除失敗", "削除に失敗しました"},
        {"No photos or videos", "没有照片或视频", "沒有照片或影片",
         "写真または動画がありません"},
        {"Sleep timer save failed", "休眠时间保存失败", "休眠時間儲存失敗",
         "スリープ時間を保存できません"},
        {"Never", "从不", "永不", "なし"},
        {"Swipe up to unlock", "向上滑动解锁", "向上滑動解鎖",
         "上にスワイプして解除"},
        {"Power", "电源", "電源", "電源"},
        {"System Update", "系统更新", "系統更新", "システム更新"},
        {"About", "关于", "關於", "このシステムについて"},
        {"A/B protected OTA", "A/B 安全更新", "A/B 安全更新",
         "A/B 保護付き更新"},
        {"Network download", "网络下载", "網路下載",
         "ネットワーク更新"},
        {"Ready to update", "可以开始更新", "可以開始更新",
         "更新できます"},
        {"Downloading update package", "正在下载更新包",
         "正在下載更新包", "更新パッケージをダウンロード中"},
        {"Verifying and writing inactive slot", "正在校验并写入备用槽",
         "正在驗證並寫入備用槽", "検証して非アクティブスロットへ書き込み中"},
        {"Update installed - restarting automatically",
         "更新安装完成，正在自动重启", "更新安裝完成，正在自動重新啟動",
         "更新完了。自動的に再起動しています"},
        {"Update ready - restart to apply", "更新已就绪，重启后生效",
         "更新已就緒，重新啟動後生效", "更新準備完了。再起動してください"},
        {"Update failed - current system is unchanged",
         "更新失败，当前系统未改变", "更新失敗，目前系統未改變",
         "更新に失敗しました。現在のシステムは変更されていません"},
        {"Power loss or download failure keeps the active slot bootable.",
         "断电或下载失败不会破坏当前可启动系统。",
         "斷電或下載失敗不會破壞目前可啟動系統。",
         "停電やダウンロード失敗でも現在のスロットを保持します。"},
        {"Update already running", "更新正在进行中", "更新正在進行中",
         "更新処理中です"},
        {"System Version", "系统版本", "系統版本", "システムバージョン"},
        {"Model Name", "型号名称", "型號名稱", "モデル名"},
        {"Operating System", "操作系统", "作業系統", "オペレーティングシステム"},
        {"nncase Version", "nncase 版本", "nncase 版本", "nncase バージョン"},
        {"DongshanPI CanMV-K230", "东山派 CanMV-K230",
         "東山派 CanMV-K230", "DongshanPI CanMV-K230"},
        {"RT-Smart real-time operating system", "RT-Smart 实时操作系统",
         "RT-Smart 即時作業系統", "RT-Smart リアルタイムOS"},
        {"Choose compatibility or high-speed transport",
         "选择兼容或高速传输模式", "選擇相容或高速傳輸模式",
         "互換または高速通信を選択"},
        {"Applies when an AI app or UART Lab opens",
         "下次打开 AI 应用或 UART 调试器时生效",
         "下次開啟 AI 應用或 UART 調試器時生效",
         "次回 AI アプリまたは UART ラボ起動時に適用"},
        {"Not connected", "未连接", "未連線", "未接続"},
        {"Not Connected", "未连接", "未連線", "未接続"},
        {"Desktop", "桌面", "桌面", "デスクトップ"},
        {"Restart, flash, or shut down", "重启、烧录或关机",
         "重新啟動、燒錄或關機", "再起動、書き込み、電源オフ"},
        {"Choose the display language", "选择显示语言", "選擇顯示語言",
         "表示言語を選択"},
        {"Failed to save language setting", "无法保存语言设置",
         "無法儲存語言設定", "言語設定を保存できません"},
        {"Language is already active", "该语言已经生效",
         "該語言已經生效", "この言語はすでに有効です"},
        {"Time zone", "时区", "時區", "タイムゾーン"},
        {"Time zone applied", "时区已应用", "時區已套用",
         "タイムゾーンを適用しました"},
        {"Open this app automatically after power-on",
         "开机后自动打开此应用", "開機後自動開啟此應用",
         "起動後にこのアプリを自動で開きます"},
        {"Startup app saved", "开机自启应用已保存", "開機自啟應用已儲存",
         "起動アプリを保存しました"},
        {"Used by Camera and all AI applications",
         "用于相机和所有 AI 应用", "用於相機和所有 AI 應用",
         "カメラとすべての AI アプリで使用します"},
        {"Rear", "后摄", "後鏡頭", "背面カメラ"},
        {"Front", "前摄", "前鏡頭", "前面カメラ"},
        {"Changes apply after reboot", "更改将在重启后生效",
         "變更將在重新啟動後生效", "変更は再起動後に反映されます"},
        {"Failed to save camera setting", "无法保存相机设置",
         "無法儲存相機設定", "カメラ設定を保存できません"},
        {"Camera setting save failed", "相机设置保存失败",
         "相機設定儲存失敗", "カメラ設定の保存に失敗しました"},
        {"%s camera selected", "已选择%s", "已選擇%s", "%sを選択しました"},
        {"Camera is already active", "该相机已经生效", "該相機已經生效",
         "このカメラはすでに有効です"},
        {"Restart required", "需要重新启动", "需要重新啟動",
         "再起動が必要です"},
        {"Restarting in %d seconds to apply the camera change.",
         "相机更改将在 %d 秒后重启生效。",
         "相機變更將在 %d 秒後重新啟動生效。",
         "カメラ変更を反映するため %d 秒後に再起動します。"},
        {"Restarting in %d seconds to apply the language change.",
         "语言更改将在 %d 秒后重启生效。",
         "語言變更將在 %d 秒後重新啟動生效。",
         "言語変更を反映するため %d 秒後に再起動します。"},
        {"Restart now", "立即重启", "立即重新啟動", "今すぐ再起動"},
        {"Restart later", "稍后重启", "稍後重新啟動", "後で再起動"},
        {"Camera change saved. Restart later to apply.",
         "相机更改已保存，请稍后重启使其生效。",
         "相機變更已儲存，請稍後重新啟動使其生效。",
         "カメラ変更を保存しました。後で再起動してください。"},
        {"Language change saved. Restart later to apply.",
         "语言更改已保存，请稍后重启使其生效。",
         "語言變更已儲存，請稍後重新啟動使其生效。",
         "言語変更を保存しました。後で再起動してください。"},
        {"Restarting...", "正在重启...", "正在重新啟動...", "再起動中..."},
        {"Restart failed", "重启失败", "重新啟動失敗", "再起動に失敗しました"},
        {"Restart, enter flashing mode, or safely shut down the device",
         "重新启动、进入烧录模式或安全关闭设备",
         "重新啟動、進入燒錄模式或安全關閉裝置",
         "再起動、書き込みモード、または安全に電源を切ります"},
        {"Restart", "重新启动", "重新啟動", "再起動"},
        {"Flashing Mode", "烧录模式", "燒錄模式", "書き込みモード"},
        {"Shut down", "关机", "關機", "電源オフ"},
        {"Restart the device?", "要重新启动设备吗？", "要重新啟動裝置嗎？",
         "デバイスを再起動しますか？"},
        {"Enter flashing mode?", "要进入烧录模式吗？",
         "要進入燒錄模式嗎？", "書き込みモードに入りますか？"},
        {"Shut down the device?", "要关闭设备吗？", "要關閉裝置嗎？",
         "デバイスの電源を切りますか？"},
        {"Entering flashing mode...", "正在进入烧录模式...",
         "正在進入燒錄模式...", "書き込みモードに移行中..."},
        {"Shutting down...", "正在关机...", "正在關機...", "電源を切っています..."},
        {"Unsaved work may be lost.", "未保存的内容可能会丢失。",
         "未儲存的內容可能會遺失。", "未保存の内容は失われる場合があります。"},
        {"Cancel", "取消", "取消", "キャンセル"},
        {"Done", "完成", "完成", "完了"},
        {"Current Selection", "当前选择", "目前選擇", "現在の選択"},
        {"Confirm", "确认", "確認", "確認"},
        {"Power command failed", "电源操作失败", "電源操作失敗",
         "電源操作に失敗しました"},
        {"AVAILABLE NETWORKS", "可用网络", "可用網路", "利用可能なネットワーク"},
        {"Scanning...", "正在扫描...", "正在掃描...", "スキャン中..."},
        {"Scan failed - check Wi-Fi module", "扫描失败，请检查 Wi-Fi 模块",
         "掃描失敗，請檢查 Wi-Fi 模組", "スキャン失敗。Wi-Fi を確認してください"},
        {"%d available networks", "发现 %d 个可用网络", "發現 %d 個可用網路",
         "%d 件のネットワーク"},
        {"Connected", "已连接", "已連線", "接続済み"},
        {"Connection started", "正在建立连接", "正在建立連線", "接続を開始しました"},
        {"Wi-Fi connected", "Wi-Fi 已连接", "Wi-Fi 已連線", "Wi-Fi に接続しました"},
        {"Connection failed", "连接失败", "連線失敗", "接続に失敗しました"},
        {"Wrong password or connection failed", "密码错误或连接失败",
         "密碼錯誤或連線失敗", "パスワードが違うか接続に失敗しました"},
        {"Unable to start scan", "无法开始扫描", "無法開始掃描",
         "スキャンを開始できません"},
        {"Reconnecting saved Wi-Fi...", "正在重新连接已保存的 Wi-Fi...",
         "正在重新連線已儲存的 Wi-Fi...", "保存済み Wi-Fi に再接続中..."},
        {"Wi-Fi password must contain at least 8 characters",
         "Wi-Fi 密码至少需要 8 个字符", "Wi-Fi 密碼至少需要 8 個字元",
         "Wi-Fi パスワードは8文字以上必要です"},
        {"Wi-Fi is busy", "Wi-Fi 正忙", "Wi-Fi 忙碌中", "Wi-Fi は処理中です"},
        {"Authenticating...", "正在验证...", "正在驗證...", "認証中..."},
        {"Unable to start connection", "无法开始连接", "無法開始連線",
         "接続を開始できません"},
        {"Connect to %s", "连接到 %s", "連線到 %s", "%s に接続"},
        {"IP Address  %s", "IP 地址  %s", "IP 位址  %s", "IP アドレス  %s"},
        {"Wait for the current Wi-Fi operation", "请等待当前 Wi-Fi 操作完成",
         "請等待目前 Wi-Fi 操作完成", "現在の Wi-Fi 操作が終わるまでお待ちください"},
        {"Saved network removed", "已移除保存的网络", "已移除儲存的網路",
         "保存済みネットワークを削除しました"},
        {"Wi-Fi network forgotten", "已忘记 Wi-Fi 网络", "已忘記 Wi-Fi 網路",
         "Wi-Fi ネットワークを削除しました"},
        {"Auto-Join enabled", "自动加入已开启", "自動加入已開啟", "自動接続を有効にしました"},
        {"Auto-Join disabled", "自动加入已关闭", "自動加入已關閉", "自動接続を無効にしました"},
        {"Connect to Wi-Fi", "连接 Wi-Fi", "連線 Wi-Fi", "Wi-Fi に接続"},
        {"Wi-Fi password", "Wi-Fi 密码", "Wi-Fi 密碼", "Wi-Fi パスワード"},
        {"Forget", "忘记网络", "忘記網路", "削除"},
        {"Auto-Join", "自动加入", "自動加入", "自動接続"},
        {"Connect", "连接", "連线", "接続"},
        {"VAXP baud save failed", "VAXP 波特率保存失败",
         "VAXP 波特率儲存失敗", "VAXP ボーレートの保存に失敗しました"},
        {"VAXP baud rate saved", "VAXP 波特率已保存",
         "VAXP 波特率已儲存", "VAXP ボーレートを保存しました"},
        {"%lu baud", "%lu 波特率", "%lu 波特率", "%lu ボーレート"},
    };
    if (g_system_settings.language == DSHANPI_LANG_EN) {
        return english;
    }
    int column = g_system_settings.language == DSHANPI_LANG_ZH_CN ? 1 :
                 g_system_settings.language == DSHANPI_LANG_ZH_TW ? 2 : 3;
    for (size_t i = 0; i < sizeof(translated) / sizeof(translated[0]); ++i) {
        if (strcmp(english, translated[i][0]) == 0) {
            return translated[i][column];
        }
    }
    return english;
}

static const lv_font_t *settings_ui_font(const lv_font_t *english_font)
{
    return g_system_settings.language == DSHANPI_LANG_EN
               ? english_font : &ui_font_source_han_20;
}

static const char *uart_text(const char *english)
{
    static const char *const translated[][4] = {
        {"UART Lab", "UART 调试器", "UART 調試器", "UART ラボ"},
        {"Disconnected", "未连接", "未連線", "未接続"},
        {"BAUD RATE", "波特率", "波特率", "ボーレート"},
        {"FRAME", "帧格式", "幀格式", "フレーム"},
        {"MODE", "模式", "模式", "モード"},
        {"LOOPBACK TEST", "回环测试", "迴路測試", "ループバック"},
        {"PROTOCOL TEST", "协议测试", "協議測試", "プロトコル試験"},
        {"WIRING", "接线", "接線", "配線"},
        {"VAXP MONITOR", "VAXP 监视器", "VAXP 監視器", "VAXP モニター"},
        {"SERIAL MONITOR", "串口监视器", "串口監視器", "シリアルモニター"},
        {"CLEAR", "清空", "清除", "消去"},
        {"Waiting for serial data...", "等待串口数据...",
         "等待串口資料...", "シリアルデータ待機中..."},
        {"SEND", "发送", "傳送", "送信"},
        {"TEXT", "文本", "文字", "テキスト"},
        {"CRLF ON", "CRLF 开", "CRLF 開", "CRLF オン"},
        {"CRLF OFF", "CRLF 关", "CRLF 關", "CRLF オフ"},
        {"REQUEST", "请求", "請求", "リクエスト"},
        {"Compose payload", "编辑发送内容", "編輯傳送內容",
         "送信内容を編集"},
        {"CANCEL", "取消", "取消", "キャンセル"},
        {"Enter text payload", "输入发送文本", "輸入傳送文字",
         "送信テキストを入力"},
        {"Tap to enter payload", "点击输入发送内容", "點擊輸入傳送內容",
         "タップして入力"},
        {"Example: 55 AA 01 FF", "示例：55 AA 01 FF",
         "範例：55 AA 01 FF", "例：55 AA 01 FF"},
        {"UART is not connected", "UART 尚未连接", "UART 尚未連線",
         "UART は未接続です"},
        {"Enter complete HEX byte pairs", "请输入完整的 HEX 字节对",
         "請輸入完整的 HEX 位元組對", "完全な HEX バイト対を入力"},
        {"Enter a payload first", "请先输入发送内容", "請先輸入傳送內容",
         "先に送信内容を入力"},
        {"Write failed", "写入失败", "寫入失敗", "書き込み失敗"},
        {"UART write failed", "UART 写入失败", "UART 寫入失敗",
         "UART 書き込み失敗"},
        {"Loopback passed", "回环测试通过", "迴路測試通過",
         "ループバック成功"},
        {"Loopback failed", "回环测试失败", "迴路測試失敗",
         "ループバック失敗"},
        {"Loopback test passed", "回环测试通过", "迴路測試通過",
         "ループバック試験成功"},
        {"Loopback failed - connect IO44 to IO45",
         "回环失败，请连接 IO44 和 IO45", "迴路失敗，請連接 IO44 與 IO45",
         "失敗：IO44 と IO45 を接続してください"},
        {"VAXP response failed", "VAXP 响应失败", "VAXP 回應失敗",
         "VAXP 応答失敗"},
        {"Device restarted - negotiating VAXP", "设备已重启，正在协商 VAXP",
         "裝置已重啟，正在協商 VAXP", "再起動後 VAXP を交渉中"},
        {"VAXP session ready", "VAXP 会话已就绪", "VAXP 工作階段已就緒",
         "VAXP セッション準備完了"},
        {"VAXP frame error", "VAXP 帧错误", "VAXP 幀錯誤",
         "VAXP フレームエラー"},
        {"Send HELLO to establish a session first", "请先发送 HELLO 建立会话",
         "請先傳送 HELLO 建立工作階段", "先に HELLO で接続を確立"},
        {"Payload exceeds negotiated VAXP limit", "发送内容超过 VAXP 协商上限",
         "傳送內容超過 VAXP 協商上限", "送信内容が VAXP 上限を超過"},
        {"VAXP request window is full", "VAXP 请求队列已满",
         "VAXP 請求佇列已滿", "VAXP 要求キューが満杯"},
        {"Too many pending VAXP requests", "等待中的 VAXP 请求过多",
         "等待中的 VAXP 請求過多", "保留中の VAXP 要求が多すぎます"},
        {"VAXP send failed", "VAXP 发送失败", "VAXP 傳送失敗",
         "VAXP 送信失敗"},
        {"VAXP request send failed", "VAXP 请求发送失败",
         "VAXP 請求傳送失敗", "VAXP 要求の送信失敗"},
        {"VAXP HELLO timed out", "VAXP HELLO 超时", "VAXP HELLO 逾時",
         "VAXP HELLO タイムアウト"},
        {"VAXP self-test passed", "VAXP 自检通过", "VAXP 自我測試通過",
         "VAXP セルフテスト成功"},
        {"VAXP self-test failed", "VAXP 自检失败", "VAXP 自我測試失敗",
         "VAXP セルフテスト失敗"},
        {"VAXP wire-format self-test passed", "VAXP 帧格式自检通过",
         "VAXP 幀格式自我測試通過", "VAXP フレーム試験成功"},
        {"VAXP wire-format self-test failed", "VAXP 帧格式自检失败",
         "VAXP 幀格式自我測試失敗", "VAXP フレーム試験失敗"},
        {"Read failed", "读取失败", "讀取失敗", "読み取り失敗"},
        {"UART receive error", "UART 接收错误", "UART 接收錯誤",
         "UART 受信エラー"},
        {"Configuration failed", "配置失败", "設定失敗", "設定失敗"},
        {"Connected at", "已连接", "已連線", "接続済み"},
        {"Unable to start loopback test", "无法启动回环测试",
         "無法啟動迴路測試", "ループバックを開始できません"},
        {"Testing IO44 -> IO45...", "正在测试 IO44 -> IO45...",
         "正在測試 IO44 -> IO45...", "IO44 -> IO45 を試験中..."},
        {"Open failed", "打开失败", "開啟失敗", "オープン失敗"},
    };
    int column;

    if (g_system_settings.language == DSHANPI_LANG_EN)
        return english;
    column = g_system_settings.language == DSHANPI_LANG_ZH_CN ? 1 :
             g_system_settings.language == DSHANPI_LANG_ZH_TW ? 2 : 3;
    for (size_t i = 0; i < sizeof(translated) / sizeof(translated[0]); ++i) {
        if (strcmp(english, translated[i][0]) == 0)
            return translated[i][column];
    }
    return english;
}

static const lv_font_t *uart_ui_font(const lv_font_t *english_font)
{
    return g_system_settings.language == DSHANPI_LANG_EN
               ? english_font : &ui_font_source_han_20;
}

static const char *const
g_cloud_task_names[DSHANPI_LANG_COUNT][CLOUD_TASK_COUNT] = {
    {"图像分类", "目标检测", "语义分割", "OCR 文字检测",
     "OCR 文字识别", "OCR 检测与识别", "度量学习", "多标签分类"},
    {"影像分類", "目標偵測", "語意分割", "OCR 文字偵測",
     "OCR 文字辨識", "OCR 偵測與辨識", "度量學習", "多標籤分類"},
    {"Image Classification", "Object Detection", "Semantic Segmentation",
     "OCR Detection", "OCR Recognition", "OCR Detect + Recognize",
     "Metric Learning", "Multi-label Classification"},
    {"画像分類", "物体検出", "セマンティック分割", "OCR 文字検出",
     "OCR 文字認識", "OCR 検出と認識", "メトリック学習", "マルチラベル分類"},
};

static const char *cloud_text(const char *english)
{
    static const char *const translated[][4] = {
        {"Cloud Model", "云平台模型", "雲端平台模型", "クラウドモデル"},
        {"Select a task, then choose an input", "选择任务，然后选择输入方式",
         "選擇任務，然後選擇輸入方式", "タスクと入力方法を選択"},
        {"1  Model task", "1  模型任务", "1  模型任務", "1  モデルタスク"},
        {"Files are detected automatically from /sdcard",
         "自动检测 /sdcard 中的文件", "自動偵測 /sdcard 中的檔案",
         "/sdcard のファイルを自動検出"},
        {"Scanning...", "正在扫描...", "正在掃描...", "スキャン中..."},
        {"2  Choose input", "2  选择输入方式", "2  選擇輸入方式",
         "2  入力方法を選択"},
        {"Image inference", "图片推理", "圖片推論", "画像推論"},
        {"Live camera", "实时相机", "即時相機", "ライブカメラ"},
        {"No inference result yet.", "暂无推理结果。", "尚無推論結果。",
         "推論結果はまだありません。"},
        {"Select Refresh after copying new files.", "复制新文件后请点击刷新。",
         "複製新檔案後請點選重新整理。", "新しいファイルをコピー後、更新してください。"},
        {"Cloud model files are ready", "云模型文件已就绪",
         "雲端模型檔案已就緒", "クラウドモデルのファイルを確認しました"},
        {"Some required files are missing", "部分必需文件缺失",
         "部分必要檔案遺失", "必要なファイルが不足しています"},
        {"Copy all required files to CanMV/sdcard",
         "请将全部必需文件复制到 CanMV/sdcard",
         "請將全部必要檔案複製到 CanMV/sdcard",
         "必要なファイルを CanMV/sdcard にコピーしてください"},
        {"Loading model and running test.jpg...", "正在加载模型并运行 test.jpg...",
         "正在載入模型並執行 test.jpg...", "モデルを読み込み test.jpg を実行中..."},
        {"Starting image inference...", "正在启动图片推理...",
         "正在啟動圖片推論...", "画像推論を開始しています..."},
        {"Unable to start image inference", "无法启动图片推理",
         "無法啟動圖片推論", "画像推論を開始できません"},
        {"Unable to start image inference.", "无法启动图片推理。",
         "無法啟動圖片推論。", "画像推論を開始できません。"},
        {"This task only supports test.jpg", "此任务仅支持 test.jpg",
         "此任務僅支援 test.jpg", "このタスクは test.jpg のみ対応しています"},
        {"Unable to save cloud model request", "无法保存云模型请求",
         "無法儲存雲端模型請求", "クラウドモデル要求を保存できません"},
        {"Starting live camera inference...\nPress q on serial to return.",
         "正在启动实时相机推理...\n请在串口按 q 返回。",
         "正在啟動即時相機推論...\n請在串口按 q 返回。",
         "ライブカメラ推論を開始中...\nシリアルで q を押すと戻ります。"},
        {"Live inference: press q on serial to exit",
         "实时推理：请在串口按 q 退出", "即時推論：請在串口按 q 結束",
         "ライブ推論：シリアルで q を押すと終了します"},
        {"Running inference...", "正在执行推理...", "正在執行推論...",
         "推論を実行中..."},
        {"Please wait", "请稍候", "請稍候", "しばらくお待ちください"},
        {"Inference completed", "推理完成", "推論完成", "推論が完了しました"},
        {"Inference completed successfully.\n\nResult image saved to:\n%s",
         "推理成功完成。\n\n结果图片已保存到：\n%s",
         "推論成功完成。\n\n結果圖片已儲存至：\n%s",
         "推論が完了しました。\n\n結果画像の保存先：\n%s"},
        {"Completed successfully.\n%.300s", "已成功完成。\n%.300s",
         "已成功完成。\n%.300s", "正常に完了しました。\n%.300s"},
        {"Cloud model test completed", "云模型测试完成", "雲端模型測試完成",
         "クラウドモデルのテストが完了しました"},
        {"Inference completed, but the result file is missing.\n%.300s",
         "推理已完成，但结果文件缺失。\n%.300s",
         "推論已完成，但結果檔案遺失。\n%.300s",
         "推論は完了しましたが、結果ファイルがありません。\n%.300s"},
        {"Cloud model result is missing", "云模型结果文件缺失",
         "雲端模型結果檔案遺失", "クラウドモデルの結果がありません"},
        {"Inference failed with status %d. Check the serial log.",
         "推理失败，状态码 %d，请检查串口日志。",
         "推論失敗，狀態碼 %d，請檢查串口日誌。",
         "推論に失敗しました（状態 %d）。シリアルログを確認してください。"},
        {"Cloud model test failed", "云模型测试失败", "雲端模型測試失敗",
         "クラウドモデルのテストに失敗しました"},
        {"Setup needed", "需要配置", "需要設定", "設定が必要です"},
        {"Missing: ", "缺少：", "缺少：", "不足："},
        {"detection kmodel", "检测模型", "偵測模型", "検出モデル"},
        {"recognition kmodel", "识别模型", "辨識模型", "認識モデル"},
        {"runtime", "运行程序", "執行程式", "実行プログラム"},
        {"Camera ready\nAdd test.jpg to enable image inference.",
         "相机已就绪\n添加 test.jpg 可启用图片推理。",
         "相機已就緒\n加入 test.jpg 可啟用圖片推論。",
         "カメラ準備完了\ntest.jpg を追加すると画像推論を利用できます。"},
        {"Setup needed\nMissing: test.jpg", "需要配置\n缺少：test.jpg",
         "需要設定\n缺少：test.jpg", "設定が必要です\n不足：test.jpg"},
        {"Image ready\nLive camera is unavailable for this task.",
         "图片已就绪\n此任务不支持实时相机。",
         "圖片已就緒\n此任務不支援即時相機。",
         "画像準備完了\nこのタスクはライブカメラに対応していません。"},
        {"Ready\nChoose an input mode to start inference.",
         "已就绪\n请选择输入方式开始推理。",
         "已就緒\n請選擇輸入方式開始推論。",
         "準備完了\n入力方法を選択して推論を開始してください。"},
    };
    int column;

    if (g_system_settings.language == DSHANPI_LANG_EN)
        return english;
    column = g_system_settings.language == DSHANPI_LANG_ZH_CN ? 1 :
             g_system_settings.language == DSHANPI_LANG_ZH_TW ? 2 : 3;
    for (size_t i = 0; i < sizeof(translated) / sizeof(translated[0]); ++i) {
        if (strcmp(english, translated[i][0]) == 0)
            return translated[i][column];
    }
    return english;
}

static const char *cloud_task_name(unsigned task)
{
    dshanpi_language_t language = g_system_settings.language;

    if (task >= CLOUD_TASK_COUNT)
        task = CLOUD_TASK_CLASSIFICATION;
    if (language < 0 || language >= DSHANPI_LANG_COUNT)
        language = DSHANPI_LANG_EN;
    return g_cloud_task_names[language][task];
}

static const char *cloud_task_options_text(void)
{
    static char options[512];
    size_t used = 0;

    options[0] = '\0';
    for (unsigned task = 0; task < CLOUD_TASK_COUNT; ++task) {
        int written = snprintf(options + used, sizeof(options) - used,
                               "%s%s", task == 0 ? "" : "\n",
                               cloud_task_name(task));
        if (written < 0 || (size_t)written >= sizeof(options) - used)
            break;
        used += (size_t)written;
    }
    return options;
}

static const lv_font_t *cloud_ui_font(const lv_font_t *english_font)
{
    return g_system_settings.language == DSHANPI_LANG_EN
               ? english_font : &ui_font_source_han_20;
}

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_screenshot_saved_pending;
static volatile sig_atomic_t g_desktop_worker_pid = -1;
static const dshanpi_ai_app_t *g_launch_ai_app;
static bool g_launch_face_studio;
static bool g_launch_face_geometry;
static bool g_launch_hand_studio;
static bool g_launch_human_studio;
static bool g_launch_smart_driving;
static bool g_launch_ocr_detection;
static bool g_launch_yolov8_vision;
static bool g_launch_network_camera;
static bool g_launch_yolo_models;
static bool g_launch_gallery_player;
static bool g_launch_cv_lite;
static bool g_launch_plate_ocr;
static bool g_launch_code_scanner;
static bool g_launch_self_learning;
static bool g_launch_cloud_model;
static bool g_launch_uvc_camera;
static dshanpi_autostart_t g_initial_view;
static lv_display_t *g_display;
static lv_indev_t *g_touch_indev;
static lv_obj_t *g_home_screen;
static lv_obj_t *g_app_grid;
static lv_obj_t *g_screensaver_screen;
static lv_obj_t *g_screensaver_time;
static lv_obj_t *g_screensaver_date;
static lv_timer_t *g_screensaver_timer;
static bool g_screensaver_active;
static bool g_screensaver_gesture_tracking;
static lv_point_t g_screensaver_gesture_start;
static lv_obj_t *g_camera_screen;
static lv_obj_t *g_dual_camera_screen;
static lv_obj_t *g_modal;
static lv_obj_t *g_modal_title;
static lv_obj_t *g_modal_symbol;
static lv_obj_t *g_modal_description;
static lv_obj_t *g_toast;
static lv_obj_t *g_toast_label;
static lv_timer_t *g_toast_timer;
static lv_obj_t *g_app_launch_overlay;
static lv_obj_t *g_app_launch_spinner;
static lv_timer_t *g_app_launch_timer;
static lv_obj_t *g_camera_view;
static lv_obj_t *g_camera_preview;
static lv_obj_t *g_camera_status_chip;
static lv_obj_t *g_camera_status;
static lv_obj_t *g_camera_status_dot;
static lv_obj_t *g_camera_loading_overlay;
static lv_obj_t *g_camera_loading_outer;
static lv_obj_t *g_camera_focus_reticle;
static lv_obj_t *g_camera_capture_flash;
static lv_obj_t *g_camera_last_media_button;
static lv_obj_t *g_camera_last_media_image;
static lv_obj_t *g_camera_last_media_play_badge;
static lv_image_dsc_t g_camera_last_media_dsc;
static uint8_t *g_camera_last_media_data;
static char g_camera_last_media_path[320];
static char g_camera_recording_path[320];
static bool g_camera_last_media_is_video;
static lv_obj_t *g_camera_shutter;
static lv_obj_t *g_camera_shutter_inner;
static lv_obj_t *g_camera_record;
static lv_obj_t *g_camera_record_icon;
static lv_obj_t *g_camera_record_time;
static lv_obj_t *g_camera_record_badge;
static lv_obj_t *g_camera_resolution_button;
static lv_obj_t *g_camera_resolution_label;
static lv_timer_t *g_camera_record_timer;
static uint32_t g_camera_record_started_tick;
static bool g_camera_video_mode;
static lv_timer_t *g_camera_init_timer;
static pthread_t g_camera_init_thread;
static volatile int g_camera_init_running;
static volatile int g_camera_init_cancelled;
static volatile int g_camera_init_result;
static int g_camera_resolution;
static lv_obj_t *g_dual_camera_view;
static lv_obj_t *g_dual_camera_pip_frame;
static lv_obj_t *g_dual_camera_loading;
static lv_obj_t *g_dual_camera_spinner;
static lv_obj_t *g_dual_camera_record;
static lv_obj_t *g_dual_camera_record_inner;
static lv_obj_t *g_dual_camera_mode;
static lv_obj_t *g_dual_camera_mode_icon;
static lv_obj_t *g_dual_camera_capture_flash;
static lv_obj_t *g_dual_camera_time_badge;
static lv_obj_t *g_dual_camera_time;
static lv_obj_t *g_dual_camera_status;
static lv_obj_t *g_dual_camera_resolution_button;
static lv_obj_t *g_dual_camera_resolution_label;
static lv_obj_t *g_dual_camera_last_media_button;
static lv_obj_t *g_dual_camera_last_media_image;
static lv_obj_t *g_dual_camera_last_media_icon;
static lv_obj_t *g_dual_camera_last_media_play_badge;
static lv_image_dsc_t g_dual_camera_last_media_dsc;
static uint8_t *g_dual_camera_last_media_data;
#define DUAL_CAMERA_SESSION_MEDIA_MAX 64
static char g_dual_camera_session_paths[DUAL_CAMERA_SESSION_MEDIA_MAX][320];
static bool g_dual_camera_session_video[DUAL_CAMERA_SESSION_MEDIA_MAX];
static size_t g_dual_camera_session_count;
static lv_timer_t *g_dual_camera_record_timer;
static lv_timer_t *g_dual_camera_init_timer;
static lv_timer_t *g_dual_camera_status_timer;
static lv_timer_t *g_dual_camera_fps_test_timer;
static pthread_t g_dual_camera_init_thread;
static volatile int g_dual_camera_init_running;
static volatile int g_dual_camera_init_cancelled;
static volatile int g_dual_camera_init_result;
static uint32_t g_dual_camera_record_started_tick;
static char g_dual_camera_recording_path[320];
static int g_dual_camera_pip_x = DUAL_PIP_DEFAULT_X;
static int g_dual_camera_pip_y = DUAL_PIP_DEFAULT_Y;
static lv_point_t g_dual_camera_pip_press;
static int g_dual_camera_pip_press_x;
static int g_dual_camera_pip_press_y;
static bool g_dual_camera_pip_dragging;
static bool g_dual_camera_pip_moved;
static bool g_dual_camera_pip_locked;
static bool g_dual_camera_video_mode;
static bool g_dual_camera_resume_session;
static int g_dual_camera_resolution;
static unsigned int g_dual_camera_fps_test_seconds;
static lv_obj_t *g_gallery_view;
static lv_obj_t *g_gallery_content;
static lv_obj_t *g_gallery_media_screen;
static lv_obj_t *g_gallery_media_content;
static lv_obj_t *g_gallery_media_back;
static lv_obj_t *g_gallery_media_title;
static lv_obj_t *g_gallery_video_controls;
static lv_obj_t *g_gallery_video_slider;
static lv_obj_t *g_gallery_video_time;
static lv_obj_t *g_gallery_video_pause_icon;
static lv_timer_t *g_gallery_player_timer;
static volatile int g_gallery_player_eof;
static volatile int g_gallery_player_eof_hold;
static uint64_t g_gallery_player_last_pts;
static uint64_t g_gallery_player_clock_pts_ms;
static uint64_t g_gallery_player_clock_wall_us;
static uint64_t g_gallery_player_last_wake_us;
static uint64_t g_gallery_player_decode_cost_us = 3000;
static volatile uint64_t g_gallery_player_current_ms;
static volatile uint64_t g_gallery_player_total_ms;
static bool g_gallery_slider_dragging;
static bool g_gallery_video_playing;
static bool g_gallery_osd_hidden;
static char g_gallery_media_paths[120][320];
static int g_gallery_media_types[120];
static time_t g_gallery_media_modified[120];
static size_t g_gallery_media_count;
static size_t g_gallery_media_index;
static size_t g_gallery_rendered_count;
static lv_timer_t *g_gallery_load_timer;
static lv_obj_t *g_gallery_active_row;
static char g_gallery_active_date[16];
static bool g_gallery_selection_mode;
static bool g_gallery_selected[120];
static size_t g_gallery_selected_count;
static lv_obj_t *g_gallery_media_cards[120];
static lv_obj_t *g_gallery_selection_badges[120];
static lv_obj_t *g_gallery_select_button;
static lv_obj_t *g_gallery_select_label;
static lv_obj_t *g_gallery_selection_bar;
static lv_obj_t *g_gallery_selection_count_label;
static lv_obj_t *g_gallery_batch_delete_button;
static lv_obj_t *g_gallery_batch_delete_dialog;
static lv_obj_t *g_gallery_batch_delete_title;
static bool g_gallery_return_to_camera;
static bool g_gallery_return_to_dual_camera;
static bool g_gallery_video_paused;
static bool g_gallery_controls_visible;
static lv_point_t g_gallery_gesture_start;
static lv_obj_t *g_gallery_prev_button;
static lv_obj_t *g_gallery_next_button;
static lv_obj_t *g_gallery_info_button;
static lv_obj_t *g_gallery_delete_button;
static lv_obj_t *g_gallery_info_dialog;
static lv_obj_t *g_gallery_delete_dialog;
static uint32_t g_gallery_current_width;
static uint32_t g_gallery_current_height;
static lv_obj_t *g_settings_view;
static lv_obj_t *g_settings_header;
static lv_obj_t *g_settings_status;
static lv_obj_t *g_settings_camera_rear;
static lv_obj_t *g_settings_camera_front;
static lv_obj_t *g_settings_camera_rear_label;
static lv_obj_t *g_settings_camera_front_label;
static lv_obj_t *g_wifi_list;
static lv_obj_t *g_wifi_status;
static lv_obj_t *g_wifi_ip_label;
static lv_obj_t *g_wifi_scan_button;
static lv_obj_t *g_wifi_scan_progress;
static lv_obj_t *g_desktop_network_status;
static lv_obj_t *g_desktop_ip_status;
static lv_obj_t *g_desktop_time_status;
static lv_obj_t *g_desktop_system_status;
static lv_obj_t *g_wifi_dialog;
static lv_obj_t *g_wifi_password;
static lv_obj_t *g_wifi_password_visibility_icon;
static lv_obj_t *g_wifi_dialog_title;
static lv_obj_t *g_wifi_forget_button;
static lv_obj_t *g_cloud_view;
static lv_obj_t *g_cloud_task_dropdown;
static lv_obj_t *g_cloud_title_label;
static lv_obj_t *g_cloud_subtitle_label;
static lv_obj_t *g_cloud_task_title_label;
static lv_obj_t *g_cloud_copy_help_label;
static lv_obj_t *g_cloud_input_title_label;
static lv_obj_t *g_cloud_file_status;
static lv_obj_t *g_cloud_result_status;
static lv_obj_t *g_cloud_run_button;
static lv_obj_t *g_cloud_run_button_label;
static lv_obj_t *g_cloud_live_button;
static lv_obj_t *g_cloud_live_button_label;
static lv_obj_t *g_cloud_progress_dialog;
static int g_cloud_selected_task;
static volatile bool g_cloud_background_running;
static int launch_cloud_model(void);
static int launch_cloud_model_request(const char *request_path);
static void cloud_poll_image_inference(void);
static void cloud_show_progress_dialog(void);
static void cloud_close_progress_dialog(void);
static lv_obj_t *g_date_status;
static lv_obj_t *g_settings_panels[SETTINGS_SECTION_COUNT];
static lv_obj_t *g_settings_nav[SETTINGS_SECTION_COUNT];
static lv_obj_t *g_settings_nav_labels[SETTINGS_SECTION_COUNT];
static lv_obj_t *g_settings_nav_values[SETTINGS_SECTION_COUNT];
static lv_obj_t *g_settings_navigation;
static lv_obj_t *g_settings_heading;
static lv_obj_t *g_settings_detail;
static lv_obj_t *g_settings_detail_title;
static lv_obj_t *g_settings_detail_icon;
static lv_obj_t *g_settings_detail_icon_symbol;
typedef enum {
    SETTINGS_PICKER_NONE = 0,
    SETTINGS_PICKER_LANGUAGE,
    SETTINGS_PICKER_TIMEZONE,
    SETTINGS_PICKER_AUTOSTART,
    SETTINGS_PICKER_VAXP_BAUD,
    SETTINGS_PICKER_SLEEP_TIMEOUT,
} settings_picker_kind_t;
typedef enum {
    SETTINGS_REBOOT_CAMERA = 0,
    SETTINGS_REBOOT_LANGUAGE,
} settings_reboot_reason_t;
static lv_obj_t *g_settings_picker;
static lv_obj_t *g_settings_picker_title;
static lv_obj_t *g_settings_picker_roller;
static lv_obj_t *g_settings_picker_current;
static lv_obj_t *g_settings_picker_icon;
static lv_obj_t *g_settings_picker_icon_symbol;
static settings_picker_kind_t g_settings_picker_kind;
static lv_obj_t *g_power_confirm;
static lv_obj_t *g_power_confirm_title;
static int g_power_pending_action;
static lv_obj_t *g_ota_status_label;
static lv_obj_t *g_ota_progress;
static lv_obj_t *g_ota_progress_label;
static lv_obj_t *g_ota_network_button;
static lv_obj_t *g_ota_network_button_label;
static lv_obj_t *g_settings_reboot_dialog;
static lv_obj_t *g_settings_reboot_countdown;
static lv_timer_t *g_settings_reboot_timer;
static int g_settings_reboot_seconds;
static settings_reboot_reason_t g_settings_reboot_reason;
static lv_timer_t *g_wifi_timer;
static struct rt_wlan_info_t g_wifi_aps[RT_WLAN_STA_SCAN_MAX_AP];
static volatile int g_wifi_operation;
static volatile int g_wifi_completed_operation;
static bool g_wifi_panel_active;
static bool g_wifi_scan_requested;
static volatile int g_wifi_result;
static pthread_mutex_t g_wifi_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_wifi_auto_failures;
static time_t g_wifi_retry_after;
static int g_wifi_ap_count;
static int g_wifi_selected_ap = -1;
static char g_wifi_connect_password[RT_WLAN_PASSWORD_MAX_LENGTH + 1];
static char g_wifi_ip[16];

static bool wifi_begin_operation(int operation)
{
    bool started = false;
    pthread_mutex_lock(&g_wifi_lock);
    if (g_wifi_operation == 0) {
        g_wifi_operation = operation;
        g_wifi_result = -1;
        started = true;
    }
    pthread_mutex_unlock(&g_wifi_lock);
    return started;
}

static void wifi_cancel_operation_start(void)
{
    pthread_mutex_lock(&g_wifi_lock);
    g_wifi_operation = 0;
    pthread_mutex_unlock(&g_wifi_lock);
}

static bool wifi_is_busy(void)
{
    bool busy;
    pthread_mutex_lock(&g_wifi_lock);
    busy = g_wifi_operation != 0;
    pthread_mutex_unlock(&g_wifi_lock);
    return busy;
}

static void wifi_autoconnect_start(void);
static void wifi_scan_start(void);

static void wifi_scan_icon_angle_cb(void *object, int32_t angle)
{
    lv_obj_set_style_transform_angle((lv_obj_t *)object, angle, 0);
}

static void wifi_scan_animation_start(void)
{
    lv_anim_t animation;
    if (g_wifi_scan_progress == NULL) return;
    lv_obj_remove_flag(g_wifi_scan_progress, LV_OBJ_FLAG_HIDDEN);
    lv_anim_delete(g_wifi_scan_progress, wifi_scan_icon_angle_cb);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_wifi_scan_progress);
    lv_anim_set_exec_cb(&animation, wifi_scan_icon_angle_cb);
    lv_anim_set_values(&animation, 0, 3600);
    lv_anim_set_duration(&animation, 850);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

static void wifi_scan_animation_stop(void)
{
    if (g_wifi_scan_progress == NULL) return;
    lv_anim_delete(g_wifi_scan_progress, wifi_scan_icon_angle_cb);
    lv_obj_set_style_transform_angle(g_wifi_scan_progress, 0, 0);
    lv_obj_add_flag(g_wifi_scan_progress, LV_OBJ_FLAG_HIDDEN);
}

static const char *const g_wifi_kb_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l",
    LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
    LV_SYMBOL_KEYBOARD, "1#", "@", "_", " ", LV_SYMBOL_OK, ""
};
static const char *const g_wifi_kb_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L",
    LV_SYMBOL_BACKSPACE, "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
    LV_SYMBOL_KEYBOARD, "1#", "@", "_", " ", LV_SYMBOL_OK, ""
};
/*
 * LVGL handles the built-in "1#" key by switching to SPECIAL mode.  Keep
 * the phone-style numeric pad in both NUMBER and SPECIAL so that the pad is
 * reachable from the alphabetic layouts as well as programmatically.
 */
static const char *const g_wifi_kb_number[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", "abc", "\n",
    "7", "8", "9", "@", "\n",
    "-", "0", ".", LV_SYMBOL_OK, ""
};
#define WIFI_KB_CTRL(width)                                                   \
    ((lv_buttonmatrix_ctrl_t)((width) | LV_BUTTONMATRIX_CTRL_NO_REPEAT |      \
                              LV_BUTTONMATRIX_CTRL_CLICK_TRIG))
static const lv_buttonmatrix_ctrl_t g_wifi_kb_ctrl_36[36] = {
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(1),
    WIFI_KB_CTRL(1), WIFI_KB_CTRL(1), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2),
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(4), WIFI_KB_CTRL(2)
};
static const lv_buttonmatrix_ctrl_t g_wifi_kb_ctrl_16[16] = {
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2),
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2),
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2),
    WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2), WIFI_KB_CTRL(2)
};
#undef WIFI_KB_CTRL
static lv_point_t g_tap_guard_start;
static bool g_tap_guard_moved;
static lv_obj_t *g_paint_view;
static lv_obj_t *g_uart_view;
static lv_obj_t *g_uart_terminal;
static lv_obj_t *g_uart_terminal_panel;
static lv_obj_t *g_uart_status;
static lv_obj_t *g_uart_status_dot;
static lv_obj_t *g_uart_counter;
static lv_obj_t *g_uart_baud_title_label;
static lv_obj_t *g_uart_frame_title_label;
static lv_obj_t *g_uart_mode_title_label;
static lv_obj_t *g_uart_loopback_label;
static lv_obj_t *g_uart_protocol_test_label;
static lv_obj_t *g_uart_clear_label;
static lv_obj_t *g_uart_send_label;
static lv_obj_t *g_uart_hex_label;
static lv_obj_t *g_uart_crlf_label;
static lv_obj_t *g_uart_payload_preview;
static lv_obj_t *g_uart_editor;
static lv_obj_t *g_uart_editor_title_label;
static lv_obj_t *g_uart_editor_cancel_label;
static lv_obj_t *g_uart_editor_send_label;
static lv_obj_t *g_uart_editor_input;
static lv_obj_t *g_uart_keyboard;
static lv_obj_t *g_uart_wiring_panel;
static lv_obj_t *g_uart_wiring_toggle_label;
static lv_obj_t *g_uart_baud_dropdown;
static lv_obj_t *g_uart_format_dropdown;
static lv_timer_t *g_uart_poll_timer;
static dshanpi_uart_lab_t *g_uart_lab;
static char g_uart_log[UART_LOG_CAPACITY];
static size_t g_uart_log_length;
static uint64_t g_uart_tx_bytes;
static uint64_t g_uart_rx_bytes;
static bool g_uart_hex_mode;
static bool g_uart_crlf = true;
static bool g_uart_vaxp_mode = true;
static bool g_uart_loopback_pending;
static bool g_uart_loopback_mismatch;
static size_t g_uart_loopback_received;
static uint32_t g_uart_loopback_started;
static lv_obj_t *g_vaxp_mode_label;
static lv_obj_t *g_vaxp_session_label;
static lv_obj_t *g_vaxp_frame_counter;
static lv_obj_t *g_vaxp_command_dropdown;
static lv_obj_t *g_vaxp_send_button_label;
static lv_obj_t *g_vaxp_terminal_title;
static lv_obj_t *g_uart_raw_controls[9];
static lv_obj_t *g_uart_vaxp_controls[8];
static vaxp_lab_parser_t g_vaxp_parser;

typedef struct {
    bool used;
    uint16_t sequence;
    uint16_t command;
    uint16_t session;
    uint16_t payload_length;
    uint32_t sent_tick;
    uint32_t timeout_ms;
    uint8_t retries;
    uint8_t payload[UART_MAX_PAYLOAD];
} uart_vaxp_pending_t;

typedef struct {
    bool valid;
    uint16_t session;
    uint16_t sequence;
    uint16_t command;
    uint8_t source;
    uint16_t payload_length;
    uint8_t payload[sizeof(VaxpResponseHeader) + sizeof(VaxpDeviceInfo)];
} uart_vaxp_cached_response_t;

typedef struct {
    bool active;
    uint16_t sequence;
    uint16_t command;
    uint16_t session;
    uint16_t payload_length;
    uint32_t sent_tick;
    uint8_t retries;
    uint8_t payload[sizeof(VaxpDeviceBootEvent)];
} uart_vaxp_critical_event_t;

static uint16_t g_vaxp_host_session;
static uint16_t g_vaxp_device_session;
static uint16_t g_vaxp_next_sequence = 1;
static uint16_t g_vaxp_device_sequence = 1;
static uint16_t g_vaxp_host_peer_max_rx = VAXP_DEFAULT_MAX_PAYLOAD;
static uint16_t g_vaxp_host_rx_limit = VAXP_DEFAULT_MAX_PAYLOAD;
static uint16_t g_vaxp_host_max_pending = 1;
static uint16_t g_vaxp_host_heartbeat_ms = VAXP_DEFAULT_HEARTBEAT_MS;
static uint16_t g_vaxp_device_peer_max_rx = VAXP_DEFAULT_MAX_PAYLOAD;
static uint16_t g_vaxp_boot_session_seen;
static bool g_vaxp_device_established;
static uint32_t g_vaxp_rx_frames;
static uint32_t g_vaxp_tx_frames;
static uint32_t g_vaxp_device_rx_packets;
static uint32_t g_vaxp_device_tx_packets;
static uint32_t g_vaxp_started_tick;
static uint32_t g_vaxp_last_heartbeat_tick;
static uint32_t g_vaxp_boot_count;
static uart_vaxp_pending_t g_vaxp_pending[UART_VAXP_PENDING_CAPACITY];
static uart_vaxp_cached_response_t
    g_vaxp_response_cache[UART_VAXP_RESPONSE_CACHE_CAPACITY];
static size_t g_vaxp_response_cache_next;
static uart_vaxp_critical_event_t g_vaxp_critical_event;
static uint8_t g_vaxp_tx_buffer[VAXP_LAB_MAX_FRAME_SIZE];
static lv_obj_t *g_ai_view;
static lv_obj_t *g_ai_scene_view;
static lv_obj_t *g_ai_scene_title;
static lv_obj_t *g_ai_scene_subtitle;
static lv_obj_t *g_ai_scene_content;
static lv_obj_t *g_paint_canvas;
static lv_draw_buf_t *g_paint_draw_buf;
static void *g_paint_buffer;
static lv_color_t g_paint_color;
static int g_paint_radius = 4;
static int g_paint_last_x = -1;
static int g_paint_last_y = -1;
static uint32_t g_paint_last_tick;
static uint32_t g_paint_release_tick;
static unsigned g_paint_move_count;
static int g_paint_recently_released;
static int g_camera_csi;
/* Latched once at process start. A saved change applies after reboot. */
static int g_system_camera_csi;

static const char *const g_settings_titles[] = {
    "Wi-Fi", "Language", "Date & Time", "Startup App", "Default Camera",
    "VAXP UART", "System Update", "Sleep & Screen Saver", "Power",
    "About"
};

/* Shared Material 3 palette for both settings levels.  Keeping these values
 * in one place prevents detail pages from drifting back to the unrelated
 * plain white/grey style that they used before. */
static const char *const g_settings_symbols[] = {
    LV_SYMBOL_WIFI, "A", LV_SYMBOL_REFRESH, LV_SYMBOL_PLAY,
    LV_SYMBOL_IMAGE, "U", LV_SYMBOL_DOWNLOAD, "Z", LV_SYMBOL_POWER,
    "i"
};
static const uint32_t g_settings_card_colors[] = {
    0xECF3FF, 0xF2EDFF, 0xE8F7F6,
    0xFFF1E2, 0xEBF7E9, 0xE8F1FF, 0xE8F7F6, 0xE8F1FF, 0xFDECEF,
    0xF2EDFF,
};
static const uint32_t g_settings_pressed_colors[] = {
    0xD9E8FF, 0xE5DBFF, 0xD3EFEC,
    0xFFE0BD, 0xD8EED5, 0xD6E5FC, 0xD3EFEC, 0xD6E5FC, 0xF9D8DF,
    0xE5DBFF,
};
static const uint32_t g_settings_border_colors[] = {
    0xCCDDF8, 0xDED3F4, 0xCBE7E3,
    0xF4D6B4, 0xCDE4C9, 0xCADCF5, 0xCBE7E3, 0xCADCF5, 0xF2CED6,
    0xDED3F4,
};
static const uint32_t g_settings_icon_colors[] = {
    0x4B7EF0, 0x8359DF, 0x31A6A3,
    0xF79217, 0x53A85C, 0x3976C5, 0x169C92, 0x3976C5, 0xEC536D,
    0x8359DF,
};

static const uint32_t g_vaxp_baud_rates[] = {
    115200u, 460800u, 921600u
};

static const uint32_t g_sleep_timeout_values[] = {
    0u, 30u, 60u, 120u, 300u, 600u, 1800u
};

static void show_camera(void);
static void show_dual_camera(void);
static void show_gallery(void);
static void show_settings(void);
static void show_paint(void);
static void show_uart_lab(void);
static void uart_lab_stop(void);
static int uart_vaxp_send_request(uint16_t command, bool notify);
static void show_cloud_model(void);
static void show_app_launch_overlay(const app_info_t *app);
static void settings_detail_back_cb(lv_event_t *event);
static void settings_refresh_nav_values(void);
static void ota_refresh_ui(void);
static unsigned vaxp_baud_selection(uint32_t baud_rate);
static void camera_record_timer_cb(lv_timer_t *timer);
static void camera_resolution_cb(lv_event_t *event);
static void dual_camera_resolution_cb(lv_event_t *event);
static void dual_camera_cleanup_ui_timers(void);
static void camera_loading_show(void);
static void camera_loading_hide(void);
static void *camera_init_worker(void *argument);
static void camera_init_timer_cb(lv_timer_t *timer);
static void *dual_camera_init_worker(void *argument);
static void dual_camera_init_timer_cb(lv_timer_t *timer);
static void gallery_refresh(void);
static void gallery_open_media_index(size_t media_index);

static void signal_handler(int signum)
{
    printf("[desktop] received stop signal %d\n", signum);
    g_stop = 1;
}

static void screenshot_saved_signal_handler(int signum)
{
    (void)signum;
    g_screenshot_saved_pending = 1;
}

static void tap_guard_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;

    if (indev == NULL) {
        return;
    }
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        g_tap_guard_start = point;
        g_tap_guard_moved = false;
    } else if (code == LV_EVENT_PRESSING) {
        int dx = abs((int)point.x - (int)g_tap_guard_start.x);
        int dy = abs((int)point.y - (int)g_tap_guard_start.y);
        if (dx >= TAP_GUARD_DISTANCE || dy >= TAP_GUARD_DISTANCE) {
            g_tap_guard_moved = true;
        }
    } else if (code == LV_EVENT_CLICKED && g_tap_guard_moved) {
        lv_event_stop_processing(event);
    }
}

static int vb_init(void)
{
    k_vb_config config;
    k_vb_supplement_config supplement;

    memset(&config, 0, sizeof(config));
    config.max_pool_cnt = VB_MAX_POOLS;
    config.comm_pool[0].blk_cnt = 1;
    config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
    config.comm_pool[0].blk_size =
        VB_ALIGN_UP(MAX_DISPLAY_WIDTH * MAX_DISPLAY_HEIGHT * 4, 4096);
    /* Two offline VICAP devices need four native input buffers each. */
    config.comm_pool[1].blk_cnt = 8;
    config.comm_pool[1].mode = VB_REMAP_MODE_NOCACHE;
    config.comm_pool[1].blk_size =
        VB_ALIGN_UP(MAX_DISPLAY_WIDTH * MAX_DISPLAY_HEIGHT * 2, 4096);
    /* Preview, AI and selectable-resolution capture channels share this
     * pool; size it for the largest 1920x1080 NV12 output. */
    config.comm_pool[2].blk_cnt = 16;
    config.comm_pool[2].mode = VB_REMAP_MODE_NOCACHE;
    config.comm_pool[2].blk_size =
        VB_ALIGN_UP(MAX_DISPLAY_WIDTH * MAX_DISPLAY_HEIGHT * 3 / 2, 4096);
    /* Dual Camera exposes one 224x168 PIP channel per sensor. */
    config.comm_pool[3].blk_cnt = 8;
    config.comm_pool[3].mode = VB_REMAP_MODE_NOCACHE;
    config.comm_pool[3].blk_size = VB_ALIGN_UP(224 * 168 * 3 / 2, 4096);

    if (kd_mpi_vb_set_config(&config) != 0) {
        return -1;
    }

    memset(&supplement, 0, sizeof(supplement));
    supplement.supplement_config = VB_SUPPLEMENT_JPEG_MASK;
    if (kd_mpi_vb_set_supplement_config(&supplement) != 0) {
        return -1;
    }

    return kd_mpi_vb_init();
}

static void style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static bool screensaver_overlay_visible(lv_obj_t *object)
{
    return object != NULL &&
           !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static bool screensaver_can_enter(void)
{
    if (g_screensaver_active || g_home_screen == NULL ||
        lv_screen_active() != g_home_screen) {
        return false;
    }

    /* Automatic sleep is intentionally limited to the idle desktop.  It
     * must never interrupt a camera recording, OTA operation, serial test,
     * gallery playback, or a settings dialog that the user is reading. */
    return !screensaver_overlay_visible(g_settings_view) &&
           !screensaver_overlay_visible(g_gallery_view) &&
           !screensaver_overlay_visible(g_paint_view) &&
           !screensaver_overlay_visible(g_uart_view) &&
           !screensaver_overlay_visible(g_cloud_view) &&
           !screensaver_overlay_visible(g_ai_view) &&
           !screensaver_overlay_visible(g_ai_scene_view) &&
           !screensaver_overlay_visible(g_modal) &&
           !screensaver_overlay_visible(g_app_launch_overlay);
}

static void screensaver_update_clock(void)
{
    char time_text[16];
    char date_text[24];
    time_t now;
    struct tm local_time;

    if (g_screensaver_time == NULL || g_screensaver_date == NULL)
        return;
    now = time(NULL);
    if (localtime_r(&now, &local_time) == NULL)
        return;
    strftime(time_text, sizeof(time_text), "%H:%M", &local_time);
    strftime(date_text, sizeof(date_text), "%Y.%m.%d", &local_time);
    lv_label_set_text(g_screensaver_time, time_text);
    lv_label_set_text(g_screensaver_date, date_text);
}

static void screensaver_enter(bool manual)
{
    if (!screensaver_can_enter() || g_screensaver_screen == NULL)
        return;

    g_screensaver_active = true;
    screensaver_update_clock();
    lv_screen_load_anim(g_screensaver_screen,
                        manual ? LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM
                               : LV_SCREEN_LOAD_ANIM_FADE_IN,
                        manual ? 220 : 320, 0, false);
    printf("[screensaver] entered (%s, timeout=%lu seconds)\n",
           manual ? "top swipe" : "idle",
           (unsigned long)g_system_settings.sleep_timeout_seconds);
}

static void screensaver_exit(void)
{
    if (!g_screensaver_active || g_home_screen == NULL)
        return;

    g_screensaver_active = false;
    lv_screen_load_anim(g_home_screen, LV_SCREEN_LOAD_ANIM_MOVE_TOP,
                        240, 0, false);
    if (g_display != NULL)
        lv_display_trigger_activity(g_display);
    printf("[screensaver] unlocked by upward swipe\n");
}

static void screensaver_timer_cb(lv_timer_t *timer)
{
    uint32_t timeout_seconds = g_system_settings.sleep_timeout_seconds;

    (void)timer;
    if (g_screensaver_active) {
        screensaver_update_clock();
        return;
    }
    if (timeout_seconds == 0 || g_display == NULL ||
        !screensaver_can_enter()) {
        return;
    }
    if (lv_display_get_inactive_time(g_display) >=
        timeout_seconds * 1000u) {
        screensaver_enter(false);
    }
}

static void screensaver_touch_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if (g_touch_indev == NULL)
        return;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(g_touch_indev, &g_screensaver_gesture_start);
        g_screensaver_gesture_tracking = true;
        return;
    }
    if ((code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) ||
        !g_screensaver_gesture_tracking) {
        return;
    }

    g_screensaver_gesture_tracking = false;
    lv_indev_get_point(g_touch_indev, &point);
    int dx = (int)point.x - (int)g_screensaver_gesture_start.x;
    int dy = (int)point.y - (int)g_screensaver_gesture_start.y;

    if (g_screensaver_active) {
        if (dy <= -SCREENSAVER_SWIPE_DISTANCE &&
            abs(dy) > abs(dx)) {
            screensaver_exit();
        }
        return;
    }

    if (g_screensaver_gesture_start.y <= SCREENSAVER_TOP_GESTURE_Y &&
        dy >= SCREENSAVER_SWIPE_DISTANCE && abs(dy) > abs(dx)) {
        screensaver_enter(true);
    }
}

static void create_screensaver_view(void)
{
    g_screensaver_active = false;
    g_screensaver_gesture_tracking = false;
    g_screensaver_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(g_screensaver_screen);
    lv_obj_set_size(g_screensaver_screen, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(g_screensaver_screen,
                              lv_color_hex(0xF7F9FC), 0);
    lv_obj_set_style_bg_opa(g_screensaver_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_screensaver_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *background = lv_image_create(g_screensaver_screen);
    lv_image_set_src(background, &dshanpi_screensaver_background);
    lv_obj_set_pos(background, 0, 0);
    lv_obj_remove_flag(background,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand =
        make_label(g_screensaver_screen, "100ASK", &lv_font_montserrat_28,
                   0x16A225);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_t *brand_cn =
        make_label(g_screensaver_screen, "百问网", &ui_font_source_han_20,
                   0x16A225);
    lv_obj_align(brand_cn, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_t *module =
        make_label(g_screensaver_screen, "A I   M O D U L E",
                   &lv_font_montserrat_14, 0x1769D1);
    lv_obj_align(module, LV_ALIGN_TOP_MID, 0, 158);

    g_screensaver_time =
        make_label(g_screensaver_screen, "--:--", &lv_font_montserrat_48,
                   0x14233A);
    lv_obj_align(g_screensaver_time, LV_ALIGN_TOP_MID, 0, 204);
    g_screensaver_date =
        make_label(g_screensaver_screen, "----.--.--",
                   &lv_font_montserrat_20, 0x5C6B7D);
    lv_obj_align(g_screensaver_date, LV_ALIGN_TOP_MID, 0, 268);

    lv_obj_t *unlock = lv_obj_create(g_screensaver_screen);
    lv_obj_set_size(unlock, 248, 48);
    lv_obj_align(unlock, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(unlock, 24, 0);
    lv_obj_set_style_bg_color(unlock, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(unlock, LV_OPA_90, 0);
    lv_obj_set_style_border_width(unlock, 1, 0);
    lv_obj_set_style_border_color(unlock, lv_color_hex(0xBBD7F3), 0);
    lv_obj_set_style_shadow_width(unlock, 10, 0);
    lv_obj_set_style_shadow_opa(unlock, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(unlock, 0, 0);
    lv_obj_remove_flag(unlock,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *unlock_text =
        make_label(unlock, settings_text("Swipe up to unlock"),
                   settings_ui_font(&lv_font_montserrat_14), 0x14569D);
    lv_obj_center(unlock_text);
    lv_obj_t *unlock_arrow =
        make_label(unlock, LV_SYMBOL_UP, &lv_font_montserrat_16, 0x14569D);
    lv_obj_align(unlock_arrow, LV_ALIGN_LEFT_MID, 16, 0);

    screensaver_update_clock();
    g_screensaver_timer = lv_timer_create(screensaver_timer_cb, 1000, NULL);
}

static void toast_hide_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    g_toast_timer = NULL;
}

static void show_toast(const char *text)
{
    /* Toasts are shared by every view.  Select the font when the toast is
     * shown so a runtime language change also applies to status popups.
     * Montserrat does not contain the CJK glyphs used by UART Lab. */
    lv_obj_set_style_text_font(
        g_toast_label,
        g_system_settings.language == DSHANPI_LANG_EN
            ? &lv_font_montserrat_14 : &ui_font_source_han_20,
        0);
    lv_label_set_text(g_toast_label, text);
    lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_toast);
    if (g_toast_timer != NULL) {
        lv_timer_delete(g_toast_timer);
    }
    g_toast_timer = lv_timer_create(toast_hide_cb, 1400, NULL);
    lv_timer_set_repeat_count(g_toast_timer, 1);
}

static bool screenshot_notification_consume(void)
{
    char image_path[384];
    FILE *file;

    file = fopen(DSHANPI_SCREENSHOT_NOTICE_PATH, "r");
    if (file == NULL)
        return false;
    bool valid = fgets(image_path, sizeof(image_path), file) != NULL;
    fclose(file);
    unlink(DSHANPI_SCREENSHOT_NOTICE_PATH);
    if (!valid)
        return false;
    image_path[strcspn(image_path, "\r\n")] = '\0';
    printf("[screenshot] showing saved notification: %s\n", image_path);
    return true;
}

static bool ota_update_is_active(void)
{
    dshanpi_ota_snapshot_t snapshot;

    dshanpi_ota_get_snapshot(&snapshot);
    return snapshot.busy ||
           snapshot.state == DSHANPI_OTA_CHECKING ||
           snapshot.state == DSHANPI_OTA_VERIFYING_MANIFEST ||
           snapshot.state == DSHANPI_OTA_DOWNLOADING ||
           snapshot.state == DSHANPI_OTA_VERIFYING_PACKAGE ||
           snapshot.state == DSHANPI_OTA_INSTALLING ||
           snapshot.state == DSHANPI_OTA_REBOOTING;
}

static void app_launch_overlay_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void app_launch_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    g_app_launch_timer = NULL;
    g_stop = 1;
}

static void show_app_launch_overlay(const app_info_t *app)
{
    lv_anim_t animation;

    (void)app;
    if (g_app_launch_overlay == NULL) {
        g_stop = 1;
        return;
    }
    if (g_app_launch_timer != NULL) {
        lv_timer_delete(g_app_launch_timer);
        g_app_launch_timer = NULL;
    }

    lv_spinner_set_anim_params(g_app_launch_spinner, 820, 82);

    lv_obj_remove_flag(g_app_launch_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_app_launch_overlay);
    lv_obj_set_style_opa_layered(g_app_launch_overlay, LV_OPA_TRANSP, 0);
    lv_anim_delete(g_app_launch_overlay, NULL);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_app_launch_overlay);
    lv_anim_set_exec_cb(&animation, app_launch_overlay_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 90);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);

    /*
     * Keep only a short hand-off frame.  The old 520 ms timeout delayed every
     * external AI app even after the launch request had already been accepted.
     */
    g_app_launch_timer = lv_timer_create(app_launch_timer_cb, 120, NULL);
    lv_timer_set_repeat_count(g_app_launch_timer, 1);
}

static void close_modal_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
}

static void open_app_cb(lv_event_t *event)
{
    const app_info_t *app = lv_event_get_user_data(event);
    char message[64];

    if (!lv_k230_touch_accept_click()) {
        return;
    }
    if (app == &g_apps[APP_SETTINGS]) {
        show_settings();
        return;
    }
    if (ota_update_is_active()) {
        show_toast(settings_text("Update already running"));
        return;
    }
    if (app == &g_apps[APP_CAMERA]) {
        show_camera();
        return;
    }
    if (app == &g_apps[APP_DUAL_CAMERA]) {
        show_dual_camera();
        return;
    }
    if (app == &g_apps[APP_FACE_STUDIO]) {
        printf("[desktop] launching Face Studio directly\n");
        g_launch_face_studio = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_FACE_GEOMETRY]) {
        printf("[desktop] launching Face Geometry directly\n");
        g_launch_face_geometry = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_HAND_STUDIO]) {
        printf("[desktop] launching Hand Studio directly\n");
        g_launch_hand_studio = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_HUMAN_STUDIO]) {
        printf("[desktop] launching Human Studio directly\n");
        g_launch_human_studio = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_SMART_DRIVING]) {
        printf("[desktop] launching Smart Driving directly\n");
        g_launch_smart_driving = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_OCR_DETECTION]) {
        g_launch_ocr_detection = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_OBJECT_DETECTION]) {
        g_launch_yolov8_vision = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_YOLO_MODELS]) {
        g_launch_yolo_models = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_NETWORK_CAMERA] ||
        app == &g_apps[APP_RTMP_STREAM_LEGACY]) {
        int connected = 0;
        struct ifconfig_t config;
        memset(&config, 0, sizeof(config));
        if (netmgmt_wlan_sta_isconnected(&connected) != 0 || !connected ||
            netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) != 0 ||
            config.ip.addr == 0) {
            show_toast("Connect Wi-Fi in Settings first");
            return;
        }
        g_launch_network_camera = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_GALLERY]) {
        show_gallery();
        return;
    }
    if (app == &g_apps[APP_DRAWING]) {
        show_paint();
        return;
    }
    if (app == &g_apps[APP_CV_LITE]) {
        printf("[desktop] launching CV Lite directly\n");
        g_launch_cv_lite = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_PLATE_OCR]) {
        printf("[desktop] launching Plate OCR directly\n");
        g_launch_plate_ocr = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_CODE_SCANNER]) {
        g_launch_code_scanner = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_SELF_LEARNING]) {
        g_launch_self_learning = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_CLOUD_MODEL]) {
        show_cloud_model();
        return;
    }
    if (app == &g_apps[APP_UVC_CAMERA]) {
        char device_info[128];
        if (uvc_host_get_devinfo(device_info, sizeof(device_info)) != 0) {
            show_toast("No USB camera detected");
            return;
        }
        g_launch_uvc_camera = true;
        show_app_launch_overlay(app);
        return;
    }
    if (app == &g_apps[APP_UART_LAB]) {
        show_uart_lab();
        return;
    }

    lv_label_set_text(g_modal_title, app->name);
    lv_label_set_text(g_modal_symbol, app->symbol);
    lv_label_set_text(g_modal_description, app->description);
    lv_obj_remove_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_modal);

    snprintf(message, sizeof(message), "Selected: %s", app->name);
    show_toast(message);
}

static void launch_app_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
    show_toast("Application module is ready");
}

#if 0
/* Superseded by the generated ARGB application artwork. Kept out of the
 * build temporarily so older icon geometry remains easy to compare while
 * the new bitmap set is validated on the target panel. */
static void material_draw_line(lv_layer_t *layer, lv_color_t color,
                               int width, int x1, int y1, int x2, int y2)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

static void material_draw_rect(lv_layer_t *layer, lv_color_t color,
                               int x1, int y1, int x2, int y2,
                               int radius, int border_width, bool filled)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t area = {x1, y1, x2, y2};
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = radius;
    dsc.bg_color = color;
    dsc.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    dsc.border_color = color;
    dsc.border_width = filled ? 0 : border_width;
    dsc.border_opa = filled ? LV_OPA_TRANSP : LV_OPA_COVER;
    dsc.outline_opa = LV_OPA_TRANSP;
    dsc.shadow_opa = LV_OPA_TRANSP;
    lv_draw_rect(layer, &dsc, &area);
}

static void material_draw_arc(lv_layer_t *layer, lv_color_t color,
                              int width, int cx, int cy, int radius,
                              int start_angle, int end_angle)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = radius;
    dsc.start_angle = start_angle;
    dsc.end_angle = end_angle;
    dsc.rounded = 1;
    lv_draw_arc(layer, &dsc);
}

static void material_draw_scan_corners(lv_layer_t *layer, lv_color_t color,
                                       int x, int y, int left, int top,
                                       int right, int bottom)
{
    const int length = 13;
    const int width = 5;
    material_draw_line(layer, color, width, x + left, y + top,
                       x + left + length, y + top);
    material_draw_line(layer, color, width, x + left, y + top,
                       x + left, y + top + length);
    material_draw_line(layer, color, width, x + right - length, y + top,
                       x + right, y + top);
    material_draw_line(layer, color, width, x + right, y + top,
                       x + right, y + top + length);
    material_draw_line(layer, color, width, x + left, y + bottom - length,
                       x + left, y + bottom);
    material_draw_line(layer, color, width, x + left, y + bottom,
                       x + left + length, y + bottom);
    material_draw_line(layer, color, width, x + right, y + bottom - length,
                       x + right, y + bottom);
    material_draw_line(layer, color, width, x + right - length, y + bottom,
                       x + right, y + bottom);
}

static void material_icon_draw_cb(lv_event_t *event)
{
    const app_info_t *app = lv_event_get_user_data(event);
    lv_obj_t *obj = lv_event_get_current_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const int x = area.x1;
    const int y = area.y1;
    const int cx = x + 54;
    const int cy = y + 54;
    const lv_color_t color = lv_color_hex(app->icon_color);
    const lv_color_t cutout = lv_color_hex(app->color);

    switch (app->icon_id) {
    case MATERIAL_ICON_SETTINGS: {
        static const int8_t points[][4] = {
            {54, 20, 54, 34}, {54, 74, 54, 88},
            {20, 54, 34, 54}, {74, 54, 88, 54},
            {30, 30, 40, 40}, {68, 68, 78, 78},
            {78, 30, 68, 40}, {40, 68, 30, 78},
        };
        for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
            material_draw_line(layer, color, 7,
                               x + points[i][0], y + points[i][1],
                               x + points[i][2], y + points[i][3]);
        }
        material_draw_rect(layer, color, x + 36, y + 36, x + 72, y + 72,
                           LV_RADIUS_CIRCLE, 6, false);
        material_draw_rect(layer, color, x + 49, y + 49, x + 59, y + 59,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    }
    case MATERIAL_ICON_CAMERA:
        material_draw_rect(layer, color, x + 20, y + 34, x + 88, y + 79,
                           12, 0, true);
        material_draw_rect(layer, color, x + 36, y + 27, x + 61, y + 42,
                           7, 0, true);
        material_draw_rect(layer, cutout, x + 40, y + 42, x + 68, y + 70,
                           LV_RADIUS_CIRCLE, 5, false);
        material_draw_rect(layer, cutout, x + 75, y + 42, x + 81, y + 48,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    case MATERIAL_ICON_GALLERY:
        material_draw_rect(layer, color, x + 29, y + 24, x + 84, y + 72,
                           9, 4, false);
        material_draw_rect(layer, color, x + 19, y + 34, x + 76, y + 83,
                           10, 5, false);
        material_draw_rect(layer, color, x + 29, y + 44, x + 39, y + 54,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_line(layer, color, 5, x + 27, y + 73, x + 43, y + 57);
        material_draw_line(layer, color, 5, x + 43, y + 57, x + 51, y + 66);
        material_draw_line(layer, color, 5, x + 51, y + 66, x + 61, y + 55);
        material_draw_line(layer, color, 5, x + 61, y + 55, x + 70, y + 65);
        break;
    case MATERIAL_ICON_FACE:
        material_draw_scan_corners(layer, color, x, y, 19, 19, 89, 89);
        material_draw_rect(layer, color, x + 43, y + 31, x + 65, y + 53,
                           LV_RADIUS_CIRCLE, 4, false);
        material_draw_arc(layer, color, 5, cx, y + 75, 20, 200, 340);
        break;
    case MATERIAL_ICON_FACE_GEOMETRY:
        material_draw_rect(layer, color, x + 38, y + 23, x + 70, y + 55,
                           LV_RADIUS_CIRCLE, 4, false);
        material_draw_line(layer, color, 4, x + 54, y + 56, x + 36, y + 82);
        material_draw_line(layer, color, 4, x + 54, y + 56, x + 72, y + 82);
        material_draw_line(layer, color, 4, x + 36, y + 82, x + 72, y + 82);
        material_draw_line(layer, color, 3, x + 43, y + 35, x + 65, y + 43);
        material_draw_line(layer, color, 3, x + 43, y + 35, x + 54, y + 56);
        material_draw_line(layer, color, 3, x + 65, y + 43, x + 54, y + 56);
        material_draw_rect(layer, color, x + 40, y + 32, x + 46, y + 38,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 62, y + 40, x + 68, y + 46,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 51, y + 53, x + 57, y + 59,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    case MATERIAL_ICON_HAND:
        material_draw_rect(layer, color, x + 39, y + 48, x + 73, y + 82,
                           13, 0, true);
        material_draw_line(layer, color, 8, x + 44, y + 55, x + 41, y + 31);
        material_draw_line(layer, color, 8, x + 52, y + 50, x + 52, y + 23);
        material_draw_line(layer, color, 8, x + 61, y + 50, x + 62, y + 26);
        material_draw_line(layer, color, 8, x + 69, y + 55, x + 72, y + 34);
        material_draw_line(layer, color, 8, x + 42, y + 62, x + 29, y + 52);
        break;
    case MATERIAL_ICON_HUMAN:
        material_draw_rect(layer, color, x + 45, y + 20, x + 63, y + 38,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_line(layer, color, 7, cx, y + 45, cx, y + 68);
        material_draw_line(layer, color, 7, x + 34, y + 51, x + 74, y + 51);
        material_draw_line(layer, color, 7, cx, y + 67, x + 40, y + 86);
        material_draw_line(layer, color, 7, cx, y + 67, x + 68, y + 86);
        break;
    case MATERIAL_ICON_DRIVING:
        material_draw_rect(layer, color, x + 19, y + 47, x + 89, y + 75,
                           11, 0, true);
        material_draw_line(layer, color, 7, x + 34, y + 48, x + 44, y + 35);
        material_draw_line(layer, color, 7, x + 44, y + 35, x + 68, y + 35);
        material_draw_line(layer, color, 7, x + 68, y + 35, x + 78, y + 48);
        material_draw_rect(layer, cutout, x + 31, y + 66, x + 43, y + 78,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, cutout, x + 65, y + 66, x + 77, y + 78,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_line(layer, cutout, 4, x + 28, y + 56, x + 80, y + 56);
        break;
    case MATERIAL_ICON_OCR:
        material_draw_scan_corners(layer, color, x, y, 18, 20, 90, 88);
        material_draw_line(layer, color, 5, x + 34, y + 37, x + 74, y + 37);
        material_draw_line(layer, color, 5, x + 34, y + 52, x + 68, y + 52);
        material_draw_line(layer, color, 5, x + 34, y + 67, x + 76, y + 67);
        break;
    case MATERIAL_ICON_DETECTION:
        material_draw_scan_corners(layer, color, x, y, 18, 18, 90, 90);
        material_draw_rect(layer, color, x + 35, y + 34, x + 73, y + 73,
                           7, 5, false);
        material_draw_rect(layer, color, x + 50, y + 49, x + 58, y + 57,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    case MATERIAL_ICON_YOLO:
        material_draw_rect(layer, color, x + 22, y + 22, x + 86, y + 86,
                           12, 5, false);
        material_draw_rect(layer, color, x + 35, y + 38, x + 73, y + 70,
                           9, 4, false);
        material_draw_line(layer, color, 4, x + 54, y + 31, x + 54, y + 46);
        material_draw_line(layer, color, 4, x + 54, y + 62, x + 54, y + 77);
        break;
    case MATERIAL_ICON_STREAM:
    case MATERIAL_ICON_RTMP:
        material_draw_rect(layer, color, x + 19, y + 35, x + 65, y + 75,
                           8, 0, true);
        material_draw_line(layer, color, 8, x + 65, y + 49, x + 82, y + 40);
        material_draw_line(layer, color, 8, x + 65, y + 61, x + 82, y + 70);
        material_draw_arc(layer, color, 4, x + 83, y + 28, 12, 190, 270);
        material_draw_arc(layer, color, 4, x + 83, y + 28, 22, 190, 270);
        break;
    case MATERIAL_ICON_DRAWING:
        material_draw_line(layer, color, 11, x + 31, y + 76, x + 72, y + 35);
        material_draw_line(layer, cutout, 3, x + 35, y + 72, x + 68, y + 39);
        material_draw_line(layer, color, 5, x + 27, y + 83, x + 38, y + 79);
        material_draw_rect(layer, color, x + 68, y + 27, x + 81, y + 40,
                           4, 0, true);
        break;
    case MATERIAL_ICON_CV:
        material_draw_scan_corners(layer, color, x, y, 20, 22, 88, 86);
        material_draw_arc(layer, color, 5, cx, cy, 27, 205, 335);
        material_draw_arc(layer, color, 5, cx, cy, 27, 25, 155);
        material_draw_rect(layer, color, x + 45, y + 45, x + 63, y + 63,
                           LV_RADIUS_CIRCLE, 4, false);
        break;
    case MATERIAL_ICON_PLATE:
        material_draw_rect(layer, color, x + 18, y + 35, x + 90, y + 74,
                           9, 5, false);
        material_draw_line(layer, color, 5, x + 31, y + 48, x + 31, y + 62);
        material_draw_line(layer, color, 5, x + 31, y + 48, x + 42, y + 48);
        material_draw_line(layer, color, 5, x + 31, y + 62, x + 42, y + 62);
        material_draw_line(layer, color, 5, x + 52, y + 48, x + 52, y + 62);
        material_draw_line(layer, color, 5, x + 64, y + 48, x + 64, y + 62);
        material_draw_line(layer, color, 5, x + 76, y + 48, x + 76, y + 62);
        break;
    case MATERIAL_ICON_SCANNER:
        material_draw_scan_corners(layer, color, x, y, 17, 17, 91, 91);
        material_draw_rect(layer, color, x + 29, y + 29, x + 43, y + 43,
                           2, 4, false);
        material_draw_rect(layer, color, x + 64, y + 29, x + 78, y + 43,
                           2, 4, false);
        material_draw_rect(layer, color, x + 29, y + 64, x + 43, y + 78,
                           2, 4, false);
        material_draw_rect(layer, color, x + 60, y + 58, x + 67, y + 65,
                           1, 0, true);
        material_draw_rect(layer, color, x + 72, y + 70, x + 79, y + 78,
                           1, 0, true);
        material_draw_line(layer, color, 3, x + 23, y + 54, x + 85, y + 54);
        break;
    case MATERIAL_ICON_LEARNING:
        material_draw_line(layer, color, 4, x + 32, y + 35, x + 54, y + 25);
        material_draw_line(layer, color, 4, x + 54, y + 25, x + 76, y + 37);
        material_draw_line(layer, color, 4, x + 32, y + 35, x + 39, y + 65);
        material_draw_line(layer, color, 4, x + 76, y + 37, x + 69, y + 67);
        material_draw_line(layer, color, 4, x + 39, y + 65, x + 54, y + 82);
        material_draw_line(layer, color, 4, x + 69, y + 67, x + 54, y + 82);
        material_draw_line(layer, color, 4, x + 32, y + 35, x + 69, y + 67);
        material_draw_line(layer, color, 4, x + 76, y + 37, x + 39, y + 65);
        material_draw_rect(layer, color, x + 26, y + 29, x + 38, y + 41,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 48, y + 19, x + 60, y + 31,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 70, y + 31, x + 82, y + 43,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 33, y + 59, x + 45, y + 71,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 63, y + 61, x + 75, y + 73,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 48, y + 76, x + 60, y + 88,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    case MATERIAL_ICON_CLOUD:
        material_draw_rect(layer, color, x + 24, y + 50, x + 84, y + 76,
                           13, 0, true);
        material_draw_rect(layer, color, x + 31, y + 37, x + 57, y + 63,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 49, y + 30, x + 79, y + 60,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_line(layer, cutout, 5, cx, y + 48, cx, y + 69);
        material_draw_line(layer, cutout, 5, x + 46, y + 61, cx, y + 69);
        material_draw_line(layer, cutout, 5, x + 62, y + 61, cx, y + 69);
        break;
    case MATERIAL_ICON_USB:
        material_draw_line(layer, color, 6, cx, y + 24, cx, y + 76);
        material_draw_line(layer, color, 6, cx, y + 49, x + 36, y + 39);
        material_draw_line(layer, color, 6, x + 36, y + 39, x + 36, y + 31);
        material_draw_line(layer, color, 6, cx, y + 61, x + 71, y + 49);
        material_draw_line(layer, color, 5, x + 50, y + 29, cx, y + 22);
        material_draw_line(layer, color, 5, x + 58, y + 29, cx, y + 22);
        material_draw_rect(layer, color, x + 31, y + 25, x + 41, y + 35,
                           2, 0, true);
        material_draw_rect(layer, color, x + 66, y + 43, x + 77, y + 54,
                           LV_RADIUS_CIRCLE, 0, true);
        material_draw_rect(layer, color, x + 47, y + 73, x + 61, y + 87,
                           LV_RADIUS_CIRCLE, 0, true);
        break;
    }
}
#endif

static lv_obj_t *create_app_button(lv_obj_t *parent, const app_info_t *app,
                                   int width, int height, int icon_size)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xE7E9EF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 28, LV_STATE_PRESSED);
    /*
     * lv_button enables SCROLL_ON_FOCUS by default.  A pointer press focuses
     * a new desktop icon before CLICKED is emitted, which makes LVGL animate
     * the grid to bring that icon into view.  On touch this looks exactly
     * like the page slides first and launches the application afterwards.
     * Desktop icons do not need keyboard focus, so keep scrolling exclusively
     * under the drag gesture path.  Avoid pressed-state scaling as well: the
     * color feedback is sufficient and does not resemble a short movement.
     */
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                                   LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(button, open_app_cb, LV_EVENT_CLICKED, (void *)app);

    lv_obj_t *icon = lv_obj_create(button);
    lv_obj_set_size(icon, icon_size, icon_size);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(icon, 30, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(app->color), 0);
    lv_obj_set_style_bg_grad_dir(icon, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
#if EDGEOS_SOFTWARE_RENDER_FAST_PATH
    /* The rotated K230 display currently uses a full-frame software draw.
     * Blurring a shadow around every visible icon is disproportionately
     * expensive while the grid moves, so use the same clean flat icon style
     * as the rest of the controls. */
    lv_obj_set_style_shadow_width(icon, 0, 0);
#else
    lv_obj_set_style_shadow_width(icon, 14, 0);
    lv_obj_set_style_shadow_spread(icon, 0, 0);
    lv_obj_set_style_shadow_offset_y(icon, 5, 0);
    lv_obj_set_style_shadow_color(icon, lv_color_hex(0x4A5568), 0);
    lv_obj_set_style_shadow_opa(icon, LV_OPA_10, 0);
#endif
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *artwork = lv_image_create(icon);
    lv_image_set_src(artwork, material_app_icon_get((unsigned int)app->icon_id));
    lv_obj_center(artwork);
    lv_obj_remove_flag(artwork,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t *name_font =
        g_system_settings.language == DSHANPI_LANG_EN
            ? &lv_font_montserrat_16
            : &ui_font_source_han_20;
    lv_obj_t *name = make_label(button, localized_app_name(app), name_font,
                                0x25262A);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, icon_size + 10);
    return button;
}

static void create_app_launch_overlay(lv_obj_t *screen)
{
    g_app_launch_overlay = lv_obj_create(screen);
    /*
     * A plain lv_obj inherits the theme's rounded container style.  That left
     * the light desktop visible through all four corners of the black launch
     * screen.  Start from a completely style-free full-screen backdrop.
     */
    lv_obj_remove_style_all(g_app_launch_overlay);
    lv_obj_set_size(g_app_launch_overlay, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_app_launch_overlay, 0, 0);
    lv_obj_set_style_radius(g_app_launch_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_app_launch_overlay,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_app_launch_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(g_app_launch_overlay, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(g_app_launch_overlay, 0, 0);
    lv_obj_set_style_outline_width(g_app_launch_overlay, 0, 0);
    lv_obj_set_style_shadow_width(g_app_launch_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_app_launch_overlay, 0, 0);
    lv_obj_remove_flag(g_app_launch_overlay, LV_OBJ_FLAG_SCROLLABLE);

    g_app_launch_spinner = lv_spinner_create(g_app_launch_overlay);
    lv_obj_set_size(g_app_launch_spinner, 50, 50);
    lv_obj_center(g_app_launch_spinner);
    lv_obj_set_style_arc_width(g_app_launch_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_app_launch_spinner, 4,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_app_launch_spinner,
                               lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_app_launch_spinner,
                               lv_color_hex(0xF4F7FB),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_app_launch_spinner, true,
                                 LV_PART_INDICATOR);
    lv_spinner_set_anim_params(g_app_launch_spinner, 820, 82);

    lv_obj_add_flag(g_app_launch_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void create_material_background(lv_obj_t *screen)
{
#if EDGEOS_SOFTWARE_RENDER_FAST_PATH
    /* Keep the opaque screen color as the desktop background.  The two old
     * half-transparent 500 px circles forced hundreds of thousands of alpha
     * blends on every FULL-rendered scroll frame. */
    (void)screen;
#else
    /* Soft tonal shapes keep the home screen light without competing with
     * the application icons. They are ordinary LVGL objects so the desktop
     * stays resolution-independent and adds no bitmap assets to firmware. */
    lv_obj_t *blue_shape = lv_obj_create(screen);
    lv_obj_remove_style_all(blue_shape);
    lv_obj_set_size(blue_shape, 500, 500);
    lv_obj_set_pos(blue_shape, -285, 235);
    lv_obj_set_style_radius(blue_shape, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(blue_shape, lv_color_hex(0xDDE9F6), 0);
    lv_obj_set_style_bg_opa(blue_shape, LV_OPA_50, 0);
    lv_obj_remove_flag(blue_shape,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *green_shape = lv_obj_create(screen);
    lv_obj_remove_style_all(green_shape);
    lv_obj_set_size(green_shape, 390, 390);
    lv_obj_set_pos(green_shape, 470, -230);
    lv_obj_set_style_radius(green_shape, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(green_shape, lv_color_hex(0xE7EEE0), 0);
    lv_obj_set_style_bg_opa(green_shape, LV_OPA_60, 0);
    lv_obj_remove_flag(green_shape,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
#endif
}

static void create_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, UI_WIDTH, 44);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    style_plain(bar);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFCFBF7), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    /* Android-style status: time on the left, compact system glyphs on the
     * right. Once DHCP succeeds, show the address immediately before the
     * Wi-Fi glyph so the active board endpoint is visible at a glance. */
    g_desktop_time_status =
        make_label(bar, "--:--", &lv_font_montserrat_28, 0x202124);
    lv_obj_align(g_desktop_time_status, LV_ALIGN_LEFT_MID, 16, 0);
    g_desktop_network_status = make_label(
        bar, LV_SYMBOL_WIFI "   " LV_SYMBOL_DRIVE "   " LV_SYMBOL_VIDEO,
        &lv_font_montserrat_28, 0x343438);
    lv_obj_align(g_desktop_network_status, LV_ALIGN_RIGHT_MID, -16, 0);
    g_desktop_ip_status =
        make_label(bar, "", &lv_font_montserrat_28, 0x5F6368);
    lv_obj_align_to(g_desktop_ip_status, g_desktop_network_status,
                    LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_add_flag(g_desktop_ip_status, LV_OBJ_FLAG_HIDDEN);
    g_desktop_system_status = NULL;
}

static void create_app_grid(lv_obj_t *screen)
{
    lv_obj_t *grid = lv_obj_create(screen);
    g_app_grid = grid;
    lv_obj_set_size(grid, 616, 422);
    lv_obj_set_pos(grid, 12, 56);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 4, 0);
    /*
     * Let Flex calculate rows from the number of applications. The previous
     * fixed LVGL grid row template had to be extended manually whenever a new
     * application was added; objects assigned past its last row fell back to
     * row zero and overlapped the first icon.
     *
     * Fixed 184 px children plus 12 px column gaps fit exactly three items in
     * this 616 px container. ROW_WRAP creates as many rows as necessary.
     */
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
    /*
     * Keep desktop scrolling continuous instead of snapping every release to
     * the beginning of a Flex row.  LVGL's snap path clamps the predicted
     * throw to the valid range, which prevents its elastic overscroll path
     * from producing a visible top/bottom rebound.  With free momentum the
     * content follows the finger with resistance beyond either edge, then
     * animates back to the closest valid position on release.
     */
    lv_obj_set_scroll_snap_y(grid, LV_SCROLL_SNAP_NONE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLL_ELASTIC |
                              LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_width(grid, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(grid, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x7C8188),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(grid, LV_OPA_30, LV_PART_SCROLLBAR);

    for (size_t i = 0;
         i < sizeof(g_desktop_app_order) / sizeof(g_desktop_app_order[0]);
         ++i) {
        create_app_button(grid, &g_apps[g_desktop_app_order[i]],
                          184, 184, 108);
    }

    /* A launched native application replaces this desktop worker. Restore
     * the one-shot scroll position left by the previous worker after Flex
     * has calculated the complete application list height. */
    FILE *state = fopen(DESKTOP_SCROLL_STATE_PATH, "r");
    if (state != NULL) {
        int scroll_y = 0;
        if (fscanf(state, "%d", &scroll_y) == 1 && scroll_y >= 0) {
            lv_obj_update_layout(grid);
            lv_obj_scroll_to_y(grid, scroll_y, LV_ANIM_OFF);
            printf("[desktop] restored app scroll position: %d\n", scroll_y);
        }
        fclose(state);
        unlink(DESKTOP_SCROLL_STATE_PATH);
    }
}

static bool has_external_launch_request(void)
{
    return g_launch_ai_app != NULL || g_launch_face_studio ||
           g_launch_face_geometry || g_launch_hand_studio ||
           g_launch_human_studio || g_launch_smart_driving ||
           g_launch_ocr_detection || g_launch_yolov8_vision ||
           g_launch_network_camera ||
           g_launch_yolo_models ||
           g_launch_gallery_player || g_launch_cv_lite ||
           g_launch_plate_ocr || g_launch_code_scanner ||
           g_launch_self_learning || g_launch_cloud_model ||
           g_launch_uvc_camera;
}

static void save_app_scroll_position(void)
{
    if (!has_external_launch_request() || g_app_grid == NULL) return;
    FILE *state = fopen(DESKTOP_SCROLL_STATE_PATH, "w");
    if (state == NULL) {
        printf("[desktop] unable to save app scroll position: %s\n",
               strerror(errno));
        return;
    }
    int scroll_y = lv_obj_get_scroll_y(g_app_grid);
    if (scroll_y < 0) scroll_y = 0;
    fprintf(state, "%d\n", scroll_y);
    fclose(state);
    printf("[desktop] saved app scroll position: %d\n", scroll_y);
}

static void create_app_pages(lv_obj_t *screen)
{
    create_app_grid(screen);
}

typedef struct {
    char path[320];
    time_t modified;
    int is_video;
} photo_entry_t;

typedef struct {
    char path[320];
    int is_video;
    size_t index;
    lv_image_dsc_t thumbnail;
    uint8_t *thumbnail_data;
} gallery_media_action_t;

static void gallery_media_action_delete_cb(lv_event_t *event);

typedef struct {
    FILE *file;
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} gallery_jpeg_decoder_t;

static void format_file_size(off_t size, char *buf, size_t buf_size)
{
    if (size < 1024)
        snprintf(buf, buf_size, "%ld B", (long)size);
    else if (size < 1024 * 1024)
        snprintf(buf, buf_size, "%.1f KB", (double)size / 1024.0);
    else
        snprintf(buf, buf_size, "%.1f MB",
                 (double)size / (1024.0 * 1024.0));
}

static size_t gallery_jpeg_input(JDEC *decoder, uint8_t *buffer,
                                 size_t length)
{
    gallery_jpeg_decoder_t *source = decoder->device;
    if (buffer != NULL)
        return fread(buffer, 1, length, source->file);
    return fseek(source->file, (long)length, SEEK_CUR) == 0 ? length : 0;
}

static int gallery_jpeg_output(JDEC *decoder, void *bitmap,
                               JRECT *rectangle)
{
    gallery_jpeg_decoder_t *destination = decoder->device;
    uint32_t rectangle_width =
        (uint32_t)rectangle->right - rectangle->left + 1;
    uint32_t rectangle_height =
        (uint32_t)rectangle->bottom - rectangle->top + 1;
    const uint8_t *source = bitmap;

    if (rectangle->right >= destination->width ||
        rectangle->bottom >= destination->height)
        return 0;
    for (uint32_t row = 0; row < rectangle_height; ++row) {
        uint8_t *target =
            destination->pixels +
            ((uint32_t)rectangle->top + row) * destination->stride +
            (uint32_t)rectangle->left * 3;
        memcpy(target, source + row * rectangle_width * 3,
               rectangle_width * 3);
    }
    return 1;
}

static int gallery_decode_jpeg(const char *path, uint8_t scale,
                               gallery_media_action_t *action)
{
    uint8_t work_buffer[4096];
    JDEC decoder;
    gallery_jpeg_decoder_t io = { 0 };
    JRESULT result;

    io.file = fopen(path, "rb");
    if (io.file == NULL)
        return -1;
    result = jd_prepare(&decoder, gallery_jpeg_input, work_buffer,
                        sizeof(work_buffer), &io);
    if (result != JDR_OK || decoder.width == 0 || decoder.height == 0) {
        fclose(io.file);
        return -1;
    }

    /* scale 0 keeps native resolution for the fullscreen viewer; scale 3 is
     * 1/8 native (1920x1080 -> 240x135) for compact gallery thumbnails. */
    if (scale == 0) {
        io.width = decoder.width;
        io.height = decoder.height;
    }
    else {
        io.width = ((uint32_t)decoder.width + 7U) >> 3;
        io.height = ((uint32_t)decoder.height + 7U) >> 3;
    }
    io.stride = io.width * 3U;
    io.pixels = malloc((size_t)io.stride * io.height);
    if (io.pixels == NULL) {
        fclose(io.file);
        return -1;
    }
    result = jd_decomp(&decoder, gallery_jpeg_output, scale);
    fclose(io.file);
    if (result != JDR_OK) {
        free(io.pixels);
        printf("[gallery] invalid JPEG skipped: %s (decoder=%d)\n",
               path, result);
        return -1;
    }

    action->thumbnail_data = io.pixels;
    memset(&action->thumbnail, 0, sizeof(action->thumbnail));
    action->thumbnail.header.cf = LV_COLOR_FORMAT_RGB888;
    action->thumbnail.header.w = io.width;
    action->thumbnail.header.h = io.height;
    action->thumbnail.header.stride = io.stride;
    action->thumbnail.data_size = io.stride * io.height;
    action->thumbnail.data = io.pixels;
    return 0;
}

static bool gallery_is_dual_camera_media(const char *path)
{
    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    return strncmp(name, "DUAL_", 5) == 0;
}

static void gallery_rotate_thumbnail_180(gallery_media_action_t *action)
{
    uint8_t *pixels = action->thumbnail_data;
    uint32_t width = action->thumbnail.header.w;
    uint32_t height = action->thumbnail.header.h;
    uint32_t stride = action->thumbnail.header.stride;
    size_t pixel_count = (size_t)width * height;

    if (pixels == NULL || width == 0 || height == 0 ||
        stride < width * 3U)
        return;

    for (size_t index = 0; index < pixel_count / 2U; ++index) {
        size_t opposite = pixel_count - 1U - index;
        uint8_t *left = pixels + (index / width) * stride +
                        (index % width) * 3U;
        uint8_t *right = pixels + (opposite / width) * stride +
                         (opposite % width) * 3U;
        for (unsigned channel = 0; channel < 3U; ++channel) {
            uint8_t temporary = left[channel];
            left[channel] = right[channel];
            right[channel] = temporary;
        }
    }
}

static int gallery_decode_grid_thumbnail(const char *path,
                                         gallery_media_action_t *action)
{
    int result = gallery_decode_jpeg(path, 3, action);
    if (result == 0 && gallery_is_dual_camera_media(path))
        gallery_rotate_thumbnail_180(action);
    return result;
}

static int compare_photos(const void *left, const void *right)
{
    const photo_entry_t *a = left;
    const photo_entry_t *b = right;
    if (a->modified < b->modified) {
        return 1;
    }
    if (a->modified > b->modified) {
        return -1;
    }
    return strcmp(b->path, a->path);
}

static void close_fullscreen_cb(lv_event_t *event)
{
    lv_obj_t *view = lv_event_get_user_data(event);
#if UI_TOUCH_DEBUG
    lv_point_t point = {0, 0};
    lv_indev_t *active = lv_indev_active();
    if (active != NULL) {
        lv_indev_get_point(active, &point);
    }
    printf("[ui-touch] fullscreen back PRESSED point=(%d,%d) view=%p\n",
           (int)point.x, (int)point.y, (void *)view);
#endif
    if (view == g_gallery_view && g_gallery_load_timer != NULL) {
        lv_timer_delete(g_gallery_load_timer);
        g_gallery_load_timer = NULL;
    }
    lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
    lv_indev_t *indev = lv_indev_active();
    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }
#if UI_TOUCH_DEBUG
    printf("[ui-touch] fullscreen hidden=%d\n",
           lv_obj_has_flag(view, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
#endif
}

static lv_obj_t *create_round_button(lv_obj_t *parent, int size,
                                     uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, size / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    return button;
}

/* Keep back icons visually balanced while making the whole top-left corner
 * easy to hit.  All top-level views place their back control close enough to
 * the edges for this extension to include x=0/y=0. */
static void expand_top_left_back_hit_area(lv_obj_t *button)
{
    lv_obj_set_ext_click_area(button, 24);
}

static int cloud_read_json_string(const char *path, const char *key,
                                  char *value, size_t value_size)
{
    FILE *file;
    char *json;
    char pattern[64];
    char *cursor;
    long length;
    size_t output = 0;

    if (value_size == 0) return -1;
    value[0] = '\0';
    file = fopen(path, "rb");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || length > 256 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    json = malloc((size_t)length + 1);
    if (json == NULL) {
        fclose(file);
        return -1;
    }
    if (fread(json, 1, (size_t)length, file) != (size_t)length) {
        free(json);
        fclose(file);
        return -1;
    }
    fclose(file);
    json[length] = '\0';

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    cursor = strstr(json, pattern);
    if (cursor != NULL) {
        cursor += strlen(pattern);
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor == ':') ++cursor;
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor == '"') {
            ++cursor;
            while (*cursor != '\0' && *cursor != '"' &&
                   output + 1 < value_size) {
                if (*cursor == '\\' && cursor[1] != '\0') ++cursor;
                value[output++] = *cursor++;
            }
            value[output] = '\0';
        }
    }
    free(json);
    return output > 0 ? 0 : -1;
}

static int cloud_resolve_kmodel(const char *config_path, char *resolved,
                                size_t resolved_size)
{
    char configured[320];
    char candidate[384];
    const char *relative;
    const char *base;
    const char *backslash;

    if (cloud_read_json_string(config_path, "kmodel_path", configured,
                               sizeof(configured)) != 0)
        return -1;
    relative = configured;
    while (relative[0] == '.' && relative[1] == '/') relative += 2;
    if (relative[0] == '/')
        snprintf(candidate, sizeof(candidate), "%s", relative);
    else
        snprintf(candidate, sizeof(candidate), "/sdcard/%s", relative);
    if (access(candidate, R_OK) == 0) {
        snprintf(resolved, resolved_size, "%s", candidate);
        return 0;
    }

    /* Cloud exports can contain the training host's old directory.  Users
     * only need to copy the referenced model's basename to /sdcard. */
    base = strrchr(configured, '/');
    backslash = strrchr(configured, '\\');
    if (backslash != NULL && (base == NULL || backslash > base))
        base = backslash;
    base = base == NULL ? configured : base + 1;
    snprintf(candidate, sizeof(candidate), "/sdcard/%s", base);
    snprintf(resolved, resolved_size, "%s", candidate);
    return access(candidate, R_OK) == 0 ? 0 : -1;
}

static bool cloud_update_file_status(void)
{
    const cloud_task_info_t *task;
    const char *config_a;
    const char *config_b = NULL;
    char model_a[384] = "model not declared";
    char model_b[384] = "model not declared";
    char executable[384];
    char status[768];
    bool config_a_ok;
    bool config_b_ok = true;
    bool model_a_ok;
    bool model_b_ok = true;
    bool image_ok;
    bool dict_ok;
    bool executable_ok;
    bool ready;
    bool live_ready;

    if (g_cloud_selected_task < 0 ||
        g_cloud_selected_task >= CLOUD_TASK_COUNT)
        g_cloud_selected_task = CLOUD_TASK_CLASSIFICATION;
    task = &g_cloud_tasks[g_cloud_selected_task];
    config_a = task->dual_config
                   ? "/sdcard/ocrdet_deploy_config.json"
                   : "/sdcard/deploy_config.json";
    if (task->dual_config)
        config_b = "/sdcard/ocrrec_deploy_config.json";

    config_a_ok = access(config_a, R_OK) == 0;
    config_b_ok = config_b == NULL || access(config_b, R_OK) == 0;
    model_a_ok = config_a_ok &&
                 cloud_resolve_kmodel(config_a, model_a,
                                      sizeof(model_a)) == 0;
    if (config_b != NULL)
        model_b_ok = config_b_ok &&
                     cloud_resolve_kmodel(config_b, model_b,
                                          sizeof(model_b)) == 0;
    image_ok = access("/sdcard/test.jpg", R_OK) == 0;
    dict_ok = !task->needs_dictionary ||
              access("/sdcard/dict.txt", R_OK) == 0;
    snprintf(executable, sizeof(executable),
             "/sdcard/app/cloudplat/%s", task->executable);
    executable_ok = access(executable, X_OK) == 0;
    ready = config_a_ok && config_b_ok && model_a_ok && model_b_ok &&
            image_ok && dict_ok && executable_ok;
    live_ready = config_a_ok && config_b_ok && model_a_ok && model_b_ok &&
                 dict_ok && executable_ok && task->supports_video;

    if (!config_a_ok || !config_b_ok || !model_a_ok || !model_b_ok ||
        !dict_ok || !executable_ok) {
        size_t used = (size_t)snprintf(status, sizeof(status), "%s\n%s",
                                       cloud_text("Setup needed"),
                                       cloud_text("Missing: "));
        bool first_missing = true;
#define CLOUD_APPEND_MISSING(condition, text)                              \
        do {                                                              \
            if ((condition) && used < sizeof(status))                     \
                used += (size_t)snprintf(status + used,                   \
                                         sizeof(status) - used,           \
                                         "%s%s", first_missing ? "" : ", ", \
                                         (text));                         \
            if (condition) first_missing = false;                         \
        } while (0)
        CLOUD_APPEND_MISSING(!config_a_ok,
                             task->dual_config
                                 ? "ocrdet_deploy_config.json"
                                 : "deploy_config.json");
        CLOUD_APPEND_MISSING(!config_b_ok, "ocrrec_deploy_config.json");
        CLOUD_APPEND_MISSING(config_a_ok && !model_a_ok,
                             task->dual_config ? cloud_text("detection kmodel")
                                               : "model.kmodel");
        CLOUD_APPEND_MISSING(config_b_ok && !model_b_ok,
                             cloud_text("recognition kmodel"));
        CLOUD_APPEND_MISSING(!dict_ok, "dict.txt");
        CLOUD_APPEND_MISSING(!executable_ok, cloud_text("runtime"));
#undef CLOUD_APPEND_MISSING
    } else if (!image_ok && task->supports_video) {
        snprintf(status, sizeof(status), "%s", cloud_text(
                     "Camera ready\nAdd test.jpg to enable image inference."));
    } else if (!image_ok) {
        snprintf(status, sizeof(status), "%s",
                 cloud_text("Setup needed\nMissing: test.jpg"));
    } else if (!task->supports_video) {
        snprintf(status, sizeof(status), "%s", cloud_text(
                     "Image ready\nLive camera is unavailable for this task."));
    } else {
        snprintf(status, sizeof(status), "%s", cloud_text(
                     "Ready\nChoose an input mode to start inference."));
    }
    if (g_cloud_file_status != NULL)
        lv_label_set_text(g_cloud_file_status, status);
    if (g_cloud_run_button != NULL) {
        if (ready && !g_cloud_background_running)
            lv_obj_remove_state(g_cloud_run_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(g_cloud_run_button, LV_STATE_DISABLED);
    }
    if (g_cloud_live_button != NULL) {
        if (live_ready && !g_cloud_background_running)
            lv_obj_remove_state(g_cloud_live_button, LV_STATE_DISABLED);
        else
            lv_obj_add_state(g_cloud_live_button, LV_STATE_DISABLED);
    }
    return ready;
}

static void cloud_task_changed_cb(lv_event_t *event)
{
    g_cloud_selected_task =
        (int)lv_dropdown_get_selected(lv_event_get_target(event));
    if (g_cloud_result_status != NULL)
        lv_label_set_text(g_cloud_result_status,
                          cloud_text("Select Refresh after copying new files."));
    cloud_update_file_status();
}

static void cloud_refresh_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    bool ready = cloud_update_file_status();
    show_toast(ready ? cloud_text("Cloud model files are ready")
                     : cloud_text("Some required files are missing"));
}

static int cloud_write_request(const char *path, int live_mode)
{
    char temporary[128];
    FILE *request;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    request = fopen(temporary, "w");
    if (request == NULL) {
        return -1;
    }
    fprintf(request, "%d %d\n", g_cloud_selected_task, live_mode);
    int request_failed = fflush(request) != 0;
    if (fclose(request) != 0) request_failed = 1;
    if (request_failed) {
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static void cloud_run_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    if (!cloud_update_file_status()) {
        show_toast(cloud_text("Copy all required files to CanMV/sdcard"));
        return;
    }
    if (g_cloud_background_running) return;
    printf("[cloud-result] image inference requested, task=%d (%s)\n",
           g_cloud_selected_task,
           g_cloud_tasks[g_cloud_selected_task].name);
    unlink(CLOUD_MODEL_RESULT_PATH);
    lv_label_set_text(g_cloud_result_status,
                      cloud_text("Loading model and running test.jpg..."));
    show_toast(cloud_text("Starting image inference..."));
    lv_refr_now(NULL);
    if (cloud_write_request(CLOUD_MODEL_REQUEST_PATH, 0) != 0) {
        lv_label_set_text(g_cloud_result_status,
                          cloud_text("Unable to start image inference."));
        show_toast(cloud_text("Unable to start image inference"));
        cloud_update_file_status();
        return;
    }
    g_cloud_background_running = true;
    cloud_update_file_status();
    cloud_show_progress_dialog();
    printf("[cloud-result] handing image inference to session supervisor\n");
    g_launch_cloud_model = true;
    g_stop = 1;
}

static void cloud_live_cb(lv_event_t *event)
{
    const cloud_task_info_t *task = &g_cloud_tasks[g_cloud_selected_task];
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    cloud_update_file_status();
    if (!task->supports_video) {
        show_toast(cloud_text("This task only supports test.jpg"));
        return;
    }
    if (cloud_write_request(CLOUD_MODEL_REQUEST_PATH, 1) != 0) {
        show_toast(cloud_text("Unable to save cloud model request"));
        return;
    }
    unlink(CLOUD_MODEL_RESULT_PATH);
    lv_label_set_text(g_cloud_result_status,
                      cloud_text("Starting live camera inference...\n"
                                 "Press q on serial to return."));
    show_toast(cloud_text("Live inference: press q on serial to exit"));
    lv_refr_now(NULL);
    g_launch_cloud_model = true;
    g_stop = 1;
}

static void cloud_close_dropdown(void)
{
    if (g_cloud_task_dropdown != NULL &&
        lv_dropdown_is_open(g_cloud_task_dropdown)) {
        lv_dropdown_close(g_cloud_task_dropdown);
    }
}

static void cloud_back_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click())
        return;
    /* LVGL attaches an opened dropdown list to the screen rather than to
     * g_cloud_view. Close it before hiding the view so it cannot remain over
     * the desktop. */
    cloud_close_dropdown();
    lv_obj_add_flag(g_cloud_view, LV_OBJ_FLAG_HIDDEN);
}

static void cloud_localize_label(lv_obj_t *label, const char *english,
                                 const lv_font_t *english_font)
{
    if (label == NULL)
        return;
    lv_label_set_text(label, cloud_text(english));
    lv_obj_set_style_text_font(label, cloud_ui_font(english_font), 0);
}

static void cloud_refresh_language(void)
{
    uint32_t selected;

    cloud_localize_label(g_cloud_title_label, "Cloud Model",
                         &lv_font_montserrat_20);
    cloud_localize_label(g_cloud_subtitle_label,
                         "Select a task, then choose an input",
                         &lv_font_montserrat_12);
    cloud_localize_label(g_cloud_task_title_label, "1  Model task",
                         &lv_font_montserrat_16);
    cloud_localize_label(g_cloud_copy_help_label,
                         "Files are detected automatically from /sdcard",
                         &lv_font_montserrat_12);
    cloud_localize_label(g_cloud_input_title_label, "2  Choose input",
                         &lv_font_montserrat_16);
    cloud_localize_label(g_cloud_run_button_label, "Image inference",
                         &lv_font_montserrat_16);
    cloud_localize_label(g_cloud_live_button_label, "Live camera",
                         &lv_font_montserrat_16);
    if (g_cloud_task_dropdown != NULL) {
        selected = lv_dropdown_get_selected(g_cloud_task_dropdown);
        cloud_close_dropdown();
        lv_dropdown_set_options(g_cloud_task_dropdown,
                                cloud_task_options_text());
        lv_dropdown_set_selected(g_cloud_task_dropdown, selected);
        lv_obj_set_style_text_font(g_cloud_task_dropdown,
                                   cloud_ui_font(&lv_font_montserrat_14), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(
                                       g_cloud_task_dropdown),
                                   cloud_ui_font(&lv_font_montserrat_14), 0);
    }
    if (g_cloud_file_status != NULL)
        lv_obj_set_style_text_font(g_cloud_file_status,
                                   cloud_ui_font(&lv_font_montserrat_14), 0);
    if (g_cloud_result_status != NULL)
        lv_obj_set_style_text_font(g_cloud_result_status,
                                   cloud_ui_font(&lv_font_montserrat_12), 0);
}

static void create_cloud_model_view(lv_obj_t *screen)
{
    g_cloud_view = lv_obj_create(screen);
    lv_obj_set_size(g_cloud_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_cloud_view, 0, 0);
    lv_obj_set_style_bg_color(g_cloud_view, lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_border_width(g_cloud_view, 0, 0);
    lv_obj_set_style_pad_all(g_cloud_view, 0, 0);
    lv_obj_remove_flag(g_cloud_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(g_cloud_view);
    lv_obj_set_size(header, UI_WIDTH, 68);
    lv_obj_set_pos(header, 0, 0);
    style_plain(header);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *back = create_round_button(header, 48, 0xEEF2F6);
    lv_obj_set_pos(back, 10, 10);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, cloud_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, 0x263238);
    lv_obj_center(back_icon);

    g_cloud_title_label = make_label(header, "Cloud Model",
                                     &lv_font_montserrat_20, 0x172033);
    lv_obj_set_pos(g_cloud_title_label, 74, 10);
    g_cloud_subtitle_label = make_label(
        header, "Select a task, then choose an input",
        &lv_font_montserrat_12, 0x667085);
    lv_obj_set_pos(g_cloud_subtitle_label, 75, 37);

    lv_obj_t *task_card = lv_obj_create(g_cloud_view);
    lv_obj_set_size(task_card, 600, 384);
    lv_obj_set_pos(task_card, 20, 80);
    lv_obj_set_style_bg_color(task_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(task_card, 20, 0);
    lv_obj_set_style_border_width(task_card, 0, 0);
    lv_obj_set_style_pad_all(task_card, 20, 0);
    lv_obj_remove_flag(task_card, LV_OBJ_FLAG_SCROLLABLE);

    g_cloud_task_title_label = make_label(task_card, "1  Model task",
                                          &lv_font_montserrat_16, 0x172033);
    lv_obj_set_pos(g_cloud_task_title_label, 0, 0);
    lv_obj_t *refresh = create_round_button(task_card, 40, 0xF0ECF8);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, 0, -8);
    lv_obj_set_ext_click_area(refresh, 12);
    lv_obj_add_event_cb(refresh, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(refresh, cloud_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_icon = make_label(
        refresh, LV_SYMBOL_REFRESH, &lv_font_montserrat_18, 0x6750A4);
    lv_obj_center(refresh_icon);

    g_cloud_task_dropdown = lv_dropdown_create(task_card);
    lv_obj_set_size(g_cloud_task_dropdown, 560, 50);
    lv_obj_set_pos(g_cloud_task_dropdown, 0, 30);
    lv_dropdown_set_options(g_cloud_task_dropdown,
                            cloud_task_options_text());
    lv_dropdown_set_selected(g_cloud_task_dropdown,
                             (uint32_t)g_cloud_selected_task);
    lv_obj_set_style_text_font(g_cloud_task_dropdown,
                               &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(g_cloud_task_dropdown, cloud_task_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    g_cloud_copy_help_label = make_label(
        task_card, "Files are detected automatically from /sdcard",
        &lv_font_montserrat_12, 0x667085);
    lv_obj_set_pos(g_cloud_copy_help_label, 2, 88);

    lv_obj_t *status_card = lv_obj_create(task_card);
    lv_obj_set_size(status_card, 560, 64);
    lv_obj_set_pos(status_card, 0, 112);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0xF4F1FA), 0);
    lv_obj_set_style_radius(status_card, 14, 0);
    lv_obj_set_style_border_width(status_card, 0, 0);
    lv_obj_set_style_pad_all(status_card, 12, 0);
    lv_obj_remove_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    g_cloud_file_status = make_label(status_card, cloud_text("Scanning..."),
                                     cloud_ui_font(&lv_font_montserrat_14),
                                     0x3B3155);
    lv_obj_set_pos(g_cloud_file_status, 0, 0);
    lv_obj_set_width(g_cloud_file_status, 536);
    lv_label_set_long_mode(g_cloud_file_status, LV_LABEL_LONG_WRAP);

    g_cloud_input_title_label = make_label(task_card, "2  Choose input",
                                           &lv_font_montserrat_16, 0x172033);
    lv_obj_set_pos(g_cloud_input_title_label, 0, 194);

    g_cloud_run_button = lv_button_create(task_card);
    lv_obj_set_size(g_cloud_run_button, 270, 70);
    lv_obj_set_pos(g_cloud_run_button, 0, 220);
    lv_obj_set_style_radius(g_cloud_run_button, 18, 0);
    lv_obj_set_style_bg_color(g_cloud_run_button,
                              lv_color_hex(0x6750A4), 0);
    lv_obj_set_style_shadow_width(g_cloud_run_button, 0, 0);
    lv_obj_add_event_cb(g_cloud_run_button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_cloud_run_button, cloud_run_cb,
                        LV_EVENT_CLICKED, NULL);
    g_cloud_run_button_label = make_label(g_cloud_run_button,
                                          "Image inference",
                                          &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(g_cloud_run_button_label);

    g_cloud_live_button = lv_button_create(task_card);
    lv_obj_set_size(g_cloud_live_button, 270, 70);
    lv_obj_set_pos(g_cloud_live_button, 290, 220);
    lv_obj_set_style_radius(g_cloud_live_button, 18, 0);
    lv_obj_set_style_bg_color(g_cloud_live_button,
                              lv_color_hex(0x006C4C), 0);
    lv_obj_set_style_shadow_width(g_cloud_live_button, 0, 0);
    lv_obj_add_event_cb(g_cloud_live_button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_cloud_live_button, cloud_live_cb,
                        LV_EVENT_CLICKED, NULL);
    g_cloud_live_button_label = make_label(g_cloud_live_button, "Live camera",
                                           &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(g_cloud_live_button_label);

    g_cloud_result_status = make_label(
        task_card, cloud_text("No inference result yet."),
        cloud_ui_font(&lv_font_montserrat_12), 0x52606D);
    lv_obj_set_pos(g_cloud_result_status, 2, 306);
    lv_obj_set_width(g_cloud_result_status, 556);
    lv_label_set_long_mode(g_cloud_result_status, LV_LABEL_LONG_WRAP);

    cloud_refresh_language();
    cloud_update_file_status();
    lv_obj_add_flag(g_cloud_view, LV_OBJ_FLAG_HIDDEN);
}

static void show_cloud_model(void)
{
    cloud_close_dropdown();
    cloud_refresh_language();
    cloud_update_file_status();
    lv_obj_remove_flag(g_cloud_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_cloud_view);
}

static void cloud_show_progress_dialog(void)
{
    if (g_cloud_progress_dialog != NULL) return;
    lv_obj_t *dialog = lv_obj_create(lv_screen_active());
    g_cloud_progress_dialog = dialog;
    lv_obj_set_size(dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_40, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 0, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(dialog);
    lv_obj_set_size(card, 340, 180);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spinner = lv_spinner_create(card);
    lv_obj_set_size(spinner, 54, 54);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xD8E2FF),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x315DA8),
                               LV_PART_INDICATOR);
    lv_obj_t *title = make_label(card, cloud_text("Running inference..."),
                                 cloud_ui_font(&lv_font_montserrat_18),
                                 0x172033);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_t *hint = make_label(card, cloud_text("Please wait"),
                                cloud_ui_font(&lv_font_montserrat_12),
                                0x52606D);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_move_foreground(dialog);
    lv_refr_now(NULL);
    printf("[cloud-result] progress dialog displayed\n");
}

static void cloud_close_progress_dialog(void)
{
    if (g_cloud_progress_dialog == NULL) return;
    lv_obj_delete(g_cloud_progress_dialog);
    g_cloud_progress_dialog = NULL;
    printf("[cloud-result] progress dialog closed\n");
}

static void cloud_result_dialog_close_cb(lv_event_t *event)
{
    lv_obj_t *dialog = lv_event_get_user_data(event);
    if (!lv_k230_touch_accept_click()) return;
    printf("[cloud-result] result dialog close pressed, dialog=%p\n",
           (void *)dialog);
    if (dialog != NULL) lv_obj_delete(dialog);
}

static void cloud_show_result_dialog(const cloud_task_info_t *task,
                                     const char *output)
{
    printf("[cloud-result] creating dialog: task=%s output=%s screen=%p\n",
           task->name, output, (void *)lv_screen_active());
    lv_obj_t *dialog = lv_obj_create(lv_screen_active());
    printf("[cloud-result] dialog object=%p\n", (void *)dialog);
    lv_obj_set_size(dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_60, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 0, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(dialog);
    lv_obj_set_size(card, 600, 444);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    unsigned task_index = (unsigned)(task - g_cloud_tasks);
    lv_obj_t *title = make_label(card, cloud_task_name(task_index),
                                 cloud_ui_font(&lv_font_montserrat_20),
                                 0x172033);
    lv_obj_set_pos(title, 4, 0);
    lv_obj_t *subtitle = make_label(card, cloud_text("Inference completed"),
                                    cloud_ui_font(&lv_font_montserrat_12),
                                    0x52606D);
    lv_obj_set_pos(subtitle, 4, 28);

    lv_obj_t *close = create_round_button(card, 44, 0xE8EEF8);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -6);
    lv_obj_add_event_cb(close, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(close, cloud_result_dialog_close_cb,
                        LV_EVENT_CLICKED, dialog);
    lv_obj_t *close_icon = make_label(close, LV_SYMBOL_CLOSE,
                                      &lv_font_montserrat_18, 0x263238);
    lv_obj_center(close_icon);

    size_t output_length = strlen(output);
    bool is_jpeg = output_length >= 4 &&
        (strcasecmp(output + output_length - 4, ".jpg") == 0 ||
         (output_length >= 5 &&
          strcasecmp(output + output_length - 5, ".jpeg") == 0));
    printf("[cloud-result] output type: jpeg=%d length=%u\n",
           is_jpeg ? 1 : 0, (unsigned int)output_length);
    if (is_jpeg) {
        char saved_message[512];
        snprintf(saved_message, sizeof(saved_message),
                 cloud_text("Inference completed successfully.\n\n"
                            "Result image saved to:\n%s"), output);
        lv_obj_t *path = make_label(card, saved_message,
                                    cloud_ui_font(&lv_font_montserrat_18),
                                    0x334155);
        lv_obj_align(path, LV_ALIGN_CENTER, 0, 22);
        lv_obj_set_width(path, 520);
        lv_obj_set_style_text_align(path, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(path, LV_LABEL_LONG_WRAP);
        printf("[cloud-result] image preview disabled; showing saved path\n");
    } else {
        lv_obj_t *path = make_label(card, output,
                                    cloud_ui_font(&lv_font_montserrat_16),
                                    0x334155);
        lv_obj_align(path, LV_ALIGN_CENTER, 0, 12);
        lv_obj_set_width(path, 540);
        lv_label_set_long_mode(path, LV_LABEL_LONG_WRAP);
    }
    lv_obj_move_foreground(dialog);
    lv_refr_now(NULL);
    printf("[cloud-result] dialog moved to foreground and display refreshed\n");
}

static void show_pending_cloud_result(void)
{
    FILE *result = fopen(CLOUD_MODEL_RESULT_PATH, "r");
    int task = -1;
    int status = -1;
    char message[384];
    char output[384];

    if (result == NULL) {
        printf("[cloud-result] result marker not found: %s (%s)\n",
               CLOUD_MODEL_RESULT_PATH, strerror(errno));
        return;
    }
    printf("[cloud-result] result marker opened: %s\n",
           CLOUD_MODEL_RESULT_PATH);
    if (fscanf(result, "%d %d", &task, &status) != 2 || task < 0 ||
        task >= CLOUD_TASK_COUNT) {
        printf("[cloud-result] invalid marker contents: task=%d status=%d\n",
               task, status);
        fclose(result);
        unlink(CLOUD_MODEL_RESULT_PATH);
        return;
    }
    fclose(result);
    unlink(CLOUD_MODEL_RESULT_PATH);
    g_cloud_selected_task = task;
    lv_dropdown_set_selected(g_cloud_task_dropdown, (uint32_t)task);
    cloud_update_file_status();
    snprintf(output, sizeof(output), "/sdcard/%s",
             g_cloud_tasks[task].result_file);
    int output_access = access(output, R_OK);
    printf("[cloud-result] marker parsed: task=%d (%s) status=%d\n",
           task, g_cloud_tasks[task].name, status);
    printf("[cloud-result] output check: path=%s readable=%d errno=%d\n",
           output, output_access == 0 ? 1 : 0,
           output_access == 0 ? 0 : errno);

    /* Restore the Cloud Model view before creating the result dialog.
     * Otherwise show_cloud_model() would move the main view above the dialog
     * and make a successfully-created result popup appear to be missing. */
    show_cloud_model();
    if (status == 0 && output_access == 0) {
        snprintf(message, sizeof(message),
                 cloud_text("Completed successfully.\n%.300s"), output);
        show_toast(cloud_text("Cloud model test completed"));
        cloud_show_result_dialog(&g_cloud_tasks[task], output);
    } else if (status == 0) {
        snprintf(message, sizeof(message), cloud_text(
                     "Inference completed, but the result file is missing.\n"
                     "%.300s"), output);
        show_toast(cloud_text("Cloud model result is missing"));
    } else {
        snprintf(message, sizeof(message), cloud_text(
                     "Inference failed with status %d. Check the serial log."),
                 status);
        show_toast(cloud_text("Cloud model test failed"));
    }
    lv_label_set_text(g_cloud_result_status, message);
}

static void close_camera_cb(lv_event_t *event)
{
    (void)event;
#if UI_TOUCH_DEBUG
    lv_point_t point = {0, 0};
    lv_indev_t *active = lv_indev_active();
    if (active != NULL) {
        lv_indev_get_point(active, &point);
    }
    printf("[ui-touch] camera back PRESSED point=(%d,%d)\n",
           (int)point.x, (int)point.y);
#endif
    if (g_camera_init_running) {
        g_camera_init_cancelled = 1;
        lv_screen_load(g_home_screen);
        show_toast("Camera startup cancelled");
        return;
    }
    if (g_camera_init_timer != NULL) {
        lv_timer_delete(g_camera_init_timer);
        g_camera_init_timer = NULL;
    }
    if (g_camera_record_timer != NULL) {
        lv_timer_delete(g_camera_record_timer);
        g_camera_record_timer = NULL;
    }
    lv_obj_add_flag(g_camera_record_badge, LV_OBJ_FLAG_HIDDEN);
    dshanpi_camera_stop();
    lv_screen_load(g_home_screen);
    lv_indev_t *indev = lv_indev_active();
    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }
#if UI_TOUCH_DEBUG
    printf("[ui-touch] camera home screen loaded\n");
#endif
}

static const char *settings_camera_name(int csi)
{
    return settings_text(csi == DSHANPI_CAMERA_FRONT_CSI ? "Front" : "Rear");
}

static void settings_camera_refresh_selection(int selected_csi)
{
    bool front_selected = selected_csi == DSHANPI_CAMERA_FRONT_CSI;

    if (g_settings_camera_rear == NULL ||
        g_settings_camera_front == NULL ||
        g_settings_camera_rear_label == NULL ||
        g_settings_camera_front_label == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(
        g_settings_camera_rear,
        lv_color_hex(front_selected ? 0xFFFFFF : 0x2F7D3A), 0);
    lv_obj_set_style_bg_color(
        g_settings_camera_rear,
        lv_color_hex(front_selected ? 0xD8EED5 : 0x245F2D),
        LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_settings_camera_rear,
                                  front_selected ? 1 : 0, 0);
    lv_obj_set_style_text_color(
        g_settings_camera_rear_label,
        lv_color_hex(front_selected ? 0x315A37 : 0xFFFFFF), 0);

    lv_obj_set_style_bg_color(
        g_settings_camera_front,
        lv_color_hex(front_selected ? 0x2F7D3A : 0xFFFFFF), 0);
    lv_obj_set_style_bg_color(
        g_settings_camera_front,
        lv_color_hex(front_selected ? 0x245F2D : 0xD8EED5),
        LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_settings_camera_front,
                                  front_selected ? 0 : 1, 0);
    lv_obj_set_style_text_color(
        g_settings_camera_front_label,
        lv_color_hex(front_selected ? 0xFFFFFF : 0x315A37), 0);
}

static void settings_reboot_update_countdown(void)
{
    char text[192];
    const char *message =
        g_settings_reboot_reason == SETTINGS_REBOOT_LANGUAGE
            ? "Restarting in %d seconds to apply the language change."
            : "Restarting in %d seconds to apply the camera change.";

    snprintf(text, sizeof(text), settings_text(message),
             g_settings_reboot_seconds);
    lv_label_set_text(g_settings_reboot_countdown, text);
}

static const char *settings_reboot_saved_message(void)
{
    return g_settings_reboot_reason == SETTINGS_REBOOT_LANGUAGE
               ? "Language change saved. Restart later to apply."
               : "Camera change saved. Restart later to apply.";
}

static void settings_reboot_execute(void)
{
    int result;

    if (g_settings_reboot_timer != NULL) {
        lv_timer_delete(g_settings_reboot_timer);
        g_settings_reboot_timer = NULL;
    }
    if (ota_update_is_active()) {
        lv_obj_add_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);
        show_toast(settings_text(settings_reboot_saved_message()));
        return;
    }
    lv_label_set_text(g_settings_reboot_countdown,
                      settings_text("Restarting..."));
    lv_refr_now(NULL);
    result = dshanpi_power_reboot();
    if (result != 0) {
        lv_obj_add_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);
        show_toast(settings_text("Restart failed"));
    }
}

static void settings_reboot_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_settings_reboot_seconds <= 1) {
        settings_reboot_execute();
        return;
    }
    --g_settings_reboot_seconds;
    settings_reboot_update_countdown();
}

static void settings_reboot_now_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    settings_reboot_execute();
}

static void settings_reboot_later_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    if (g_settings_reboot_timer != NULL) {
        lv_timer_delete(g_settings_reboot_timer);
        g_settings_reboot_timer = NULL;
    }
    lv_obj_add_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);
    show_toast(settings_text(settings_reboot_saved_message()));
}

static void settings_reboot_dialog_open(settings_reboot_reason_t reason)
{
    if (g_settings_reboot_timer != NULL) {
        lv_timer_delete(g_settings_reboot_timer);
        g_settings_reboot_timer = NULL;
    }
    g_settings_reboot_reason = reason;
    if (ota_update_is_active()) {
        lv_obj_add_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);
        show_toast(settings_text(settings_reboot_saved_message()));
        return;
    }
    g_settings_reboot_seconds = 10;
    settings_reboot_update_countdown();
    lv_obj_remove_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_settings_reboot_dialog);
    g_settings_reboot_timer =
        lv_timer_create(settings_reboot_timer_cb, 1000, NULL);
}

static void camera_setting_select_cb(lv_event_t *event)
{
    int csi = (int)(intptr_t)lv_event_get_user_data(event);
    char text[128];

    if (!lv_k230_touch_accept_click()) return;

    if (dshanpi_camera_setting_save(csi) != 0) {
        lv_label_set_text(g_settings_status,
                          settings_text("Failed to save camera setting"));
        show_toast(settings_text("Camera setting save failed"));
        return;
    }
    settings_camera_refresh_selection(csi);
    snprintf(text, sizeof(text), settings_text("%s camera selected"),
             settings_camera_name(csi));
    lv_label_set_text(g_settings_status, text);
    settings_refresh_nav_values();
    if (csi == g_system_camera_csi) {
        show_toast(settings_text("Camera is already active"));
        return;
    }
    settings_reboot_dialog_open(SETTINGS_REBOOT_CAMERA);
}

static void paint_put_pixel(int x, int y, lv_color_t color)
{
    if (x < 0 || y < 0 || x >= PAINT_CANVAS_WIDTH ||
        y >= PAINT_CANVAS_HEIGHT) {
        return;
    }
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(g_paint_canvas);
    uint8_t *row = (uint8_t *)g_paint_buffer +
                   (size_t)y * draw_buf->header.stride;
    lv_color16_t *pixel = (lv_color16_t *)(row + (size_t)x * 2);
    pixel->red = color.red >> 3;
    pixel->green = color.green >> 2;
    pixel->blue = color.blue >> 3;
}

static void paint_stamp(int x, int y)
{
    int radius_squared = g_paint_radius * g_paint_radius;
    for (int dy = -g_paint_radius; dy <= g_paint_radius; ++dy) {
        for (int dx = -g_paint_radius; dx <= g_paint_radius; ++dx) {
            if (dx * dx + dy * dy <= radius_squared) {
                paint_put_pixel(x + dx, y + dy, g_paint_color);
            }
        }
    }
}

static void paint_line(int x0, int y0, int x1, int y1)
{
    int dirty_x1 = x0 < x1 ? x0 : x1;
    int dirty_y1 = y0 < y1 ? y0 : y1;
    int dirty_x2 = x0 > x1 ? x0 : x1;
    int dirty_y2 = y0 > y1 ? y0 : y1;
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        paint_stamp(x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    /* Redrawing the complete 600x350 canvas for every MOVE makes the brush
     * visibly trail the finger. Flush and invalidate only the pixels touched
     * by this segment. The active draw-buffer handler decides whether a cache
     * clean is required; the software renderer safely treats it as a no-op. */
    lv_area_t local_dirty = {
        .x1 = dirty_x1 - g_paint_radius - 1,
        .y1 = dirty_y1 - g_paint_radius - 1,
        .x2 = dirty_x2 + g_paint_radius + 1,
        .y2 = dirty_y2 + g_paint_radius + 1,
    };
    if (local_dirty.x1 < 0) local_dirty.x1 = 0;
    if (local_dirty.y1 < 0) local_dirty.y1 = 0;
    if (local_dirty.x2 >= PAINT_CANVAS_WIDTH)
        local_dirty.x2 = PAINT_CANVAS_WIDTH - 1;
    if (local_dirty.y2 >= PAINT_CANVAS_HEIGHT)
        local_dirty.y2 = PAINT_CANVAS_HEIGHT - 1;
    lv_draw_buf_flush_cache(g_paint_draw_buf, &local_dirty);

    lv_area_t canvas_coords;
    lv_obj_get_coords(g_paint_canvas, &canvas_coords);
    lv_area_t dirty = {
        .x1 = canvas_coords.x1 + local_dirty.x1,
        .y1 = canvas_coords.y1 + local_dirty.y1,
        .x2 = canvas_coords.x1 + local_dirty.x2,
        .y2 = canvas_coords.y1 + local_dirty.y2,
    };
    lv_obj_invalidate_area(g_paint_canvas, &dirty);
}

static void paint_canvas_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        g_paint_release_tick = lv_tick_get();
        g_paint_recently_released =
            g_paint_last_x >= 0 && g_paint_last_y >= 0;
#if PAINT_TOUCH_DEBUG
        printf("[paint-touch] %s last=(%d,%d) dt=%u ms samples=%u "
               "pending=%d\n",
               code == LV_EVENT_RELEASED ? "RELEASED" : "PRESS_LOST",
               g_paint_last_x, g_paint_last_y,
               (unsigned)lv_tick_elaps(g_paint_last_tick),
               g_paint_move_count, g_paint_recently_released);
#endif
        g_paint_move_count = 0;
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    point.x -= lv_obj_get_x(g_paint_canvas);
    point.y -= lv_obj_get_y(g_paint_canvas);
    if (point.x < 0 || point.y < 0 ||
        point.x >= PAINT_CANVAS_WIDTH ||
        point.y >= PAINT_CANVAS_HEIGHT) {
#if PAINT_TOUCH_DEBUG
        printf("[paint-touch] OUTSIDE event=%d point=(%d,%d)\n",
               (int)code, point.x, point.y);
#endif
        return;
    }
    uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        unsigned reconnect_elapsed =
            (unsigned)lv_tick_elaps(g_paint_release_tick);
        int reconnect_dx = point.x - g_paint_last_x;
        int reconnect_dy = point.y - g_paint_last_y;
        unsigned reconnect_distance_squared =
            g_paint_last_x >= 0 && g_paint_last_y >= 0
                ? (unsigned)(reconnect_dx * reconnect_dx +
                             reconnect_dy * reconnect_dy)
                : UINT32_MAX;
        unsigned reconnect_limit_squared =
            PAINT_RECONNECT_DISTANCE * PAINT_RECONNECT_DISTANCE;
        int reconnect =
            g_paint_recently_released &&
            reconnect_elapsed <= PAINT_RECONNECT_MS &&
            reconnect_distance_squared <= reconnect_limit_squared;
#if PAINT_TOUCH_DEBUG
        printf("[paint-touch] PRESSED point=(%d,%d) gap=%u ms d2=%u "
               "%s\n",
               point.x, point.y, reconnect_elapsed,
               reconnect_distance_squared,
               reconnect ? "RECONNECT" : "NEW_STROKE");
#endif
        if (reconnect) {
            paint_line(g_paint_last_x, g_paint_last_y, point.x, point.y);
        }
        g_paint_last_x = point.x;
        g_paint_last_y = point.y;
        g_paint_recently_released = 0;
        g_paint_last_tick = now;
        g_paint_move_count = 0;
    }
    else if (g_paint_last_x < 0) {
        g_paint_last_x = point.x;
        g_paint_last_y = point.y;
        g_paint_last_tick = now;
    }
#if PAINT_TOUCH_DEBUG
    int delta_x = point.x - g_paint_last_x;
    int delta_y = point.y - g_paint_last_y;
    unsigned elapsed = (unsigned)lv_tick_elaps(g_paint_last_tick);
    unsigned distance_squared =
        (unsigned)(delta_x * delta_x + delta_y * delta_y);
    ++g_paint_move_count;
    /*
     * Print every eighth sample, plus all suspicious gaps.  Printing every
     * PRESSING event can block the UI thread and manufacture new gaps.
     */
    if ((g_paint_move_count & 7U) == 0U || elapsed > 50U ||
        distance_squared > 1600U) {
        printf("[paint-touch] MOVE point=(%d,%d) delta=(%d,%d) "
               "dt=%u ms d2=%u sample=%u%s\n",
               point.x, point.y, delta_x, delta_y, elapsed,
               distance_squared, g_paint_move_count,
               (elapsed > 50U || distance_squared > 1600U)
                   ? " GAP?" : "");
    }
#endif
    paint_line(g_paint_last_x, g_paint_last_y, point.x, point.y);
    g_paint_last_x = point.x;
    g_paint_last_y = point.y;
    g_paint_last_tick = now;
}

static void paint_color_cb(lv_event_t *event)
{
    uint32_t color = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    g_paint_color = lv_color_hex(color);
}

static void paint_size_cb(lv_event_t *event)
{
    g_paint_radius = (int)(intptr_t)lv_event_get_user_data(event);
}

static void paint_clear_cb(lv_event_t *event)
{
    (void)event;
    lv_canvas_fill_bg(g_paint_canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
}

static lv_obj_t *paint_tool_button(lv_obj_t *parent, const char *text,
                                   int x, int width)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_pos(button, x, 18);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label =
        make_label(button, text, &lv_font_montserrat_14, 0x1F2937);
    lv_obj_center(label);
    return button;
}

static void create_paint_view(lv_obj_t *screen)
{
    g_paint_view = lv_obj_create(screen);
    lv_obj_set_size(g_paint_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_paint_view);
    lv_obj_set_style_bg_color(g_paint_view, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_border_width(g_paint_view, 0, 0);
    lv_obj_set_style_pad_all(g_paint_view, 0, 0);
    lv_obj_remove_flag(g_paint_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = paint_tool_button(g_paint_view, LV_SYMBOL_LEFT, 12, 60);
    lv_obj_set_height(back, 56);
    lv_obj_set_y(back, 12);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, close_fullscreen_cb, LV_EVENT_PRESSED,
                        g_paint_view);
    lv_obj_t *clear = paint_tool_button(g_paint_view, "Clear", 78, 54);
    lv_obj_add_event_cb(clear, paint_clear_cb, LV_EVENT_CLICKED, NULL);

    static const uint32_t colors[] = {
        0x111827, 0xEF4444, 0xF59E0B, 0x22C55E, 0x3B82F6, 0x8B5CF6
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        lv_obj_t *color = lv_button_create(g_paint_view);
        lv_obj_set_size(color, 36, 36);
        lv_obj_set_pos(color, 142 + (int)i * 43, 22);
        lv_obj_set_style_radius(color, 18, 0);
        lv_obj_set_style_bg_color(color, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_shadow_width(color, 0, 0);
        lv_obj_add_event_cb(color, paint_color_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)colors[i]);
    }

    lv_obj_t *thin = paint_tool_button(g_paint_view, "S", 410, 42);
    lv_obj_add_event_cb(thin, paint_size_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)2);
    lv_obj_t *medium = paint_tool_button(g_paint_view, "M", 458, 42);
    lv_obj_add_event_cb(medium, paint_size_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)5);
    lv_obj_t *large = paint_tool_button(g_paint_view, "L", 506, 42);
    lv_obj_add_event_cb(large, paint_size_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)9);
    lv_obj_t *eraser = paint_tool_button(g_paint_view, "Eraser", 554, 74);
    lv_obj_add_event_cb(eraser, paint_color_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)0xFFFFFF);

    g_paint_canvas = lv_canvas_create(g_paint_view);
    /* Allocate with the active LVGL draw-buffer handlers so allocation size,
     * stride, alignment and destruction always remain a matched set. */
    g_paint_draw_buf =
        lv_draw_buf_create(PAINT_CANVAS_WIDTH, PAINT_CANVAS_HEIGHT,
                           LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (g_paint_draw_buf == NULL) {
        lv_obj_t *error =
            make_label(g_paint_view, "Unable to allocate drawing canvas",
                       &lv_font_montserrat_20, 0xB91C1C);
        lv_obj_center(error);
    } else {
        g_paint_buffer = g_paint_draw_buf->data;
        lv_canvas_set_draw_buf(g_paint_canvas, g_paint_draw_buf);
        lv_obj_set_pos(g_paint_canvas, 20, 78);
        lv_canvas_fill_bg(g_paint_canvas, lv_color_hex(0xFFFFFF),
                          LV_OPA_COVER);
        lv_obj_add_flag(g_paint_canvas,
                        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(g_paint_canvas, paint_canvas_cb,
                            LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(g_paint_canvas, paint_canvas_cb,
                            LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(g_paint_canvas, paint_canvas_cb,
                            LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(g_paint_canvas, paint_canvas_cb,
                            LV_EVENT_PRESS_LOST, NULL);
    }
    g_paint_color = lv_color_hex(0x111827);
    lv_obj_add_flag(g_paint_view, LV_OBJ_FLAG_HIDDEN);
}

static const uint32_t g_uart_baud_rates[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};
static const uint8_t g_uart_parities[] = {
    PARITY_NONE, PARITY_EVEN, PARITY_ODD, PARITY_NONE
};
static const uint8_t g_uart_stop_bits[] = {
    STOP_BITS_1, STOP_BITS_1, STOP_BITS_1, STOP_BITS_2
};
static const uint8_t g_uart_loopback_pattern[] = {
    0x55, 0xAA, 0x00, 0xFF, 0x12, 0x34, 0x56, 0x78
};

static uint32_t uart_selected_baud_rate(void)
{
    unsigned selected = lv_dropdown_get_selected(g_uart_baud_dropdown);
    if (g_uart_vaxp_mode) {
        if (selected >= sizeof(g_vaxp_baud_rates) /
                        sizeof(g_vaxp_baud_rates[0]))
            selected = 0;
        return g_vaxp_baud_rates[selected];
    }
    if (selected >= sizeof(g_uart_baud_rates) /
                    sizeof(g_uart_baud_rates[0]))
        selected = 4;
    return g_uart_baud_rates[selected];
}

static void uart_update_counter(void)
{
    char counter[64];
    snprintf(counter, sizeof(counter), "TX %llu B   RX %llu B",
             (unsigned long long)g_uart_tx_bytes,
             (unsigned long long)g_uart_rx_bytes);
    lv_label_set_text(g_uart_counter, counter);
}

static void uart_set_status(const char *text, uint32_t color)
{
    if (g_uart_status != NULL)
        lv_label_set_text(g_uart_status, text);
    if (g_uart_status_dot != NULL)
        lv_obj_set_style_bg_color(g_uart_status_dot, lv_color_hex(color), 0);
}

static void uart_log_refresh(void)
{
    if (g_uart_terminal == NULL || g_uart_terminal_panel == NULL)
        return;
    lv_label_set_text(g_uart_terminal,
                      g_uart_log_length == 0
                          ? uart_text("Waiting for serial data...")
                          : g_uart_log);
    lv_obj_update_layout(g_uart_terminal_panel);
    lv_obj_scroll_to_y(g_uart_terminal_panel, 32767, LV_ANIM_OFF);
}

static void uart_log_printf(const char *format, ...)
{
    char line[640];
    va_list arguments;
    size_t length;

    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    /* VAXP uses UART2 exclusively. Mirror its UI monitor to the independent
     * console UART so hardware-loopback tests remain observable without ever
     * injecting naked debug text into the binary VAXP transport. */
    if (g_uart_vaxp_mode)
        printf("[uart-vaxp] %s", line);
    length = strlen(line);
    if (length >= sizeof(g_uart_log)) {
        line[sizeof(line) - 1] = '\0';
        length = strlen(line);
    }
    if (g_uart_log_length + length >= sizeof(g_uart_log)) {
        size_t keep = g_uart_log_length > 4096 ? 4096 : g_uart_log_length;
        size_t start = g_uart_log_length - keep;
        while (start < g_uart_log_length && g_uart_log[start] != '\n')
            ++start;
        if (start < g_uart_log_length)
            ++start;
        keep = g_uart_log_length - start;
        memmove(g_uart_log, g_uart_log + start, keep);
        g_uart_log_length = keep;
        g_uart_log[keep] = '\0';
    }
    if (length < sizeof(g_uart_log) - g_uart_log_length) {
        memcpy(g_uart_log + g_uart_log_length, line, length + 1);
        g_uart_log_length += length;
    }
    uart_log_refresh();
}

static void uart_log_bytes(const char *direction, const uint8_t *data,
                           size_t size)
{
    char line[560];
    char stamp[16];
    size_t used;
    time_t now = time(NULL);
    struct tm *local = localtime(&now);

    if (local != NULL)
        strftime(stamp, sizeof(stamp), "%H:%M:%S", local);
    else
        snprintf(stamp, sizeof(stamp), "--:--:--");
    used = (size_t)snprintf(line, sizeof(line), "%s  %-2s  ",
                            stamp, direction);
    for (size_t i = 0; i < size && used + 6 < sizeof(line); ++i) {
        if (g_uart_hex_mode) {
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "%02X ", data[i]);
        } else if (data[i] == '\r') {
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "\\r");
        } else if (data[i] == '\n') {
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "\\n");
        } else if (data[i] >= 32 && data[i] <= 126) {
            line[used++] = (char)data[i];
            line[used] = '\0';
        } else {
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "\\x%02X", data[i]);
        }
    }
    snprintf(line + used, sizeof(line) - used, "\n");
    uart_log_printf("%s", line);
}

static void uart_update_payload_preview(void)
{
    const char *payload = lv_textarea_get_text(g_uart_editor_input);
    char preview[96];

    if (payload == NULL || payload[0] == '\0')
        snprintf(preview, sizeof(preview), "%s",
                 uart_text("Tap to enter payload"));
    else if (strlen(payload) > 42)
        snprintf(preview, sizeof(preview), "%.42s...", payload);
    else
        snprintf(preview, sizeof(preview), "%s", payload);
    lv_label_set_text(g_uart_payload_preview, preview);
}

static int uart_hex_value(char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static int uart_build_payload(uint8_t *payload, size_t capacity,
                              size_t *payload_size)
{
    const char *text = lv_textarea_get_text(g_uart_editor_input);
    size_t size = 0;

    if (text == NULL || text[0] == '\0')
        return -1;
    if (!g_uart_hex_mode) {
        size = strlen(text);
        if (size + (g_uart_crlf ? 2 : 0) > capacity)
            return -2;
        memcpy(payload, text, size);
        if (g_uart_crlf) {
            payload[size++] = '\r';
            payload[size++] = '\n';
        }
    } else {
        int high = -1;
        for (size_t i = 0; text[i] != '\0'; ++i) {
            int value;
            if (high < 0 && text[i] == '0' &&
                (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                ++i;
                continue;
            }
            value = uart_hex_value(text[i]);
            if (value >= 0) {
                if (high < 0) {
                    high = value;
                } else {
                    if (size >= capacity) return -2;
                    payload[size++] = (uint8_t)((high << 4) | value);
                    high = -1;
                }
            } else if (text[i] == ' ' || text[i] == '\t' ||
                       text[i] == ',' || text[i] == ':' || text[i] == '-') {
                if (high >= 0) return -3;
            } else {
                return -3;
            }
        }
        if (high >= 0 || size == 0)
            return -3;
    }
    *payload_size = size;
    return 0;
}

static int uart_send_current(bool notify)
{
    uint8_t payload[UART_MAX_PAYLOAD + 2];
    size_t payload_size;
    size_t written;
    int result;

    if (g_uart_lab == NULL) {
        if (notify) show_toast(uart_text("UART is not connected"));
        return -1;
    }
    result = uart_build_payload(payload, sizeof(payload), &payload_size);
    if (result != 0) {
        if (notify)
            show_toast(g_uart_hex_mode
                           ? uart_text("Enter complete HEX byte pairs")
                           : uart_text("Enter a payload first"));
        return -2;
    }
    written = dshanpi_uart_lab_write(g_uart_lab, payload, payload_size);
    if (written > payload_size || written != payload_size) {
        uart_set_status(uart_text("Write failed"), 0xD93025);
        if (notify) show_toast(uart_text("UART write failed"));
        return -3;
    }
    g_uart_tx_bytes += written;
    uart_update_counter();
    uart_log_bytes("TX", payload, written);
    return 0;
}

static void uart_loopback_finish(bool passed)
{
    g_uart_loopback_pending = false;
    uart_set_status(passed ? uart_text("Loopback passed")
                           : uart_text("Loopback failed"),
                    passed ? 0x25A55F : 0xD93025);
    show_toast(passed ? uart_text("Loopback test passed")
                      : uart_text("Loopback failed - connect IO44 to IO45"));
    uart_log_printf("          TEST  %s\n",
                    passed ? "PASS: IO44 -> IO45" : "FAIL: no valid echo");
}

static void uart_vaxp_update_summary(void)
{
    char text[72];

    if (g_vaxp_session_label != NULL) {
        if (g_vaxp_host_session != 0)
            snprintf(text, sizeof(text), "SESSION  0x%04X",
                     g_vaxp_host_session);
        else
            snprintf(text, sizeof(text), "SESSION  Not established");
        lv_label_set_text(g_vaxp_session_label, text);
    }
    if (g_vaxp_frame_counter != NULL) {
        snprintf(text, sizeof(text), "FRAMES   TX %lu  RX %lu",
                 (unsigned long)g_vaxp_tx_frames,
                 (unsigned long)g_vaxp_rx_frames);
        lv_label_set_text(g_vaxp_frame_counter, text);
    }
}

static int uart_vaxp_send_frame(uint8_t type, uint8_t flags,
                                uint16_t sequence, uint16_t command,
                                uint16_t session, uint8_t source,
                                uint8_t destination, const void *payload,
                                uint16_t payload_length)
{
    size_t frame_size;
    uint16_t peer_limit;

    if (g_uart_lab == NULL)
        return -1;
    peer_limit = source == VAXP_ADDR_HOST ? g_vaxp_host_peer_max_rx
                                          : g_vaxp_device_peer_max_rx;
    if (payload_length > peer_limit)
        return -4;
    if (vaxp_lab_encode_frame(g_vaxp_tx_buffer,
                              sizeof(g_vaxp_tx_buffer), type, flags,
                              sequence, command, session, source,
                              destination, lv_tick_get(), payload,
                              payload_length, &frame_size) != 0)
        return -2;
    if (dshanpi_uart_lab_write_all(g_uart_lab, g_vaxp_tx_buffer,
                                  frame_size) != 0)
        return -3;
    g_uart_tx_bytes += frame_size;
    ++g_vaxp_tx_frames;
    if (source != VAXP_ADDR_HOST)
        ++g_vaxp_device_tx_packets;
    uart_update_counter();
    uart_vaxp_update_summary();
    uart_log_printf("          TX    %-8s %-18s seq=%u len=%u\n",
                    vaxp_lab_message_type_name(type),
                    vaxp_lab_command_name(command), sequence,
                    payload_length);
    return 0;
}

static uint16_t uart_vaxp_next_sequence(uint16_t *sequence)
{
    uint16_t value = *sequence;

    if (value == 0)
        value = 1;
    *sequence = value == UINT16_MAX ? 1 : (uint16_t)(value + 1);
    return value;
}

static uint32_t uart_vaxp_request_timeout(uint16_t command)
{
    if (command == VAXP_CMD_PING)
        return 500;
    return 1000;
}

static int uart_vaxp_success_body_size(uint16_t command)
{
    switch (command) {
    case VAXP_CMD_HELLO: return sizeof(VaxpHelloResponse);
    case VAXP_CMD_PING: return 0;
    case VAXP_CMD_GET_DEVICE_INFO: return sizeof(VaxpDeviceInfo);
    case VAXP_CMD_GET_CAPABILITIES: return sizeof(VaxpCapabilities);
    case VAXP_CMD_GET_STATUS: return sizeof(VaxpDeviceStatus);
    case VAXP_CMD_TIME_SYNC: return sizeof(VaxpTimeSyncResponse);
    case VAXP_CMD_GET_HEALTH: return sizeof(VaxpHealthStatus);
    default: return -1;
    }
}

static void uart_vaxp_clear_pending(void)
{
    memset(g_vaxp_pending, 0, sizeof(g_vaxp_pending));
}

static size_t uart_vaxp_pending_count(void)
{
    size_t count = 0;

    for (size_t i = 0; i < UART_VAXP_PENDING_CAPACITY; ++i) {
        if (g_vaxp_pending[i].used)
            ++count;
    }
    return count;
}

static uart_vaxp_pending_t *uart_vaxp_pending_reserve(void)
{
    size_t limit = g_vaxp_host_max_pending;

    if (limit == 0 || limit > UART_VAXP_PENDING_CAPACITY)
        limit = UART_VAXP_PENDING_CAPACITY;
    if (uart_vaxp_pending_count() >= limit)
        return NULL;
    for (size_t i = 0; i < UART_VAXP_PENDING_CAPACITY; ++i) {
        if (!g_vaxp_pending[i].used) {
            memset(&g_vaxp_pending[i], 0, sizeof(g_vaxp_pending[i]));
            g_vaxp_pending[i].used = true;
            return &g_vaxp_pending[i];
        }
    }
    return NULL;
}

static uart_vaxp_pending_t *
uart_vaxp_pending_find(const VaxpHeader *response)
{
    for (size_t i = 0; i < UART_VAXP_PENDING_CAPACITY; ++i) {
        uart_vaxp_pending_t *pending = &g_vaxp_pending[i];

        if (!pending->used || pending->sequence != response->sequence ||
            pending->command != response->command)
            continue;
        if (pending->command == VAXP_CMD_HELLO && pending->session == 0)
            return pending;
        if (pending->session == response->session_id)
            return pending;
    }
    return NULL;
}

static void uart_vaxp_reset_host_link(void)
{
    uart_vaxp_clear_pending();
    g_vaxp_host_session = 0;
    g_vaxp_host_peer_max_rx = VAXP_DEFAULT_MAX_PAYLOAD;
    g_vaxp_host_rx_limit = VAXP_DEFAULT_MAX_PAYLOAD;
    g_vaxp_host_max_pending = 1;
    g_vaxp_host_heartbeat_ms = VAXP_DEFAULT_HEARTBEAT_MS;
    uart_vaxp_update_summary();
}

static uart_vaxp_cached_response_t *
uart_vaxp_cached_response_find(const VaxpHeader *request)
{
    for (size_t i = 0; i < UART_VAXP_RESPONSE_CACHE_CAPACITY; ++i) {
        uart_vaxp_cached_response_t *cached = &g_vaxp_response_cache[i];

        if (cached->valid && cached->session == request->session_id &&
            cached->source == request->source &&
            cached->sequence == request->sequence &&
            cached->command == request->command)
            return cached;
    }
    return NULL;
}

static void uart_vaxp_cached_response_store(const VaxpHeader *request,
                                            const uint8_t *payload,
                                            uint16_t payload_length)
{
    uart_vaxp_cached_response_t *cached;

    if (payload_length > sizeof(g_vaxp_response_cache[0].payload))
        return;
    cached = &g_vaxp_response_cache[g_vaxp_response_cache_next];
    g_vaxp_response_cache_next =
        (g_vaxp_response_cache_next + 1) %
        UART_VAXP_RESPONSE_CACHE_CAPACITY;
    memset(cached, 0, sizeof(*cached));
    cached->valid = true;
    cached->session = request->session_id;
    cached->source = request->source;
    cached->sequence = request->sequence;
    cached->command = request->command;
    cached->payload_length = payload_length;
    memcpy(cached->payload, payload, payload_length);
}

static void uart_vaxp_cached_response_send(
    const uart_vaxp_cached_response_t *cached)
{
    if (uart_vaxp_send_frame(VAXP_MSG_RESPONSE, 0, cached->sequence,
                             cached->command, g_vaxp_device_session,
                             VAXP_ADDR_DEVICE_MIN, cached->source,
                             cached->payload, cached->payload_length) == 0) {
        uart_log_printf("          REPLAY cached response seq=%u\n",
                        cached->sequence);
    }
}

static size_t uart_vaxp_response_header(uint8_t *payload, int16_t status,
                                        uint16_t detail)
{
    vaxp_write_le16(payload, (uint16_t)status);
    vaxp_write_le16(payload + 2, detail);
    return sizeof(VaxpResponseHeader);
}

static void uart_vaxp_send_response(const VaxpHeader *request,
                                    int16_t status, uint16_t detail,
                                    const uint8_t *body,
                                    uint16_t body_length)
{
    uint8_t payload[sizeof(VaxpResponseHeader) + sizeof(VaxpDeviceInfo)];
    size_t payload_length = uart_vaxp_response_header(payload, status,
                                                       detail);

    if (body_length > sizeof(payload) - payload_length)
        return;
    if (body_length != 0)
        memcpy(payload + payload_length, body, body_length);
    payload_length += body_length;
    if (payload_length > g_vaxp_device_peer_max_rx) {
        uint16_t required_length = (uint16_t)payload_length;

        payload_length = uart_vaxp_response_header(
            payload, VAXP_ERR_RESOURCE_LIMIT, required_length);
    }
    uart_vaxp_cached_response_store(request, payload,
                                    (uint16_t)payload_length);
    if (uart_vaxp_send_frame(VAXP_MSG_RESPONSE, 0, request->sequence,
                             request->command, g_vaxp_device_session,
                             VAXP_ADDR_DEVICE_MIN, request->source,
                             payload, (uint16_t)payload_length) != 0)
        uart_set_status(uart_text("VAXP response failed"), 0xD93025);
}

static void uart_vaxp_handle_request(const VaxpHeader *header,
                                     const uint8_t *payload,
                                     uint16_t payload_length)
{
    uint8_t body[sizeof(VaxpDeviceInfo)];
    uint16_t body_length = 0;
    int16_t status = VAXP_OK;
    uint16_t detail = 0;
    uint64_t capabilities = VAXP_CAP_HEALTH;
    uart_vaxp_cached_response_t *cached;

    if (header->destination == VAXP_ADDR_BROADCAST)
        return;
    if (header->destination != VAXP_ADDR_DEVICE_MIN ||
        header->source != VAXP_ADDR_HOST)
        return;
    ++g_vaxp_device_rx_packets;
    if (header->sequence == 0) {
        uart_vaxp_send_response(header, VAXP_ERR_INVALID_PARAMETER, 0,
                                NULL, 0);
        return;
    }
    if ((header->flags & ~UART_VAXP_SUPPORTED_FLAGS) != 0) {
        uart_vaxp_send_response(header, VAXP_ERR_NOT_SUPPORTED,
                                header->flags, NULL, 0);
        return;
    }
    if (header->command == VAXP_CMD_HELLO && header->session_id != 0) {
        uart_vaxp_send_response(header, VAXP_ERR_SESSION_INVALID, 0,
                                NULL, 0);
        return;
    }
    if (header->command != VAXP_CMD_HELLO &&
        (!g_vaxp_device_established ||
         header->session_id != g_vaxp_device_session)) {
        uart_vaxp_send_response(header, VAXP_ERR_SESSION_INVALID, 0,
                                NULL, 0);
        return;
    }
    cached = uart_vaxp_cached_response_find(header);
    if (cached != NULL) {
        uart_vaxp_cached_response_send(cached);
        return;
    }

    memset(body, 0, sizeof(body));
    switch (header->command) {
    case VAXP_CMD_HELLO:
        if (payload_length != sizeof(VaxpHelloRequest)) {
            status = VAXP_ERR_INVALID_LENGTH;
        } else if (payload[0] > payload[1]) {
            status = VAXP_ERR_INVALID_PARAMETER;
        } else if (payload[0] > VAXP_PROTOCOL_VERSION ||
                   payload[1] < VAXP_PROTOCOL_VERSION) {
            status = VAXP_ERR_VERSION_NOT_SUPPORTED;
            detail = VAXP_PROTOCOL_VERSION;
        } else if (vaxp_read_le16(payload + 2) <
                   sizeof(VaxpResponseHeader) + sizeof(VaxpHelloResponse)) {
            status = VAXP_ERR_INVALID_PARAMETER;
            detail = sizeof(VaxpResponseHeader) + sizeof(VaxpHelloResponse);
        } else {
            memset(g_vaxp_response_cache, 0,
                   sizeof(g_vaxp_response_cache));
            g_vaxp_response_cache_next = 0;
            g_vaxp_device_peer_max_rx = vaxp_read_le16(payload + 2);
            if (g_vaxp_device_peer_max_rx > VAXP_DEFAULT_MAX_PAYLOAD)
                g_vaxp_device_peer_max_rx = VAXP_DEFAULT_MAX_PAYLOAD;
            body[0] = VAXP_PROTOCOL_VERSION;
            body[1] = 0;
            vaxp_write_le16(body + 2, g_vaxp_device_session);
            vaxp_write_le16(body + 4, VAXP_DEFAULT_MAX_PAYLOAD);
            vaxp_write_le16(body + 6, VAXP_DEFAULT_MAX_PAYLOAD);
            vaxp_write_le16(body + 8, VAXP_DEFAULT_MAX_PENDING);
            vaxp_write_le16(body + 10, VAXP_DEFAULT_HEARTBEAT_MS);
            vaxp_write_le64(body + 12, capabilities);
            body_length = sizeof(VaxpHelloResponse);
            g_vaxp_device_established = true;
            g_vaxp_last_heartbeat_tick = lv_tick_get();
        }
        break;
    case VAXP_CMD_PING:
        if (payload_length != 0)
            status = VAXP_ERR_INVALID_LENGTH;
        break;
    case VAXP_CMD_GET_CAPABILITIES:
        if (payload_length != 0) {
            status = VAXP_ERR_INVALID_LENGTH;
            break;
        }
        vaxp_write_le64(body, capabilities);
        vaxp_write_le16(body + 8, VAXP_DEFAULT_MAX_PAYLOAD);
        vaxp_write_le16(body + 10, 0);
        body[12] = 0;
        body[13] = 0;
        body[14] = 0;
        body[15] = 0;
        body[16] = 0;
        body[17] = 0;
        vaxp_write_le16(body + 18, 0);
        body_length = sizeof(VaxpCapabilities);
        break;
    case VAXP_CMD_GET_DEVICE_INFO:
        if (payload_length != 0) {
            status = VAXP_ERR_INVALID_LENGTH;
            break;
        }
        snprintf((char *)body, 32, "DongshanPI CanMV-K230");
        snprintf((char *)body + 32, 32, "100ASK");
        snprintf((char *)body + 64, 32, "K230-UART2");
        snprintf((char *)body + 96, 16, "DongshanPI");
        snprintf((char *)body + 112, 16, "CanMV");
        snprintf((char *)body + 128, 16, "K230 SDK");
        snprintf((char *)body + 144, 16, "Kendryte K230");
        snprintf((char *)body + 160, 24, "VAXP-Core-1.0");
        vaxp_write_le32(body + 184, (uint32_t)time(NULL));
        body_length = sizeof(VaxpDeviceInfo);
        break;
    case VAXP_CMD_GET_STATUS:
        if (payload_length != 0) {
            status = VAXP_ERR_INVALID_LENGTH;
            break;
        }
        body[0] = VAXP_DEVICE_READY;
        vaxp_write_le32(body + 4, lv_tick_elaps(g_vaxp_started_tick) / 1000);
        vaxp_write_le16(body + 8, (uint16_t)3500);
        body_length = sizeof(VaxpDeviceStatus);
        break;
    case VAXP_CMD_TIME_SYNC:
        if (payload_length != sizeof(VaxpTimeSyncRequest)) {
            status = VAXP_ERR_INVALID_LENGTH;
            break;
        }
        memcpy(body, payload, sizeof(uint64_t));
        vaxp_write_le64(body + 8, (uint64_t)lv_tick_get() * 1000u);
        vaxp_write_le64(body + 16, (uint64_t)lv_tick_get() * 1000u);
        body_length = sizeof(VaxpTimeSyncResponse);
        break;
    case VAXP_CMD_GET_HEALTH:
        if (payload_length != 0) {
            status = VAXP_ERR_INVALID_LENGTH;
            break;
        }
        body_length = sizeof(VaxpHealthStatus);
        break;
    default:
        status = VAXP_ERR_UNKNOWN_COMMAND;
        break;
    }
    uart_vaxp_send_response(header, status, detail, body, body_length);
}

static void uart_vaxp_send_ack(const VaxpHeader *header)
{
    uint8_t payload[sizeof(VaxpAckPayload)];

    vaxp_write_le16(payload, header->sequence);
    vaxp_write_le16(payload + 2, header->command);
    vaxp_write_le16(payload + 4, VAXP_OK);
    vaxp_write_le16(payload + 6, 0);
    uart_vaxp_send_frame(VAXP_MSG_ACK, 0, header->sequence,
                         header->command, header->session_id,
                         VAXP_ADDR_HOST, header->source, payload,
                         sizeof(payload));
}

static void uart_vaxp_handle_boot_event(const VaxpHeader *header,
                                        const uint8_t *payload,
                                        uint16_t payload_length)
{
    uint16_t boot_session;

    if (payload_length != sizeof(VaxpDeviceBootEvent) ||
        payload[0] != VAXP_PROTOCOL_VERSION) {
        uart_log_printf("          DROP  invalid DEVICE_BOOT payload\n");
        return;
    }
    boot_session = vaxp_read_le16(payload + 2);
    if (boot_session == 0 || boot_session != header->session_id) {
        uart_log_printf("          DROP  invalid DEVICE_BOOT session\n");
        return;
    }
    if ((header->flags & VAXP_FLAG_ACK_REQUIRED) != 0)
        uart_vaxp_send_ack(header);
    if (boot_session == g_vaxp_boot_session_seen)
        return;

    uart_log_printf("          BOOT  new device session=0x%04X reason=%u\n",
                    boot_session, payload[1]);
    uart_vaxp_reset_host_link();
    g_vaxp_boot_session_seen = boot_session;
    uart_set_status(uart_text("Device restarted - negotiating VAXP"),
                    0xD68A00);
    uart_vaxp_send_request(VAXP_CMD_HELLO, false);
}

static void uart_vaxp_handle_ack(const VaxpHeader *header,
                                 const uint8_t *payload,
                                 uint16_t payload_length)
{
    int16_t status;

    if (payload_length != sizeof(VaxpAckPayload) ||
        header->session_id != g_vaxp_device_session ||
        vaxp_read_le16(payload) != header->sequence ||
        vaxp_read_le16(payload + 2) != header->command) {
        uart_log_printf("          DROP  malformed ACK\n");
        return;
    }
    if (!g_vaxp_critical_event.active ||
        g_vaxp_critical_event.session != header->session_id ||
        g_vaxp_critical_event.sequence != header->sequence ||
        g_vaxp_critical_event.command != header->command) {
        uart_log_printf("          DROP  unmatched ACK\n");
        return;
    }
    status = (int16_t)vaxp_read_le16(payload + 4);
    g_vaxp_critical_event.active = false;
    uart_log_printf("          ACK   %s seq=%u status=%d\n",
                    vaxp_lab_command_name(header->command),
                    header->sequence, status);
}

static void uart_vaxp_frame_received(void *context,
                                     const VaxpHeader *header,
                                     const uint8_t *payload,
                                     uint16_t payload_length)
{
    (void)context;
    ++g_vaxp_rx_frames;
    uart_vaxp_update_summary();
    uart_log_printf("          RX    %-8s %-18s seq=%u len=%u\n",
                    vaxp_lab_message_type_name(header->message_type),
                    vaxp_lab_command_name(header->command),
                    header->sequence, payload_length);

    if (header->message_type == VAXP_MSG_REQUEST) {
        uart_vaxp_handle_request(header, payload, payload_length);
        return;
    }
    if (header->sequence == 0) {
        uart_log_printf("          DROP  reserved sequence 0\n");
        return;
    }
    if ((header->flags & ~UART_VAXP_SUPPORTED_FLAGS) != 0) {
        uart_log_printf("          DROP  unsupported flags 0x%02X\n",
                        header->flags);
        return;
    }
    if (header->message_type == VAXP_MSG_ACK) {
        if (header->source != VAXP_ADDR_HOST ||
            header->destination != VAXP_ADDR_DEVICE_MIN) {
            uart_log_printf("          DROP  invalid ACK address\n");
            return;
        }
        uart_vaxp_handle_ack(header, payload, payload_length);
        return;
    }
    if (header->source != VAXP_ADDR_DEVICE_MIN ||
        (header->destination != VAXP_ADDR_HOST &&
         header->destination != VAXP_ADDR_BROADCAST)) {
        uart_log_printf("          DROP  invalid source/destination\n");
        return;
    }
    if (header->message_type == VAXP_MSG_RESPONSE) {
        uart_vaxp_pending_t *pending;
        int16_t status;
        uint16_t detail;

        if (header->destination == VAXP_ADDR_BROADCAST) {
            uart_log_printf("          DROP  broadcast response\n");
            return;
        }
        if (header->command != VAXP_CMD_HELLO &&
            (g_vaxp_host_session == 0 ||
             header->session_id != g_vaxp_host_session)) {
            uart_log_printf("          DROP  stale session 0x%04X\n",
                            header->session_id);
            return;
        }
        pending = uart_vaxp_pending_find(header);
        if (pending == NULL) {
            uart_log_printf("          DROP  unmatched response\n");
            return;
        }
        if (payload_length > g_vaxp_host_rx_limit) {
            uart_log_printf("          DROP  response exceeds negotiated limit\n");
            pending->used = false;
            return;
        }
        if (payload_length < sizeof(VaxpResponseHeader)) {
            uart_log_printf("          ERROR RESPONSE missing status header\n");
            pending->used = false;
            return;
        }
        status = (int16_t)vaxp_read_le16(payload);
        detail = vaxp_read_le16(payload + 2);
        if (status == VAXP_OK) {
            int expected_body =
                uart_vaxp_success_body_size(header->command);

            if (expected_body >= 0 &&
                payload_length != sizeof(VaxpResponseHeader) +
                                  (size_t)expected_body) {
                uart_log_printf("          ERROR invalid response length\n");
                pending->used = false;
                return;
            }
        }
        uart_log_printf("          RESP  status=%d detail=%u%s\n",
                        status, detail, "  matched");
        pending->used = false;
        if (header->command == VAXP_CMD_HELLO && status == VAXP_OK) {
            uint16_t hello_session;
            uint16_t peer_max_rx;
            uint16_t peer_max_tx;
            uint16_t peer_max_pending;
            uint16_t heartbeat_ms;

            if (payload_length != sizeof(VaxpResponseHeader) +
                                  sizeof(VaxpHelloResponse)) {
                uart_log_printf("          ERROR invalid HELLO response length\n");
                return;
            }
            hello_session = vaxp_read_le16(payload + 6);
            peer_max_rx = vaxp_read_le16(payload + 8);
            peer_max_tx = vaxp_read_le16(payload + 10);
            peer_max_pending = vaxp_read_le16(payload + 12);
            heartbeat_ms = vaxp_read_le16(payload + 14);
            if (payload[4] != VAXP_PROTOCOL_VERSION || hello_session == 0 ||
                header->session_id != hello_session || peer_max_rx == 0 ||
                peer_max_tx == 0 || peer_max_pending == 0 ||
                heartbeat_ms == 0) {
                uart_log_printf("          ERROR invalid HELLO negotiation\n");
                return;
            }
            g_vaxp_host_session = hello_session;
            g_vaxp_boot_session_seen = hello_session;
            g_vaxp_host_peer_max_rx =
                peer_max_rx > VAXP_DEFAULT_MAX_PAYLOAD
                    ? VAXP_DEFAULT_MAX_PAYLOAD : peer_max_rx;
            g_vaxp_host_rx_limit =
                peer_max_tx > VAXP_DEFAULT_MAX_PAYLOAD
                    ? VAXP_DEFAULT_MAX_PAYLOAD : peer_max_tx;
            g_vaxp_host_max_pending =
                peer_max_pending > UART_VAXP_PENDING_CAPACITY
                    ? UART_VAXP_PENDING_CAPACITY : peer_max_pending;
            g_vaxp_host_heartbeat_ms = heartbeat_ms;
            uart_vaxp_update_summary();
            uart_set_status(uart_text("VAXP session ready"), 0x25A55F);
            uart_log_printf("          LINK  VAXP 1.0 session=0x%04X, "
                            "heartbeat=%u ms pending=%u\n",
                            g_vaxp_host_session, g_vaxp_host_heartbeat_ms,
                            g_vaxp_host_max_pending);
            uart_vaxp_send_request(VAXP_CMD_GET_DEVICE_INFO, false);
            uart_vaxp_send_request(VAXP_CMD_GET_CAPABILITIES, false);
            uart_vaxp_send_request(VAXP_CMD_TIME_SYNC, false);
        } else if (header->command == VAXP_CMD_GET_DEVICE_INFO &&
                   status == VAXP_OK &&
                   payload_length >= sizeof(VaxpResponseHeader) + 32) {
            char product[33];
            memcpy(product, payload + sizeof(VaxpResponseHeader), 32);
            product[32] = '\0';
            uart_log_printf("          INFO  product=%s\n", product);
        } else if (header->command == VAXP_CMD_GET_STATUS &&
                   status == VAXP_OK &&
                   payload_length >= sizeof(VaxpResponseHeader) +
                                     sizeof(VaxpDeviceStatus)) {
            uart_log_printf("          STATE value=%u uptime=%lu s\n",
                            payload[4],
                            (unsigned long)vaxp_read_le32(payload + 8));
        }
        return;
    }
    if (header->message_type == VAXP_MSG_EVENT) {
        if (header->command == VAXP_EVENT_DEVICE_BOOT) {
            if (header->destination == VAXP_ADDR_BROADCAST &&
                (header->flags & VAXP_FLAG_ACK_REQUIRED) != 0) {
                uart_log_printf("          DROP  broadcast event requested ACK\n");
                return;
            }
            uart_vaxp_handle_boot_event(header, payload, payload_length);
            return;
        }
        if (g_vaxp_host_session == 0 ||
            header->session_id != g_vaxp_host_session) {
            uart_log_printf("          DROP  event from stale session\n");
            return;
        }
        if (payload_length > g_vaxp_host_rx_limit) {
            uart_log_printf("          DROP  event exceeds negotiated limit\n");
            return;
        }
        if ((header->flags & VAXP_FLAG_ACK_REQUIRED) != 0 &&
            header->destination != VAXP_ADDR_BROADCAST)
            uart_vaxp_send_ack(header);
    }
}

static void uart_vaxp_parse_error(void *context,
                                  vaxp_lab_parse_error_t error)
{
    (void)context;
    uart_set_status(uart_text("VAXP frame error"), 0xD68A00);
    uart_log_printf("          ERROR %s\n", vaxp_lab_error_name(error));
}

static uint16_t uart_vaxp_selected_command(void)
{
    static const uint16_t commands[] = {
        VAXP_CMD_HELLO, VAXP_CMD_PING, VAXP_CMD_GET_DEVICE_INFO,
        VAXP_CMD_GET_CAPABILITIES, VAXP_CMD_GET_STATUS,
        VAXP_CMD_TIME_SYNC, VAXP_CMD_GET_HEALTH
    };
    unsigned selected = lv_dropdown_get_selected(g_vaxp_command_dropdown);

    if (selected >= sizeof(commands) / sizeof(commands[0]))
        selected = 0;
    return commands[selected];
}

static int uart_vaxp_send_request(uint16_t command, bool notify)
{
    uint8_t payload[sizeof(VaxpHelloRequest)];
    uart_vaxp_pending_t *pending;
    uint16_t payload_length = 0;
    uint16_t sequence;
    uint16_t session = g_vaxp_host_session;
    int result;

    if (g_uart_lab == NULL) {
        if (notify) show_toast(uart_text("UART is not connected"));
        return -1;
    }
    memset(payload, 0, sizeof(payload));
    if (command == VAXP_CMD_HELLO) {
        uart_vaxp_reset_host_link();
        payload[0] = VAXP_PROTOCOL_VERSION;
        payload[1] = VAXP_PROTOCOL_VERSION;
        vaxp_write_le16(payload + 2, VAXP_DEFAULT_MAX_PAYLOAD);
        vaxp_write_le32(payload + 4, 0);
        payload_length = sizeof(VaxpHelloRequest);
        session = 0;
    } else if (session == 0) {
        if (notify)
            show_toast(uart_text("Send HELLO to establish a session first"));
        return -2;
    } else if (command == VAXP_CMD_TIME_SYNC) {
        vaxp_write_le64(payload, (uint64_t)lv_tick_get() * 1000u);
        payload_length = sizeof(VaxpTimeSyncRequest);
    }
    if (payload_length > g_vaxp_host_peer_max_rx) {
        if (notify)
            show_toast(uart_text("Payload exceeds negotiated VAXP limit"));
        return -3;
    }
    pending = uart_vaxp_pending_reserve();
    if (pending == NULL) {
        uart_set_status(uart_text("VAXP request window is full"), 0xD68A00);
        if (notify) show_toast(uart_text("Too many pending VAXP requests"));
        return -4;
    }
    sequence = uart_vaxp_next_sequence(&g_vaxp_next_sequence);
    pending->sequence = sequence;
    pending->command = command;
    pending->session = session;
    pending->payload_length = payload_length;
    pending->sent_tick = lv_tick_get();
    pending->timeout_ms = uart_vaxp_request_timeout(command);
    pending->retries = 0;
    if (payload_length != 0)
        memcpy(pending->payload, payload, payload_length);
    result = uart_vaxp_send_frame(VAXP_MSG_REQUEST, 0, sequence, command,
                                  session, VAXP_ADDR_HOST,
                                  VAXP_ADDR_DEVICE_MIN, payload,
                                  payload_length);
    if (result != 0) {
        pending->used = false;
        uart_set_status(uart_text("VAXP send failed"), 0xD93025);
        if (notify) show_toast(uart_text("VAXP request send failed"));
    }
    return result;
}

static void uart_vaxp_service_pending(void)
{
    uint32_t now = lv_tick_get();

    for (size_t i = 0; i < UART_VAXP_PENDING_CAPACITY; ++i) {
        uart_vaxp_pending_t *pending = &g_vaxp_pending[i];

        if (!pending->used ||
            lv_tick_elaps(pending->sent_tick) < pending->timeout_ms)
            continue;
        if (pending->retries >= UART_VAXP_MAX_RETRIES) {
            uart_log_printf("          TIMEOUT %s seq=%u after %u retries\n",
                            vaxp_lab_command_name(pending->command),
                            pending->sequence, pending->retries);
            if (pending->command == VAXP_CMD_HELLO)
                uart_set_status(uart_text("VAXP HELLO timed out"),
                                0xD93025);
            pending->used = false;
            continue;
        }
        ++pending->retries;
        pending->sent_tick = now;
        uart_log_printf("          RETRY %s seq=%u attempt=%u\n",
                        vaxp_lab_command_name(pending->command),
                        pending->sequence, pending->retries);
        if (uart_vaxp_send_frame(VAXP_MSG_REQUEST, 0,
                                 pending->sequence, pending->command,
                                 pending->session, VAXP_ADDR_HOST,
                                 VAXP_ADDR_DEVICE_MIN, pending->payload,
                                 pending->payload_length) != 0) {
            uart_log_printf("          ERROR retry write failed\n");
        }
    }
}

static void uart_vaxp_send_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    uart_vaxp_send_request(uart_vaxp_selected_command(), true);
}

static void uart_vaxp_spec_test_cb(lv_event_t *event)
{
    int result;
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    result = vaxp_lab_self_test();
    uart_log_printf("          TEST  official PING + HELLO vectors: %s\n",
                    result == 0 ? "PASS" : "FAIL");
    uart_set_status(result == 0 ? uart_text("VAXP self-test passed")
                                : uart_text("VAXP self-test failed"),
                    result == 0 ? 0x25A55F : 0xD93025);
    show_toast(result == 0
                   ? uart_text("VAXP wire-format self-test passed")
                   : uart_text("VAXP wire-format self-test failed"));
}

static void uart_vaxp_send_heartbeat(void)
{
    uint8_t payload[sizeof(VaxpHeartbeat)];

    memset(payload, 0, sizeof(payload));
    vaxp_write_le32(payload, lv_tick_elaps(g_vaxp_started_tick) / 1000);
    payload[4] = VAXP_DEVICE_READY;
    vaxp_write_le16(payload + 8, (uint16_t)3500);
    vaxp_write_le32(payload + 16, g_vaxp_device_rx_packets);
    vaxp_write_le32(payload + 20, g_vaxp_device_tx_packets);
    vaxp_write_le32(payload + 24, g_vaxp_parser.crc_errors);
    vaxp_write_le32(payload + 28, g_vaxp_parser.header_errors +
                                  g_vaxp_parser.timeouts);
    uart_vaxp_send_frame(VAXP_MSG_EVENT, 0,
                         uart_vaxp_next_sequence(&g_vaxp_device_sequence),
                         VAXP_EVENT_HEARTBEAT, g_vaxp_device_session,
                         VAXP_ADDR_DEVICE_MIN, VAXP_ADDR_HOST,
                         payload, sizeof(payload));
}

static void uart_vaxp_send_boot_event(void)
{
    uart_vaxp_critical_event_t *event = &g_vaxp_critical_event;

    memset(event, 0, sizeof(*event));
    ++g_vaxp_boot_count;
    event->active = true;
    event->sequence = uart_vaxp_next_sequence(&g_vaxp_device_sequence);
    event->command = VAXP_EVENT_DEVICE_BOOT;
    event->session = g_vaxp_device_session;
    event->payload_length = sizeof(VaxpDeviceBootEvent);
    event->sent_tick = lv_tick_get();
    event->payload[0] = VAXP_PROTOCOL_VERSION;
    event->payload[1] = VAXP_BOOT_SOFTWARE;
    vaxp_write_le16(event->payload + 2, g_vaxp_device_session);
    vaxp_write_le32(event->payload + 4, g_vaxp_boot_count);
    vaxp_write_le32(event->payload + 8,
                    lv_tick_elaps(g_vaxp_started_tick));
    uart_vaxp_send_frame(VAXP_MSG_EVENT,
                         VAXP_FLAG_ACK_REQUIRED | VAXP_FLAG_URGENT,
                         event->sequence, event->command, event->session,
                         VAXP_ADDR_DEVICE_MIN, VAXP_ADDR_HOST,
                         event->payload, event->payload_length);
}

static void uart_vaxp_service_critical_event(void)
{
    uart_vaxp_critical_event_t *event = &g_vaxp_critical_event;

    if (!event->active ||
        lv_tick_elaps(event->sent_tick) < UART_VAXP_ACK_TIMEOUT_MS)
        return;
    if (event->retries >= UART_VAXP_MAX_RETRIES) {
        uart_log_printf("          TIMEOUT ACK for %s seq=%u\n",
                        vaxp_lab_command_name(event->command),
                        event->sequence);
        event->active = false;
        return;
    }
    ++event->retries;
    event->sent_tick = lv_tick_get();
    uart_log_printf("          RETRY EVENT %s seq=%u attempt=%u\n",
                    vaxp_lab_command_name(event->command), event->sequence,
                    event->retries);
    uart_vaxp_send_frame(VAXP_MSG_EVENT,
                         VAXP_FLAG_ACK_REQUIRED | VAXP_FLAG_URGENT,
                         event->sequence, event->command, event->session,
                         VAXP_ADDR_DEVICE_MIN, VAXP_ADDR_HOST,
                         event->payload, event->payload_length);
}

static void uart_poll_timer_cb(lv_timer_t *timer)
{
    uint8_t data[128];
    int readiness;
    (void)timer;

    if (g_uart_lab == NULL)
        return;
    readiness = dshanpi_uart_lab_poll(g_uart_lab, 0);
    if (readiness > 0) {
        size_t received = dshanpi_uart_lab_read(g_uart_lab, data, sizeof(data));
        if (received <= sizeof(data) && received > 0) {
            g_uart_rx_bytes += received;
            uart_update_counter();
            if (g_uart_vaxp_mode) {
                vaxp_lab_parser_feed(&g_vaxp_parser, data, received,
                                     lv_tick_get(),
                                     uart_vaxp_frame_received,
                                     uart_vaxp_parse_error, NULL);
            } else {
                uart_log_bytes("RX", data, received);
            }
            if (!g_uart_vaxp_mode && g_uart_loopback_pending) {
                for (size_t i = 0; i < received; ++i) {
                    if (g_uart_loopback_received <
                        sizeof(g_uart_loopback_pattern)) {
                        if (data[i] !=
                            g_uart_loopback_pattern[g_uart_loopback_received])
                            g_uart_loopback_mismatch = true;
                        ++g_uart_loopback_received;
                    }
                }
                if (g_uart_loopback_received >=
                    sizeof(g_uart_loopback_pattern))
                    uart_loopback_finish(!g_uart_loopback_mismatch);
            }
        } else if (received > sizeof(data)) {
            uart_set_status(uart_text("Read failed"), 0xD93025);
        }
    } else if (readiness < 0) {
        uart_set_status(uart_text("UART receive error"), 0xD93025);
    }
    if (g_uart_vaxp_mode) {
        uint32_t timeout_ms = 100;
        uint32_t baud = uart_selected_baud_rate();
        if (g_vaxp_parser.expected_length != 0 && baud != 0) {
            uint32_t wire_ms = (uint32_t)
                ((g_vaxp_parser.expected_length * 10000u + baud - 1u) /
                 baud);
            if (wire_ms * 3u > timeout_ms)
                timeout_ms = wire_ms * 3u;
            if (timeout_ms > 2000u)
                timeout_ms = 2000u;
        }
        vaxp_lab_parser_tick(&g_vaxp_parser, lv_tick_get(), timeout_ms,
                             uart_vaxp_parse_error, NULL);
        uart_vaxp_service_pending();
        uart_vaxp_service_critical_event();
        if (g_vaxp_device_established &&
            lv_tick_elaps(g_vaxp_last_heartbeat_tick) >=
                VAXP_DEFAULT_HEARTBEAT_MS) {
            g_vaxp_last_heartbeat_tick = lv_tick_get();
            uart_vaxp_send_heartbeat();
        }
    }
    if (!g_uart_vaxp_mode && g_uart_loopback_pending &&
        lv_tick_elaps(g_uart_loopback_started) > 1000)
        uart_loopback_finish(false);
}

static void uart_send_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    uart_send_current(true);
}

static void uart_clear_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    g_uart_log_length = 0;
    g_uart_log[0] = '\0';
    g_uart_tx_bytes = 0;
    g_uart_rx_bytes = 0;
    g_vaxp_tx_frames = 0;
    g_vaxp_rx_frames = 0;
    uart_update_counter();
    uart_vaxp_update_summary();
    uart_log_refresh();
}

static void uart_hex_toggle_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    if (!lv_k230_touch_accept_click()) return;
    g_uart_hex_mode = !g_uart_hex_mode;
    lv_label_set_text(g_uart_hex_label,
                      g_uart_hex_mode ? "HEX" : uart_text("TEXT"));
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(g_uart_hex_mode
                                               ? 0xD8E7FF : 0xEEF1F5), 0);
    lv_textarea_set_placeholder_text(g_uart_editor_input,
                                     g_uart_hex_mode
                                         ? uart_text("Example: 55 AA 01 FF")
                                         : uart_text("Enter text payload"));
    lv_textarea_set_accepted_chars(g_uart_editor_input,
                                   g_uart_hex_mode
                                       ? "0123456789abcdefABCDEFxX ,:-" : NULL);
    uart_log_printf("          MODE  %s display and send\n",
                    g_uart_hex_mode ? "HEX" : "TEXT");
}

static void uart_crlf_toggle_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    if (!lv_k230_touch_accept_click()) return;
    g_uart_crlf = !g_uart_crlf;
    lv_label_set_text(g_uart_crlf_label,
                      g_uart_crlf ? uart_text("CRLF ON")
                                  : uart_text("CRLF OFF"));
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(g_uart_crlf
                                               ? 0xDDF4E6 : 0xEEF1F5), 0);
}

static void uart_config_changed_cb(lv_event_t *event)
{
    unsigned format_index = lv_dropdown_get_selected(g_uart_format_dropdown);
    uint32_t baud_rate = uart_selected_baud_rate();
    char status[96];
    (void)event;

    if (format_index >= sizeof(g_uart_parities) /
                        sizeof(g_uart_parities[0]))
        format_index = 0;
    if (g_uart_lab != NULL &&
        dshanpi_uart_lab_configure(g_uart_lab, baud_rate,
                                   g_uart_parities[format_index],
                                   g_uart_stop_bits[format_index]) != 0) {
        uart_set_status(uart_text("Configuration failed"), 0xD93025);
        return;
    }
    if (g_uart_vaxp_mode &&
        g_system_settings.vaxp_baud_rate != baud_rate) {
        g_system_settings.vaxp_baud_rate = baud_rate;
        if (dshanpi_system_settings_save(&g_system_settings) == 0)
            settings_refresh_nav_values();
    }
    snprintf(status, sizeof(status), "%s %lu %s",
             uart_text("Connected at"),
             (unsigned long)baud_rate,
             format_index == 0 ? "8N1" :
             format_index == 1 ? "8E1" :
             format_index == 2 ? "8O1" : "8N2");
    uart_set_status(status, 0x25A55F);
    uart_log_printf("          CFG   %lu %s\n",
                    (unsigned long)baud_rate,
                    format_index == 0 ? "8N1" :
                    format_index == 1 ? "8E1" :
                    format_index == 2 ? "8O1" : "8N2");
}

static void uart_loopback_cb(lv_event_t *event)
{
    uint8_t discard[64];
    size_t written;
    (void)event;

    if (!lv_k230_touch_accept_click()) return;
    if (g_uart_lab == NULL) {
        show_toast(uart_text("UART is not connected"));
        return;
    }
    while (dshanpi_uart_lab_poll(g_uart_lab, 0) > 0) {
        size_t size = dshanpi_uart_lab_read(g_uart_lab, discard,
                                           sizeof(discard));
        if (size == 0 || size > sizeof(discard)) break;
    }
    written = dshanpi_uart_lab_write(g_uart_lab, g_uart_loopback_pattern,
                                     sizeof(g_uart_loopback_pattern));
    if (written != sizeof(g_uart_loopback_pattern)) {
        show_toast(uart_text("Unable to start loopback test"));
        return;
    }
    g_uart_tx_bytes += written;
    uart_update_counter();
    uart_log_bytes("TX", g_uart_loopback_pattern, written);
    g_uart_loopback_received = 0;
    g_uart_loopback_mismatch = false;
    g_uart_loopback_pending = true;
    g_uart_loopback_started = lv_tick_get();
    uart_set_status(uart_text("Testing IO44 -> IO45..."), 0xD68A00);
}

static void uart_editor_hide(void)
{
    uart_update_payload_preview();
    lv_obj_add_flag(g_uart_editor, LV_OBJ_FLAG_HIDDEN);
}

static void uart_editor_open_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    lv_textarea_set_cursor_pos(g_uart_editor_input, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_remove_flag(g_uart_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_uart_editor);
}

static void uart_editor_cancel_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    uart_editor_hide();
}

static void uart_editor_send_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        !lv_k230_touch_accept_click())
        return;
    if (uart_send_current(true) == 0)
        uart_editor_hide();
}

static void uart_keyboard_value_changed_cb(lv_event_t *event)
{
    lv_obj_t *keyboard = lv_event_get_current_target(event);
    uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);
    const char *key = lv_buttonmatrix_get_button_text(keyboard, button);

    if (key == NULL || !lv_k230_touch_accept_click()) return;
    if (strcmp(key, "abc") == 0)
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    else if (strcmp(key, "ABC") == 0)
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
    else if (strcmp(key, "1#") == 0)
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    else if (strcmp(key, LV_SYMBOL_BACKSPACE) == 0)
        lv_textarea_delete_char(g_uart_editor_input);
    else if (strcmp(key, LV_SYMBOL_OK) == 0)
        uart_editor_send_cb(event);
    else if (strcmp(key, LV_SYMBOL_KEYBOARD) == 0)
        uart_editor_hide();
    else
        lv_textarea_add_text(g_uart_editor_input, key);
}

static lv_obj_t *uart_chip(lv_obj_t *parent, const char *text, int x,
                           int y, int width, uint32_t color,
                           lv_obj_t **label_out)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    lv_obj_set_size(button, width, 42);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xD8E7FF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    label = make_label(button, text, &lv_font_montserrat_12, 0x243447);
    lv_obj_center(label);
    if (label_out != NULL) *label_out = label;
    return button;
}

static void uart_vaxp_reset_state(void)
{
    static uint32_t session_nonce;
    uint16_t previous_session = g_vaxp_device_session;
    uint32_t seed = (uint32_t)time(NULL) ^ lv_tick_get() ^
                    (++session_nonce * UINT32_C(0x9E3779B9));
    uint16_t session = (uint16_t)(seed ^ (seed >> 16));

    if (session == 0 || session == previous_session) {
        ++session;
        if (session == 0)
            session = 1;
    }
    vaxp_lab_parser_init(&g_vaxp_parser, VAXP_DEFAULT_MAX_PAYLOAD);
    g_vaxp_device_session = session;
    g_vaxp_next_sequence = 1;
    g_vaxp_device_sequence = 1;
    uart_vaxp_reset_host_link();
    g_vaxp_boot_session_seen = 0;
    g_vaxp_device_peer_max_rx = VAXP_DEFAULT_MAX_PAYLOAD;
    g_vaxp_device_established = false;
    memset(g_vaxp_response_cache, 0, sizeof(g_vaxp_response_cache));
    g_vaxp_response_cache_next = 0;
    memset(&g_vaxp_critical_event, 0, sizeof(g_vaxp_critical_event));
    g_vaxp_rx_frames = 0;
    g_vaxp_tx_frames = 0;
    g_vaxp_device_rx_packets = 0;
    g_vaxp_device_tx_packets = 0;
    g_vaxp_started_tick = lv_tick_get();
    g_vaxp_last_heartbeat_tick = g_vaxp_started_tick;
    uart_vaxp_update_summary();
}

static void uart_mode_apply(bool configure)
{
    for (size_t i = 0;
         i < sizeof(g_uart_raw_controls) / sizeof(g_uart_raw_controls[0]);
         ++i) {
        if (g_uart_raw_controls[i] == NULL) continue;
        if (g_uart_vaxp_mode)
            lv_obj_add_flag(g_uart_raw_controls[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(g_uart_raw_controls[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = 0;
         i < sizeof(g_uart_vaxp_controls) /
             sizeof(g_uart_vaxp_controls[0]); ++i) {
        if (g_uart_vaxp_controls[i] == NULL) continue;
        if (g_uart_vaxp_mode)
            lv_obj_remove_flag(g_uart_vaxp_controls[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_uart_vaxp_controls[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(g_vaxp_mode_label,
                      g_uart_vaxp_mode ? "VAXP 1.0" : "RAW UART");
    lv_label_set_text(g_vaxp_terminal_title,
                      g_uart_vaxp_mode ? uart_text("VAXP MONITOR")
                                       : uart_text("SERIAL MONITOR"));
    if (g_uart_vaxp_mode) {
        lv_dropdown_set_options(g_uart_baud_dropdown,
                                "115200\n460800\n921600");
        lv_dropdown_set_selected(
            g_uart_baud_dropdown,
            vaxp_baud_selection(g_system_settings.vaxp_baud_rate));
        lv_dropdown_set_selected(g_uart_format_dropdown, 0);
        uart_vaxp_reset_state();
    } else {
        lv_dropdown_set_options(g_uart_baud_dropdown,
                                "9600\n19200\n38400\n57600\n115200\n"
                                "230400\n460800\n921600");
        lv_dropdown_set_selected(g_uart_baud_dropdown, 4);
        vaxp_lab_parser_reset(&g_vaxp_parser);
    }
    if (configure) {
        uart_config_changed_cb(NULL);
        if (g_uart_vaxp_mode && g_uart_lab != NULL) {
            uart_vaxp_send_boot_event();
            uart_vaxp_send_request(VAXP_CMD_HELLO, false);
        }
    }
}

static void uart_mode_toggle_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    g_uart_vaxp_mode = !g_uart_vaxp_mode;
    uart_mode_apply(true);
    uart_log_printf("          MODE  %s\n",
                    g_uart_vaxp_mode
                        ? "VAXP 1.0 protocol / Host + Device"
                        : "RAW UART / text and HEX");
}

static void uart_lab_stop(void)
{
    if (g_uart_poll_timer != NULL) {
        lv_timer_delete(g_uart_poll_timer);
        g_uart_poll_timer = NULL;
    }
    g_uart_loopback_pending = false;
    uart_vaxp_clear_pending();
    g_vaxp_critical_event.active = false;
    dshanpi_uart_lab_close(&g_uart_lab);
}

static void uart_close_dropdowns(void)
{
    lv_obj_t *dropdowns[] = {
        g_uart_baud_dropdown,
        g_uart_format_dropdown,
        g_vaxp_command_dropdown
    };

    for (size_t index = 0;
         index < sizeof(dropdowns) / sizeof(dropdowns[0]); ++index) {
        if (dropdowns[index] != NULL &&
            lv_dropdown_is_open(dropdowns[index])) {
            lv_dropdown_close(dropdowns[index]);
        }
    }
}

static void uart_back_cb(lv_event_t *event)
{
    lv_indev_t *indev;
    (void)event;
    /* LVGL parents an opened dropdown list to the screen, so hiding the UART
     * view alone would leave the list visible over the desktop. */
    uart_close_dropdowns();
    uart_lab_stop();
    if (g_uart_wiring_panel != NULL)
        lv_obj_add_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_uart_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_uart_view, LV_OBJ_FLAG_HIDDEN);
    indev = lv_indev_active();
    if (indev != NULL) lv_indev_wait_release(indev);
}

static void uart_wiring_toggle_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click() || g_uart_wiring_panel == NULL)
        return;
    if (lv_obj_has_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_uart_wiring_panel);
        lv_obj_move_foreground(lv_event_get_target(event));
    } else {
        lv_obj_add_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_uart_view(lv_obj_t *screen)
{
    lv_obj_t *header;
    lv_obj_t *back;
    lv_obj_t *label;
    lv_obj_t *left;
    lv_obj_t *loopback;
    lv_obj_t *protocol_test;
    lv_obj_t *mode;
    lv_obj_t *clear;
    lv_obj_t *composer;
    lv_obj_t *payload;
    lv_obj_t *send;
    lv_obj_t *button;
    lv_obj_t *editor_send;
    lv_obj_t *editor_cancel;
    lv_obj_t *wiring_toggle;
    static const char *const pin_names[] = {"3V3", "TX", "RX", "GND"};
    static const char *const pin_details[] = {"PWR", "IO44", "IO45", "GND"};
    static const uint32_t pin_colors[] = {
        0xFDE2E2, 0xDCEBFF, 0xDDF4E6, 0xE5E7EB
    };

    g_uart_view = lv_obj_create(screen);
    lv_obj_set_size(g_uart_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_uart_view);
    lv_obj_set_style_bg_color(g_uart_view, lv_color_hex(0xF4F7FB), 0);
    style_plain(g_uart_view);

    header = lv_obj_create(g_uart_view);
    lv_obj_set_size(header, UI_WIDTH, 52);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    style_plain(header);
    back = create_round_button(header, 48, 0xE9EFF8);
    lv_obj_set_pos(back, 10, 2);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, uart_back_cb, LV_EVENT_PRESSED, NULL);
    label = make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20,
                       0x243447);
    lv_obj_center(label);
    label = make_label(header, "UART2  |  TX IO44  |  RX IO45",
                       &lv_font_montserrat_14, 0x41546B);
    lv_obj_set_pos(label, 72, 17);
    g_uart_status_dot = lv_obj_create(header);
    lv_obj_set_size(g_uart_status_dot, 10, 10);
    lv_obj_set_pos(g_uart_status_dot, 435, 21);
    lv_obj_set_style_radius(g_uart_status_dot, 5, 0);
    lv_obj_set_style_bg_color(g_uart_status_dot, lv_color_hex(0x98A2B3), 0);
    style_plain(g_uart_status_dot);
    g_uart_status = make_label(header, "Disconnected",
                               &lv_font_montserrat_12, 0x526172);
    lv_obj_set_pos(g_uart_status, 453, 15);
    lv_obj_set_width(g_uart_status, 176);
    lv_label_set_long_mode(g_uart_status, LV_LABEL_LONG_DOT);

    left = lv_obj_create(g_uart_view);
    lv_obj_set_size(left, 190, 382);
    lv_obj_set_pos(left, 12, 64);
    lv_obj_set_style_radius(left, 22, 0);
    lv_obj_set_style_bg_color(left, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(left, 10, 0);
    lv_obj_set_style_shadow_opa(left, LV_OPA_10, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    g_uart_baud_title_label = make_label(left, "BAUD RATE",
                                         &lv_font_montserrat_10, 0x6B778C);
    lv_obj_set_pos(g_uart_baud_title_label, 14, 10);
    g_uart_baud_dropdown = lv_dropdown_create(left);
    lv_dropdown_set_options(g_uart_baud_dropdown,
                            "115200\n460800\n921600");
    lv_dropdown_set_selected(
        g_uart_baud_dropdown,
        vaxp_baud_selection(g_system_settings.vaxp_baud_rate));
    lv_obj_set_size(g_uart_baud_dropdown, 162, 43);
    lv_obj_set_pos(g_uart_baud_dropdown, 14, 38);
    lv_obj_set_style_text_font(g_uart_baud_dropdown,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(g_uart_baud_dropdown),
                               &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(g_uart_baud_dropdown, uart_config_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    g_uart_frame_title_label = make_label(left, "FRAME",
                                          &lv_font_montserrat_10, 0x6B778C);
    lv_obj_set_pos(g_uart_frame_title_label, 14, 88);
    g_uart_format_dropdown = lv_dropdown_create(left);
    lv_dropdown_set_options(g_uart_format_dropdown, "8N1\n8E1\n8O1\n8N2");
    lv_obj_set_size(g_uart_format_dropdown, 162, 43);
    lv_obj_set_pos(g_uart_format_dropdown, 14, 116);
    lv_obj_set_style_text_font(g_uart_format_dropdown,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(g_uart_format_dropdown),
                               &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(g_uart_format_dropdown, uart_config_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    g_uart_mode_title_label = make_label(left, "MODE",
                                         &lv_font_montserrat_10, 0x6B778C);
    lv_obj_set_pos(g_uart_mode_title_label, 14, 166);
    mode = uart_chip(left, "VAXP 1.0", 14, 194, 162, 0xDCEBFF,
                     &g_vaxp_mode_label);
    lv_obj_add_event_cb(mode, uart_mode_toggle_cb, LV_EVENT_CLICKED, NULL);
    g_uart_counter = make_label(left, "TX 0 B   RX 0 B",
                                &lv_font_montserrat_12, 0x41546B);
    lv_obj_set_pos(g_uart_counter, 14, 250);

    loopback = uart_chip(left, "LOOPBACK TEST", 14, 292, 162,
                         0xE1E8F8, &g_uart_loopback_label);
    lv_obj_set_height(loopback, 44);
    lv_obj_add_event_cb(loopback, uart_loopback_cb, LV_EVENT_CLICKED, NULL);
    g_uart_raw_controls[6] = loopback;
    protocol_test = uart_chip(left, "PROTOCOL TEST", 14, 292, 162,
                              0xFFE8D6, &g_uart_protocol_test_label);
    lv_obj_set_height(protocol_test, 44);
    lv_obj_add_event_cb(protocol_test, uart_vaxp_spec_test_cb,
                        LV_EVENT_CLICKED, NULL);
    g_uart_vaxp_controls[2] = protocol_test;

    g_vaxp_session_label = NULL;
    g_vaxp_frame_counter = NULL;

    g_uart_terminal_panel = lv_obj_create(g_uart_view);
    lv_obj_set_size(g_uart_terminal_panel, 416, 264);
    lv_obj_set_pos(g_uart_terminal_panel, 212, 64);
    lv_obj_set_style_radius(g_uart_terminal_panel, 22, 0);
    lv_obj_set_style_bg_color(g_uart_terminal_panel,
                              lv_color_hex(0x101827), 0);
    lv_obj_set_style_border_width(g_uart_terminal_panel, 0, 0);
    lv_obj_set_style_pad_left(g_uart_terminal_panel, 14, 0);
    lv_obj_set_style_pad_right(g_uart_terminal_panel, 14, 0);
    lv_obj_set_style_pad_top(g_uart_terminal_panel, 44, 0);
    lv_obj_set_style_pad_bottom(g_uart_terminal_panel, 12, 0);
    lv_obj_set_scroll_dir(g_uart_terminal_panel, LV_DIR_VER);
    g_vaxp_terminal_title = make_label(g_uart_terminal_panel,
                                       "VAXP MONITOR",
                                       &lv_font_montserrat_10, 0x7DD3FC);
    lv_obj_set_pos(g_vaxp_terminal_title, 14, 14);
    clear = uart_chip(g_uart_terminal_panel, "CLEAR", 326, 7, 72,
                      0x233044, &g_uart_clear_label);
    lv_obj_set_height(clear, 34);
    lv_obj_set_style_text_color(lv_obj_get_child(clear, 0),
                                lv_color_hex(0xE5EDF8), 0);
    lv_obj_add_event_cb(clear, uart_clear_cb, LV_EVENT_CLICKED, NULL);
    g_uart_terminal = make_label(g_uart_terminal_panel,
                                 "Waiting for serial data...",
                                 &lv_font_montserrat_12, 0xD7E2F0);
    lv_obj_set_pos(g_uart_terminal, 14, 47);
    lv_obj_set_width(g_uart_terminal, 374);
    lv_obj_set_style_text_line_space(g_uart_terminal, 5, 0);
    lv_label_set_long_mode(g_uart_terminal, LV_LABEL_LONG_WRAP);

    composer = lv_obj_create(g_uart_view);
    lv_obj_set_size(composer, 416, 108);
    lv_obj_set_pos(composer, 212, 338);
    lv_obj_set_style_radius(composer, 22, 0);
    lv_obj_set_style_bg_color(composer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(composer, 10, 0);
    lv_obj_set_style_shadow_opa(composer, LV_OPA_10, 0);
    style_plain(composer);
    payload = lv_button_create(composer);
    lv_obj_set_size(payload, 282, 50);
    lv_obj_set_pos(payload, 12, 10);
    lv_obj_set_style_radius(payload, 16, 0);
    lv_obj_set_style_bg_color(payload, lv_color_hex(0xF0F3F8), 0);
    lv_obj_set_style_shadow_width(payload, 0, 0);
    lv_obj_add_event_cb(payload, uart_editor_open_cb, LV_EVENT_CLICKED, NULL);
    g_uart_raw_controls[0] = payload;
    g_uart_payload_preview = make_label(payload, "Hello from K230",
                                        &lv_font_montserrat_12, 0x334155);
    lv_obj_align(g_uart_payload_preview, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_width(g_uart_payload_preview, 248);
    lv_label_set_long_mode(g_uart_payload_preview, LV_LABEL_LONG_DOT);
    send = lv_button_create(composer);
    lv_obj_set_size(send, 98, 50);
    lv_obj_set_pos(send, 306, 10);
    lv_obj_set_style_radius(send, 17, 0);
    lv_obj_set_style_bg_color(send, lv_color_hex(0x2567B8), 0);
    lv_obj_set_style_bg_color(send, lv_color_hex(0x174F96),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(send, 0, 0);
    g_uart_send_label = make_label(send, "SEND", &lv_font_montserrat_14,
                                   0xFFFFFF);
    lv_obj_center(g_uart_send_label);
    lv_obj_add_event_cb(send, uart_send_cb, LV_EVENT_CLICKED, NULL);
    g_uart_raw_controls[1] = send;
    button = uart_chip(composer, "TEXT", 12, 66, 112, 0xEEF1F5,
                       &g_uart_hex_label);
    lv_obj_set_height(button, 36);
    lv_obj_add_event_cb(button, uart_hex_toggle_cb, LV_EVENT_CLICKED, NULL);
    g_uart_raw_controls[2] = button;
    button = uart_chip(composer, "CRLF ON", 132, 66, 124, 0xDDF4E6,
                       &g_uart_crlf_label);
    lv_obj_set_height(button, 36);
    lv_obj_add_event_cb(button, uart_crlf_toggle_cb, LV_EVENT_CLICKED, NULL);
    g_uart_raw_controls[3] = button;

    g_vaxp_command_dropdown = lv_dropdown_create(composer);
    lv_dropdown_set_options(g_vaxp_command_dropdown,
                            "HELLO\nPING\nGET DEVICE INFO\nGET CAPABILITIES\nGET STATUS\nTIME SYNC\nGET HEALTH");
    lv_obj_set_size(g_vaxp_command_dropdown, 282, 54);
    lv_obj_set_pos(g_vaxp_command_dropdown, 12, 29);
    lv_obj_set_style_text_font(g_vaxp_command_dropdown,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(g_vaxp_command_dropdown),
                               &lv_font_montserrat_14, 0);
    g_uart_vaxp_controls[0] = g_vaxp_command_dropdown;
    send = lv_button_create(composer);
    lv_obj_set_size(send, 98, 54);
    lv_obj_set_pos(send, 306, 29);
    lv_obj_set_style_radius(send, 17, 0);
    lv_obj_set_style_bg_color(send, lv_color_hex(0x2567B8), 0);
    lv_obj_set_style_bg_color(send, lv_color_hex(0x174F96),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(send, 0, 0);
    g_vaxp_send_button_label = make_label(send, "REQUEST",
                                          &lv_font_montserrat_12,
                                          0xFFFFFF);
    lv_obj_center(g_vaxp_send_button_label);
    lv_obj_add_event_cb(send, uart_vaxp_send_cb, LV_EVENT_CLICKED, NULL);
    g_uart_vaxp_controls[1] = send;

    g_uart_wiring_panel = lv_obj_create(g_uart_view);
    lv_obj_remove_style_all(g_uart_wiring_panel);
    lv_obj_set_size(g_uart_wiring_panel, 356, 108);
    lv_obj_set_pos(g_uart_wiring_panel, 142, 334);
    lv_obj_set_style_radius(g_uart_wiring_panel, 22, 0);
    lv_obj_set_style_bg_color(g_uart_wiring_panel,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_uart_wiring_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_uart_wiring_panel, 1, 0);
    lv_obj_set_style_border_color(g_uart_wiring_panel,
                                  lv_color_hex(0xCBD8E8), 0);
    lv_obj_set_style_shadow_width(g_uart_wiring_panel, 18, 0);
    lv_obj_set_style_shadow_offset_y(g_uart_wiring_panel, 4, 0);
    lv_obj_set_style_shadow_color(g_uart_wiring_panel,
                                  lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_opa(g_uart_wiring_panel, LV_OPA_20, 0);
    lv_obj_remove_flag(g_uart_wiring_panel, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t i = 0; i < sizeof(pin_names) / sizeof(pin_names[0]); ++i) {
        lv_obj_t *pin = lv_obj_create(g_uart_wiring_panel);
        lv_obj_t *pin_name;
        lv_obj_t *pin_detail;

        lv_obj_remove_style_all(pin);
        lv_obj_set_size(pin, 74, 78);
        lv_obj_set_pos(pin, 10 + (int)i * 84, 14);
        lv_obj_set_style_radius(pin, 17, 0);
        lv_obj_set_style_bg_color(pin, lv_color_hex(pin_colors[i]), 0);
        lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pin, 1, 0);
        lv_obj_set_style_border_color(pin, lv_color_hex(0xC9D4E3), 0);
        pin_name = make_label(pin, pin_names[i], &lv_font_montserrat_16,
                              i == 0 ? 0xB42318 :
                              i == 3 ? 0x344054 : 0x175CA5);
        lv_obj_align(pin_name, LV_ALIGN_TOP_MID, 0, 10);
        pin_detail = make_label(pin, pin_details[i],
                                &lv_font_montserrat_10, 0x526172);
        lv_obj_align(pin_detail, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    lv_obj_add_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN);

    wiring_toggle = uart_chip(g_uart_view, "WIRING", 264, 450, 112,
                              0xDCEBFF, &g_uart_wiring_toggle_label);
    lv_obj_set_height(wiring_toggle, 26);
    lv_obj_set_style_radius(wiring_toggle, 13, 0);
    /* Keep the compact visual pill while making the bottom-center target
     * comfortable to hit on the 480 px touch panel. */
    lv_obj_set_ext_click_area(wiring_toggle, 18);
    lv_obj_add_event_cb(wiring_toggle, uart_wiring_toggle_cb,
                        LV_EVENT_CLICKED, NULL);

    g_uart_editor = lv_obj_create(g_uart_view);
    lv_obj_set_size(g_uart_editor, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_uart_editor);
    lv_obj_set_style_bg_color(g_uart_editor, lv_color_hex(0xF4F7FB), 0);
    style_plain(g_uart_editor);
    g_uart_editor_title_label = make_label(g_uart_editor, "Compose payload",
                                           &lv_font_montserrat_20, 0x172033);
    lv_obj_set_pos(g_uart_editor_title_label, 20, 14);
    editor_cancel = uart_chip(g_uart_editor, "CANCEL", 438, 8, 84,
                              0xE9EDF3, &g_uart_editor_cancel_label);
    lv_obj_add_event_cb(editor_cancel, uart_editor_cancel_cb,
                        LV_EVENT_CLICKED, NULL);
    editor_send = uart_chip(g_uart_editor, "SEND", 530, 8, 94,
                            0xD8E7FF, &g_uart_editor_send_label);
    lv_obj_set_style_bg_color(editor_send, lv_color_hex(0x2567B8), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(editor_send, 0),
                                lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(editor_send, uart_editor_send_cb,
                        LV_EVENT_CLICKED, NULL);
    g_uart_editor_input = lv_textarea_create(g_uart_editor);
    lv_obj_set_size(g_uart_editor_input, 604, 58);
    lv_obj_set_pos(g_uart_editor_input, 18, 58);
    lv_textarea_set_one_line(g_uart_editor_input, true);
    lv_textarea_set_max_length(g_uart_editor_input, UART_MAX_PAYLOAD);
    lv_textarea_set_text(g_uart_editor_input, "Hello from K230");
    lv_textarea_set_placeholder_text(g_uart_editor_input,
                                     "Enter text payload");
    lv_obj_set_style_text_font(g_uart_editor_input,
                               &lv_font_montserrat_16, 0);
    g_uart_keyboard = lv_keyboard_create(g_uart_editor);
    lv_obj_set_size(g_uart_keyboard, 616, 338);
    lv_obj_set_pos(g_uart_keyboard, 12, 130);
    lv_keyboard_set_map(g_uart_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        g_wifi_kb_lower, g_wifi_kb_ctrl_36);
    lv_keyboard_set_map(g_uart_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        g_wifi_kb_upper, g_wifi_kb_ctrl_36);
    lv_keyboard_set_map(g_uart_keyboard, LV_KEYBOARD_MODE_NUMBER,
                        g_wifi_kb_number, g_wifi_kb_ctrl_16);
    lv_keyboard_set_map(g_uart_keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        g_wifi_kb_number, g_wifi_kb_ctrl_16);
    lv_keyboard_set_mode(g_uart_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(g_uart_keyboard, false);
    lv_keyboard_set_textarea(g_uart_keyboard, g_uart_editor_input);
    lv_obj_set_style_bg_color(g_uart_keyboard, lv_color_hex(0xD7DCE4), 0);
    lv_obj_set_style_text_font(g_uart_keyboard, &lv_font_montserrat_16,
                               LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_uart_keyboard, lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_uart_keyboard, lv_color_hex(0x2567B8),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_remove_event_cb(g_uart_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(g_uart_keyboard, uart_keyboard_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(g_uart_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_uart_view, LV_OBJ_FLAG_HIDDEN);
    uart_mode_apply(false);
}

static void uart_localize_label(lv_obj_t *label, const char *english,
                                const lv_font_t *english_font)
{
    if (label == NULL)
        return;
    lv_label_set_text(label, uart_text(english));
    lv_obj_set_style_text_font(label, uart_ui_font(english_font), 0);
}

static void uart_refresh_language(void)
{
    uart_localize_label(g_uart_baud_title_label, "BAUD RATE",
                        &lv_font_montserrat_10);
    uart_localize_label(g_uart_frame_title_label, "FRAME",
                        &lv_font_montserrat_10);
    uart_localize_label(g_uart_mode_title_label, "MODE",
                        &lv_font_montserrat_10);
    uart_localize_label(g_uart_loopback_label, "LOOPBACK TEST",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_protocol_test_label, "PROTOCOL TEST",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_wiring_toggle_label, "WIRING",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_clear_label, "CLEAR",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_send_label, "SEND",
                        &lv_font_montserrat_14);
    uart_localize_label(g_uart_hex_label,
                        g_uart_hex_mode ? "HEX" : "TEXT",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_crlf_label,
                        g_uart_crlf ? "CRLF ON" : "CRLF OFF",
                        &lv_font_montserrat_12);
    uart_localize_label(g_vaxp_send_button_label, "REQUEST",
                        &lv_font_montserrat_12);
    uart_localize_label(g_vaxp_terminal_title,
                        g_uart_vaxp_mode ? "VAXP MONITOR"
                                         : "SERIAL MONITOR",
                        &lv_font_montserrat_10);
    uart_localize_label(g_uart_editor_title_label, "Compose payload",
                        &lv_font_montserrat_20);
    uart_localize_label(g_uart_editor_cancel_label, "CANCEL",
                        &lv_font_montserrat_12);
    uart_localize_label(g_uart_editor_send_label, "SEND",
                        &lv_font_montserrat_12);
    lv_obj_set_style_text_font(g_uart_status,
                               uart_ui_font(&lv_font_montserrat_12), 0);
    lv_obj_set_style_text_font(g_uart_terminal,
                               uart_ui_font(&lv_font_montserrat_12), 0);
    lv_obj_set_style_text_font(g_uart_payload_preview,
                               uart_ui_font(&lv_font_montserrat_12), 0);
    lv_obj_set_style_text_font(g_uart_editor_input,
                               uart_ui_font(&lv_font_montserrat_16), 0);
    lv_textarea_set_placeholder_text(
        g_uart_editor_input,
        g_uart_hex_mode ? uart_text("Example: 55 AA 01 FF")
                        : uart_text("Enter text payload"));
    uart_update_payload_preview();
    uart_set_status(uart_text("Disconnected"), 0x98A2B3);
    uart_log_refresh();
}

static void show_uart_lab(void)
{
    unsigned format_index = lv_dropdown_get_selected(g_uart_format_dropdown);
    uint32_t baud_rate;
    char status[96];
    int result;

    uart_close_dropdowns();
    uart_lab_stop();
    lv_obj_add_flag(g_uart_wiring_panel, LV_OBJ_FLAG_HIDDEN);
    uart_refresh_language();
    uart_mode_apply(false);
    baud_rate = uart_selected_baud_rate();
    format_index = lv_dropdown_get_selected(g_uart_format_dropdown);
    g_uart_log_length = 0;
    g_uart_log[0] = '\0';
    g_uart_tx_bytes = 0;
    g_uart_rx_bytes = 0;
    uart_update_counter();
    uart_log_refresh();
    lv_obj_add_flag(g_uart_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_uart_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_uart_view);
    lv_refr_now(NULL);
    result = dshanpi_uart_lab_open(&g_uart_lab,
                                   baud_rate,
                                   g_uart_parities[format_index],
                                   g_uart_stop_bits[format_index]);
    if (result != 0) {
        snprintf(status, sizeof(status), "%s (%d)",
                 uart_text("Open failed"), result);
        uart_set_status(status, 0xD93025);
        uart_log_printf("          ERROR Unable to open UART route (%d)\n",
                        result);
        return;
    }
    snprintf(status, sizeof(status), "%s %lu %s",
             uart_text("Connected at"),
             (unsigned long)baud_rate,
             format_index == 0 ? "8N1" :
             format_index == 1 ? "8E1" :
             format_index == 2 ? "8O1" : "8N2");
    uart_set_status(status, 0x25A55F);
    uart_log_printf("          READY UART2 TX/IO44  RX/IO45\n");
    if (g_uart_vaxp_mode) {
        uart_log_printf("          VAXP  1.0 Core / Host + Device / %lu 8N1\n",
                        (unsigned long)baud_rate);
        uart_log_printf("          TEST  official wire vectors: %s\n",
                        vaxp_lab_self_test() == 0 ? "PASS" : "FAIL");
        uart_log_printf("          TIP   Attach a VAXP module, or jumper IO44 to IO45\n");
        uart_vaxp_send_boot_event();
        uart_vaxp_send_request(VAXP_CMD_HELLO, false);
    } else {
        uart_log_printf("          TIP   Loopback: connect IO44 to IO45\n");
    }
    g_uart_poll_timer = lv_timer_create(uart_poll_timer_cb, 30, NULL);
}

static void *wifi_worker(void *argument)
{
    int operation = (int)(intptr_t)argument;
    if (operation == 1) {
        int count = 0;
        g_wifi_result = netmgmt_wlan_sta_scan(&count, g_wifi_aps);
        g_wifi_ap_count =
            count < RT_WLAN_STA_SCAN_MAX_AP ? count : RT_WLAN_STA_SCAN_MAX_AP;
    } else if (operation == 2) {
        g_wifi_result = netmgmt_wlan_sta_connect_with_scan_info(
            &g_wifi_aps[g_wifi_selected_ap], g_wifi_connect_password);
        if (g_wifi_result == 0) {
            netmgmt_wlan_sta_set_auto_reconnect(
                g_system_settings.wifi_auto_connect);
        }
    } else {
        int count = 0;
        g_wifi_result = netmgmt_wlan_sta_scan(&count, g_wifi_aps);
        g_wifi_ap_count = count < RT_WLAN_STA_SCAN_MAX_AP
                              ? count : RT_WLAN_STA_SCAN_MAX_AP;
        g_wifi_selected_ap = -1;
        if (g_wifi_result == 0) {
            for (int i = 0; i < g_wifi_ap_count; ++i) {
                g_wifi_aps[i].ssid.val[RT_WLAN_SSID_MAX_LENGTH] = '\0';
                if (strcmp((char *)g_wifi_aps[i].ssid.val,
                           g_system_settings.wifi_ssid) == 0) {
                    g_wifi_selected_ap = i;
                    break;
                }
            }
            if (g_wifi_selected_ap < 0) {
                g_wifi_result = -1;
            } else {
                g_wifi_result = netmgmt_wlan_sta_connect_with_scan_info(
                    &g_wifi_aps[g_wifi_selected_ap],
                    g_system_settings.wifi_password);
                if (g_wifi_result == 0)
                    netmgmt_wlan_sta_set_auto_reconnect(
                        g_system_settings.wifi_auto_connect);
            }
        }
    }
    pthread_mutex_lock(&g_wifi_lock);
    g_wifi_completed_operation = operation;
    pthread_mutex_unlock(&g_wifi_lock);
    return NULL;
}

static void wifi_start_connection(const char *password)
{
    pthread_t thread;
    size_t length;
    if (g_wifi_selected_ap < 0 || g_wifi_selected_ap >= g_wifi_ap_count)
        return;
    length = strlen(password);
    if (g_wifi_aps[g_wifi_selected_ap].security != SECURITY_OPEN &&
        length < 8) {
        show_toast(settings_text(
            "Wi-Fi password must contain at least 8 characters"));
        return;
    }
    if (!wifi_begin_operation(2)) {
        show_toast(settings_text("Wi-Fi is busy"));
        return;
    }
    snprintf(g_wifi_connect_password, sizeof(g_wifi_connect_password), "%s",
             password);
    lv_obj_add_flag(g_wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_wifi_status, settings_text("Authenticating..."));
    if (pthread_create(&thread, NULL, wifi_worker,
                       (void *)(intptr_t)2) != 0) {
        wifi_cancel_operation_start();
        memset(g_wifi_connect_password, 0,
               sizeof(g_wifi_connect_password));
        lv_label_set_text(g_wifi_status,
                          settings_text("Unable to start connection"));
        return;
    }
    pthread_detach(thread);
}

static void wifi_select_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    char title[128];
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    if (index < 0 || index >= g_wifi_ap_count) {
        return;
    }
    g_wifi_selected_ap = index;
    if (g_wifi_forget_button != NULL) {
        if (strcmp((char *)g_wifi_aps[index].ssid.val,
                   g_system_settings.wifi_ssid) == 0)
            lv_obj_remove_flag(g_wifi_forget_button, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_wifi_forget_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_wifi_aps[index].security == SECURITY_OPEN) {
        wifi_start_connection("");
        return;
    }
    snprintf(title, sizeof(title), settings_text("Connect to %s"),
             (char *)g_wifi_aps[index].ssid.val);
    lv_label_set_text(g_wifi_dialog_title, title);
    lv_textarea_set_text(
        g_wifi_password,
        strcmp((char *)g_wifi_aps[index].ssid.val,
               g_system_settings.wifi_ssid) == 0
            ? g_system_settings.wifi_password : "");
    lv_textarea_set_password_mode(g_wifi_password, true);
    lv_label_set_text(g_wifi_password_visibility_icon, LV_SYMBOL_EYE_CLOSE);
    lv_obj_remove_flag(g_wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_wifi_dialog);
}

static void wifi_refresh_list(void)
{
    lv_obj_clean(g_wifi_list);
    if (g_wifi_result != 0) {
        lv_label_set_text(g_wifi_status,
                          settings_text("Scan failed - check Wi-Fi module"));
        return;
    }
    char status[96];
    snprintf(status, sizeof(status), settings_text("%d available networks"),
             g_wifi_ap_count);
    if (g_wifi_ip[0] == '\0') {
        lv_label_set_text(g_wifi_status, status);
    }
    for (int i = 0; i < g_wifi_ap_count; ++i) {
        if (g_wifi_aps[i].ssid.len == 0) {
            continue;
        }
        g_wifi_aps[i].ssid.val[RT_WLAN_SSID_MAX_LENGTH] = '\0';
        lv_obj_t *row = lv_button_create(g_wifi_list);
        lv_obj_set_size(row, 540, 58);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xE5E5EA),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xD1D1D6), 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 14, 0);
        lv_obj_add_event_cb(row, tap_guard_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(row, wifi_select_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *ssid = make_label(row, (char *)g_wifi_aps[i].ssid.val,
                                    &lv_font_montserrat_16, 0x1C1C1E);
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 0, 0);
        char detail[48];
        snprintf(detail, sizeof(detail), "%s%s  " LV_SYMBOL_WIFI "  " LV_SYMBOL_RIGHT,
                 strcmp((char *)g_wifi_aps[i].ssid.val,
                        g_system_settings.wifi_ssid) == 0 ? LV_SYMBOL_OK "  " : "",
                 g_wifi_aps[i].security == SECURITY_OPEN ? "" : LV_SYMBOL_EYE_CLOSE);
        lv_obj_t *signal = make_label(row, detail, &lv_font_montserrat_12,
                                      0x8E8E93);
        lv_obj_align(signal, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void wifi_update_network_status(void)
{
    struct ifconfig_t config;
    char next_ip[sizeof(g_wifi_ip)] = "";

    memset(&config, 0, sizeof(config));
    /* A valid interface address is authoritative even if the WLAN status
     * query briefly lags behind DHCP or reports an implementation error. */
    if (netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) == 0 &&
        config.ip.addr != 0) {
        const uint8_t *octet = (const uint8_t *)&config.ip.addr;
        snprintf(next_ip, sizeof(next_ip), "%u.%u.%u.%u",
                 octet[0], octet[1], octet[2], octet[3]);
        if (strcmp(g_wifi_ip, next_ip) != 0)
            snprintf(g_wifi_ip, sizeof(g_wifi_ip), "%s", next_ip);
        if (g_wifi_status != NULL) {
            const char *status = g_system_settings.wifi_ssid[0] != '\0'
                                     ? g_system_settings.wifi_ssid
                                     : settings_text("Connected");
            if (strcmp(lv_label_get_text(g_wifi_status), status) != 0)
                lv_label_set_text(g_wifi_status, status);
        }
        if (g_wifi_ip_label != NULL) {
            char text[80];
            snprintf(text, sizeof(text), settings_text("IP Address  %s"),
                     g_wifi_ip);
            if (strcmp(lv_label_get_text(g_wifi_ip_label), text) != 0)
                lv_label_set_text(g_wifi_ip_label, text);
        }
    } else {
        if (g_wifi_ip[0] != '\0')
            g_wifi_ip[0] = '\0';
        if (g_wifi_ip_label != NULL) {
            const char *status = settings_text("Not Connected");
            if (strcmp(lv_label_get_text(g_wifi_ip_label), status) != 0)
                lv_label_set_text(g_wifi_ip_label, status);
        }
    }
}

static bool wifi_network_ready(void)
{
    int connected = 0;
    struct ifconfig_t config;
    if (netmgmt_wlan_sta_isconnected(&connected) == 0 && connected)
        return true;
    memset(&config, 0, sizeof(config));
    return netmgmt_utils_get_ifconfig(RT_NET_DEV_WLAN_STA, &config) == 0 &&
           config.ip.addr != 0;
}

static bool ui_pointer_motion_active(void)
{
    if (g_touch_indev == NULL)
        return false;
    return lv_indev_get_state(g_touch_indev) == LV_INDEV_STATE_PRESSED ||
           lv_indev_get_scroll_obj(g_touch_indev) != NULL;
}

static void desktop_status_update(void)
{
    char text[32];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(text, sizeof(text), "%H:%M", &local);
    if (g_desktop_time_status != NULL &&
        strcmp(lv_label_get_text(g_desktop_time_status), text) != 0)
        lv_label_set_text(g_desktop_time_status, text);
    if (g_desktop_network_status != NULL) {
        const char *status = LV_SYMBOL_WIFI "   " LV_SYMBOL_DRIVE "   "
                             LV_SYMBOL_VIDEO;
        if (strcmp(lv_label_get_text(g_desktop_network_status), status) != 0)
            lv_label_set_text(g_desktop_network_status, status);
    }
    if (g_desktop_ip_status != NULL) {
        if (g_wifi_ip[0] != '\0') {
            bool text_changed =
                strcmp(lv_label_get_text(g_desktop_ip_status),
                       g_wifi_ip) != 0;
            bool was_hidden = lv_obj_has_flag(g_desktop_ip_status,
                                               LV_OBJ_FLAG_HIDDEN);
            if (text_changed)
                lv_label_set_text(g_desktop_ip_status, g_wifi_ip);
            if (was_hidden)
                lv_obj_remove_flag(g_desktop_ip_status,
                                   LV_OBJ_FLAG_HIDDEN);
            if (text_changed || was_hidden)
                lv_obj_align_to(g_desktop_ip_status,
                                g_desktop_network_status,
                                LV_ALIGN_OUT_LEFT_MID, -12, 0);
        } else {
            if (lv_label_get_text(g_desktop_ip_status)[0] != '\0')
                lv_label_set_text(g_desktop_ip_status, "");
            if (!lv_obj_has_flag(g_desktop_ip_status,
                                 LV_OBJ_FLAG_HIDDEN))
                lv_obj_add_flag(g_desktop_ip_status,
                                LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void wifi_timer_cb(lv_timer_t *timer)
{
    static unsigned network_poll;
    bool pointer_motion;
    int completed;
    int result;
    pthread_mutex_lock(&g_wifi_lock);
    completed = g_wifi_completed_operation;
    result = g_wifi_result;
    if (completed != 0) {
        g_wifi_completed_operation = 0;
        g_wifi_operation = 0;
    }
    pthread_mutex_unlock(&g_wifi_lock);
    (void)timer;
    if (completed != 0) {
        if (completed == 1) {
            wifi_scan_animation_stop();
            wifi_refresh_list();
        } else if (result == 0) {
            int connected = 0;
            netmgmt_wlan_sta_isconnected(&connected);
            lv_label_set_text(g_wifi_status,
                              settings_text(connected ? "Connected"
                                                      : "Connection started"));
            if (g_wifi_selected_ap >= 0) {
                snprintf(g_system_settings.wifi_ssid,
                         sizeof(g_system_settings.wifi_ssid), "%s",
                         (char *)g_wifi_aps[g_wifi_selected_ap].ssid.val);
            }
            if (completed == 2) {
                snprintf(g_system_settings.wifi_password,
                         sizeof(g_system_settings.wifi_password), "%s",
                         g_wifi_connect_password);
                dshanpi_system_settings_save(&g_system_settings);
            }
            if (completed == 2)
                show_toast(settings_text("Wi-Fi connected"));
            g_wifi_auto_failures = 0;
            /* Allow DHCP to finish before considering another reconnect. */
            g_wifi_retry_after = time(NULL) + 15;
        } else {
            lv_label_set_text(g_wifi_status,
                              settings_text("Connection failed"));
            if (completed == 3) {
                unsigned delay = 30U << (g_wifi_auto_failures > 4
                                             ? 4 : g_wifi_auto_failures);
                ++g_wifi_auto_failures;
                g_wifi_retry_after = time(NULL) + delay;
            } else {
                show_toast(settings_text(
                    "Wrong password or connection failed"));
            }
        }
        if (completed == 2)
            memset(g_wifi_connect_password, 0,
                   sizeof(g_wifi_connect_password));
    }
    if (g_wifi_scan_requested && !wifi_is_busy())
        wifi_scan_start();
    pointer_motion = ui_pointer_motion_active();
    if (network_poll < 2)
        ++network_poll;
    /* Network ioctls, settings labels and OTA snapshots are housekeeping,
     * not input work.  Defer them while a finger or momentum owns a scroll
     * object so they cannot insert a periodic stall into a desktop swipe. */
    if (network_poll >= 2 && !pointer_motion) {
        network_poll = 0;
        wifi_update_network_status();
        desktop_status_update();
        wifi_autoconnect_start();
    }
    if (!pointer_motion && g_date_status != NULL) {
        time_t now = time(NULL);
        struct tm local;
        char text[96];
        localtime_r(&now, &local);
        strftime(text, sizeof(text), "%Y-%m-%d  %H:%M:%S", &local);
        if (strcmp(lv_label_get_text(g_date_status), text) != 0)
            lv_label_set_text(g_date_status, text);
    }
    if (!pointer_motion)
        ota_refresh_ui();
}

static void wifi_scan_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    g_wifi_scan_requested = true;
    wifi_scan_animation_start();
    lv_label_set_text(g_wifi_status, settings_text("Scanning..."));
    wifi_scan_start();
}

static void wifi_scan_start(void)
{
    pthread_t thread;
    if (!g_wifi_scan_requested || !wifi_begin_operation(1)) return;
    g_wifi_scan_requested = false;
    wifi_scan_animation_start();
    lv_label_set_text(g_wifi_status, settings_text("Scanning..."));
    if (pthread_create(&thread, NULL, wifi_worker,
                       (void *)(intptr_t)1) != 0) {
        wifi_cancel_operation_start();
        wifi_scan_animation_stop();
        lv_label_set_text(g_wifi_status,
                          settings_text("Unable to start scan"));
        return;
    }
    pthread_detach(thread);
}

static void wifi_autoconnect_start(void)
{
    pthread_t thread;
    if (g_wifi_panel_active || g_wifi_scan_requested ||
        !g_system_settings.wifi_auto_connect ||
        g_system_settings.wifi_ssid[0] == '\0' ||
        time(NULL) < g_wifi_retry_after ||
        wifi_network_ready())
        return;
    if (!wifi_begin_operation(3)) return;
    if (g_wifi_status != NULL)
        lv_label_set_text(g_wifi_status,
                          settings_text("Reconnecting saved Wi-Fi..."));
    if (pthread_create(&thread, NULL, wifi_worker,
                       (void *)(intptr_t)3) != 0) {
        wifi_cancel_operation_start();
        return;
    }
    pthread_detach(thread);
}

static void wifi_dialog_close_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    lv_obj_add_flag(g_wifi_dialog, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_password_visibility_cb(lv_event_t *event)
{
    lv_obj_t *icon = lv_event_get_user_data(event);
    bool hidden;

    if (!lv_k230_touch_accept_click()) {
        return;
    }
    hidden = lv_textarea_get_password_mode(g_wifi_password);
    lv_textarea_set_password_mode(g_wifi_password, !hidden);
    lv_label_set_text(icon, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    lv_obj_add_state(g_wifi_password, LV_STATE_FOCUSED);
}

static void wifi_connect_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        !lv_k230_touch_accept_click()) {
        return;
    }
    wifi_start_connection(lv_textarea_get_text(g_wifi_password));
}

static void wifi_keyboard_value_changed_cb(lv_event_t *event)
{
    lv_obj_t *keyboard = lv_event_get_current_target(event);
    uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);
    const char *key = lv_buttonmatrix_get_button_text(keyboard, button);
#if WIFI_KEYBOARD_DEBUG
    uint32_t now = lv_tick_get();
    const char *kind = strcmp(key ? key : "", LV_SYMBOL_BACKSPACE) == 0
                           ? "backspace"
                       : strcmp(key ? key : "", LV_SYMBOL_OK) == 0
                           ? "ok"
                       : (key && (!strcmp(key, "abc") || !strcmp(key, "ABC") ||
                                  !strcmp(key, "1#") ||
                                  !strcmp(key, LV_SYMBOL_KEYBOARD)))
                           ? "mode"
                           : "character";
#endif

    if (key == NULL) return;

    /*
     * The keyboard's control map emits VALUE_CHANGED only on RELEASE.  Accept
     * exactly one completed touch gesture before changing the password; noisy
     * DOWN/UP bursts and long-press repeats therefore cannot edit it twice.
     */
    if (!lv_k230_touch_accept_click()) {
#if WIFI_KEYBOARD_DEBUG
        printf("[wifi-kbd] ignored button=%u kind=%s tick=%u "
               "reason=no-new-tap length=%u\n",
               (unsigned)button, kind, (unsigned)now,
               (unsigned)strlen(lv_textarea_get_text(g_wifi_password)));
#endif
        return;
    }

    if (strcmp(key, "abc") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    }
    else if (strcmp(key, "ABC") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
    }
    else if (strcmp(key, "1#") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    }
    else if (strcmp(key, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(g_wifi_password);
    }
    else if (strcmp(key, LV_SYMBOL_OK) == 0) {
        lv_obj_send_event(g_wifi_password, LV_EVENT_READY, NULL);
    }
    else if (strcmp(key, LV_SYMBOL_KEYBOARD) != 0) {
        lv_textarea_add_text(g_wifi_password, key);
    }

#if WIFI_KEYBOARD_DEBUG
    printf("[wifi-kbd] accepted button=%u kind=%s tick=%u length=%u\n",
           (unsigned)button, kind, (unsigned)now,
           (unsigned)strlen(lv_textarea_get_text(g_wifi_password)));
#endif
}

static void wifi_password_changed_debug_cb(lv_event_t *event)
{
    (void)event;
#if WIFI_KEYBOARD_DEBUG
    printf("[wifi-kbd] textarea changed tick=%u length=%u\n",
           (unsigned)lv_tick_get(),
           (unsigned)strlen(lv_textarea_get_text(g_wifi_password)));
#endif
}

static void wifi_forget_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    if (wifi_is_busy()) {
        show_toast(settings_text("Wait for the current Wi-Fi operation"));
        return;
    }
    netmgmt_wlan_sta_set_auto_reconnect(0);
    netmgmt_wlan_sta_disconnect_ap();
    memset(g_system_settings.wifi_ssid, 0,
           sizeof(g_system_settings.wifi_ssid));
    memset(g_system_settings.wifi_password, 0,
           sizeof(g_system_settings.wifi_password));
    memset(g_wifi_connect_password, 0, sizeof(g_wifi_connect_password));
    g_wifi_auto_failures = 0;
    g_wifi_retry_after = 0;
    dshanpi_system_settings_save(&g_system_settings);
    lv_obj_add_flag(g_wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_wifi_status, settings_text("Saved network removed"));
    show_toast(settings_text("Wi-Fi network forgotten"));
}

static void wifi_auto_connect_changed_cb(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    g_system_settings.wifi_auto_connect =
        lv_obj_has_state(toggle, LV_STATE_CHECKED) ? 1 : 0;
    dshanpi_system_settings_save(&g_system_settings);
    netmgmt_wlan_sta_set_auto_reconnect(
        g_system_settings.wifi_auto_connect);
    g_wifi_auto_failures = 0;
    g_wifi_retry_after = 0;
    if (g_system_settings.wifi_auto_connect)
        wifi_autoconnect_start();
    show_toast(settings_text(g_system_settings.wifi_auto_connect
                                 ? "Auto-Join enabled"
                                 : "Auto-Join disabled"));
}

static void settings_apply_language(unsigned selected)
{
    dshanpi_language_t previous_pending_language;

    if (selected >= DSHANPI_LANG_COUNT)
        selected = DSHANPI_LANG_EN;
    previous_pending_language = g_system_settings.pending_language;
    g_system_settings.pending_language = (dshanpi_language_t)selected;
    if (dshanpi_system_settings_save(&g_system_settings) != 0) {
        g_system_settings.pending_language = previous_pending_language;
        show_toast(settings_text("Failed to save language setting"));
        return;
    }
    settings_refresh_nav_values();
    if ((dshanpi_language_t)selected == g_system_settings.language) {
        show_toast(settings_text("Language is already active"));
        return;
    }
    settings_reboot_dialog_open(SETTINGS_REBOOT_LANGUAGE);
}

static unsigned vaxp_baud_selection(uint32_t baud_rate)
{
    for (unsigned index = 0;
         index < sizeof(g_vaxp_baud_rates) /
                     sizeof(g_vaxp_baud_rates[0]);
         ++index) {
        if (g_vaxp_baud_rates[index] == baud_rate)
            return index;
    }
    return 0;
}

static const char *vaxp_baud_options_text(void)
{
    switch (g_system_settings.language) {
    case DSHANPI_LANG_ZH_CN:
        return "115200 · 兼容模式\n460800 · 平衡模式\n921600 · 高速模式";
    case DSHANPI_LANG_ZH_TW:
        return "115200 · 相容模式\n460800 · 平衡模式\n921600 · 高速模式";
    case DSHANPI_LANG_JA:
        return "115200 · 互換モード\n460800 · バランス\n921600 · 高速モード";
    default:
        return "115200 · Compatibility\n460800 · Balanced\n921600 · High speed";
    }
}

static void settings_apply_vaxp_baud(unsigned selected)
{
    if (selected >= sizeof(g_vaxp_baud_rates) /
                    sizeof(g_vaxp_baud_rates[0]))
        selected = 0;
    g_system_settings.vaxp_baud_rate = g_vaxp_baud_rates[selected];
    if (dshanpi_system_settings_save(&g_system_settings) != 0) {
        show_toast(settings_text("VAXP baud save failed"));
        return;
    }
    if (g_uart_baud_dropdown != NULL && g_uart_vaxp_mode)
        lv_dropdown_set_selected(g_uart_baud_dropdown, selected);
    settings_refresh_nav_values();
    show_toast(settings_text("VAXP baud rate saved"));
}

static const char *const g_sleep_timeout_names[DSHANPI_LANG_COUNT][7] = {
    {"从不", "30 秒", "1 分钟", "2 分钟", "5 分钟", "10 分钟", "30 分钟"},
    {"永不", "30 秒", "1 分鐘", "2 分鐘", "5 分鐘", "10 分鐘", "30 分鐘"},
    {"Never", "30 seconds", "1 minute", "2 minutes", "5 minutes",
     "10 minutes", "30 minutes"},
    {"なし", "30秒", "1分", "2分", "5分", "10分", "30分"},
};

static unsigned sleep_timeout_selection(uint32_t timeout_seconds)
{
    for (unsigned index = 0;
         index < sizeof(g_sleep_timeout_values) /
                     sizeof(g_sleep_timeout_values[0]);
         ++index) {
        if (g_sleep_timeout_values[index] == timeout_seconds)
            return index;
    }
    return 4;
}

static const char *sleep_timeout_name(uint32_t timeout_seconds)
{
    dshanpi_language_t language = g_system_settings.language;
    unsigned selected = sleep_timeout_selection(timeout_seconds);

    if (language < 0 || language >= DSHANPI_LANG_COUNT)
        language = DSHANPI_LANG_EN;
    return g_sleep_timeout_names[language][selected];
}

static const char *sleep_timeout_options_text(void)
{
    switch (g_system_settings.language) {
    case DSHANPI_LANG_ZH_CN:
        return "从不\n30 秒\n1 分钟\n2 分钟\n5 分钟\n10 分钟\n30 分钟";
    case DSHANPI_LANG_ZH_TW:
        return "永不\n30 秒\n1 分鐘\n2 分鐘\n5 分鐘\n10 分鐘\n30 分鐘";
    case DSHANPI_LANG_JA:
        return "なし\n30秒\n1分\n2分\n5分\n10分\n30分";
    default:
        return "Never\n30 seconds\n1 minute\n2 minutes\n5 minutes\n"
               "10 minutes\n30 minutes";
    }
}

static void settings_apply_sleep_timeout(unsigned selected)
{
    if (selected >= sizeof(g_sleep_timeout_values) /
                    sizeof(g_sleep_timeout_values[0])) {
        selected = 4;
    }
    g_system_settings.sleep_timeout_seconds =
        g_sleep_timeout_values[selected];
    if (dshanpi_system_settings_save(&g_system_settings) != 0) {
        show_toast(settings_text("Sleep timer save failed"));
        return;
    }
    if (g_display != NULL)
        lv_display_trigger_activity(g_display);
    settings_refresh_nav_values();
    show_toast(settings_text("Sleep timer saved"));
}

static void settings_apply_timezone(unsigned selected)
{
    if (selected >= 6)
        selected = 4;
    g_system_settings.timezone_index = (int)selected;
    dshanpi_system_settings_apply_timezone(&g_system_settings);
    dshanpi_system_settings_save(&g_system_settings);
    settings_refresh_nav_values();
    show_toast(settings_text("Time zone applied"));
}

/* The final RTMP enum value is retained only to migrate existing settings.
 * Present one Network Camera choice and map the compact UI list explicitly,
 * so all later startup entries keep their correct enum values. */
static const dshanpi_autostart_t g_autostart_ui_options[] = {
    DSHANPI_AUTOSTART_NONE,
    DSHANPI_AUTOSTART_FACE_STUDIO,
    DSHANPI_AUTOSTART_FACE_GEOMETRY,
    DSHANPI_AUTOSTART_HAND_STUDIO,
    DSHANPI_AUTOSTART_HUMAN_STUDIO,
    DSHANPI_AUTOSTART_SMART_DRIVING,
    DSHANPI_AUTOSTART_OCR_DETECTION,
    DSHANPI_AUTOSTART_OBJECT_DETECTION,
    DSHANPI_AUTOSTART_YOLO_MODELS,
    DSHANPI_AUTOSTART_RTSP_STREAM,
    DSHANPI_AUTOSTART_CV_LITE,
    DSHANPI_AUTOSTART_PLATE_OCR,
    DSHANPI_AUTOSTART_CODE_SCANNER,
    DSHANPI_AUTOSTART_SELF_LEARNING,
    DSHANPI_AUTOSTART_UVC_CAMERA,
    DSHANPI_AUTOSTART_SETTINGS,
    DSHANPI_AUTOSTART_CAMERA,
    DSHANPI_AUTOSTART_GALLERY,
    DSHANPI_AUTOSTART_DRAWING,
    DSHANPI_AUTOSTART_UART_LAB,
    DSHANPI_AUTOSTART_DUAL_CAMERA,
};

static const char *const g_autostart_ui_names[DSHANPI_LANG_COUNT][21] = {
    {"桌面", "人脸工作室", "人脸几何", "手部工作室", "人体工作室",
     "智能驾驶", "OCR 文字检测", "目标检测", "YOLO 多版本", "网络摄像机",
     "CV Lite", "车牌 OCR", "条码处理", "AI 自学习", "USB 相机", "设置",
     "相机", "图库", "画板", "UART 调试器", "双摄相机"},
    {"桌面", "人臉工作室", "人臉幾何", "手部工作室", "人體工作室",
     "智慧駕駛", "OCR 文字辨識", "目標偵測", "YOLO 多版本", "網路攝影機",
     "CV Lite", "車牌 OCR", "條碼處理", "AI 自學習", "USB 相機", "設定",
     "相機", "相簿", "畫板", "UART 調試器", "雙攝相機"},
    {"Desktop", "Face Studio", "Face Geometry", "Hand Studio",
     "Human Studio", "Smart Driving", "OCR Detection", "Object Detection",
     "YOLO Models", "Network Camera", "CV Lite", "Plate OCR",
     "Code Scanner", "AI Learning", "USB Camera", "Settings", "Camera",
     "Gallery", "Drawing", "UART Lab", "Dual Camera"},
    {"デスクトップ", "顔スタジオ", "顔ジオメトリ", "手スタジオ",
     "人体スタジオ", "スマート運転", "OCR文字認識", "物体検出",
     "YOLOモデル", "ネットワークカメラ", "CV Lite", "ナンバーOCR",
     "コード読取", "AI学習", "USBカメラ", "設定", "カメラ",
     "ギャラリー", "お絵描き", "UARTラボ", "デュアルカメラ"},
};

static unsigned autostart_ui_selection(dshanpi_autostart_t value)
{
    if (value == DSHANPI_AUTOSTART_RTMP_STREAM) {
        value = DSHANPI_AUTOSTART_RTSP_STREAM;
    }
    for (unsigned index = 0;
         index < sizeof(g_autostart_ui_options) /
                     sizeof(g_autostart_ui_options[0]);
         ++index) {
        if (g_autostart_ui_options[index] == value) {
            return index;
        }
    }
    return 0;
}

static const char *settings_autostart_name(dshanpi_autostart_t value)
{
    unsigned selected = autostart_ui_selection(value);
    dshanpi_language_t language = g_system_settings.language;

    if (language < 0 || language >= DSHANPI_LANG_COUNT)
        language = DSHANPI_LANG_EN;
    return g_autostart_ui_names[language][selected];
}

static const char *settings_autostart_options_text(void)
{
    static char options[1024];
    dshanpi_language_t language = g_system_settings.language;
    size_t used = 0;

    if (language < 0 || language >= DSHANPI_LANG_COUNT)
        language = DSHANPI_LANG_EN;
    options[0] = '\0';
    for (size_t i = 0;
         i < sizeof(g_autostart_ui_options) /
                 sizeof(g_autostart_ui_options[0]);
         ++i) {
        int written = snprintf(options + used, sizeof(options) - used,
                               "%s%s", i == 0 ? "" : "\n",
                               g_autostart_ui_names[language][i]);
        if (written < 0 || (size_t)written >= sizeof(options) - used)
            break;
        used += (size_t)written;
    }
    return options;
}

static const char *settings_timezone_options_text(void)
{
    switch (g_system_settings.language) {
    case DSHANPI_LANG_ZH_CN:
        return "UTC-08:00 太平洋时间\nUTC-05:00 美东时间\n"
               "UTC+00:00 伦敦\nUTC+01:00 中欧\n"
               "UTC+08:00 北京/台北\nUTC+09:00 东京";
    case DSHANPI_LANG_ZH_TW:
        return "UTC-08:00 太平洋時間\nUTC-05:00 美東時間\n"
               "UTC+00:00 倫敦\nUTC+01:00 中歐\n"
               "UTC+08:00 北京/台北\nUTC+09:00 東京";
    case DSHANPI_LANG_JA:
        return "UTC-08:00 太平洋時間\nUTC-05:00 米国東部時間\n"
               "UTC+00:00 ロンドン\nUTC+01:00 中央ヨーロッパ\n"
               "UTC+08:00 北京/台北\nUTC+09:00 東京";
    default:
        return "UTC-08:00 Pacific\nUTC-05:00 Eastern\n"
               "UTC+00:00 London\nUTC+01:00 Central Europe\n"
               "UTC+08:00 Beijing/Taipei\nUTC+09:00 Tokyo";
    }
}

static void settings_apply_autostart(unsigned selected)
{
    if (selected >= sizeof(g_autostart_ui_options) /
                        sizeof(g_autostart_ui_options[0])) {
        selected = 0;
    }
    g_system_settings.autostart = g_autostart_ui_options[selected];
    dshanpi_system_settings_save(&g_system_settings);
    settings_refresh_nav_values();
    show_toast(settings_text("Startup app saved"));
}

static const char *settings_timezone_summary(int index)
{
    static const char *const summaries[] = {
        "UTC-08:00", "UTC-05:00", "UTC+00:00",
        "UTC+01:00", "UTC+08:00", "UTC+09:00",
    };

    if (index < 0 || index >= (int)(sizeof(summaries) /
                                    sizeof(summaries[0]))) {
        index = 4;
    }
    return summaries[index];
}

static const char *settings_language_summary(dshanpi_language_t language)
{
    static const char *const summaries[] = {
        "简体中文", "繁體中文", "English", "日本語",
    };

    if (language < 0 || language >= DSHANPI_LANG_COUNT) {
        language = DSHANPI_LANG_EN;
    }
    return summaries[language];
}

static void settings_refresh_nav_values(void)
{
    char baud_summary[32];
    const char *wifi_summary;
    const lv_font_t *value_font;
    int saved_camera_csi;

    if (g_settings_nav_values[0] == NULL) {
        return;
    }

    wifi_summary = g_system_settings.wifi_ssid[0] != '\0'
                       ? g_system_settings.wifi_ssid
                       : settings_text("Not connected");
    value_font = settings_ui_font(&lv_font_montserrat_12);
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        if (g_settings_nav_values[i] != NULL)
            lv_obj_set_style_text_font(g_settings_nav_values[i],
                                       value_font, 0);
    }
    saved_camera_csi = dshanpi_camera_setting_load();
    snprintf(baud_summary, sizeof(baud_summary), settings_text("%lu baud"),
             (unsigned long)g_system_settings.vaxp_baud_rate);

    lv_label_set_text(g_settings_nav_values[0], wifi_summary);
    lv_label_set_text(g_settings_nav_values[1],
                      settings_language_summary(
                          g_system_settings.pending_language));
    lv_label_set_text(g_settings_nav_values[2],
                      settings_timezone_summary(g_system_settings.timezone_index));
    lv_label_set_text(g_settings_nav_values[3],
                      settings_autostart_name(g_system_settings.autostart));
    lv_label_set_text(g_settings_nav_values[4],
                      settings_camera_name(saved_camera_csi));
    lv_label_set_text(g_settings_nav_values[5], baud_summary);
    lv_label_set_text(g_settings_nav_values[6],
                      settings_text("A/B protected OTA"));
    lv_label_set_text(g_settings_nav_values[7],
                      sleep_timeout_name(
                          g_system_settings.sleep_timeout_seconds));
    lv_label_set_text(g_settings_nav_values[8],
                      settings_text("Restart, flash, or shut down"));
    lv_label_set_text(g_settings_nav_values[9],
                      settings_text("DongshanPI CanMV-K230"));
}

static lv_obj_t *settings_section(lv_obj_t *parent, int section,
                                  int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 596, height);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(
        card, lv_color_hex(g_settings_border_colors[section]), 0);
    lv_obj_set_style_bg_color(
        card, lv_color_hex(g_settings_card_colors[section]), 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_offset_y(card, 3, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x6B7280), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void settings_picker_x_anim_cb(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void settings_picker_refresh_current(void)
{
    char value[160];

    lv_roller_get_selected_str(g_settings_picker_roller, value,
                               sizeof(value));
    lv_label_set_text(g_settings_picker_current, value);
}

static void settings_picker_changed_cb(lv_event_t *event)
{
    (void)event;
    settings_picker_refresh_current();
}

static void settings_picker_close_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    g_settings_picker_kind = SETTINGS_PICKER_NONE;
    lv_obj_add_flag(g_settings_picker, LV_OBJ_FLAG_HIDDEN);
}

static void settings_picker_done_cb(lv_event_t *event)
{
    unsigned selected;

    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    selected = (unsigned)lv_roller_get_selected(g_settings_picker_roller);
    switch (g_settings_picker_kind) {
    case SETTINGS_PICKER_LANGUAGE:
        settings_apply_language(selected);
        break;
    case SETTINGS_PICKER_TIMEZONE:
        settings_apply_timezone(selected);
        break;
    case SETTINGS_PICKER_AUTOSTART:
        settings_apply_autostart(selected);
        break;
    case SETTINGS_PICKER_VAXP_BAUD:
        settings_apply_vaxp_baud(selected);
        break;
    case SETTINGS_PICKER_SLEEP_TIMEOUT:
        settings_apply_sleep_timeout(selected);
        break;
    default:
        break;
    }
    g_settings_picker_kind = SETTINGS_PICKER_NONE;
    lv_obj_add_flag(g_settings_picker, LV_OBJ_FLAG_HIDDEN);
}

static void settings_picker_open(settings_picker_kind_t kind)
{
    const char *title;
    const char *options;
    unsigned selected;
    int section;
    lv_anim_t animation;

    switch (kind) {
    case SETTINGS_PICKER_LANGUAGE:
        title = settings_text("Language");
        options = "简体中文\n繁體中文\nEnglish\n日本語";
        selected = g_system_settings.pending_language >= 0 &&
                           g_system_settings.pending_language <
                               DSHANPI_LANG_COUNT
                       ? (unsigned)g_system_settings.pending_language
                       : DSHANPI_LANG_EN;
        section = 1;
        break;
    case SETTINGS_PICKER_TIMEZONE:
        title = settings_text("Time zone");
        options = settings_timezone_options_text();
        selected = g_system_settings.timezone_index >= 0 &&
                           g_system_settings.timezone_index < 6
                       ? (unsigned)g_system_settings.timezone_index
                       : 4;
        section = 2;
        break;
    case SETTINGS_PICKER_AUTOSTART:
        title = settings_text("Startup App");
        options = settings_autostart_options_text();
        selected = autostart_ui_selection(g_system_settings.autostart);
        section = 3;
        break;
    case SETTINGS_PICKER_VAXP_BAUD:
        title = settings_text("VAXP UART");
        options = vaxp_baud_options_text();
        selected = vaxp_baud_selection(g_system_settings.vaxp_baud_rate);
        section = 5;
        break;
    case SETTINGS_PICKER_SLEEP_TIMEOUT:
        title = settings_text("Sleep & Screen Saver");
        options = sleep_timeout_options_text();
        selected = sleep_timeout_selection(
            g_system_settings.sleep_timeout_seconds);
        section = 7;
        break;
    default:
        return;
    }

    g_settings_picker_kind = kind;
    lv_label_set_text(g_settings_picker_title, title);
    lv_roller_set_options(g_settings_picker_roller, options,
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(g_settings_picker_roller, selected, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        g_settings_picker_icon,
        lv_color_hex(g_settings_icon_colors[section]), 0);
    lv_label_set_text(g_settings_picker_icon_symbol,
                      g_settings_symbols[section]);
    settings_picker_refresh_current();
    lv_anim_delete(g_settings_picker, settings_picker_x_anim_cb);
    lv_obj_set_x(g_settings_picker, UI_WIDTH);
    lv_obj_remove_flag(g_settings_picker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_settings_picker);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_settings_picker);
    lv_anim_set_exec_cb(&animation, settings_picker_x_anim_cb);
    lv_anim_set_values(&animation, UI_WIDTH, 0);
    lv_anim_set_duration(&animation, 240);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void settings_create_picker(lv_obj_t *parent)
{
    g_settings_picker = lv_obj_create(parent);
    lv_obj_set_size(g_settings_picker, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_settings_picker);
    lv_obj_set_style_bg_color(g_settings_picker, lv_color_hex(0xF2F2F7), 0);
    lv_obj_set_style_bg_opa(g_settings_picker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_settings_picker, 0, 0);
    lv_obj_set_style_pad_all(g_settings_picker, 0, 0);
    lv_obj_remove_flag(g_settings_picker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(g_settings_picker);
    lv_obj_set_size(header, UI_WIDTH, 64);
    lv_obj_set_pos(header, 0, 0);
    style_plain(header);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFAFAFC), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0xD9D9DE), 0);

    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 104, 62);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(back, 20, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0xE5E5EA),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(back, settings_picker_close_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_symbol =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_28, 0x007AFF);
    lv_obj_center(back_symbol);

    g_settings_picker_title =
        make_label(header, settings_text("Language"),
                   &ui_font_source_han_20, 0x111827);
    lv_obj_set_width(g_settings_picker_title, 330);
    lv_obj_set_style_text_align(g_settings_picker_title,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_settings_picker_title);

    lv_obj_t *done = lv_button_create(header);
    lv_obj_set_size(done, 104, 62);
    lv_obj_align(done, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(done, 20, 0);
    lv_obj_set_style_bg_opa(done, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(done, lv_color_hex(0xE5E5EA),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(done, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(done, 0, 0);
    lv_obj_add_event_cb(done, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(done, settings_picker_done_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *done_symbol =
        make_label(done, LV_SYMBOL_OK, &lv_font_montserrat_28, 0x007AFF);
    lv_obj_center(done_symbol);

    lv_obj_t *current_card = lv_obj_create(g_settings_picker);
    lv_obj_set_size(current_card, 588, 82);
    lv_obj_align(current_card, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_radius(current_card, 24, 0);
    lv_obj_set_style_border_width(current_card, 1, 0);
    lv_obj_set_style_border_color(current_card, lv_color_hex(0xE1E1E8), 0);
    lv_obj_set_style_bg_color(current_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(current_card, 12, 0);
    lv_obj_set_style_shadow_offset_y(current_card, 4, 0);
    lv_obj_set_style_shadow_color(current_card, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_shadow_opa(current_card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(current_card, 0, 0);
    lv_obj_remove_flag(current_card, LV_OBJ_FLAG_SCROLLABLE);

    g_settings_picker_icon = lv_obj_create(current_card);
    lv_obj_set_size(g_settings_picker_icon, 52, 52);
    lv_obj_align(g_settings_picker_icon, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_radius(g_settings_picker_icon, 16, 0);
    lv_obj_set_style_border_width(g_settings_picker_icon, 0, 0);
    lv_obj_set_style_pad_all(g_settings_picker_icon, 0, 0);
    lv_obj_set_style_bg_color(g_settings_picker_icon,
                              lv_color_hex(g_settings_icon_colors[1]), 0);
    lv_obj_remove_flag(g_settings_picker_icon,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_settings_picker_icon_symbol =
        make_label(g_settings_picker_icon, g_settings_symbols[1],
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(g_settings_picker_icon_symbol);

    lv_obj_t *current_caption =
        make_label(current_card, settings_text("Current Selection"),
                   &ui_font_source_han_20, 0x8E8E93);
    lv_obj_align(current_caption, LV_ALIGN_LEFT_MID, 84, -17);
    g_settings_picker_current =
        make_label(current_card, settings_language_summary(
                                     g_system_settings.pending_language),
                   &ui_font_source_han_20, 0x1C1C1E);
    lv_obj_set_width(g_settings_picker_current, 430);
    lv_label_set_long_mode(g_settings_picker_current, LV_LABEL_LONG_DOT);
    lv_obj_align(g_settings_picker_current, LV_ALIGN_LEFT_MID, 84, 17);

    lv_obj_t *roller_card = lv_obj_create(g_settings_picker);
    lv_obj_set_size(roller_card, 588, 286);
    lv_obj_align(roller_card, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(roller_card, 28, 0);
    lv_obj_set_style_border_width(roller_card, 1, 0);
    lv_obj_set_style_border_color(roller_card, lv_color_hex(0xE1E1E8), 0);
    lv_obj_set_style_bg_color(roller_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(roller_card, 14, 0);
    lv_obj_set_style_shadow_offset_y(roller_card, 5, 0);
    lv_obj_set_style_shadow_color(roller_card, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_shadow_opa(roller_card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(roller_card, 12, 0);
    lv_obj_remove_flag(roller_card, LV_OBJ_FLAG_SCROLLABLE);

    g_settings_picker_roller = lv_roller_create(roller_card);
    lv_roller_set_options(g_settings_picker_roller,
                          "简体中文\n繁體中文\nEnglish\n日本語",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(g_settings_picker_roller, 5);
    lv_obj_set_size(g_settings_picker_roller, 548, 254);
    lv_obj_center(g_settings_picker_roller);
    lv_obj_set_style_radius(g_settings_picker_roller, 20, 0);
    lv_obj_set_style_border_width(g_settings_picker_roller, 0, 0);
    lv_obj_set_style_bg_opa(g_settings_picker_roller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(g_settings_picker_roller,
                               &ui_font_source_han_20, 0);
    lv_obj_set_style_text_color(g_settings_picker_roller,
                                lv_color_hex(0xB1B1B8), 0);
    lv_obj_set_style_text_align(g_settings_picker_roller,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(g_settings_picker_roller, 18, 0);
    lv_obj_set_style_bg_color(g_settings_picker_roller,
                              lv_color_hex(0xEAF3FF), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(g_settings_picker_roller, LV_OPA_COVER,
                            LV_PART_SELECTED);
    lv_obj_set_style_radius(g_settings_picker_roller, 16,
                            LV_PART_SELECTED);
    lv_obj_set_style_border_width(g_settings_picker_roller, 1,
                                  LV_PART_SELECTED);
    lv_obj_set_style_border_color(g_settings_picker_roller,
                                  lv_color_hex(0xB6D7FF),
                                  LV_PART_SELECTED);
    lv_obj_set_style_text_font(g_settings_picker_roller,
                               &ui_font_source_han_20,
                               LV_PART_SELECTED);
    lv_obj_set_style_text_color(g_settings_picker_roller,
                                lv_color_hex(0x0057B8),
                                LV_PART_SELECTED);
    lv_obj_add_event_cb(g_settings_picker_roller,
                        settings_picker_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(g_settings_picker, LV_OBJ_FLAG_HIDDEN);
}

static void settings_nav_cb(lv_event_t *event)
{
    int selected = (int)(intptr_t)lv_event_get_user_data(event);
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    switch (selected) {
    case 1:
        settings_picker_open(SETTINGS_PICKER_LANGUAGE);
        return;
    case 2:
        settings_picker_open(SETTINGS_PICKER_TIMEZONE);
        return;
    case 3:
        settings_picker_open(SETTINGS_PICKER_AUTOSTART);
        return;
    case 5:
        settings_picker_open(SETTINGS_PICKER_VAXP_BAUD);
        return;
    case 7:
        settings_picker_open(SETTINGS_PICKER_SLEEP_TIMEOUT);
        return;
    default:
        break;
    }
    lv_label_set_text(g_settings_detail_title,
                      settings_text(g_settings_titles[selected]));
    lv_obj_set_style_bg_color(
        g_settings_detail_icon,
        lv_color_hex(g_settings_icon_colors[selected]), 0);
    lv_label_set_text(g_settings_detail_icon_symbol,
                      g_settings_symbols[selected]);
    lv_obj_add_flag(g_settings_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_settings_navigation, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_detail, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        if (i == selected) {
            lv_obj_remove_flag(g_settings_panels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_settings_panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (selected == 4) {
        settings_camera_refresh_selection(
            dshanpi_camera_setting_load());
    }
    if (selected == 6)
        ota_refresh_ui();
    if (selected == 0) {
        /* Manual Wi-Fi setup takes priority over background auto-join. */
        g_wifi_panel_active = true;
        g_wifi_scan_requested = true;
        wifi_scan_animation_start();
        lv_label_set_text(g_wifi_status, settings_text("Scanning..."));
        wifi_scan_start();
    }
}

static void settings_detail_back_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    lv_obj_add_flag(g_settings_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_navigation, LV_OBJ_FLAG_HIDDEN);
    g_wifi_panel_active = false;
    g_wifi_scan_requested = false;
    wifi_scan_animation_stop();
    settings_refresh_nav_values();
}

static void settings_home_back_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    lv_obj_add_flag(g_settings_view, LV_OBJ_FLAG_HIDDEN);
}

static void power_request_cb(lv_event_t *event)
{
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    const char *question;

    if (!lv_k230_touch_accept_click()) return;
    if (ota_update_is_active()) {
        show_toast(settings_text("Update already running"));
        return;
    }
    g_power_pending_action = action;
    question = action == 1 ? "Restart the device?" :
               action == 3 ? "Enter flashing mode?" :
                             "Shut down the device?";
    lv_label_set_text(g_power_confirm_title, settings_text(question));
    lv_obj_remove_flag(g_power_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_power_confirm);
}

static void power_cancel_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    lv_obj_add_flag(g_power_confirm, LV_OBJ_FLAG_HIDDEN);
}

static void power_confirm_cb(lv_event_t *event)
{
    int result;
    const char *progress;

    (void)event;
    if (!lv_k230_touch_accept_click()) return;
    if (ota_update_is_active()) {
        lv_obj_add_flag(g_power_confirm, LV_OBJ_FLAG_HIDDEN);
        show_toast(settings_text("Update already running"));
        return;
    }
    progress = g_power_pending_action == 1 ? "Restarting..." :
               g_power_pending_action == 3 ? "Entering flashing mode..." :
                                             "Shutting down...";
    lv_label_set_text(g_power_confirm_title, settings_text(progress));
    lv_refr_now(NULL);
    if (g_power_pending_action == 1)
        result = dshanpi_power_reboot();
    else if (g_power_pending_action == 3)
        result = dshanpi_power_reboot_to_upgrade();
    else
        result = dshanpi_power_off();
    if (result != 0) {
        lv_obj_add_flag(g_power_confirm, LV_OBJ_FLAG_HIDDEN);
        show_toast(settings_text("Power command failed"));
    }
}

static void ota_refresh_ui(void)
{
    dshanpi_ota_snapshot_t snapshot;
    const char *status;
    char progress_text[12];
    uint32_t status_color = 0x164E63;
    uint32_t progress_color = 0x169C92;
    bool active;

    if (g_ota_status_label == NULL || g_ota_progress == NULL ||
        (g_settings_view != NULL &&
         lv_obj_has_flag(g_settings_view, LV_OBJ_FLAG_HIDDEN)))
        return;
    dshanpi_ota_get_snapshot(&snapshot);
    switch (snapshot.state) {
    case DSHANPI_OTA_CHECKING:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Checking signed update manifest");
        status_color = 0x0F766E;
        break;
    case DSHANPI_OTA_VERIFYING_MANIFEST:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Verifying update publisher signature");
        status_color = 0x0F766E;
        break;
    case DSHANPI_OTA_DOWNLOADING:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Downloading update package");
        status_color = 0x0F766E;
        break;
    case DSHANPI_OTA_VERIFYING_PACKAGE:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Verifying signed update package");
        status_color = 0x0F766E;
        break;
    case DSHANPI_OTA_INSTALLING:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Verifying and writing inactive slot");
        status_color = 0x0F766E;
        break;
    case DSHANPI_OTA_REBOOTING:
        status = settings_text("Update installed - restarting automatically");
        status_color = 0x15803D;
        progress_color = 0x22A559;
        break;
    case DSHANPI_OTA_READY:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Update ready - restart to apply");
        status_color = 0x15803D;
        progress_color = 0x22A559;
        break;
    case DSHANPI_OTA_AVAILABLE:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("A newer signed update is available");
        status_color = 0x15803D;
        progress_color = 0x22A559;
        break;
    case DSHANPI_OTA_UP_TO_DATE:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("System is already up to date");
        status_color = 0x15803D;
        progress_color = 0x22A559;
        break;
    case DSHANPI_OTA_FAILED:
        status = snapshot.detail[0] != '\0'
                     ? snapshot.detail
                     : settings_text("Update failed - current system is unchanged");
        status_color = 0xB42318;
        progress_color = 0xDC5364;
        break;
    default:
        status = settings_text("Ready to update");
        break;
    }
    if (strcmp(lv_label_get_text(g_ota_status_label), status) != 0)
        lv_label_set_text(g_ota_status_label, status);
    if (!lv_color_eq(lv_obj_get_style_text_color(g_ota_status_label, 0),
                     lv_color_hex(status_color)))
        lv_obj_set_style_text_color(g_ota_status_label,
                                    lv_color_hex(status_color), 0);
    if (lv_bar_get_value(g_ota_progress) != (int32_t)snapshot.progress)
        lv_bar_set_value(g_ota_progress, (int32_t)snapshot.progress,
                         LV_ANIM_ON);
    if (!lv_color_eq(lv_obj_get_style_bg_color(g_ota_progress,
                                                LV_PART_INDICATOR),
                     lv_color_hex(progress_color)))
        lv_obj_set_style_bg_color(g_ota_progress,
                                  lv_color_hex(progress_color),
                                  LV_PART_INDICATOR);
    if (g_ota_progress_label != NULL) {
        snprintf(progress_text, sizeof(progress_text), "%u%%",
                 snapshot.progress);
        if (strcmp(lv_label_get_text(g_ota_progress_label),
                   progress_text) != 0)
            lv_label_set_text(g_ota_progress_label, progress_text);
        if (!lv_color_eq(lv_obj_get_style_text_color(g_ota_progress_label,
                                                      0),
                         lv_color_hex(status_color)))
            lv_obj_set_style_text_color(g_ota_progress_label,
                                        lv_color_hex(status_color), 0);
    }
    if (g_ota_network_button_label != NULL) {
        const char *label = settings_text("Network download");
        if (strcmp(lv_label_get_text(g_ota_network_button_label),
                   label) != 0)
            lv_label_set_text(g_ota_network_button_label, label);
    }
    active = snapshot.busy ||
             snapshot.state == DSHANPI_OTA_CHECKING ||
             snapshot.state == DSHANPI_OTA_VERIFYING_MANIFEST ||
             snapshot.state == DSHANPI_OTA_DOWNLOADING ||
             snapshot.state == DSHANPI_OTA_VERIFYING_PACKAGE ||
             snapshot.state == DSHANPI_OTA_INSTALLING ||
             snapshot.state == DSHANPI_OTA_REBOOTING;
    if (g_ota_network_button != NULL) {
        bool disabled = active || snapshot.state == DSHANPI_OTA_READY;
        bool was_disabled = lv_obj_has_state(g_ota_network_button,
                                              LV_STATE_DISABLED);
        if (disabled && !was_disabled)
            lv_obj_add_state(g_ota_network_button, LV_STATE_DISABLED);
        else if (!disabled && was_disabled)
            lv_obj_remove_state(g_ota_network_button, LV_STATE_DISABLED);
    }
}

static void ota_start_cb(lv_event_t *event)
{
    int result;

    (void)event;
    if (!lv_k230_touch_accept_click())
        return;
    result = dshanpi_ota_start_network();
    if (result != 0) {
        ota_refresh_ui();
        show_toast(settings_text(
            result == -1 ? "Update already running" :
                           "Update failed - current system is unchanged"));
        return;
    }
    ota_refresh_ui();
}

static void create_settings_view(lv_obj_t *screen)
{
    g_settings_view = lv_obj_create(screen);
    lv_obj_set_size(g_settings_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_settings_view);
    lv_obj_set_style_bg_color(g_settings_view, lv_color_hex(0xF7F7FC), 0);
    lv_obj_set_style_border_width(g_settings_view, 0, 0);
    lv_obj_set_style_pad_all(g_settings_view, 0, 0);
    lv_obj_remove_flag(g_settings_view, LV_OBJ_FLAG_SCROLLABLE);

    g_settings_header = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_settings_header, UI_WIDTH, 72);
    style_plain(g_settings_header);
    lv_obj_set_style_bg_color(g_settings_header, lv_color_hex(0xF9F8FF), 0);
    lv_obj_set_style_border_width(g_settings_header, 1, 0);
    lv_obj_set_style_border_side(g_settings_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(g_settings_header, lv_color_hex(0xE7E5EF), 0);
    lv_obj_t *back = create_round_button(g_settings_header, 52, 0xECE9F7);
    lv_obj_set_pos(back, 12, 10);
    expand_top_left_back_hit_area(back);
    lv_obj_set_style_bg_color(back, lv_color_hex(0xDED8F1), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, settings_home_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, 0x454452);
    lv_obj_center(back_label);
    const lv_font_t *heading_font =
        g_system_settings.language == DSHANPI_LANG_EN
            ? &lv_font_montserrat_28
            : &ui_font_source_han_20;
    g_settings_heading =
        make_label(g_settings_header, settings_text("Settings"), heading_font,
                   0x202027);
    lv_obj_align(g_settings_heading, LV_ALIGN_LEFT_MID, 80, 0);

    g_settings_navigation = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_settings_navigation, 608, 388);
    lv_obj_set_pos(g_settings_navigation, 16, 80);
    lv_obj_set_style_bg_opa(g_settings_navigation, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_settings_navigation, 0, 0);
    lv_obj_set_style_pad_all(g_settings_navigation, 0, 0);
    lv_obj_add_flag(g_settings_navigation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_settings_navigation, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_settings_navigation, LV_SCROLLBAR_MODE_AUTO);

    static const char *const initial_values[] = {
        "Not connected", "English", "UTC+08:00",
        "Desktop", "Rear", "115200 baud",
        "A/B protected OTA", "5 minutes",
        "Restart, flash, or shut down", "DongshanPI CanMV-K230",
    };
    const lv_font_t *nav_font =
        g_system_settings.language == DSHANPI_LANG_EN
            ? &lv_font_montserrat_16
            : &ui_font_source_han_20;
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        g_settings_nav[i] = lv_button_create(g_settings_navigation);
        lv_obj_set_size(g_settings_nav[i], 296, 112);
        lv_obj_set_pos(g_settings_nav[i], (i % 2) * 312,
                       (i / 2) * 124);
        lv_obj_set_style_radius(g_settings_nav[i], 22, 0);
        lv_obj_set_style_pad_all(g_settings_nav[i], 0, 0);
        lv_obj_set_style_border_width(g_settings_nav[i], 1, 0);
        lv_obj_set_style_border_color(g_settings_nav[i],
                                      lv_color_hex(g_settings_border_colors[i]), 0);
        lv_obj_set_style_shadow_width(g_settings_nav[i], 10, 0);
        lv_obj_set_style_shadow_offset_y(g_settings_nav[i], 3, 0);
        lv_obj_set_style_shadow_color(g_settings_nav[i],
                                      lv_color_hex(0x6B7280), 0);
        lv_obj_set_style_shadow_opa(g_settings_nav[i], LV_OPA_10, 0);
        lv_obj_set_style_bg_color(g_settings_nav[i],
                                  lv_color_hex(g_settings_card_colors[i]), 0);
        lv_obj_set_style_bg_color(g_settings_nav[i],
                                  lv_color_hex(g_settings_pressed_colors[i]),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_transform_scale_x(g_settings_nav[i], 250,
                                           LV_STATE_PRESSED);
        lv_obj_set_style_transform_scale_y(g_settings_nav[i], 250,
                                           LV_STATE_PRESSED);
        lv_obj_add_event_cb(g_settings_nav[i], tap_guard_cb,
                            LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(g_settings_nav[i], settings_nav_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = lv_obj_create(g_settings_nav[i]);
        lv_obj_set_size(icon, 72, 72);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_style_radius(icon, 20, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_style_pad_all(icon, 0, 0);
        lv_obj_set_style_bg_color(
            icon, lv_color_hex(g_settings_icon_colors[i]), 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE |
                                 LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *symbol =
            make_label(icon, g_settings_symbols[i], &lv_font_montserrat_28,
                       0xFFFFFF);
        lv_obj_center(symbol);
        g_settings_nav_labels[i] =
            make_label(g_settings_nav[i], settings_text(g_settings_titles[i]),
                       nav_font, 0x202027);
        lv_obj_set_width(g_settings_nav_labels[i], 160);
        lv_label_set_long_mode(g_settings_nav_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_align(g_settings_nav_labels[i], LV_ALIGN_LEFT_MID, 104, -17);
        g_settings_nav_values[i] =
            make_label(g_settings_nav[i], initial_values[i],
                       settings_ui_font(&lv_font_montserrat_12), 0x5F6170);
        lv_obj_set_width(g_settings_nav_values[i], 152);
        lv_label_set_long_mode(g_settings_nav_values[i], LV_LABEL_LONG_DOT);
        lv_obj_align(g_settings_nav_values[i], LV_ALIGN_LEFT_MID, 104, 18);
        lv_obj_t *chevron =
            make_label(g_settings_nav[i], LV_SYMBOL_RIGHT,
                       &lv_font_montserrat_16, 0x686A78);
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -13, 0);
    }
    settings_refresh_nav_values();

    g_settings_detail = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_settings_detail, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_settings_detail, 0, 0);
    lv_obj_set_style_bg_color(g_settings_detail, lv_color_hex(0xF7F7FC), 0);
    lv_obj_set_style_bg_opa(g_settings_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_settings_detail, 0, 0);
    lv_obj_set_style_pad_all(g_settings_detail, 0, 0);
    lv_obj_remove_flag(g_settings_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_settings_detail, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *detail_header = lv_obj_create(g_settings_detail);
    lv_obj_set_size(detail_header, UI_WIDTH, 72);
    lv_obj_set_pos(detail_header, 0, 0);
    style_plain(detail_header);
    lv_obj_set_style_bg_color(detail_header, lv_color_hex(0xF9F8FF), 0);
    lv_obj_set_style_border_width(detail_header, 1, 0);
    lv_obj_set_style_border_side(detail_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(detail_header, lv_color_hex(0xE7E5EF), 0);
    lv_obj_t *detail_back = create_round_button(detail_header, 52, 0xECE9F7);
    lv_obj_set_pos(detail_back, 12, 10);
    expand_top_left_back_hit_area(detail_back);
    lv_obj_set_style_bg_color(detail_back, lv_color_hex(0xDED8F1),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(detail_back, settings_detail_back_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *detail_back_symbol =
        make_label(detail_back, LV_SYMBOL_LEFT, &lv_font_montserrat_20,
                   0x454452);
    lv_obj_center(detail_back_symbol);
    const lv_font_t *detail_title_font =
        g_system_settings.language == DSHANPI_LANG_EN
            ? &lv_font_montserrat_28
            : &ui_font_source_han_20;
    g_settings_detail_title =
        make_label(detail_header, settings_text("Wi-Fi"), detail_title_font,
                   0x202027);
    lv_obj_align(g_settings_detail_title, LV_ALIGN_LEFT_MID, 80, 0);

    g_settings_detail_icon = lv_obj_create(detail_header);
    lv_obj_set_size(g_settings_detail_icon, 46, 46);
    lv_obj_align(g_settings_detail_icon, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_radius(g_settings_detail_icon, 14, 0);
    lv_obj_set_style_border_width(g_settings_detail_icon, 0, 0);
    lv_obj_set_style_pad_all(g_settings_detail_icon, 0, 0);
    lv_obj_set_style_bg_color(
        g_settings_detail_icon,
        lv_color_hex(g_settings_icon_colors[0]), 0);
    lv_obj_remove_flag(g_settings_detail_icon,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_settings_detail_icon_symbol =
        make_label(g_settings_detail_icon, g_settings_symbols[0],
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(g_settings_detail_icon_symbol);

    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        g_settings_panels[i] = lv_obj_create(g_settings_detail);
        lv_obj_set_size(g_settings_panels[i], 620, 398);
        lv_obj_set_pos(g_settings_panels[i], 10, 78);
        lv_obj_set_style_bg_opa(g_settings_panels[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_settings_panels[i], 0, 0);
        lv_obj_set_style_pad_all(g_settings_panels[i], 0, 0);
        lv_obj_remove_flag(g_settings_panels[i], LV_OBJ_FLAG_SCROLLABLE);
        if (i != 0) {
            lv_obj_add_flag(g_settings_panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t *wifi = settings_section(g_settings_panels[0], 0, 382);
    lv_obj_t *current_card = lv_obj_create(wifi);
    lv_obj_set_size(current_card, 552, 92);
    lv_obj_align(current_card, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(current_card, 18, 0);
    lv_obj_set_style_border_width(current_card, 1, 0);
    lv_obj_set_style_border_color(
        current_card, lv_color_hex(g_settings_border_colors[0]), 0);
    lv_obj_set_style_bg_color(current_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_remove_flag(current_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *wifi_icon = lv_obj_create(current_card);
    lv_obj_set_size(wifi_icon, 54, 54);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(wifi_icon, 14, 0);
    lv_obj_set_style_border_width(wifi_icon, 0, 0);
    lv_obj_set_style_bg_color(wifi_icon, lv_color_hex(0x007AFF), 0);
    lv_obj_remove_flag(wifi_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *wifi_icon_label =
        make_label(wifi_icon, LV_SYMBOL_WIFI, &lv_font_montserrat_20,
                   0xFFFFFF);
    lv_obj_center(wifi_icon_label);
    g_wifi_status = make_label(current_card, "Wi-Fi",
                               settings_ui_font(&lv_font_montserrat_16),
                               0x1C1C1E);
    lv_obj_align(g_wifi_status, LV_ALIGN_LEFT_MID, 72, -15);
    g_wifi_ip_label = make_label(current_card, settings_text("Not Connected"),
                                 settings_ui_font(&lv_font_montserrat_12),
                                 0x8E8E93);
    lv_obj_align(g_wifi_ip_label, LV_ALIGN_LEFT_MID, 72, 15);
    g_wifi_scan_button = lv_button_create(wifi);
    lv_obj_set_size(g_wifi_scan_button, 54, 54);
    lv_obj_align(g_wifi_scan_button, LV_ALIGN_TOP_RIGHT, -22, 19);
    lv_obj_set_style_radius(g_wifi_scan_button, 27, 0);
    lv_obj_set_style_bg_color(g_wifi_scan_button,
                              lv_color_hex(0xE5F1FF), 0);
    lv_obj_set_style_bg_color(g_wifi_scan_button,
                              lv_color_hex(0xD1E7FF), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(g_wifi_scan_button, 0, 0);
    lv_obj_add_event_cb(g_wifi_scan_button, tap_guard_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_wifi_scan_button, wifi_scan_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_icon = make_label(g_wifi_scan_button, LV_SYMBOL_REFRESH,
                                     &lv_font_montserrat_28, 0x007AFF);
    lv_obj_center(scan_icon);
    lv_obj_t *networks_title =
        make_label(wifi, settings_text("AVAILABLE NETWORKS"),
                   settings_ui_font(&lv_font_montserrat_12), 0x6D6D72);
    lv_obj_align(networks_title, LV_ALIGN_TOP_LEFT, 24, 106);
    g_wifi_scan_progress = lv_obj_create(wifi);
    lv_obj_set_size(g_wifi_scan_progress, 28, 28);
    lv_obj_set_style_bg_opa(g_wifi_scan_progress, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_wifi_scan_progress, 0, 0);
    lv_obj_set_style_pad_all(g_wifi_scan_progress, 0, 0);
    lv_obj_remove_flag(g_wifi_scan_progress,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(g_wifi_scan_progress, 14, 0);
    lv_obj_set_style_transform_pivot_y(g_wifi_scan_progress, 14, 0);
    lv_obj_t *scan_progress_icon =
        make_label(g_wifi_scan_progress, LV_SYMBOL_REFRESH,
                   &lv_font_montserrat_18, 0x007AFF);
    lv_obj_center(scan_progress_icon);
    lv_obj_align(g_wifi_scan_progress, LV_ALIGN_TOP_RIGHT, -22, 98);
    lv_obj_add_flag(g_wifi_scan_progress, LV_OBJ_FLAG_HIDDEN);
    g_wifi_list = lv_obj_create(wifi);
    lv_obj_set_size(g_wifi_list, 552, 232);
    lv_obj_align(g_wifi_list, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_set_style_border_width(g_wifi_list, 1, 0);
    lv_obj_set_style_border_color(
        g_wifi_list, lv_color_hex(g_settings_border_colors[0]), 0);
    lv_obj_set_style_radius(g_wifi_list, 18, 0);
    lv_obj_set_style_bg_color(g_wifi_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(g_wifi_list, 6, 0);
    lv_obj_set_style_pad_row(g_wifi_list, 0, 0);
    lv_obj_set_flex_flow(g_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_wifi_list, LV_DIR_VER);

    lv_obj_t *camera =
        settings_section(g_settings_panels[4], 4, 388);
    lv_obj_t *camera_hint =
        make_label(camera,
                   settings_text("Used by Camera and all AI applications"),
                   settings_ui_font(&lv_font_montserrat_12), 0x64748B);
    lv_obj_align(camera_hint, LV_ALIGN_TOP_MID, 0, 52);
    g_settings_camera_rear = lv_button_create(camera);
    lv_obj_set_size(g_settings_camera_rear, 500, 76);
    lv_obj_align(g_settings_camera_rear, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_radius(g_settings_camera_rear, 24, 0);
    lv_obj_set_style_border_color(
        g_settings_camera_rear,
        lv_color_hex(g_settings_border_colors[4]), 0);
    lv_obj_set_style_bg_color(g_settings_camera_rear,
                              lv_color_hex(0x2F7D3A), 0);
    lv_obj_set_style_bg_color(g_settings_camera_rear,
                              lv_color_hex(0x245F2D),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(g_settings_camera_rear, 0, 0);
    lv_obj_add_event_cb(g_settings_camera_rear, tap_guard_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_settings_camera_rear,
                        camera_setting_select_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)DSHANPI_CAMERA_REAR_CSI);
    g_settings_camera_rear_label =
        make_label(g_settings_camera_rear, settings_text("Rear"),
                   settings_ui_font(&lv_font_montserrat_16), 0xFFFFFF);
    lv_obj_center(g_settings_camera_rear_label);
    g_settings_camera_front = lv_button_create(camera);
    lv_obj_set_size(g_settings_camera_front, 500, 76);
    lv_obj_align(g_settings_camera_front, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_set_style_radius(g_settings_camera_front, 24, 0);
    lv_obj_set_style_bg_color(g_settings_camera_front,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(g_settings_camera_front,
                              lv_color_hex(0xD8EED5),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_settings_camera_front, 1, 0);
    lv_obj_set_style_border_color(
        g_settings_camera_front,
        lv_color_hex(g_settings_border_colors[4]), 0);
    lv_obj_set_style_shadow_width(g_settings_camera_front, 0, 0);
    lv_obj_add_event_cb(g_settings_camera_front, tap_guard_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_settings_camera_front,
                        camera_setting_select_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)DSHANPI_CAMERA_FRONT_CSI);
    g_settings_camera_front_label =
        make_label(g_settings_camera_front, settings_text("Front"),
                   settings_ui_font(&lv_font_montserrat_16), 0x315A37);
    lv_obj_center(g_settings_camera_front_label);
    settings_camera_refresh_selection(dshanpi_camera_setting_load());
    g_settings_status = make_label(camera,
                                   settings_text("Changes apply after reboot"),
                                   settings_ui_font(&lv_font_montserrat_12),
                                   0x64748B);
    lv_obj_align(g_settings_status, LV_ALIGN_TOP_MID, 0, 282);

    lv_obj_t *update = settings_section(g_settings_panels[6], 6, 388);
    lv_obj_t *update_status_card = lv_obj_create(update);
    lv_obj_set_size(update_status_card, 552, 138);
    lv_obj_align(update_status_card, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(update_status_card, 20, 0);
    lv_obj_set_style_border_width(update_status_card, 1, 0);
    lv_obj_set_style_border_color(
        update_status_card, lv_color_hex(g_settings_border_colors[6]), 0);
    lv_obj_set_style_bg_color(update_status_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(update_status_card, 0, 0);
    lv_obj_remove_flag(update_status_card, LV_OBJ_FLAG_SCROLLABLE);
    g_ota_status_label =
        make_label(update_status_card, settings_text("Ready to update"),
                   settings_ui_font(&lv_font_montserrat_16), 0x164E63);
    lv_obj_set_size(g_ota_status_label, 410, 48);
    lv_label_set_long_mode(g_ota_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_ota_status_label, LV_ALIGN_TOP_LEFT, 20, 14);
    g_ota_progress_label =
        make_label(update_status_card, "0%", &lv_font_montserrat_20,
                   0x164E63);
    lv_obj_set_width(g_ota_progress_label, 82);
    lv_obj_set_style_text_align(g_ota_progress_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(g_ota_progress_label, LV_ALIGN_TOP_RIGHT, -20, 14);
    lv_obj_t *ota_protection =
        make_label(update_status_card, settings_text("A/B protected OTA"),
                   settings_ui_font(&lv_font_montserrat_12), 0x64748B);
    lv_obj_align(ota_protection, LV_ALIGN_TOP_LEFT, 20, 70);
    g_ota_progress = lv_bar_create(update_status_card);
    lv_obj_set_size(g_ota_progress, 510, 16);
    lv_obj_align(g_ota_progress, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_bar_set_range(g_ota_progress, 0, 100);
    lv_bar_set_value(g_ota_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_ota_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ota_progress, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ota_progress, lv_color_hex(0xDCEDEA),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ota_progress, lv_color_hex(0x169C92),
                              LV_PART_INDICATOR);

    g_ota_network_button = lv_button_create(update);
    lv_obj_set_size(g_ota_network_button, 552, 96);
    lv_obj_align(g_ota_network_button, LV_ALIGN_TOP_LEFT, 6, 154);
    lv_obj_set_style_radius(g_ota_network_button, 24, 0);
    lv_obj_set_style_bg_color(g_ota_network_button,
                              lv_color_hex(0x169C92), 0);
    lv_obj_set_style_bg_color(g_ota_network_button, lv_color_hex(0x0F766E),
                              LV_STATE_PRESSED);
    lv_obj_set_style_opa(g_ota_network_button, LV_OPA_50,
                         LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(g_ota_network_button, 0, 0);
    lv_obj_add_event_cb(g_ota_network_button, tap_guard_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g_ota_network_button, ota_start_cb, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *network_update_icon =
        make_label(g_ota_network_button, LV_SYMBOL_DOWNLOAD,
                   &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_align(network_update_icon, LV_ALIGN_LEFT_MID, 26, 0);
    g_ota_network_button_label =
        make_label(g_ota_network_button, settings_text("Network download"),
                   settings_ui_font(&lv_font_montserrat_18), 0xFFFFFF);
    lv_obj_set_width(g_ota_network_button_label, 430);
    lv_obj_set_style_text_align(g_ota_network_button_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_ota_network_button_label, LV_ALIGN_RIGHT_MID, -12, 0);

    lv_obj_t *safety_hint = make_label(
        update,
        settings_text(
            "Power loss or download failure keeps the active slot bootable."),
        settings_ui_font(&lv_font_montserrat_12), 0x64748B);
    lv_obj_set_width(safety_hint, 540);
    lv_label_set_long_mode(safety_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(safety_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(safety_hint, LV_ALIGN_TOP_MID, 0, 306);
    ota_refresh_ui();

    lv_obj_t *about = settings_section(g_settings_panels[9], 9, 388);
    static const char *const about_titles[] = {
        "System Version", "Model Name", "Operating System", "nncase Version"
    };
    const char *about_values[] = {
        SYSTEM_VERSION_, settings_text("DongshanPI CanMV-K230"),
        settings_text("RT-Smart real-time operating system"), NNCASE_VERSION_
    };
    for (int row = 0; row < 4; ++row) {
        lv_obj_t *info = lv_obj_create(about);
        lv_obj_set_size(info, 552, 76);
        lv_obj_align(info, LV_ALIGN_TOP_MID, 0, row * 86);
        lv_obj_set_style_radius(info, 20, 0);
        lv_obj_set_style_border_width(info, 1, 0);
        lv_obj_set_style_border_color(
            info, lv_color_hex(g_settings_border_colors[9]), 0);
        lv_obj_set_style_bg_color(info, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_remove_flag(info, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *caption =
            make_label(info, settings_text(about_titles[row]),
                       settings_ui_font(&lv_font_montserrat_14), 0x64748B);
        lv_obj_align(caption, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_t *value =
            make_label(info, about_values[row],
                       settings_ui_font(&lv_font_montserrat_14), 0x172033);
        lv_obj_set_width(value, 350);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -18, 0);
    }

    lv_obj_t *power =
        settings_section(g_settings_panels[8], 8, 388);
    lv_obj_t *power_hint =
        make_label(power,
                   settings_text("Restart, enter flashing mode, or safely shut down the device"),
                   settings_ui_font(&lv_font_montserrat_14), 0x64748B);
    lv_obj_set_width(power_hint, 540);
    lv_label_set_long_mode(power_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(power_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(power_hint, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *restart_button = lv_button_create(power);
    lv_obj_set_size(restart_button, 500, 66);
    lv_obj_align(restart_button, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_radius(restart_button, 22, 0);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(0xF9D8DF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(restart_button, 1, 0);
    lv_obj_set_style_border_color(
        restart_button, lv_color_hex(g_settings_border_colors[8]), 0);
    lv_obj_set_style_shadow_width(restart_button, 0, 0);
    lv_obj_add_event_cb(restart_button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(restart_button, power_request_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    lv_obj_t *restart_text =
        make_label(restart_button, settings_text("Restart"),
                   settings_ui_font(&lv_font_montserrat_20), 0xA62941);
    lv_obj_center(restart_text);

    lv_obj_t *upgrade_button = lv_button_create(power);
    lv_obj_set_size(upgrade_button, 500, 66);
    lv_obj_align(upgrade_button, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_radius(upgrade_button, 22, 0);
    lv_obj_set_style_bg_color(upgrade_button, lv_color_hex(0xFFF5D6), 0);
    lv_obj_set_style_bg_color(upgrade_button, lv_color_hex(0xFFE9A8),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(upgrade_button, 1, 0);
    lv_obj_set_style_border_color(upgrade_button, lv_color_hex(0xF2D07A), 0);
    lv_obj_set_style_shadow_width(upgrade_button, 0, 0);
    lv_obj_add_event_cb(upgrade_button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(upgrade_button, power_request_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)3);
    lv_obj_t *upgrade_text =
        make_label(upgrade_button, settings_text("Flashing Mode"),
                   settings_ui_font(&lv_font_montserrat_20), 0x8A5A00);
    lv_obj_center(upgrade_text);

    lv_obj_t *shutdown_button = lv_button_create(power);
    lv_obj_set_size(shutdown_button, 500, 66);
    lv_obj_align(shutdown_button, LV_ALIGN_TOP_MID, 0, 238);
    lv_obj_set_style_radius(shutdown_button, 22, 0);
    lv_obj_set_style_bg_color(shutdown_button, lv_color_hex(0xEC536D), 0);
    lv_obj_set_style_bg_color(shutdown_button, lv_color_hex(0xD73E59),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(shutdown_button, 0, 0);
    lv_obj_add_event_cb(shutdown_button, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(shutdown_button, power_request_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)2);
    lv_obj_t *shutdown_text =
        make_label(shutdown_button, settings_text("Shut down"),
                   settings_ui_font(&lv_font_montserrat_20), 0xFFFFFF);
    lv_obj_center(shutdown_text);

    g_power_confirm = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_power_confirm, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_power_confirm);
    lv_obj_set_style_bg_color(g_power_confirm, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_power_confirm, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_power_confirm, 0, 0);
    lv_obj_set_style_pad_all(g_power_confirm, 0, 0);
    lv_obj_remove_flag(g_power_confirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *confirm_card = lv_obj_create(g_power_confirm);
    lv_obj_set_size(confirm_card, 470, 250);
    lv_obj_center(confirm_card);
    lv_obj_set_style_radius(confirm_card, 28, 0);
    lv_obj_set_style_border_width(confirm_card, 0, 0);
    lv_obj_remove_flag(confirm_card, LV_OBJ_FLAG_SCROLLABLE);
    g_power_confirm_title =
        make_label(confirm_card, settings_text("Power"),
                   settings_ui_font(&lv_font_montserrat_28), 0x172033);
    lv_obj_align(g_power_confirm_title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *confirm_hint =
        make_label(confirm_card, settings_text("Unsaved work may be lost."),
                   settings_ui_font(&lv_font_montserrat_14), 0x64748B);
    lv_obj_align(confirm_hint, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *confirm_cancel = lv_button_create(confirm_card);
    lv_obj_set_size(confirm_cancel, 170, 60);
    lv_obj_align(confirm_cancel, LV_ALIGN_BOTTOM_LEFT, 18, -12);
    lv_obj_set_style_radius(confirm_cancel, 22, 0);
    lv_obj_set_style_bg_color(confirm_cancel, lv_color_hex(0xE2E8F0), 0);
    lv_obj_add_event_cb(confirm_cancel, power_cancel_cb, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *confirm_cancel_text =
        make_label(confirm_cancel, settings_text("Cancel"),
                   settings_ui_font(&lv_font_montserrat_18), 0x334155);
    lv_obj_center(confirm_cancel_text);
    lv_obj_t *confirm_action = lv_button_create(confirm_card);
    lv_obj_set_size(confirm_action, 170, 60);
    lv_obj_align(confirm_action, LV_ALIGN_BOTTOM_RIGHT, -18, -12);
    lv_obj_set_style_radius(confirm_action, 22, 0);
    lv_obj_set_style_bg_color(confirm_action, lv_color_hex(0xDC2626), 0);
    lv_obj_add_event_cb(confirm_action, power_confirm_cb, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *confirm_action_text =
        make_label(confirm_action, settings_text("Confirm"),
                   settings_ui_font(&lv_font_montserrat_18), 0xFFFFFF);
    lv_obj_center(confirm_action_text);
    lv_obj_add_flag(g_power_confirm, LV_OBJ_FLAG_HIDDEN);

    g_settings_reboot_dialog = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_settings_reboot_dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_settings_reboot_dialog);
    lv_obj_set_style_bg_color(g_settings_reboot_dialog,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_settings_reboot_dialog, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_settings_reboot_dialog, 0, 0);
    lv_obj_set_style_pad_all(g_settings_reboot_dialog, 0, 0);
    lv_obj_remove_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *settings_reboot_card = lv_obj_create(g_settings_reboot_dialog);
    lv_obj_set_size(settings_reboot_card, 500, 278);
    lv_obj_center(settings_reboot_card);
    lv_obj_set_style_radius(settings_reboot_card, 30, 0);
    lv_obj_set_style_border_width(settings_reboot_card, 0, 0);
    lv_obj_set_style_bg_color(settings_reboot_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_remove_flag(settings_reboot_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *settings_reboot_title =
        make_label(settings_reboot_card, settings_text("Restart required"),
                   settings_ui_font(&lv_font_montserrat_28), 0x172033);
    lv_obj_align(settings_reboot_title, LV_ALIGN_TOP_MID, 0, 24);
    g_settings_reboot_countdown =
        make_label(settings_reboot_card, "",
                   settings_ui_font(&lv_font_montserrat_16), 0x526172);
    lv_obj_set_width(g_settings_reboot_countdown, 430);
    lv_obj_set_style_text_align(g_settings_reboot_countdown,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_settings_reboot_countdown, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_settings_reboot_countdown, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *settings_reboot_later = lv_button_create(settings_reboot_card);
    lv_obj_set_size(settings_reboot_later, 204, 64);
    lv_obj_align(settings_reboot_later, LV_ALIGN_BOTTOM_LEFT, 22, -18);
    lv_obj_set_style_radius(settings_reboot_later, 22, 0);
    lv_obj_set_style_bg_color(settings_reboot_later,
                              lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(settings_reboot_later, 0, 0);
    lv_obj_add_event_cb(settings_reboot_later, tap_guard_cb, LV_EVENT_ALL,
                        NULL);
    lv_obj_add_event_cb(settings_reboot_later, settings_reboot_later_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *settings_reboot_later_text =
        make_label(settings_reboot_later, settings_text("Restart later"),
                   settings_ui_font(&lv_font_montserrat_18), 0x334155);
    lv_obj_center(settings_reboot_later_text);
    lv_obj_t *settings_reboot_now = lv_button_create(settings_reboot_card);
    lv_obj_set_size(settings_reboot_now, 204, 64);
    lv_obj_align(settings_reboot_now, LV_ALIGN_BOTTOM_RIGHT, -22, -18);
    lv_obj_set_style_radius(settings_reboot_now, 22, 0);
    lv_obj_set_style_bg_color(settings_reboot_now, lv_color_hex(0x2567B8), 0);
    lv_obj_set_style_bg_color(settings_reboot_now, lv_color_hex(0x174F96),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(settings_reboot_now, 0, 0);
    lv_obj_add_event_cb(settings_reboot_now, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(settings_reboot_now, settings_reboot_now_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *settings_reboot_now_text =
        make_label(settings_reboot_now, settings_text("Restart now"),
                   settings_ui_font(&lv_font_montserrat_18), 0xFFFFFF);
    lv_obj_center(settings_reboot_now_text);
    lv_obj_add_flag(g_settings_reboot_dialog, LV_OBJ_FLAG_HIDDEN);

    settings_create_picker(g_settings_view);

    g_wifi_dialog = lv_obj_create(g_settings_view);
    lv_obj_set_size(g_wifi_dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_wifi_dialog);
    lv_obj_set_style_bg_color(g_wifi_dialog, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_border_width(g_wifi_dialog, 0, 0);
    lv_obj_set_style_pad_all(g_wifi_dialog, 14, 0);
    lv_obj_remove_flag(g_wifi_dialog, LV_OBJ_FLAG_SCROLLABLE);
    g_wifi_dialog_title = make_label(g_wifi_dialog,
                                     settings_text("Connect to Wi-Fi"),
                                     settings_ui_font(&lv_font_montserrat_20),
                                     0x172033);
    lv_obj_set_pos(g_wifi_dialog_title, 16, 8);
    g_wifi_password = lv_textarea_create(g_wifi_dialog);
    lv_obj_set_size(g_wifi_password, 408, 52);
    lv_obj_set_pos(g_wifi_password, 16, 48);
    lv_textarea_set_password_mode(g_wifi_password, true);
    lv_textarea_set_placeholder_text(g_wifi_password,
                                     settings_text("Wi-Fi password"));
    lv_obj_set_style_text_font(g_wifi_password,
                               settings_ui_font(&lv_font_montserrat_16), 0);
    lv_textarea_set_max_length(g_wifi_password, RT_WLAN_PASSWORD_MAX_LENGTH);
    lv_textarea_set_one_line(g_wifi_password, true);
    lv_obj_add_event_cb(g_wifi_password, wifi_connect_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_wifi_password, wifi_password_changed_debug_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *show_password = lv_button_create(g_wifi_dialog);
    lv_obj_set_size(show_password, 54, 52);
    lv_obj_set_pos(show_password, 432, 48);
    lv_obj_set_style_radius(show_password, 14, 0);
    lv_obj_set_style_bg_color(show_password, lv_color_hex(0xE5F1FF), 0);
    lv_obj_set_style_bg_color(show_password, lv_color_hex(0xD1E7FF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(show_password, 0, 0);
    g_wifi_password_visibility_icon =
        make_label(show_password, LV_SYMBOL_EYE_CLOSE,
                   &lv_font_montserrat_18, 0x007AFF);
    lv_obj_center(g_wifi_password_visibility_icon);
    lv_obj_add_event_cb(show_password, tap_guard_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(show_password, wifi_password_visibility_cb,
                        LV_EVENT_CLICKED,
                        g_wifi_password_visibility_icon);
    lv_obj_t *cancel = lv_button_create(g_wifi_dialog);
    lv_obj_set_size(cancel, 112, 52);
    lv_obj_set_pos(cancel, 510, 48);
    lv_obj_add_event_cb(cancel, wifi_dialog_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_text =
        make_label(cancel, settings_text("Cancel"),
                   settings_ui_font(&lv_font_montserrat_14), 0xFFFFFF);
    lv_obj_center(cancel_text);
    lv_obj_t *keyboard = lv_keyboard_create(g_wifi_dialog);
    lv_obj_set_size(keyboard, 612, 270);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        g_wifi_kb_lower, g_wifi_kb_ctrl_36);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        g_wifi_kb_upper, g_wifi_kb_ctrl_36);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_NUMBER,
                        g_wifi_kb_number, g_wifi_kb_ctrl_16);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        g_wifi_kb_number, g_wifi_kb_ctrl_16);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(keyboard, false);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0xD1D1D6), 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_90, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_radius(keyboard, 20, 0);
    lv_obj_set_style_pad_all(keyboard, 6, 0);
    lv_obj_set_style_pad_row(keyboard, 5, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    lv_obj_set_style_radius(keyboard, 9, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x007AFF),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(0x1C1C1E),
                                LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_28,
                               LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_keyboard_set_textarea(keyboard, g_wifi_password);
    /* Replace LVGL's press-triggered editor with the release-validated
     * handler above.  This is required for touch controllers which emit
     * multiple DOWN/UP records during one physical tap. */
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, wifi_keyboard_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    g_wifi_forget_button = lv_button_create(g_wifi_dialog);
    lv_obj_set_size(g_wifi_forget_button, 112, 48);
    lv_obj_set_pos(g_wifi_forget_button, 16, 112);
    lv_obj_set_style_radius(g_wifi_forget_button, 18, 0);
    lv_obj_set_style_bg_color(g_wifi_forget_button,
                              lv_color_hex(0xFFE5E5), 0);
    lv_obj_add_event_cb(g_wifi_forget_button, wifi_forget_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *forget_text =
        make_label(g_wifi_forget_button, settings_text("Forget"),
                   settings_ui_font(&lv_font_montserrat_14), 0xFF3B30);
    lv_obj_center(forget_text);
    lv_obj_add_flag(g_wifi_forget_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *auto_label =
        make_label(g_wifi_dialog, settings_text("Auto-Join"),
                   settings_ui_font(&lv_font_montserrat_14), 0x3A3A3C);
    lv_obj_set_pos(auto_label, 148, 127);
    lv_obj_t *auto_toggle = lv_switch_create(g_wifi_dialog);
    lv_obj_set_size(auto_toggle, 62, 34);
    lv_obj_set_pos(auto_toggle, 232, 119);
    if (g_system_settings.wifi_auto_connect)
        lv_obj_add_state(auto_toggle, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(auto_toggle, lv_color_hex(0x34C759),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_toggle, wifi_auto_connect_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *connect = lv_button_create(g_wifi_dialog);
    lv_obj_set_size(connect, 122, 48);
    lv_obj_align(connect, LV_ALIGN_TOP_RIGHT, -18, 112);
    lv_obj_add_event_cb(connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_text =
        make_label(connect, settings_text("Connect"),
                   settings_ui_font(&lv_font_montserrat_14), 0xFFFFFF);
    lv_obj_center(connect_text);
    lv_obj_add_flag(g_wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_settings_view, LV_OBJ_FLAG_HIDDEN);
    g_wifi_timer = lv_timer_create(wifi_timer_cb, 500, NULL);
    wifi_autoconnect_start();
}

static void camera_status_show(const char *text, uint32_t color)
{
    lv_obj_set_style_bg_color(g_camera_status_dot, lv_color_hex(color), 0);
    lv_label_set_text(g_camera_status, text);
    lv_obj_remove_flag(g_camera_status_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_camera_status_chip);
}

static void camera_status_hide(void)
{
    lv_obj_add_flag(g_camera_status_chip, LV_OBJ_FLAG_HIDDEN);
}

static void camera_capture_flash_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void camera_capture_scale_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)object, value, 0);
}

static int camera_update_last_media(const char *path, bool is_video)
{
    gallery_media_action_t decoded = { 0 };
    lv_anim_t animation;
    char thumbnail_path[320];
    const char *decode_path = path;

    if (is_video) {
        size_t length = strlen(path);
        if (length > 4 && length < sizeof(thumbnail_path)) {
            memcpy(thumbnail_path, path, length - 4);
            memcpy(thumbnail_path + length - 4, ".jpg", 5);
            decode_path = thumbnail_path;
        }
    }

    if (gallery_decode_jpeg(decode_path, 3, &decoded) != 0 && !is_video)
        return -1;

    lv_image_set_src(g_camera_last_media_image, NULL);
    free(g_camera_last_media_data);
    g_camera_last_media_data = decoded.thumbnail_data;
    g_camera_last_media_dsc = decoded.thumbnail;
    snprintf(g_camera_last_media_path, sizeof(g_camera_last_media_path),
             "%s", path);
    g_camera_last_media_is_video = is_video;
    if (g_camera_last_media_data != NULL)
        lv_image_set_src(g_camera_last_media_image,
                         &g_camera_last_media_dsc);
    if (is_video)
        lv_obj_remove_flag(g_camera_last_media_play_badge,
                           LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_camera_last_media_play_badge,
                        LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_camera_last_media_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_transform_scale(g_camera_last_media_button, 220, 0);

    lv_anim_delete(g_camera_last_media_button, camera_capture_scale_anim_cb);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_last_media_button);
    lv_anim_set_exec_cb(&animation, camera_capture_scale_anim_cb);
    lv_anim_set_values(&animation, 220, 256);
    lv_anim_set_duration(&animation, 280);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
    return 0;
}

static void camera_last_media_open_cb(lv_event_t *event)
{
    (void)event;
    if (g_camera_last_media_path[0] == '\0' ||
        access(g_camera_last_media_path, R_OK) != 0) {
        lv_obj_add_flag(g_camera_last_media_button, LV_OBJ_FLAG_HIDDEN);
        show_toast("Media is no longer available");
        return;
    }

    snprintf(g_gallery_media_paths[0], sizeof(g_gallery_media_paths[0]),
             "%s", g_camera_last_media_path);
    g_gallery_media_types[0] = g_camera_last_media_is_video ? 1 : 0;
    g_gallery_media_count = 1;
    g_gallery_media_index = 0;
    g_gallery_return_to_camera = true;
    g_gallery_return_to_dual_camera = false;
    if (g_camera_last_media_is_video)
        dshanpi_camera_stop();
    gallery_open_media_index(0);
}

static void camera_capture_flash_hidden_cb(lv_anim_t *animation)
{
    (void)animation;
    lv_obj_add_flag(g_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(g_camera_capture_flash, LV_OPA_TRANSP, 0);
}

static void camera_capture_animation_begin(void)
{
    lv_anim_delete(g_camera_capture_flash, NULL);
    lv_anim_delete(g_camera_shutter_inner, camera_capture_scale_anim_cb);
    lv_obj_remove_flag(g_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_camera_capture_flash);
    lv_obj_set_style_bg_opa(g_camera_capture_flash, LV_OPA_50, 0);
    lv_obj_set_style_transform_scale(g_camera_shutter_inner, 218, 0);
    lv_refr_now(NULL);
}

static void camera_capture_animation_end(void)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_capture_flash);
    lv_anim_set_exec_cb(&animation, camera_capture_flash_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_50, LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, camera_capture_flash_hidden_cb);
    lv_anim_start(&animation);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_shutter_inner);
    lv_anim_set_exec_cb(&animation, camera_capture_scale_anim_cb);
    lv_anim_set_values(&animation, 218, 256);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void camera_shutter_cb(lv_event_t *event)
{
    (void)event;
    char photo_path[320];

    if (!lv_k230_touch_accept_click()) return;
    if (g_camera_init_running) {
        show_toast("Camera is still starting");
        return;
    }
    if (g_camera_video_mode) {
        if (!dshanpi_camera_is_recording()) {
            g_camera_recording_path[0] = '\0';
            if (dshanpi_camera_record_start(
                    g_camera_recording_path,
                    sizeof(g_camera_recording_path)) != 0) {
                camera_status_show("Recording failed", 0xFF453A);
                show_toast("Unable to start recording");
                return;
            }
            g_camera_record_started_tick = lv_tick_get();
            lv_obj_remove_flag(g_camera_record_badge,
                               LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_camera_record_time, "00:00");
            g_camera_record_timer =
                lv_timer_create(camera_record_timer_cb, 250, NULL);
            lv_obj_set_style_bg_color(g_camera_shutter_inner,
                                      lv_color_hex(0xFF3B30), 0);
            lv_obj_set_size(g_camera_shutter_inner, 44, 44);
            lv_obj_set_style_radius(g_camera_shutter_inner, 11, 0);
            lv_obj_remove_flag(g_camera_record, LV_OBJ_FLAG_CLICKABLE);
            camera_status_hide();
        } else {
            /* Never interpret contact bounce queued while the blocking start
             * call was running as an immediate request to stop recording. */
            if (lv_tick_elaps(g_camera_record_started_tick) < 700U)
                return;
            int stop_result = dshanpi_camera_record_stop();
            if (g_camera_record_timer != NULL) {
                lv_timer_delete(g_camera_record_timer);
                g_camera_record_timer = NULL;
            }
            lv_obj_add_flag(g_camera_record_badge,
                            LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(g_camera_shutter_inner, 74, 74);
            lv_obj_set_style_radius(g_camera_shutter_inner, 37, 0);
            lv_obj_set_style_bg_color(g_camera_shutter_inner,
                                      lv_color_hex(0xFF3B30), 0);
            lv_obj_add_flag(g_camera_record, LV_OBJ_FLAG_CLICKABLE);
            if (stop_result != 0) {
                camera_status_show("Video finalization failed", 0xFF453A);
                show_toast("Video finalization failed");
            } else {
                camera_status_hide();
                if (camera_update_last_media(g_camera_recording_path,
                                             true) != 0)
                    printf("[camera] video thumbnail failed: %s\n",
                           g_camera_recording_path);
                show_toast("MP4 video saved");
            }
        }
        return;
    }
    camera_capture_animation_begin();
    if (dshanpi_camera_capture_jpeg(g_camera_csi, photo_path,
                                    sizeof(photo_path)) != 0) {
        camera_capture_animation_end();
        camera_status_show("Capture failed", 0xFF453A);
        show_toast("Camera capture failed");
        return;
    }

    camera_capture_animation_end();
    camera_status_hide();
    if (camera_update_last_media(photo_path, false) != 0)
        printf("[camera] thumbnail decode failed: %s\n", photo_path);
    show_toast("Photo saved");
}

static void camera_record_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t elapsed = lv_tick_elaps(g_camera_record_started_tick) / 1000U;
    char text[24];
    snprintf(text, sizeof(text), "%02lu:%02lu",
             (unsigned long)(elapsed / 60U),
             (unsigned long)(elapsed % 60U));
    lv_label_set_text(g_camera_record_time, text);
}

static void camera_record_cb(lv_event_t *event)
{
    (void)event;
    if (g_camera_init_running || dshanpi_camera_is_recording())
        return;
    g_camera_video_mode = !g_camera_video_mode;
    if (g_camera_video_mode) {
        lv_obj_set_style_bg_color(g_camera_record,
                                  lv_color_hex(0xFFF0F0), 0);
        lv_obj_set_style_bg_opa(g_camera_record, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(g_camera_record_icon,
                                    lv_color_hex(0xFF3B30), 0);
        lv_label_set_text(g_camera_record_icon, LV_SYMBOL_IMAGE);
        lv_obj_set_style_bg_color(g_camera_shutter_inner,
                                  lv_color_hex(0xFF3B30), 0);
    } else {
        lv_obj_set_style_bg_color(g_camera_record,
                                  lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(g_camera_record, LV_OPA_20, 0);
        lv_obj_set_style_text_color(g_camera_record_icon,
                                    lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(g_camera_record_icon, LV_SYMBOL_VIDEO);
        lv_obj_set_style_bg_color(g_camera_shutter_inner,
                                  lv_color_hex(0xFFFFFF), 0);
    }
    camera_status_hide();
}

static void camera_resolution_cb(lv_event_t *event)
{
    (void)event;
    if (g_camera_init_running || dshanpi_camera_is_recording())
        return;
    int next = (g_camera_resolution + 1) % DSHANPI_CAMERA_RESOLUTION_COUNT;
    if (dshanpi_camera_resolution_save(next) != 0) {
        camera_status_show("Resolution save failed", 0xFF453A);
        return;
    }
    g_camera_resolution = next;
    lv_label_set_text(g_camera_resolution_label,
                      dshanpi_camera_resolution_name(next));
    dshanpi_camera_stop();
    camera_loading_show();
    lv_obj_remove_flag(g_camera_shutter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_camera_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_camera_resolution_button, LV_OBJ_FLAG_CLICKABLE);
    g_camera_init_result = -1;
    g_camera_init_cancelled = 0;
    g_camera_init_running = 1;
    if (pthread_create(&g_camera_init_thread, NULL, camera_init_worker,
                       (void *)(intptr_t)g_camera_csi) != 0) {
        g_camera_init_running = 0;
        camera_loading_hide();
        camera_status_show("Unable to change resolution", 0xFF453A);
        return;
    }
    g_camera_init_timer = lv_timer_create(camera_init_timer_cb, 100, NULL);
}

static void camera_loading_layer_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void camera_loading_scale_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)object, value, 0);
}

static void camera_loading_stop_animations(void)
{
    if (g_camera_loading_outer != NULL)
        lv_anim_delete(g_camera_loading_outer, NULL);
}

static void camera_loading_hidden_cb(lv_anim_t *animation)
{
    (void)animation;
    camera_loading_stop_animations();
    lv_obj_add_flag(g_camera_loading_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa_layered(g_camera_loading_overlay, LV_OPA_COVER, 0);
}

static void camera_loading_show(void)
{
    lv_anim_t animation;

    camera_loading_stop_animations();
    lv_anim_delete(g_camera_loading_overlay, NULL);
    lv_obj_remove_flag(g_camera_loading_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_camera_loading_overlay);
    lv_obj_set_style_opa_layered(g_camera_loading_overlay, LV_OPA_TRANSP, 0);

    lv_spinner_set_anim_params(g_camera_loading_outer, 900, 82);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_loading_overlay);
    lv_anim_set_exec_cb(&animation, camera_loading_layer_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 160);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void camera_loading_hide(void)
{
    lv_anim_t animation;

    lv_anim_delete(g_camera_loading_overlay, NULL);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_loading_overlay);
    lv_anim_set_exec_cb(&animation, camera_loading_layer_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 160);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&animation, camera_loading_hidden_cb);
    lv_anim_start(&animation);
}

static void camera_focus_hide(void)
{
    lv_anim_delete(g_camera_focus_reticle, NULL);
    lv_obj_add_flag(g_camera_focus_reticle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa_layered(g_camera_focus_reticle, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(g_camera_focus_reticle, 256, 0);
}

static void camera_focus_show(void)
{
    lv_anim_t animation;

    lv_anim_delete(g_camera_focus_reticle, NULL);
    lv_obj_remove_flag(g_camera_focus_reticle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_camera_focus_reticle);
    lv_obj_set_style_opa_layered(g_camera_focus_reticle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_scale(g_camera_focus_reticle, 296, 0);

    /* One quick focus-lock gesture, then the reticle stays unobtrusive. */
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_camera_focus_reticle);
    lv_anim_set_exec_cb(&animation, camera_loading_layer_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);

    lv_anim_set_exec_cb(&animation, camera_loading_scale_anim_cb);
    lv_anim_set_values(&animation, 296, 256);
    lv_anim_set_duration(&animation, 420);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static lv_obj_t *camera_focus_bar_create(lv_obj_t *parent, int32_t x,
                                         int32_t y, int32_t width,
                                         int32_t height)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, width, height);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xEAF8FF), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(0x35C7FF), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(bar, 5, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

static void create_camera_view(lv_obj_t *screen)
{
    g_camera_view = lv_obj_create(screen);
    lv_obj_set_size(g_camera_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_camera_view);
    lv_obj_set_style_bg_opa(g_camera_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_camera_view, 0, 0);
    lv_obj_set_style_pad_all(g_camera_view, 0, 0);
    lv_obj_remove_flag(g_camera_view, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Keep the live VIDEO1 layer full-bleed.  The LVGL camera screen is an
     * ARGB overlay, so every opaque object below is deliberately limited to
     * a compact glass-like control surface instead of shrinking the preview.
     */
    g_camera_preview = lv_image_create(g_camera_view);
    lv_obj_set_size(g_camera_preview, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_camera_preview, 0, 0);
    lv_obj_set_style_bg_opa(g_camera_preview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_camera_preview, 0, 0);
    lv_obj_set_style_pad_all(g_camera_preview, 0, 0);
    lv_obj_remove_flag(g_camera_preview,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_inner_align(g_camera_preview, LV_IMAGE_ALIGN_COVER);

    /*
     * Keep the focus UI to a clean corner-only square.  The open silhouette
     * preserves the preview without any circle or dot in the centre.
     */
    g_camera_focus_reticle = lv_obj_create(g_camera_preview);
    lv_obj_set_size(g_camera_focus_reticle, 88, 88);
    lv_obj_align(g_camera_focus_reticle, LV_ALIGN_CENTER, -52, 0);
    lv_obj_set_style_bg_opa(g_camera_focus_reticle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_camera_focus_reticle, 0, 0);
    lv_obj_set_style_pad_all(g_camera_focus_reticle, 0, 0);
    lv_obj_remove_flag(g_camera_focus_reticle,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    static const int16_t focus_bars[][4] = {
        { 6, 6, 18, 2 }, { 6, 6, 2, 18 },
        { 64, 6, 18, 2 }, { 80, 6, 2, 18 },
        { 6, 80, 18, 2 }, { 6, 64, 2, 18 },
        { 64, 80, 18, 2 }, { 80, 64, 2, 18 },
    };
    for (size_t i = 0; i < sizeof(focus_bars) / sizeof(focus_bars[0]); ++i) {
        camera_focus_bar_create(g_camera_focus_reticle,
                                focus_bars[i][0], focus_bars[i][1],
                                focus_bars[i][2], focus_bars[i][3]);
    }

    lv_obj_add_flag(g_camera_focus_reticle, LV_OBJ_FLAG_HIDDEN);

    /* Keep startup fully black until the camera pipeline is ready. */
    g_camera_loading_overlay = lv_obj_create(g_camera_preview);
    lv_obj_remove_style_all(g_camera_loading_overlay);
    lv_obj_set_size(g_camera_loading_overlay, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_camera_loading_overlay, 0, 0);
    lv_obj_set_style_radius(g_camera_loading_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_camera_loading_overlay,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_camera_loading_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(g_camera_loading_overlay,
                                 LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(g_camera_loading_overlay, 0, 0);
    lv_obj_set_style_outline_width(g_camera_loading_overlay, 0, 0);
    lv_obj_set_style_shadow_width(g_camera_loading_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_camera_loading_overlay, 0, 0);
    lv_obj_remove_flag(g_camera_loading_overlay,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    g_camera_loading_outer = lv_spinner_create(g_camera_loading_overlay);
    lv_obj_set_size(g_camera_loading_outer, 50, 50);
    lv_obj_center(g_camera_loading_outer);
    lv_obj_set_style_arc_width(g_camera_loading_outer, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_camera_loading_outer, 4,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_camera_loading_outer,
                               lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_camera_loading_outer,
                               lv_color_hex(0xF4F7FB),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_camera_loading_outer, true,
                                 LV_PART_INDICATOR);

    /* The loading animations are started only while Camera is opening. */
    camera_loading_stop_animations();
    lv_obj_add_flag(g_camera_loading_overlay, LV_OBJ_FLAG_HIDDEN);

    /* A cool-white preview flash is revealed only for still capture. */
    g_camera_capture_flash = lv_obj_create(g_camera_preview);
    lv_obj_set_size(g_camera_capture_flash, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_camera_capture_flash, 0, 0);
    lv_obj_set_style_bg_color(g_camera_capture_flash,
                              lv_color_hex(0xECF9FF), 0);
    lv_obj_set_style_bg_opa(g_camera_capture_flash, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_camera_capture_flash, 0, 0);
    lv_obj_set_style_pad_all(g_camera_capture_flash, 0, 0);
    lv_obj_remove_flag(g_camera_capture_flash,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = create_round_button(g_camera_view, 54, 0x111827);
    lv_obj_set_pos(back, 14, 14);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_20, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_90, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_x(back, 240, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(back, 240, LV_STATE_PRESSED);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, close_camera_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *back_label =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(back_label);

    g_camera_resolution_button = lv_button_create(g_camera_view);
    lv_obj_set_size(g_camera_resolution_button, 86, 42);
    lv_obj_set_pos(g_camera_resolution_button, 82, 20);
    lv_obj_set_style_radius(g_camera_resolution_button, 21, 0);
    lv_obj_set_style_bg_color(g_camera_resolution_button,
                              lv_color_hex(0x080B10), 0);
    lv_obj_set_style_bg_opa(g_camera_resolution_button, LV_OPA_70, 0);
    lv_obj_set_style_border_color(g_camera_resolution_button,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_resolution_button, LV_OPA_30, 0);
    lv_obj_set_style_border_width(g_camera_resolution_button, 1, 0);
    lv_obj_set_style_shadow_width(g_camera_resolution_button, 0, 0);
    lv_obj_set_style_bg_opa(g_camera_resolution_button, LV_OPA_90,
                            LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_camera_resolution_button, camera_resolution_cb,
                        LV_EVENT_CLICKED, NULL);
    g_camera_resolution_label =
        make_label(g_camera_resolution_button, "1080P",
                   &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(g_camera_resolution_label);

    g_camera_record_badge = lv_obj_create(g_camera_view);
    lv_obj_set_size(g_camera_record_badge, 112, 42);
    lv_obj_set_pos(g_camera_record_badge, 264, 20);
    lv_obj_set_style_radius(g_camera_record_badge, 21, 0);
    lv_obj_set_style_bg_color(g_camera_record_badge,
                              lv_color_hex(0x3A1114), 0);
    lv_obj_set_style_bg_opa(g_camera_record_badge, LV_OPA_90, 0);
    lv_obj_set_style_border_color(g_camera_record_badge,
                                  lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_border_opa(g_camera_record_badge, LV_OPA_60, 0);
    lv_obj_set_style_border_width(g_camera_record_badge, 1, 0);
    lv_obj_set_style_pad_all(g_camera_record_badge, 0, 0);
    lv_obj_remove_flag(g_camera_record_badge,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_camera_record_time =
        make_label(g_camera_record_badge, "00:00",
                   &lv_font_montserrat_16, 0xFF6B63);
    lv_obj_align(g_camera_record_time, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_t *record_dot = lv_obj_create(g_camera_record_badge);
    lv_obj_set_size(record_dot, 10, 10);
    lv_obj_align(record_dot, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_radius(record_dot, 5, 0);
    lv_obj_set_style_bg_color(record_dot, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_bg_opa(record_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(record_dot, 0, 0);
    lv_obj_set_style_pad_all(record_dot, 0, 0);
    lv_obj_remove_flag(record_dot,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_camera_record_badge, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *control_rail = lv_obj_create(g_camera_view);
    lv_obj_set_size(control_rail, 120, 300);
    lv_obj_set_pos(control_rail, 508, 90);
    lv_obj_set_style_radius(control_rail, 40, 0);
    lv_obj_set_style_bg_color(control_rail, lv_color_hex(0x080B10), 0);
    lv_obj_set_style_bg_opa(control_rail, LV_OPA_70, 0);
    lv_obj_set_style_border_color(control_rail, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(control_rail, LV_OPA_20, 0);
    lv_obj_set_style_border_width(control_rail, 1, 0);
    lv_obj_set_style_pad_all(control_rail, 0, 0);
    lv_obj_remove_flag(control_rail, LV_OBJ_FLAG_SCROLLABLE);

    g_camera_shutter = create_round_button(control_rail, 100, 0xFFFFFF);
    lv_obj_set_pos(g_camera_shutter, 10, 88);
    lv_obj_set_style_bg_opa(g_camera_shutter, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_camera_shutter,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_shutter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_camera_shutter, 3, 0);
    lv_obj_set_style_transform_scale_x(g_camera_shutter, 242,
                                       LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(g_camera_shutter, 242,
                                       LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_camera_shutter, camera_shutter_cb,
                        LV_EVENT_CLICKED, NULL);
    g_camera_shutter_inner = lv_obj_create(g_camera_shutter);
    lv_obj_set_size(g_camera_shutter_inner, 74, 74);
    lv_obj_center(g_camera_shutter_inner);
    lv_obj_set_style_radius(g_camera_shutter_inner, 37, 0);
    lv_obj_set_style_bg_color(g_camera_shutter_inner,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_camera_shutter_inner, 0, 0);
    lv_obj_remove_flag(g_camera_shutter_inner,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    g_camera_last_media_button = lv_button_create(control_rail);
    lv_obj_set_size(g_camera_last_media_button, 92, 68);
    lv_obj_set_pos(g_camera_last_media_button, 14, 206);
    lv_obj_set_style_radius(g_camera_last_media_button, 18, 0);
    lv_obj_set_style_bg_color(g_camera_last_media_button,
                              lv_color_hex(0x0A0F18), 0);
    lv_obj_set_style_bg_opa(g_camera_last_media_button, LV_OPA_90, 0);
    lv_obj_set_style_border_color(g_camera_last_media_button,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_last_media_button, LV_OPA_60, 0);
    lv_obj_set_style_border_width(g_camera_last_media_button, 2, 0);
    lv_obj_set_style_shadow_color(g_camera_last_media_button,
                                  lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_camera_last_media_button, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(g_camera_last_media_button, 14, 0);
    lv_obj_set_style_shadow_ofs_y(g_camera_last_media_button, 5, 0);
    lv_obj_set_style_pad_all(g_camera_last_media_button, 0, 0);
    lv_obj_set_style_clip_corner(g_camera_last_media_button, true, 0);
    lv_obj_set_style_transform_scale(g_camera_last_media_button, 242,
                                     LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_camera_last_media_button,
                        camera_last_media_open_cb, LV_EVENT_CLICKED, NULL);
    g_camera_last_media_image = lv_image_create(g_camera_last_media_button);
    lv_obj_set_size(g_camera_last_media_image, 86, 62);
    lv_obj_center(g_camera_last_media_image);
    lv_image_set_inner_align(g_camera_last_media_image,
                             LV_IMAGE_ALIGN_COVER);
    lv_obj_set_style_radius(g_camera_last_media_image, 15, 0);
    lv_obj_remove_flag(g_camera_last_media_image,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_camera_last_media_play_badge = lv_obj_create(
        g_camera_last_media_button);
    lv_obj_set_size(g_camera_last_media_play_badge, 36, 36);
    lv_obj_center(g_camera_last_media_play_badge);
    lv_obj_set_style_radius(g_camera_last_media_play_badge, 18, 0);
    lv_obj_set_style_bg_color(g_camera_last_media_play_badge,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_camera_last_media_play_badge, LV_OPA_70, 0);
    lv_obj_set_style_border_color(g_camera_last_media_play_badge,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_last_media_play_badge, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_camera_last_media_play_badge, 1, 0);
    lv_obj_set_style_pad_all(g_camera_last_media_play_badge, 0, 0);
    lv_obj_remove_flag(g_camera_last_media_play_badge,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *play_icon =
        make_label(g_camera_last_media_play_badge, LV_SYMBOL_PLAY,
                   &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(play_icon);
    lv_obj_add_flag(g_camera_last_media_play_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_camera_last_media_button, LV_OBJ_FLAG_HIDDEN);

    g_camera_record = lv_button_create(control_rail);
    lv_obj_set_size(g_camera_record, 90, 58);
    lv_obj_set_pos(g_camera_record, 15, 14);
    lv_obj_set_style_radius(g_camera_record, 29, 0);
    lv_obj_set_style_bg_color(g_camera_record, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_camera_record, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_camera_record,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_record, LV_OPA_20, 0);
    lv_obj_set_style_border_width(g_camera_record, 1, 0);
    lv_obj_set_style_shadow_width(g_camera_record, 0, 0);
    lv_obj_set_style_bg_opa(g_camera_record, LV_OPA_40,
                            LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_camera_record, camera_record_cb,
                        LV_EVENT_CLICKED, NULL);
    g_camera_record_icon =
        make_label(g_camera_record, LV_SYMBOL_VIDEO,
                   &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_center(g_camera_record_icon);

    g_camera_status_chip = lv_obj_create(g_camera_view);
    lv_obj_set_size(g_camera_status_chip, 388, 44);
    lv_obj_set_pos(g_camera_status_chip, 16, 420);
    lv_obj_set_style_radius(g_camera_status_chip, 22, 0);
    lv_obj_set_style_bg_color(g_camera_status_chip,
                              lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(g_camera_status_chip, LV_OPA_70, 0);
    lv_obj_set_style_border_color(g_camera_status_chip,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_camera_status_chip, LV_OPA_20, 0);
    lv_obj_set_style_border_width(g_camera_status_chip, 1, 0);
    lv_obj_set_style_pad_all(g_camera_status_chip, 0, 0);
    lv_obj_remove_flag(g_camera_status_chip,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_camera_status_dot = lv_obj_create(g_camera_status_chip);
    lv_obj_set_size(g_camera_status_dot, 8, 8);
    lv_obj_set_pos(g_camera_status_dot, 16, 18);
    lv_obj_set_style_radius(g_camera_status_dot, 4, 0);
    lv_obj_set_style_bg_color(g_camera_status_dot,
                              lv_color_hex(0xFFCC00), 0);
    lv_obj_set_style_border_width(g_camera_status_dot, 0, 0);
    lv_obj_remove_flag(g_camera_status_dot,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_camera_status =
        make_label(g_camera_status_chip, "",
                   &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_set_pos(g_camera_status, 34, 13);
    lv_obj_set_width(g_camera_status, 338);
    lv_label_set_long_mode(g_camera_status, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(g_camera_status_chip, LV_OBJ_FLAG_HIDDEN);
}

static void *camera_init_worker(void *argument)
{
    int csi = (int)(intptr_t)argument;
    g_camera_init_result = dshanpi_camera_start(csi, g_camera_resolution);
    g_camera_init_running = 0;
    return NULL;
}

static void camera_init_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_camera_init_running)
        return;
    pthread_join(g_camera_init_thread, NULL);
    if (g_camera_init_cancelled) {
        dshanpi_camera_stop();
        camera_loading_hide();
        lv_timer_delete(g_camera_init_timer);
        g_camera_init_timer = NULL;
        return;
    }
    camera_loading_hide();
    if (g_camera_init_result != 0) {
        camera_status_show("Camera initialization failed", 0xFF453A);
    } else {
        camera_focus_show();
        lv_obj_add_flag(g_camera_shutter, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_camera_record, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_camera_resolution_button, LV_OBJ_FLAG_CLICKABLE);
        camera_status_hide();
    }
    lv_timer_delete(g_camera_init_timer);
    g_camera_init_timer = NULL;
}

static void dual_camera_status_hide_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_add_flag(g_dual_camera_status, LV_OBJ_FLAG_HIDDEN);
    g_dual_camera_status_timer = NULL;
}

static void dual_camera_status_show(const char *text, uint32_t color)
{
    if (g_dual_camera_status_timer != NULL)
        lv_timer_delete(g_dual_camera_status_timer);
    lv_label_set_text(g_dual_camera_status, text);
    lv_obj_set_style_text_color(g_dual_camera_status,
                                lv_color_hex(color), 0);
    lv_obj_remove_flag(g_dual_camera_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_dual_camera_status);
    g_dual_camera_status_timer =
        lv_timer_create(dual_camera_status_hide_cb, 1800, NULL);
    lv_timer_set_repeat_count(g_dual_camera_status_timer, 1);
}

static void dual_camera_record_visual(int recording)
{
    if (recording) {
        lv_obj_set_size(g_dual_camera_record_inner, 36, 36);
        lv_obj_set_style_radius(g_dual_camera_record_inner, 9, 0);
        lv_obj_set_style_bg_color(g_dual_camera_record_inner,
                                  lv_color_hex(0xFF3B30), 0);
        lv_obj_remove_flag(g_dual_camera_time_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(g_dual_camera_record_inner, 62, 62);
        lv_obj_set_style_radius(g_dual_camera_record_inner, 31, 0);
        lv_obj_set_style_bg_color(
            g_dual_camera_record_inner,
            lv_color_hex(g_dual_camera_video_mode ? 0xFF3B30 : 0xFFFFFF),
            0);
        lv_obj_add_flag(g_dual_camera_time_badge, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_center(g_dual_camera_record_inner);
}

static void dual_camera_mode_visual(void)
{
    if (g_dual_camera_video_mode) {
        lv_obj_set_style_bg_color(g_dual_camera_mode,
                                  lv_color_hex(0xFFF0F0), 0);
        lv_obj_set_style_bg_opa(g_dual_camera_mode, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(g_dual_camera_mode_icon,
                                    lv_color_hex(0xFF3B30), 0);
        lv_label_set_text(g_dual_camera_mode_icon, LV_SYMBOL_IMAGE);
    } else {
        lv_obj_set_style_bg_color(g_dual_camera_mode,
                                  lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(g_dual_camera_mode, LV_OPA_20, 0);
        lv_obj_set_style_text_color(g_dual_camera_mode_icon,
                                    lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(g_dual_camera_mode_icon, LV_SYMBOL_VIDEO);
    }
    dual_camera_record_visual(0);
}

static void dual_camera_capture_flash_hidden_cb(lv_anim_t *animation)
{
    (void)animation;
    lv_obj_add_flag(g_dual_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(g_dual_camera_capture_flash,
                            LV_OPA_TRANSP, 0);
}

static void dual_camera_capture_animation_begin(void)
{
    lv_anim_delete(g_dual_camera_capture_flash, NULL);
    lv_anim_delete(g_dual_camera_record_inner,
                   camera_capture_scale_anim_cb);
    lv_obj_remove_flag(g_dual_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_dual_camera_capture_flash);
    lv_obj_set_style_bg_opa(g_dual_camera_capture_flash, LV_OPA_50, 0);
    lv_obj_set_style_transform_scale(g_dual_camera_record_inner, 218, 0);
    lv_refr_now(NULL);
}

static void dual_camera_capture_animation_end(void)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_dual_camera_capture_flash);
    lv_anim_set_exec_cb(&animation, camera_capture_flash_opa_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_50, LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation,
                             dual_camera_capture_flash_hidden_cb);
    lv_anim_start(&animation);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, g_dual_camera_record_inner);
    lv_anim_set_exec_cb(&animation, camera_capture_scale_anim_cb);
    lv_anim_set_values(&animation, 218, 256);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void dual_camera_session_media_reset(void)
{
    g_dual_camera_session_count = 0;
    if (g_dual_camera_last_media_button != NULL)
        lv_obj_add_flag(g_dual_camera_last_media_button,
                        LV_OBJ_FLAG_HIDDEN);
    if (g_dual_camera_last_media_image != NULL)
        lv_image_set_src(g_dual_camera_last_media_image, NULL);
    free(g_dual_camera_last_media_data);
    g_dual_camera_last_media_data = NULL;
    memset(&g_dual_camera_last_media_dsc, 0,
           sizeof(g_dual_camera_last_media_dsc));
}

static int dual_camera_session_media_add(const char *path, bool is_video)
{
    gallery_media_action_t decoded = { 0 };
    char thumbnail_path[320];
    const char *decode_path = path;

    if (path == NULL || path[0] == '\0')
        return -1;
    if (is_video) {
        size_t length = strlen(path);
        if (length <= 4 || length >= sizeof(thumbnail_path))
            return -1;
        memcpy(thumbnail_path, path, length - 4);
        memcpy(thumbnail_path + length - 4, ".jpg", 5);
        decode_path = thumbnail_path;
    }
    if (gallery_decode_grid_thumbnail(decode_path, &decoded) != 0)
        return -1;

    if (g_dual_camera_session_count == DUAL_CAMERA_SESSION_MEDIA_MAX) {
        memmove(g_dual_camera_session_paths,
                g_dual_camera_session_paths + 1,
                sizeof(g_dual_camera_session_paths[0]) *
                    (DUAL_CAMERA_SESSION_MEDIA_MAX - 1));
        memmove(g_dual_camera_session_video,
                g_dual_camera_session_video + 1,
                sizeof(g_dual_camera_session_video[0]) *
                    (DUAL_CAMERA_SESSION_MEDIA_MAX - 1));
        --g_dual_camera_session_count;
    }
    snprintf(g_dual_camera_session_paths[g_dual_camera_session_count],
             sizeof(g_dual_camera_session_paths[0]), "%s", path);
    g_dual_camera_session_video[g_dual_camera_session_count] = is_video;
    ++g_dual_camera_session_count;

    lv_image_set_src(g_dual_camera_last_media_image, NULL);
    free(g_dual_camera_last_media_data);
    g_dual_camera_last_media_data = decoded.thumbnail_data;
    g_dual_camera_last_media_dsc = decoded.thumbnail;
    lv_image_set_src(g_dual_camera_last_media_image,
                     &g_dual_camera_last_media_dsc);
    lv_obj_add_flag(g_dual_camera_last_media_icon, LV_OBJ_FLAG_HIDDEN);
    if (is_video)
        lv_obj_remove_flag(g_dual_camera_last_media_play_badge,
                           LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_dual_camera_last_media_play_badge,
                        LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_dual_camera_last_media_button,
                       LV_OBJ_FLAG_HIDDEN);
    return 0;
}

static void dual_camera_last_media_open_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click() || g_dual_camera_init_running ||
        dshanpi_dual_camera_is_recording())
        return;
    if (g_dual_camera_session_count == 0) {
        dual_camera_status_show("No captures in this session", 0xFFFFFF);
        return;
    }

    size_t valid_count = 0;
    for (size_t source = 0; source < g_dual_camera_session_count; ++source) {
        if (access(g_dual_camera_session_paths[source], R_OK) != 0)
            continue;
        memcpy(g_gallery_media_paths[valid_count],
               g_dual_camera_session_paths[source],
               sizeof(g_gallery_media_paths[valid_count]));
        g_gallery_media_paths[valid_count]
                             [sizeof(g_gallery_media_paths[0]) - 1] = '\0';
        g_gallery_media_types[valid_count] =
            g_dual_camera_session_video[source] ? 1 : 0;
        ++valid_count;
    }
    if (valid_count == 0) {
        dual_camera_session_media_reset();
        dual_camera_status_show("No captures in this session", 0xFFB4AB);
        return;
    }

    dual_camera_cleanup_ui_timers();
    dshanpi_dual_camera_stop();
    dual_camera_record_visual(0);
    g_dual_camera_pip_dragging = false;
    g_dual_camera_pip_locked = false;
    g_gallery_media_count = valid_count;
    g_gallery_media_index = valid_count - 1;
    g_gallery_return_to_camera = false;
    g_gallery_return_to_dual_camera = true;
    gallery_open_media_index(g_gallery_media_index);
}

static void dual_camera_record_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t elapsed =
        lv_tick_elaps(g_dual_camera_record_started_tick) / 1000U;
    char text[24];
    snprintf(text, sizeof(text), "%02lu:%02lu",
             (unsigned long)(elapsed / 60U),
             (unsigned long)(elapsed % 60U));
    lv_label_set_text(g_dual_camera_time, text);
}

static int dual_camera_video_record_start(void)
{
    g_dual_camera_pip_locked = true;
    g_dual_camera_pip_dragging = false;
    lv_obj_remove_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
    g_dual_camera_recording_path[0] = '\0';
    if (dshanpi_dual_camera_record_start(
            g_dual_camera_recording_path,
            sizeof(g_dual_camera_recording_path)) != 0) {
        g_dual_camera_pip_locked = false;
        lv_obj_add_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
        return -1;
    }
    g_dual_camera_record_started_tick = lv_tick_get();
    lv_label_set_text(g_dual_camera_time, "00:00");
    g_dual_camera_record_timer =
        lv_timer_create(dual_camera_record_timer_cb, 250, NULL);
    dual_camera_record_visual(1);
    lv_obj_remove_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
    return 0;
}

static int dual_camera_video_record_stop(void)
{
    int result = dshanpi_dual_camera_record_stop();

    if (g_dual_camera_record_timer != NULL) {
        lv_timer_delete(g_dual_camera_record_timer);
        g_dual_camera_record_timer = NULL;
    }
    dual_camera_record_visual(0);
    g_dual_camera_pip_locked = false;
    lv_obj_add_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
    if (result == 0)
        dual_camera_session_media_add(g_dual_camera_recording_path, true);
    return result;
}

static void dual_camera_fps_test_stop_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    g_dual_camera_fps_test_timer = NULL;
    int result = dual_camera_video_record_stop();
    printf("[dual-camera-test] recording %s: %s\n",
           result == 0 ? "complete" : "failed",
           g_dual_camera_recording_path);
}

static int dual_camera_clamp(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static void dual_camera_pip_drag_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;

    if (indev == NULL)
        return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        if (g_dual_camera_pip_locked || g_dual_camera_init_running ||
            dshanpi_dual_camera_is_recording())
            return;
        g_dual_camera_pip_press = point;
        g_dual_camera_pip_press_x = g_dual_camera_pip_x;
        g_dual_camera_pip_press_y = g_dual_camera_pip_y;
        g_dual_camera_pip_dragging = true;
        g_dual_camera_pip_moved = false;
    } else if (code == LV_EVENT_PRESSING &&
               g_dual_camera_pip_dragging) {
        int dx = (int)point.x - (int)g_dual_camera_pip_press.x;
        int dy = (int)point.y - (int)g_dual_camera_pip_press.y;
        if (!g_dual_camera_pip_moved &&
            abs(dx) < DUAL_PIP_DRAG_THRESHOLD &&
            abs(dy) < DUAL_PIP_DRAG_THRESHOLD)
            goto done;
        g_dual_camera_pip_moved = true;
        int x = g_dual_camera_pip_press_x + dx;
        int y = g_dual_camera_pip_press_y + dy;
        x = dual_camera_clamp(x, DUAL_PIP_MARGIN,
                              UI_WIDTH - DUAL_PIP_WIDTH - DUAL_PIP_MARGIN);
        y = dual_camera_clamp(y, DUAL_PIP_MARGIN,
                              UI_HEIGHT - DUAL_PIP_HEIGHT - DUAL_PIP_MARGIN);
        x &= ~1;
        y &= ~1;
        if ((x != g_dual_camera_pip_x || y != g_dual_camera_pip_y) &&
            dshanpi_dual_camera_set_pip_position((unsigned)x,
                                                 (unsigned)y) == 0) {
            g_dual_camera_pip_x = x;
            g_dual_camera_pip_y = y;
            lv_obj_set_pos(g_dual_camera_pip_frame, x, y);
        }
    } else if (code == LV_EVENT_RELEASED ||
               code == LV_EVENT_PRESS_LOST) {
        int swap = code == LV_EVENT_RELEASED &&
                   g_dual_camera_pip_dragging &&
                   !g_dual_camera_pip_moved;
        g_dual_camera_pip_dragging = false;
        if (swap && dshanpi_dual_camera_swap_views() != 0)
            dual_camera_status_show("Unable to switch cameras", 0xFFB4AB);
    }
done:
    lv_event_stop_bubbling(event);
}

static void dual_camera_record_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click() || g_dual_camera_init_running)
        return;

    if (!g_dual_camera_video_mode) {
        char photo_path[320];
        int result;

        g_dual_camera_pip_locked = true;
        g_dual_camera_pip_dragging = false;
        lv_obj_remove_flag(g_dual_camera_pip_frame,
                           LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
        dual_camera_capture_animation_begin();
        result = dshanpi_dual_camera_capture_jpeg(
            photo_path, sizeof(photo_path));
        dual_camera_capture_animation_end();
        g_dual_camera_pip_locked = false;
        lv_obj_add_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
        if (result == 0)
            dual_camera_session_media_add(photo_path, false);
        dual_camera_status_show(result == 0 ? "Photo saved"
                                            : "Photo capture failed",
                                result == 0 ? 0xFFFFFF : 0xFFB4AB);
        return;
    }

    if (!dshanpi_dual_camera_is_recording()) {
        if (dual_camera_video_record_start() != 0) {
            dual_camera_status_show("Unable to start recording", 0xFFB4AB);
            return;
        }
    } else {
        if (lv_tick_elaps(g_dual_camera_record_started_tick) < 700U)
            return;
        int result = dual_camera_video_record_stop();
        dual_camera_status_show(result == 0 ? "Video saved"
                                            : "Video save failed",
                                result == 0 ? 0xFFFFFF : 0xFFB4AB);
    }
}

static void dual_camera_mode_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click() || g_dual_camera_init_running ||
        dshanpi_dual_camera_is_recording())
        return;
    g_dual_camera_video_mode = !g_dual_camera_video_mode;
    dual_camera_mode_visual();
    if (g_dual_camera_status_timer != NULL) {
        lv_timer_delete(g_dual_camera_status_timer);
        g_dual_camera_status_timer = NULL;
    }
    lv_obj_add_flag(g_dual_camera_status, LV_OBJ_FLAG_HIDDEN);
}

static void dual_camera_resolution_cb(lv_event_t *event)
{
    (void)event;
    if (!lv_k230_touch_accept_click() || g_dual_camera_init_running ||
        dshanpi_dual_camera_is_recording())
        return;
    int next = (g_dual_camera_resolution + 1) %
               DSHANPI_CAMERA_RESOLUTION_COUNT;
    if (dshanpi_camera_resolution_save(next) != 0) {
        dual_camera_status_show("Resolution save failed", 0xFFB4AB);
        return;
    }
    g_dual_camera_resolution = next;
    lv_label_set_text(g_dual_camera_resolution_label,
                      dshanpi_camera_resolution_name(next));
    dshanpi_dual_camera_stop();
    lv_obj_remove_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_dual_camera_loading);
    lv_obj_remove_flag(g_dual_camera_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dual_camera_resolution_button,
                       LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
    g_dual_camera_init_result = -1;
    g_dual_camera_init_cancelled = 0;
    g_dual_camera_init_running = 1;
    if (pthread_create(&g_dual_camera_init_thread, NULL,
                       dual_camera_init_worker, NULL) != 0) {
        g_dual_camera_init_running = 0;
        lv_obj_add_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
        dual_camera_status_show("Unable to change resolution", 0xFFB4AB);
        return;
    }
    g_dual_camera_init_timer =
        lv_timer_create(dual_camera_init_timer_cb, 100, NULL);
}

static void dual_camera_cleanup_ui_timers(void)
{
    if (g_dual_camera_init_timer != NULL) {
        lv_timer_delete(g_dual_camera_init_timer);
        g_dual_camera_init_timer = NULL;
    }
    if (g_dual_camera_record_timer != NULL) {
        lv_timer_delete(g_dual_camera_record_timer);
        g_dual_camera_record_timer = NULL;
    }
    if (g_dual_camera_status_timer != NULL) {
        lv_timer_delete(g_dual_camera_status_timer);
        g_dual_camera_status_timer = NULL;
    }
    if (g_dual_camera_fps_test_timer != NULL) {
        lv_timer_delete(g_dual_camera_fps_test_timer);
        g_dual_camera_fps_test_timer = NULL;
    }
}

static void close_dual_camera_cb(lv_event_t *event)
{
    (void)event;
    if (g_dual_camera_init_running) {
        g_dual_camera_init_cancelled = 1;
        return;
    }
    dual_camera_cleanup_ui_timers();
    dshanpi_dual_camera_stop();
    dual_camera_record_visual(0);
    g_dual_camera_pip_dragging = false;
    g_dual_camera_pip_locked = false;
    dual_camera_session_media_reset();
    lv_obj_remove_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_screen_load(g_home_screen);
    lv_indev_t *indev = lv_indev_active();
    if (indev != NULL)
        lv_indev_wait_release(indev);
}

static void *dual_camera_init_worker(void *argument)
{
    (void)argument;
    g_dual_camera_init_result =
        dshanpi_dual_camera_start(g_dual_camera_resolution);
    g_dual_camera_init_running = 0;
    return NULL;
}

static void dual_camera_init_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_dual_camera_init_running)
        return;
    pthread_join(g_dual_camera_init_thread, NULL);
    if (g_dual_camera_init_cancelled) {
        dshanpi_dual_camera_stop();
        lv_screen_load(g_home_screen);
    } else if (g_dual_camera_init_result == 0) {
        lv_obj_add_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_dual_camera_record, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_dual_camera_resolution_button,
                        LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
        if (g_dual_camera_fps_test_seconds > 0) {
            g_dual_camera_video_mode = true;
            dual_camera_mode_visual();
            if (dual_camera_video_record_start() == 0) {
                printf("[dual-camera-test] recording for %u seconds\n",
                       g_dual_camera_fps_test_seconds);
                g_dual_camera_fps_test_timer = lv_timer_create(
                    dual_camera_fps_test_stop_cb,
                    g_dual_camera_fps_test_seconds * 1000U, NULL);
            } else {
                printf("[dual-camera-test] unable to start recording\n");
            }
            g_dual_camera_fps_test_seconds = 0;
        }
    } else {
        lv_obj_add_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
        dual_camera_status_show("Dual camera initialization failed",
                                0xFFB4AB);
    }
    lv_timer_delete(g_dual_camera_init_timer);
    g_dual_camera_init_timer = NULL;
}

static void create_dual_camera_view(lv_obj_t *screen)
{
    g_dual_camera_view = lv_obj_create(screen);
    lv_obj_set_size(g_dual_camera_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_dual_camera_view);
    lv_obj_set_style_bg_opa(g_dual_camera_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dual_camera_view, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_view, 0, 0);
    lv_obj_remove_flag(g_dual_camera_view, LV_OBJ_FLAG_SCROLLABLE);

    g_dual_camera_pip_frame = lv_obj_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_pip_frame, DUAL_PIP_WIDTH,
                    DUAL_PIP_HEIGHT);
    lv_obj_set_pos(g_dual_camera_pip_frame, g_dual_camera_pip_x,
                   g_dual_camera_pip_y);
    /* VIDEO2 is a rectangular hardware layer and cannot be clipped by LVGL.
     * A square, shadow-free outline avoids dark corner wedges. */
    lv_obj_set_style_radius(g_dual_camera_pip_frame, 0, 0);
    lv_obj_set_style_bg_opa(g_dual_camera_pip_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(g_dual_camera_pip_frame,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_dual_camera_pip_frame, LV_OPA_80, 0);
    lv_obj_set_style_border_width(g_dual_camera_pip_frame, 2, 0);
    lv_obj_set_style_shadow_opa(g_dual_camera_pip_frame,
                                LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_dual_camera_pip_frame, 0, 0);
    lv_obj_add_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(g_dual_camera_pip_frame,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_dual_camera_pip_frame, dual_camera_pip_drag_cb,
                        LV_EVENT_ALL, NULL);

    /* A brief cool-white flash confirms a still photo without obscuring the
     * live view before or after capture. */
    g_dual_camera_capture_flash = lv_obj_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_capture_flash, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_dual_camera_capture_flash, 0, 0);
    lv_obj_set_style_bg_color(g_dual_camera_capture_flash,
                              lv_color_hex(0xECF9FF), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_capture_flash,
                            LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dual_camera_capture_flash, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_capture_flash, 0, 0);
    lv_obj_remove_flag(g_dual_camera_capture_flash,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_dual_camera_capture_flash, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = create_round_button(g_dual_camera_view, 54, 0x05070A);
    lv_obj_set_pos(back, 18, 18);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_30, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, close_dual_camera_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *back_icon = make_label(back, LV_SYMBOL_LEFT,
                                     &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(back_icon);

    g_dual_camera_resolution_button = lv_button_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_resolution_button, 86, 42);
    lv_obj_set_pos(g_dual_camera_resolution_button, 82, 20);
    lv_obj_set_style_radius(g_dual_camera_resolution_button, 21, 0);
    lv_obj_set_style_bg_color(g_dual_camera_resolution_button,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_resolution_button, LV_OPA_70, 0);
    lv_obj_set_style_border_color(g_dual_camera_resolution_button,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_dual_camera_resolution_button,
                                LV_OPA_30, 0);
    lv_obj_set_style_border_width(g_dual_camera_resolution_button, 1, 0);
    lv_obj_set_style_shadow_width(g_dual_camera_resolution_button, 0, 0);
    lv_obj_add_event_cb(g_dual_camera_resolution_button,
                        dual_camera_resolution_cb, LV_EVENT_CLICKED, NULL);
    g_dual_camera_resolution_label =
        make_label(g_dual_camera_resolution_button, "1080P",
                   &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(g_dual_camera_resolution_label);

    g_dual_camera_time_badge = lv_obj_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_time_badge, 104, 40);
    lv_obj_set_pos(g_dual_camera_time_badge, 268, 22);
    lv_obj_set_style_radius(g_dual_camera_time_badge, 20, 0);
    lv_obj_set_style_bg_color(g_dual_camera_time_badge,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_time_badge, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_dual_camera_time_badge, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_time_badge, 0, 0);
    lv_obj_remove_flag(g_dual_camera_time_badge,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *record_dot = lv_obj_create(g_dual_camera_time_badge);
    lv_obj_set_size(record_dot, 8, 8);
    lv_obj_set_pos(record_dot, 15, 16);
    lv_obj_set_style_radius(record_dot, 4, 0);
    lv_obj_set_style_bg_color(record_dot, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_border_width(record_dot, 0, 0);
    lv_obj_remove_flag(record_dot,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_dual_camera_time = make_label(g_dual_camera_time_badge, "00:00",
                                    &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_set_pos(g_dual_camera_time, 32, 10);
    lv_obj_add_flag(g_dual_camera_time_badge, LV_OBJ_FLAG_HIDDEN);

    g_dual_camera_record = create_round_button(g_dual_camera_view, 86,
                                                0xFFFFFF);
    lv_obj_set_pos(g_dual_camera_record, 277, 374);
    lv_obj_set_style_bg_opa(g_dual_camera_record, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_dual_camera_record,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_dual_camera_record, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dual_camera_record, 3, 0);
    lv_obj_set_style_transform_scale(g_dual_camera_record, 238,
                                     LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_dual_camera_record, dual_camera_record_cb,
                        LV_EVENT_CLICKED, NULL);
    g_dual_camera_record_inner = lv_obj_create(g_dual_camera_record);
    lv_obj_set_size(g_dual_camera_record_inner, 62, 62);
    lv_obj_center(g_dual_camera_record_inner);
    lv_obj_set_style_radius(g_dual_camera_record_inner, 31, 0);
    lv_obj_set_style_bg_color(g_dual_camera_record_inner,
                              lv_color_hex(0xFF3B30), 0);
    lv_obj_set_style_border_width(g_dual_camera_record_inner, 0, 0);
    lv_obj_remove_flag(g_dual_camera_record_inner,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    g_dual_camera_mode = lv_button_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_mode, 100, 64);
    lv_obj_set_pos(g_dual_camera_mode, 382, 385);
    lv_obj_set_style_radius(g_dual_camera_mode, 32, 0);
    lv_obj_set_style_bg_color(g_dual_camera_mode,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_mode, LV_OPA_20, 0);
    lv_obj_set_style_border_color(g_dual_camera_mode,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_dual_camera_mode, LV_OPA_20, 0);
    lv_obj_set_style_border_width(g_dual_camera_mode, 1, 0);
    lv_obj_set_style_shadow_width(g_dual_camera_mode, 0, 0);
    lv_obj_set_style_bg_opa(g_dual_camera_mode, LV_OPA_40,
                            LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_dual_camera_mode, dual_camera_mode_cb,
                        LV_EVENT_CLICKED, NULL);
    g_dual_camera_mode_icon =
        make_label(g_dual_camera_mode, LV_SYMBOL_VIDEO,
                   &lv_font_montserrat_36, 0xFFFFFF);
    lv_obj_center(g_dual_camera_mode_icon);

    g_dual_camera_last_media_button = lv_button_create(g_dual_camera_view);
    lv_obj_set_size(g_dual_camera_last_media_button, 82, 70);
    lv_obj_set_pos(g_dual_camera_last_media_button, 548, 382);
    lv_obj_set_style_radius(g_dual_camera_last_media_button, 18, 0);
    lv_obj_set_style_bg_color(g_dual_camera_last_media_button,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_last_media_button, LV_OPA_90, 0);
    lv_obj_set_style_border_color(g_dual_camera_last_media_button,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(g_dual_camera_last_media_button,
                                LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_dual_camera_last_media_button, 2, 0);
    lv_obj_set_style_shadow_width(g_dual_camera_last_media_button, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_last_media_button, 0, 0);
    lv_obj_set_style_clip_corner(g_dual_camera_last_media_button, true, 0);
    lv_obj_add_event_cb(g_dual_camera_last_media_button,
                        dual_camera_last_media_open_cb,
                        LV_EVENT_CLICKED, NULL);
    g_dual_camera_last_media_image =
        lv_image_create(g_dual_camera_last_media_button);
    lv_obj_set_size(g_dual_camera_last_media_image, 76, 64);
    lv_obj_center(g_dual_camera_last_media_image);
    lv_image_set_inner_align(g_dual_camera_last_media_image,
                             LV_IMAGE_ALIGN_COVER);
    lv_obj_remove_flag(g_dual_camera_last_media_image,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_dual_camera_last_media_icon =
        make_label(g_dual_camera_last_media_button, LV_SYMBOL_IMAGE,
                   &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_center(g_dual_camera_last_media_icon);
    g_dual_camera_last_media_play_badge =
        lv_obj_create(g_dual_camera_last_media_button);
    lv_obj_set_size(g_dual_camera_last_media_play_badge, 34, 34);
    lv_obj_center(g_dual_camera_last_media_play_badge);
    lv_obj_set_style_radius(g_dual_camera_last_media_play_badge, 17, 0);
    lv_obj_set_style_bg_color(g_dual_camera_last_media_play_badge,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_last_media_play_badge,
                            LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_dual_camera_last_media_play_badge, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_last_media_play_badge, 0, 0);
    lv_obj_remove_flag(g_dual_camera_last_media_play_badge,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *play_icon =
        make_label(g_dual_camera_last_media_play_badge, LV_SYMBOL_PLAY,
                   &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(play_icon);
    lv_obj_add_flag(g_dual_camera_last_media_play_badge,
                    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_dual_camera_last_media_button, LV_OBJ_FLAG_HIDDEN);

    g_dual_camera_status = make_label(g_dual_camera_view, "",
                                      &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_set_style_bg_color(g_dual_camera_status,
                              lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_status, LV_OPA_70, 0);
    lv_obj_set_style_radius(g_dual_camera_status, 16, 0);
    lv_obj_set_style_pad_hor(g_dual_camera_status, 16, 0);
    lv_obj_set_style_pad_ver(g_dual_camera_status, 9, 0);
    lv_obj_align(g_dual_camera_status, LV_ALIGN_BOTTOM_MID, 0, -102);
    lv_obj_add_flag(g_dual_camera_status, LV_OBJ_FLAG_HIDDEN);

    /* Fully opaque, edge-to-edge startup surface: no corner artifacts. */
    g_dual_camera_loading = lv_obj_create(g_dual_camera_view);
    lv_obj_remove_style_all(g_dual_camera_loading);
    lv_obj_set_size(g_dual_camera_loading, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_dual_camera_loading, 0, 0);
    lv_obj_set_style_radius(g_dual_camera_loading, 0, 0);
    lv_obj_set_style_bg_color(g_dual_camera_loading,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_dual_camera_loading, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_dual_camera_loading,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_dual_camera_spinner = lv_spinner_create(g_dual_camera_loading);
    lv_obj_set_size(g_dual_camera_spinner, 50, 50);
    lv_obj_center(g_dual_camera_spinner);
    lv_spinner_set_anim_params(g_dual_camera_spinner, 820, 82);
    lv_obj_set_style_arc_width(g_dual_camera_spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_dual_camera_spinner, 4,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_dual_camera_spinner,
                               lv_color_hex(0x343434), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_dual_camera_spinner,
                               lv_color_hex(0xF4F7FB),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_dual_camera_spinner, true,
                                 LV_PART_INDICATOR);
    lv_obj_add_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
}

static k_s32 gallery_player_event_cb(K_PLAYER_EVENT_E event, void *data)
{
    if (event == K_PLAYER_EVENT_PROGRESS && data != NULL) {
        K_PLAYER_PROGRESS_INFO *progress = data;
        uint64_t pts = progress->cur_time;
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_us = (uint64_t)now_ts.tv_sec * 1000000ULL +
                          (uint64_t)now_ts.tv_nsec / 1000ULL;
        if (g_gallery_player_last_pts != 0 &&
            pts > g_gallery_player_last_pts) {
            /*
             * kplayer reports presentation progress in milliseconds but
             * otherwise submits frames as fast as VDEC accepts them.  Pace
             * against an absolute PTS clock and subtract the measured
             * demux/decode submission cost.  Sleeping for the whole PTS gap
             * would add that cost to every frame and reduce the output FPS.
             */
            uint64_t delta = pts - g_gallery_player_last_pts;
            if (delta >= 2 && delta <= 1000) {
                if (g_gallery_player_last_wake_us != 0 &&
                    now_us > g_gallery_player_last_wake_us) {
                    uint64_t cost = now_us - g_gallery_player_last_wake_us;
                    if (cost <= 100000) {
                        g_gallery_player_decode_cost_us =
                            (g_gallery_player_decode_cost_us * 3 + cost) / 4;
                    }
                }
                if (g_gallery_player_clock_wall_us == 0) {
                    g_gallery_player_clock_wall_us = now_us;
                    g_gallery_player_clock_pts_ms = pts;
                }

                uint64_t next_pts = pts + delta;
                uint64_t target_us = g_gallery_player_clock_wall_us +
                    (next_pts - g_gallery_player_clock_pts_ms) * 1000ULL;
                uint64_t wake_us =
                    target_us > g_gallery_player_decode_cost_us
                        ? target_us - g_gallery_player_decode_cost_us
                        : target_us;

                /* Re-anchor after pause, seek, or an unusually long stall. */
                if (now_us > target_us + 250000ULL) {
                    g_gallery_player_clock_wall_us = now_us;
                    g_gallery_player_clock_pts_ms = pts;
                    target_us = now_us + delta * 1000ULL;
                    wake_us = target_us > g_gallery_player_decode_cost_us
                                  ? target_us - g_gallery_player_decode_cost_us
                                  : target_us;
                }
                if (wake_us > now_us)
                    usleep((useconds_t)(wake_us - now_us));
            } else {
                usleep(17000);
                g_gallery_player_clock_wall_us = 0;
            }
        } else {
            usleep(17000);
            g_gallery_player_clock_wall_us = 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        g_gallery_player_last_wake_us =
            (uint64_t)now_ts.tv_sec * 1000000ULL +
            (uint64_t)now_ts.tv_nsec / 1000ULL;
        g_gallery_player_last_pts = pts;
        g_gallery_player_current_ms = progress->cur_time;
        g_gallery_player_total_ms = progress->total_time;
    } else if (event == K_PLAYER_EVENT_EOF) {
        g_gallery_player_eof = 1;
        g_gallery_player_eof_hold = 6;
    }
    return K_SUCCESS;
}

static void gallery_close_media(void)
{
    bool restart_camera =
        g_gallery_return_to_camera &&
        g_gallery_media_index < g_gallery_media_count &&
        g_gallery_media_types[g_gallery_media_index] != 0;

    /* Dismiss any open info or delete dialogs. */
    lv_obj_add_flag(g_gallery_info_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_HIDDEN);

    if (g_gallery_player_timer != NULL) {
        lv_timer_delete(g_gallery_player_timer);
        g_gallery_player_timer = NULL;
    }
    if (g_gallery_video_playing) {
        kd_player_resume();
        kd_player_stop();
        g_gallery_video_playing = false;
    }
    if (g_gallery_osd_hidden) {
        kd_display_layer_enable(K_VO_LAYER_OSD0);
        g_gallery_osd_hidden = false;
    }
    lv_obj_clean(g_gallery_media_content);
    if (g_gallery_return_to_dual_camera) {
        g_gallery_return_to_dual_camera = false;
        g_dual_camera_resume_session = true;
        show_dual_camera();
        return;
    }
    if (g_gallery_return_to_camera) {
        g_gallery_return_to_camera = false;
        if (restart_camera)
            show_camera();
        else
            lv_screen_load(g_camera_screen);
        return;
    }
    lv_screen_load(g_home_screen);
    lv_obj_remove_flag(g_gallery_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gallery_view);
}

static void gallery_media_back_cb(lv_event_t *event)
{
    (void)event;
    gallery_close_media();
    lv_indev_t *indev = lv_indev_active();
    if (indev != NULL)
        lv_indev_wait_release(indev);
}

static void gallery_set_video_pause_visual(bool paused)
{
    g_gallery_video_paused = paused;
    lv_label_set_text(g_gallery_video_pause_icon,
                      paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

static void gallery_set_controls_visible(bool visible)
{
    g_gallery_controls_visible = visible;

    lv_obj_t *always_controls[] = {
        g_gallery_media_back,
        g_gallery_media_title,
        g_gallery_info_button,
    };
    for (size_t i = 0;
         i < sizeof(always_controls) / sizeof(always_controls[0]); ++i) {
        if (visible)
            lv_obj_remove_flag(always_controls[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(always_controls[i], LV_OBJ_FLAG_HIDDEN);
    }

    bool is_video =
        g_gallery_media_index < g_gallery_media_count &&
        g_gallery_media_types[g_gallery_media_index] != 0;
    if (visible && is_video)
        lv_obj_remove_flag(g_gallery_video_controls, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_gallery_video_controls, LV_OBJ_FLAG_HIDDEN);

    if (visible && g_gallery_media_index > 0)
        lv_obj_remove_flag(g_gallery_prev_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_gallery_prev_button, LV_OBJ_FLAG_HIDDEN);
    if (visible && g_gallery_media_index + 1 < g_gallery_media_count)
        lv_obj_remove_flag(g_gallery_next_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_gallery_next_button, LV_OBJ_FLAG_HIDDEN);

    if (visible && !g_gallery_return_to_camera &&
        !g_gallery_return_to_dual_camera)
        lv_obj_remove_flag(g_gallery_delete_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_gallery_delete_button, LV_OBJ_FLAG_HIDDEN);
}

static void gallery_reset_player_clock(void)
{
    g_gallery_player_last_pts = 0;
    g_gallery_player_clock_wall_us = 0;
    g_gallery_player_last_wake_us = 0;
}

static void gallery_video_pause_cb(lv_event_t *event)
{
    (void)event;
    if (!g_gallery_video_playing)
        return;

    if (g_gallery_player_eof) {
        gallery_open_media_index(g_gallery_media_index);
        return;
    }
    if (g_gallery_video_paused) {
        gallery_reset_player_clock();
        kd_player_resume();
        gallery_set_video_pause_visual(false);
    } else {
        kd_player_pause();
        gallery_set_video_pause_visual(true);
    }
}

static void gallery_video_seek_button_cb(lv_event_t *event)
{
    bool seek_to_end = (uintptr_t)lv_event_get_user_data(event) != 0;
    if (!g_gallery_video_playing || g_gallery_player_total_ms == 0)
        return;

    if (g_gallery_player_eof) {
        if (!seek_to_end)
            gallery_open_media_index(g_gallery_media_index);
        return;
    }

    uint64_t target = 0;
    if (seek_to_end) {
        /* Land on the final decodable frame instead of seeking beyond the
         * last sample and immediately receiving EOF. */
        target = g_gallery_player_total_ms > 80
                     ? g_gallery_player_total_ms - 80
                     : g_gallery_player_total_ms;
    }
    if (kd_player_seek(target) == K_SUCCESS) {
        g_gallery_player_current_ms = target;
        gallery_reset_player_clock();
        g_gallery_player_eof = 0;
        lv_slider_set_value(g_gallery_video_slider,
                            seek_to_end ? 1000 : 0, LV_ANIM_OFF);
    }
}

static void gallery_player_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!g_gallery_slider_dragging && g_gallery_player_total_ms > 0) {
        uint64_t value =
            g_gallery_player_current_ms * 1000 /
            g_gallery_player_total_ms;
        if (value > 1000)
            value = 1000;
        lv_slider_set_value(g_gallery_video_slider, (int32_t)value,
                            LV_ANIM_OFF);
    }
    uint64_t current_seconds = g_gallery_player_current_ms / 1000;
    uint64_t total_seconds = g_gallery_player_total_ms / 1000;
    char time_text[40];
    snprintf(time_text, sizeof(time_text), "%02llu:%02llu / %02llu:%02llu",
             (unsigned long long)(current_seconds / 60),
             (unsigned long long)(current_seconds % 60),
             (unsigned long long)(total_seconds / 60),
             (unsigned long long)(total_seconds % 60));
    lv_label_set_text(g_gallery_video_time, time_text);
    if (g_gallery_player_eof && !g_gallery_video_paused)
        gallery_set_video_pause_visual(true);
}

static void gallery_slider_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        g_gallery_slider_dragging = true;
        if (!g_gallery_video_paused)
            kd_player_pause();
    } else if (code == LV_EVENT_RELEASED) {
        if (g_gallery_player_total_ms > 0) {
            int32_t value = lv_slider_get_value(g_gallery_video_slider);
            uint64_t target =
                g_gallery_player_total_ms * (uint64_t)value / 1000;
            kd_player_seek(target);
            g_gallery_player_current_ms = target;
            g_gallery_player_last_pts = 0;
            g_gallery_player_clock_wall_us = 0;
            g_gallery_player_last_wake_us = 0;
            g_gallery_player_eof = 0;
        }
        g_gallery_slider_dragging = false;
        if (!g_gallery_video_paused)
            kd_player_resume();
    }
}

static void gallery_media_action_delete_cb(lv_event_t *event)
{
    gallery_media_action_t *action = lv_event_get_user_data(event);
    if (action != NULL) {
        free(action->thumbnail_data);
        free(action);
    }
}

static void gallery_update_selection_card(size_t index)
{
    if (index >= sizeof(g_gallery_selected) /
                     sizeof(g_gallery_selected[0]))
        return;

    lv_obj_t *card = g_gallery_media_cards[index];
    lv_obj_t *badge = g_gallery_selection_badges[index];
    if (card == NULL || badge == NULL)
        return;

    if (!g_gallery_selection_mode) {
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(card, 0, 0);
        return;
    }

    lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(badge);
    lv_obj_t *check = lv_obj_get_child(badge, 0);
    if (g_gallery_selected[index]) {
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x2563EB), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(card, 4, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x2563EB), 0);
        if (check != NULL)
            lv_obj_remove_flag(check, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x111827), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_50, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        if (check != NULL)
            lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
    }
}

static void gallery_update_selection_summary(void)
{
    if (g_gallery_selection_count_label == NULL ||
        g_gallery_batch_delete_button == NULL)
        return;

    char count_text[64];
    snprintf(count_text, sizeof(count_text), settings_text("Selected: %u"),
             (unsigned int)g_gallery_selected_count);
    lv_label_set_text(g_gallery_selection_count_label, count_text);

    if (g_gallery_selected_count > 0) {
        lv_obj_remove_state(g_gallery_batch_delete_button,
                            LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(g_gallery_batch_delete_button,
                                LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(g_gallery_batch_delete_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(g_gallery_batch_delete_button,
                                LV_OPA_40, 0);
    }
}

static void gallery_set_selection_mode(bool enabled)
{
    g_gallery_selection_mode = enabled;
    memset(g_gallery_selected, 0, sizeof(g_gallery_selected));
    g_gallery_selected_count = 0;

    for (size_t i = 0;
         i < sizeof(g_gallery_selected) / sizeof(g_gallery_selected[0]); ++i)
        gallery_update_selection_card(i);

    if (g_gallery_select_label != NULL)
        lv_label_set_text(g_gallery_select_label,
                          settings_text(enabled ? "Cancel" : "Select"));
    if (g_gallery_selection_bar != NULL) {
        if (enabled) {
            lv_obj_remove_flag(g_gallery_selection_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_gallery_selection_bar);
        } else {
            lv_obj_add_flag(g_gallery_selection_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_gallery_content != NULL)
        lv_obj_set_height(g_gallery_content, enabled ? 326 : 400);
    if (!enabled && g_gallery_batch_delete_dialog != NULL)
        lv_obj_add_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_HIDDEN);
    gallery_update_selection_summary();
}

static void gallery_toggle_media_selection(size_t index)
{
    if (!g_gallery_selection_mode || index >= g_gallery_media_count ||
        index >= sizeof(g_gallery_selected) /
                     sizeof(g_gallery_selected[0]))
        return;

    g_gallery_selected[index] = !g_gallery_selected[index];
    if (g_gallery_selected[index])
        ++g_gallery_selected_count;
    else if (g_gallery_selected_count > 0)
        --g_gallery_selected_count;
    gallery_update_selection_card(index);
    gallery_update_selection_summary();
}

static void gallery_select_toggle_cb(lv_event_t *event)
{
    (void)event;
    if (!g_gallery_selection_mode && g_gallery_media_count == 0) {
        show_toast(settings_text("No photos or videos"));
        return;
    }
    gallery_set_selection_mode(!g_gallery_selection_mode);
}

static void gallery_grid_back_cb(lv_event_t *event)
{
    if (g_gallery_selection_mode) {
        gallery_set_selection_mode(false);
        lv_indev_t *indev = lv_indev_active();
        if (indev != NULL)
            lv_indev_wait_release(indev);
        return;
    }
    close_fullscreen_cb(event);
}

static void gallery_open_media_index(size_t media_index)
{
    if (media_index >= g_gallery_media_count)
        return;
    gallery_media_action_t media_value = { 0 };
    snprintf(media_value.path, sizeof(media_value.path), "%s",
             g_gallery_media_paths[media_index]);
    media_value.is_video = g_gallery_media_types[media_index];
    media_value.index = media_index;
    gallery_media_action_t *media = &media_value;

    if (g_gallery_player_timer != NULL) {
        lv_timer_delete(g_gallery_player_timer);
        g_gallery_player_timer = NULL;
    }
    if (g_gallery_video_playing) {
        kd_player_resume();
        kd_player_stop();
        g_gallery_video_playing = false;
    }
    g_gallery_media_index = media_index;
    gallery_set_video_pause_visual(false);

    printf("[gallery] opening %s: %s\n",
           media->is_video ? "video" : "photo", media->path);
    lv_obj_clean(g_gallery_media_content);
    lv_label_set_text(g_gallery_media_title,
                      media->is_video ? "Video" : "Photo");
    gallery_set_controls_visible(true);
    lv_screen_load(g_gallery_media_screen);
    lv_refr_now(NULL);

    if (!media->is_video) {
        lv_obj_set_style_bg_opa(g_gallery_media_content, LV_OPA_COVER, 0);
        /* Decode the JPEG into a memory buffer at native resolution so LVGL
         * can downscale the full 1080p frame to fit the 640x480 screen.  The
         * file-source path streams MCUs at 1:1 and only exposes the center
         * crop instead of the whole picture. */
        gallery_media_action_t *photo = calloc(1, sizeof(*photo));
        if (photo != NULL && gallery_decode_jpeg(media->path, 0, photo) == 0) {
            /* Dual Camera stores the composited JPEG in the VICAP/VENC
             * orientation.  The Gallery OSD is rotated for the physical
             * panel, so still photos need the same 180-degree correction as
             * their grid thumbnails.  Video playback uses VIDEO1's hardware
             * rotation and must remain unchanged. */
            if (gallery_is_dual_camera_media(media->path))
                gallery_rotate_thumbnail_180(photo);
            g_gallery_current_width = photo->thumbnail.header.w;
            g_gallery_current_height = photo->thumbnail.header.h;
            lv_obj_t *image = lv_image_create(g_gallery_media_content);
            lv_obj_set_size(image, UI_WIDTH, UI_HEIGHT);
            lv_obj_center(image);
            lv_image_set_src(image, &photo->thumbnail);
            /* Match video viewing: edge-to-edge, aspect-correct fullscreen.
             * COVER crops the excess edge instead of distorting the photo. */
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
            lv_obj_remove_flag(image,
                               LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(image, gallery_media_action_delete_cb,
                                LV_EVENT_DELETE, photo);
        }
        else {
            free(photo);
            show_toast("Unable to open this photo");
        }
        return;
    }

    g_gallery_current_width = 0;
    g_gallery_current_height = 0;
    lv_obj_set_style_bg_opa(g_gallery_media_content, LV_OPA_TRANSP, 0);
    g_gallery_player_eof = 0;
    g_gallery_player_eof_hold = 0;
    g_gallery_player_last_pts = 0;
    g_gallery_player_clock_wall_us = 0;
    g_gallery_player_last_wake_us = 0;
    g_gallery_player_decode_cost_us = 3000;
    g_gallery_player_current_ms = 0;
    g_gallery_player_total_ms = 0;
    g_gallery_slider_dragging = false;
    lv_slider_set_value(g_gallery_video_slider, 0, LV_ANIM_OFF);
    lv_label_set_text(g_gallery_video_time, "00:00 / 00:00");
    kd_player_set_connector_type(ST7701_480_640_DSI_V1);
    kd_player_regcallback(gallery_player_event_cb, NULL);
    kd_player_resume();
    if (kd_player_setdatasource(media->path) != K_SUCCESS) {
        show_toast("Unable to play this video");
        gallery_close_media();
        return;
    }

    /*
     * mp4_player/display_cfg only recognizes the older ST7701 enum and
     * configures this panel as an unrotated 640x480 layer.  The physical
     * DongshanPI panel is 480x640; that invalid scan-out is decoded
     * successfully but remains black.  Reuse the exact VIDEO1 geometry that
     * the working Camera preview uses.
     */
    kd_display_layer_disable(K_VO_LAYER_VIDEO1);
    if (kd_display_layer_configure(
            K_VO_LAYER_VIDEO1, PIXEL_FORMAT_YUV_SEMIPLANAR_420,
            640, 480, 0, 0, 255, GDMA_ROTATE_DEGREE_270, 2, 2) !=
        K_SUCCESS ||
        kd_display_layer_enable(K_VO_LAYER_VIDEO1) != K_SUCCESS) {
        printf("[gallery] VIDEO1 rotation configuration failed\n");
    } else {
        printf("[gallery] VIDEO1 configured 640x480 rotate=270\n");
    }

    printf("[gallery] starting video decoder\n");
    if (kd_player_start() != K_SUCCESS) {
        kd_player_stop();
        show_toast("Unable to start video decoder");
        gallery_close_media();
        return;
    }
    printf("[gallery] video decoder started\n");
    g_gallery_video_playing = true;
    /*
     * OSD0 is an independent hardware layer.  A transparent LVGL screen does
     * not clear the two direct-render buffers, so the last gallery frame can
     * otherwise remain over VIDEO1 and make playback look black.  Hide OSD0
     * while VDEC owns VIDEO1; touch handling remains active and the top-left
     * area still exits playback.
     */
    g_gallery_player_timer =
        lv_timer_create(gallery_player_timer_cb, 250, NULL);
}

static void gallery_open_media_cb(lv_event_t *event)
{
    gallery_media_action_t *media = lv_event_get_user_data(event);
    if (media == NULL)
        return;
    if (g_gallery_selection_mode) {
        gallery_toggle_media_selection(media->index);
        return;
    }
    gallery_open_media_index(media->index);
}

static void gallery_media_gesture_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t point;
    if (indev == NULL)
        return;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        g_gallery_gesture_start = point;
    } else if (code == LV_EVENT_RELEASED) {
        int dx = (int)point.x - (int)g_gallery_gesture_start.x;
        int dy = (int)point.y - (int)g_gallery_gesture_start.y;
        if (abs(dx) >= 70 && abs(dx) > abs(dy)) {
            if (dx < 0 && g_gallery_media_index + 1 <
                              g_gallery_media_count) {
                gallery_open_media_index(g_gallery_media_index + 1);
            } else if (dx > 0 && g_gallery_media_index > 0) {
                gallery_open_media_index(g_gallery_media_index - 1);
            }
            return;
        }
        if (abs(dx) <= 20 && abs(dy) <= 20)
            gallery_set_controls_visible(!g_gallery_controls_visible);
    }
}

static int gallery_get_video_resolution(const char *path, uint32_t *width,
                                        uint32_t *height)
{
    k_mp4_config_s cfg = { 0 };
    KD_HANDLE mp4 = NULL;
    k_mp4_file_info_s file_info = { 0 };
    int ret = -1;

    cfg.config_type = K_MP4_CONFIG_DEMUXER;
    if (strlen(path) >= sizeof(cfg.demuxer_config.file_name))
        return -1;
    memcpy(cfg.demuxer_config.file_name, path, strlen(path) + 1);
    if (kd_mp4_create(&mp4, &cfg) != 0 || mp4 == NULL)
        return -1;
    if (kd_mp4_get_file_info(mp4, &file_info) != 0)
        goto cleanup;

    for (uint32_t i = 0; i < file_info.track_num; ++i) {
        k_mp4_track_info_s track = { 0 };
        if (kd_mp4_get_track_by_index(mp4, i, &track) == 0 &&
            track.track_type == K_MP4_STREAM_VIDEO) {
            *width = track.video_info.width;
            *height = track.video_info.height;
            ret = 0;
            break;
        }
    }

cleanup:
    kd_mp4_destroy(mp4);
    return ret;
}

static void gallery_prev_media_cb(lv_event_t *event)
{
    (void)event;
    if (g_gallery_media_index > 0)
        gallery_open_media_index(g_gallery_media_index - 1);
}

static void gallery_next_media_cb(lv_event_t *event)
{
    (void)event;
    if (g_gallery_media_index + 1 < g_gallery_media_count)
        gallery_open_media_index(g_gallery_media_index + 1);
}

static void gallery_info_close_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_gallery_info_dialog, LV_OBJ_FLAG_HIDDEN);
}

static void gallery_info_show_cb(lv_event_t *event)
{
    (void)event;
    if (g_gallery_media_index >= g_gallery_media_count)
        return;

    const char *path = g_gallery_media_paths[g_gallery_media_index];
    int is_video = g_gallery_media_types[g_gallery_media_index];
    struct stat file_stat;
    off_t file_size = stat(path, &file_stat) == 0 ? file_stat.st_size : 0;

    char size_text[32];
    format_file_size(file_size, size_text, sizeof(size_text));

    char type_text[32];
    char res_text[64];
    if (is_video) {
        snprintf(type_text, sizeof(type_text), "MP4 Video");
        {
            uint32_t vw = 0, vh = 0;
            if (gallery_get_video_resolution(path, &vw, &vh) == 0 &&
                vw > 0 && vh > 0)
                snprintf(res_text, sizeof(res_text), "%lu x %lu",
                         (unsigned long)vw, (unsigned long)vh);
            else
                snprintf(res_text, sizeof(res_text), "-");
        }
    } else {
        snprintf(type_text, sizeof(type_text), "JPEG Image");
        if (g_gallery_current_width > 0 && g_gallery_current_height > 0)
            snprintf(res_text, sizeof(res_text), "%lu x %lu",
                     (unsigned long)g_gallery_current_width,
                     (unsigned long)g_gallery_current_height);
        else
            snprintf(res_text, sizeof(res_text), "-");
    }

    /* Update dialog labels — children are laid out in creation order.
     * g_gallery_info_dialog has one child: info_card.
     * info_card children: 0=title, 1-4=info rows, 5=close button.
     * Each info row has two children: 0=field label, 1=value label. */
    lv_obj_t *card = lv_obj_get_child(g_gallery_info_dialog, 0);
    if (card != NULL && lv_obj_get_child_count(card) >= 6) {
        lv_obj_t *res_row = lv_obj_get_child(card, 1);
        lv_obj_t *type_row = lv_obj_get_child(card, 2);
        lv_obj_t *size_row = lv_obj_get_child(card, 3);
        lv_obj_t *path_row = lv_obj_get_child(card, 4);

        if (lv_obj_get_child_count(res_row) >= 2)
            lv_label_set_text(
                lv_obj_get_child(res_row, 1), res_text);
        if (lv_obj_get_child_count(type_row) >= 2)
            lv_label_set_text(
                lv_obj_get_child(type_row, 1), type_text);
        if (lv_obj_get_child_count(size_row) >= 2)
            lv_label_set_text(
                lv_obj_get_child(size_row, 1), size_text);
        if (lv_obj_get_child_count(path_row) >= 2)
            lv_label_set_text(
                lv_obj_get_child(path_row, 1), path);
    }

    lv_obj_remove_flag(g_gallery_info_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gallery_info_dialog);
}

static void gallery_delete_cancel_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_HIDDEN);
}

static int gallery_unlink_media_file(const char *path, int is_video)
{
    if (path == NULL || path[0] == '\0')
        return -1;
    if (unlink(path) != 0) {
        printf("[gallery] failed to delete %s (errno=%d)\n", path, errno);
        return -1;
    }
    printf("[gallery] deleted %s\n", path);

    /* Video cards use a same-stem JPEG as their first-frame thumbnail. */
    if (is_video) {
        char thumb_path[328];
        size_t len = strlen(path);
        if (len > 4 && len < sizeof(thumb_path)) {
            memcpy(thumb_path, path, len);
            memcpy(thumb_path + len - 4, ".jpg", 5);
            if (unlink(thumb_path) == 0)
                printf("[gallery] deleted thumbnail %s\n", thumb_path);
        }
    }
    return 0;
}

static void gallery_delete_confirm_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_HIDDEN);

    size_t idx = g_gallery_media_index;
    if (idx >= g_gallery_media_count)
        return;

    const char *path = g_gallery_media_paths[idx];

    /* Stop video playback if active. */
    if (g_gallery_video_playing) {
        if (g_gallery_player_timer != NULL) {
            lv_timer_delete(g_gallery_player_timer);
            g_gallery_player_timer = NULL;
        }
        kd_player_resume();
        kd_player_stop();
        g_gallery_video_playing = false;
    }
    if (g_gallery_osd_hidden) {
        kd_display_layer_enable(K_VO_LAYER_OSD0);
        g_gallery_osd_hidden = false;
    }

    if (gallery_unlink_media_file(path, g_gallery_media_types[idx]) != 0) {
        show_toast(settings_text("Delete failed"));
        return;
    }

    /* Clean up the media content and switch back to the home screen so
     * the stale fullscreen frame is not visible while the grid refreshes. */
    lv_obj_clean(g_gallery_media_content);
    lv_screen_load(g_home_screen);

    /* Remove from the global arrays by shifting remaining entries. */
    for (size_t i = idx; i + 1 < g_gallery_media_count; ++i) {
        memcpy(g_gallery_media_paths[i], g_gallery_media_paths[i + 1],
               sizeof(g_gallery_media_paths[0]));
        g_gallery_media_types[i] = g_gallery_media_types[i + 1];
    }
    --g_gallery_media_count;

    /* Re-scan the directory to rebuild the thumbnail grid. */
    gallery_refresh();

    if (g_gallery_media_count > 0) {
        /* Open the next item (or the last if we deleted the tail). */
        size_t next_idx = idx < g_gallery_media_count ? idx
                                                      : g_gallery_media_count - 1;
        gallery_open_media_index(next_idx);
    } else {
        /* All media deleted — show the (now empty) gallery grid. */
        lv_obj_remove_flag(g_gallery_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_gallery_view);
    }

    show_toast(settings_text("Deleted"));
}

static void gallery_delete_show_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_remove_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gallery_delete_dialog);
}

static void gallery_batch_delete_cancel_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_HIDDEN);
}

static void gallery_batch_delete_show_cb(lv_event_t *event)
{
    (void)event;
    if (!g_gallery_selection_mode || g_gallery_selected_count == 0)
        return;

    char title[128];
    snprintf(title, sizeof(title),
             settings_text("Delete %u selected items?"),
             (unsigned int)g_gallery_selected_count);
    lv_label_set_text(g_gallery_batch_delete_title, title);
    lv_obj_remove_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gallery_batch_delete_dialog);
}

static void gallery_batch_delete_confirm_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_HIDDEN);
    if (!g_gallery_selection_mode || g_gallery_selected_count == 0)
        return;

    size_t deleted = 0;
    size_t failed = 0;
    for (size_t i = 0; i < g_gallery_media_count; ++i) {
        if (!g_gallery_selected[i])
            continue;
        if (gallery_unlink_media_file(g_gallery_media_paths[i],
                                      g_gallery_media_types[i]) == 0)
            ++deleted;
        else
            ++failed;
    }

    printf("[gallery] batch delete complete: deleted=%lu failed=%lu\n",
           (unsigned long)deleted, (unsigned long)failed);
    gallery_set_selection_mode(false);
    gallery_refresh();

    char result[96];
    if (deleted > 0 && failed == 0) {
        snprintf(result, sizeof(result), settings_text("Deleted %u items"),
                 (unsigned int)deleted);
        show_toast(result);
    } else if (deleted > 0) {
        snprintf(result, sizeof(result),
                 settings_text("Deleted %u items, %u failed"),
                 (unsigned int)deleted, (unsigned int)failed);
        show_toast(result);
    } else {
        show_toast(settings_text("Delete failed"));
    }
}

static void gallery_append_media_card(size_t index)
{
    if (index >= g_gallery_media_count)
        return;

    const char *path = g_gallery_media_paths[index];
    int is_video = g_gallery_media_types[index];
    struct tm media_tm;
    char date_key[16];
    char date_text[64];
    localtime_r(&g_gallery_media_modified[index], &media_tm);
    strftime(date_key, sizeof(date_key), "%Y-%m-%d", &media_tm);
    if (g_gallery_active_row == NULL ||
        strcmp(g_gallery_active_date, date_key) != 0) {
        snprintf(g_gallery_active_date, sizeof(g_gallery_active_date), "%s",
                 date_key);
        strftime(date_text, sizeof(date_text), "%A, %B %d", &media_tm);
        lv_obj_t *date =
            make_label(g_gallery_content, date_text,
                       &lv_font_montserrat_16, 0x1F2937);
        lv_obj_set_width(date, 596);
        g_gallery_active_row = lv_obj_create(g_gallery_content);
        /* Wrap thumbnails to extra lines inside a date group so every
         * photo for the day stays visible instead of clipping past 3. */
        lv_obj_set_width(g_gallery_active_row, 596);
        lv_obj_set_height(g_gallery_active_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(g_gallery_active_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_gallery_active_row, 0, 0);
        lv_obj_set_style_pad_all(g_gallery_active_row, 0, 0);
        lv_obj_set_style_pad_column(g_gallery_active_row, 8, 0);
        lv_obj_set_style_pad_row(g_gallery_active_row, 8, 0);
        lv_obj_set_flex_flow(g_gallery_active_row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(g_gallery_active_row, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *card = lv_obj_create(g_gallery_active_row);
    lv_obj_set_size(card, 193, 126);
    /* Fixed thumbnail size so cards wrap evenly; flex-grow would stretch
     * a lone photo into one long full-row tile. */
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    gallery_media_action_t *action = calloc(1, sizeof(*action));
    if (action != NULL) {
        snprintf(action->path, sizeof(action->path), "%s", path);
        action->is_video = is_video;
        action->index = index;
        lv_obj_add_event_cb(card, tap_guard_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(card, gallery_open_media_cb,
                            LV_EVENT_CLICKED, action);
        lv_obj_add_event_cb(card, gallery_media_action_delete_cb,
                            LV_EVENT_DELETE, action);
    }

    if (!is_video && action != NULL &&
        gallery_decode_grid_thumbnail(path, action) == 0) {
        lv_obj_t *image = lv_image_create(card);
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_obj_center(image);
        lv_image_set_src(image, &action->thumbnail);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
    } else if (is_video) {
        /* Try to show the first-frame JPEG thumbnail saved alongside the
         * MP4 recording.  Fall back to a video icon for legacy files. */
        int has_thumb = 0;
        if (action != NULL) {
            char thumb_path[328];
            size_t len = strlen(path);
            if (len > 4 && len < sizeof(thumb_path)) {
                memcpy(thumb_path, path, len);
                memcpy(thumb_path + len - 4, ".jpg", 5);
                if (gallery_decode_grid_thumbnail(thumb_path, action) == 0) {
                    lv_obj_t *image = lv_image_create(card);
                    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
                    lv_obj_center(image);
                    lv_image_set_src(image, &action->thumbnail);
                    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
                    has_thumb = 1;
                }
            }
        }
        if (!has_thumb) {
            lv_obj_set_style_bg_color(card, lv_color_hex(0x20242B), 0);
            lv_obj_t *video_icon =
                make_label(card, LV_SYMBOL_VIDEO,
                           &lv_font_montserrat_36, 0xFFFFFF);
            lv_obj_align(video_icon, LV_ALIGN_CENTER, 0, -8);
        }
    } else {
        /* Keep the gallery responsive even when an old interrupted
         * capture left a malformed JPEG on disk. */
        lv_obj_set_style_bg_color(card, lv_color_hex(0xE5E7EB), 0);
        lv_obj_t *photo_icon =
            make_label(card, LV_SYMBOL_IMAGE,
                       &lv_font_montserrat_36, 0x6B7280);
        lv_obj_center(photo_icon);
    }

    char time_text[24];
    strftime(time_text, sizeof(time_text), "%H:%M", &media_tm);
    lv_obj_t *time_chip = lv_obj_create(card);
    lv_obj_set_size(time_chip, is_video ? 88 : 62, 24);
    lv_obj_align(time_chip, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(time_chip, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(time_chip, LV_OPA_70, 0);
    lv_obj_set_style_radius(time_chip, 12, 0);
    lv_obj_set_style_border_width(time_chip, 0, 0);
    lv_obj_set_style_pad_all(time_chip, 0, 0);
    lv_obj_remove_flag(time_chip,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *time_label =
        make_label(time_chip, time_text, &lv_font_montserrat_10, 0xFFFFFF);
    lv_obj_center(time_label);
    if (is_video) {
        lv_obj_t *video_badge =
            make_label(time_chip, LV_SYMBOL_PLAY,
                       &lv_font_montserrat_10, 0xFFFFFF);
        lv_obj_align(video_badge, LV_ALIGN_LEFT_MID, 7, 0);
        lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -7, 0);
    }

    /* A non-interactive overlay keeps the entire thumbnail as the tap
     * target while making multi-selection obvious at a glance. */
    lv_obj_t *selection_badge = lv_obj_create(card);
    lv_obj_set_size(selection_badge, 32, 32);
    lv_obj_align(selection_badge, LV_ALIGN_TOP_RIGHT, -7, 7);
    lv_obj_set_style_radius(selection_badge, 16, 0);
    lv_obj_set_style_border_width(selection_badge, 2, 0);
    lv_obj_set_style_border_color(selection_badge,
                                  lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(selection_badge, 0, 0);
    lv_obj_remove_flag(selection_badge,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *selection_check =
        make_label(selection_badge, LV_SYMBOL_OK,
                   &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(selection_check);
    lv_obj_remove_flag(selection_check,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_gallery_media_cards[index] = card;
    g_gallery_selection_badges[index] = selection_badge;
    gallery_update_selection_card(index);
}

static void gallery_load_timer_cb(lv_timer_t *timer)
{
    /* Do not spend decode time behind the media viewer or after Gallery has
     * been dismissed.  The timer resumes naturally when the grid is visible
     * again. */
    if (lv_screen_active() != g_home_screen ||
        lv_obj_has_flag(g_gallery_view, LV_OBJ_FLAG_HIDDEN))
        return;

    /* JPEG thumbnail decode is synchronous on this SDK.  Never start one
     * while the finger is down or LVGL is still applying scroll momentum;
     * the timer will resume naturally as soon as interaction settles. */
    if (ui_pointer_motion_active())
        return;

    if (g_gallery_rendered_count < g_gallery_media_count) {
        gallery_append_media_card(g_gallery_rendered_count);
        ++g_gallery_rendered_count;
    }
    if (g_gallery_rendered_count >= g_gallery_media_count) {
        printf("[gallery] all %lu thumbnails loaded\n",
               (unsigned long)g_gallery_media_count);
        lv_timer_delete(timer);
        g_gallery_load_timer = NULL;
    }
}

static void gallery_refresh(void)
{
    enum { GALLERY_INITIAL_THUMBNAILS = 3 };
    photo_entry_t photos[120];
    size_t photo_count = 0;
    DIR *directory;
    struct dirent *entry;

    /* Drop stale file-backed decodes so a newly captured JPEG with a fresh
     * directory entry is visible immediately on opening Gallery. */
    if (g_gallery_load_timer != NULL) {
        lv_timer_delete(g_gallery_load_timer);
        g_gallery_load_timer = NULL;
    }
    gallery_set_selection_mode(false);
    lv_image_cache_drop(NULL);
    g_gallery_media_count = 0;
    g_gallery_rendered_count = 0;
    g_gallery_active_row = NULL;
    g_gallery_active_date[0] = '\0';
    lv_obj_clean(g_gallery_content);
    lv_obj_set_layout(g_gallery_content, LV_LAYOUT_NONE);
    memset(g_gallery_media_cards, 0, sizeof(g_gallery_media_cards));
    memset(g_gallery_selection_badges, 0,
           sizeof(g_gallery_selection_badges));
    directory = opendir(DSHANPI_PHOTO_DIR);
    if (directory != NULL) {
        while ((entry = readdir(directory)) != NULL && photo_count < 120) {
            size_t length = strlen(entry->d_name);
            int is_jpg =
                length >= 4 &&
                strcasecmp(entry->d_name + length - 4, ".jpg") == 0;
            int is_jpeg =
                length >= 5 &&
                strcasecmp(entry->d_name + length - 5, ".jpeg") == 0;
            int is_mp4 =
                length >= 4 &&
                strcasecmp(entry->d_name + length - 4, ".mp4") == 0;
            if (!is_jpg && !is_jpeg && !is_mp4) {
                continue;
            }
            /* Camera and Dual Camera recordings use a same-stem JPEG as
             * their first-frame cover.  Keep that sidecar out of the grid;
             * it belongs to the MP4 card and is deleted with the video. */
            if (is_jpg || is_jpeg) {
                char video_path[384];
                size_t extension = is_jpeg ? 5U : 4U;
                int video_length = snprintf(
                    video_path, sizeof(video_path), "%s/%.*s.mp4",
                    DSHANPI_PHOTO_DIR,
                    (int)(length - extension), entry->d_name);
                if (video_length > 0 &&
                    (size_t)video_length < sizeof(video_path) &&
                    access(video_path, R_OK) == 0)
                    continue;
            }
            snprintf(photos[photo_count].path,
                     sizeof(photos[photo_count].path), "%s/%s",
                     DSHANPI_PHOTO_DIR, entry->d_name);
            struct stat file_stat;
            photos[photo_count].modified =
                stat(photos[photo_count].path, &file_stat) == 0
                    ? file_stat.st_mtime
                    : 0;
            photos[photo_count].is_video = is_mp4;
            ++photo_count;
        }
        closedir(directory);
    }

    if (photo_count == 0) {
        printf("[gallery] no media found in %s\n", DSHANPI_PHOTO_DIR);
        lv_obj_t *empty_icon =
            make_label(g_gallery_content, LV_SYMBOL_IMAGE,
                       &lv_font_montserrat_36, 0x9CA3AF);
        lv_obj_align(empty_icon, LV_ALIGN_TOP_MID, 0, 95);
        lv_obj_t *empty =
            make_label(g_gallery_content, settings_text("No photos or videos"),
                       settings_ui_font(&lv_font_montserrat_20), 0x6B7280);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 155);
        return;
    }

    printf("[gallery] found %lu media files in %s\n",
           (unsigned long)photo_count, DSHANPI_PHOTO_DIR);
    qsort(photos, photo_count, sizeof(photos[0]), compare_photos);
    g_gallery_media_count = photo_count;
    for (size_t i = 0; i < photo_count; ++i) {
        size_t path_length =
            strnlen(photos[i].path, sizeof(g_gallery_media_paths[i]) - 1);
        memcpy(g_gallery_media_paths[i], photos[i].path, path_length);
        g_gallery_media_paths[i][path_length] = '\0';
        g_gallery_media_types[i] = photos[i].is_video;
        g_gallery_media_modified[i] = photos[i].modified;
    }
    lv_obj_set_flex_flow(g_gallery_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_gallery_content, 8, 0);
    size_t initial_count =
        photo_count < GALLERY_INITIAL_THUMBNAILS
            ? photo_count : GALLERY_INITIAL_THUMBNAILS;
    while (g_gallery_rendered_count < initial_count) {
        gallery_append_media_card(g_gallery_rendered_count);
        ++g_gallery_rendered_count;
    }
    printf("[gallery] first %lu thumbnails ready, %lu queued\n",
           (unsigned long)g_gallery_rendered_count,
           (unsigned long)(g_gallery_media_count -
                           g_gallery_rendered_count));
    if (g_gallery_rendered_count < g_gallery_media_count)
        g_gallery_load_timer =
            lv_timer_create(gallery_load_timer_cb, 100, NULL);
}

static void create_gallery_view(lv_obj_t *screen)
{
    g_gallery_view = lv_obj_create(screen);
    lv_obj_set_size(g_gallery_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_gallery_view);
    lv_obj_set_style_bg_color(g_gallery_view, lv_color_hex(0xF7F8FC), 0);
    lv_obj_set_style_border_width(g_gallery_view, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_view, 0, 0);
    lv_obj_remove_flag(g_gallery_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(g_gallery_view);
    lv_obj_set_size(header, UI_WIDTH, 72);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    style_plain(header);
    lv_obj_t *back = create_round_button(header, 64, 0xE8EAF2);
    lv_obj_set_pos(back, 8, 4);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, gallery_grid_back_cb, LV_EVENT_PRESSED,
                        g_gallery_view);
    lv_obj_t *back_label =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_18, 0x222222);
    lv_obj_center(back_label);

    g_gallery_select_button = lv_button_create(header);
    lv_obj_set_size(g_gallery_select_button, 120, 44);
    lv_obj_align(g_gallery_select_button, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_ext_click_area(g_gallery_select_button, 12);
    lv_obj_set_style_radius(g_gallery_select_button, 22, 0);
    lv_obj_set_style_bg_color(g_gallery_select_button,
                              lv_color_hex(0xE8EAF2), 0);
    lv_obj_set_style_bg_color(g_gallery_select_button,
                              lv_color_hex(0xD9E7FF), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(g_gallery_select_button, 0, 0);
    lv_obj_set_style_border_width(g_gallery_select_button, 0, 0);
    lv_obj_add_event_cb(g_gallery_select_button, gallery_select_toggle_cb,
                        LV_EVENT_CLICKED, NULL);
    g_gallery_select_label =
        make_label(g_gallery_select_button, settings_text("Select"),
                   settings_ui_font(&lv_font_montserrat_16), 0x2563EB);
    lv_obj_center(g_gallery_select_label);

    lv_obj_t *title =
        make_label(header, localized_app_name(&g_apps[APP_GALLERY]),
                   settings_ui_font(&lv_font_montserrat_20), 0x111827);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    g_gallery_content = lv_obj_create(g_gallery_view);
    lv_obj_set_size(g_gallery_content, 616, 400);
    lv_obj_set_pos(g_gallery_content, 12, 74);
    lv_obj_set_style_bg_opa(g_gallery_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_gallery_content, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_content, 0, 0);
    lv_obj_set_scroll_dir(g_gallery_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_gallery_content, LV_SCROLLBAR_MODE_AUTO);

    g_gallery_selection_bar = lv_obj_create(g_gallery_view);
    lv_obj_set_size(g_gallery_selection_bar, 616, 72);
    lv_obj_set_pos(g_gallery_selection_bar, 12, 400);
    lv_obj_set_style_bg_color(g_gallery_selection_bar,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_gallery_selection_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_gallery_selection_bar, 22, 0);
    lv_obj_set_style_border_width(g_gallery_selection_bar, 1, 0);
    lv_obj_set_style_border_color(g_gallery_selection_bar,
                                  lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(g_gallery_selection_bar, 12, 0);
    lv_obj_set_style_shadow_opa(g_gallery_selection_bar, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(g_gallery_selection_bar, 0, 0);
    lv_obj_remove_flag(g_gallery_selection_bar, LV_OBJ_FLAG_SCROLLABLE);

    g_gallery_selection_count_label =
        make_label(g_gallery_selection_bar, "",
                   settings_ui_font(&lv_font_montserrat_16), 0x334155);
    lv_obj_align(g_gallery_selection_count_label, LV_ALIGN_LEFT_MID, 20, 0);

    g_gallery_batch_delete_button = lv_button_create(g_gallery_selection_bar);
    lv_obj_set_size(g_gallery_batch_delete_button, 180, 60);
    lv_obj_align(g_gallery_batch_delete_button, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_ext_click_area(g_gallery_batch_delete_button, 6);
    lv_obj_set_style_radius(g_gallery_batch_delete_button, 30, 0);
    lv_obj_set_style_bg_color(g_gallery_batch_delete_button,
                              lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_shadow_width(g_gallery_batch_delete_button, 0, 0);
    lv_obj_set_style_border_width(g_gallery_batch_delete_button, 0, 0);
    lv_obj_add_event_cb(g_gallery_batch_delete_button,
                        gallery_batch_delete_show_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *batch_delete_text =
        make_label(g_gallery_batch_delete_button, settings_text("Delete"),
                   settings_ui_font(&lv_font_montserrat_18), 0xFFFFFF);
    lv_obj_center(batch_delete_text);
    lv_obj_add_flag(g_gallery_selection_bar, LV_OBJ_FLAG_HIDDEN);

    g_gallery_batch_delete_dialog = lv_obj_create(g_gallery_view);
    lv_obj_set_size(g_gallery_batch_delete_dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_gallery_batch_delete_dialog);
    lv_obj_set_style_bg_color(g_gallery_batch_delete_dialog,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_gallery_batch_delete_dialog, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_gallery_batch_delete_dialog, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_batch_delete_dialog, 0, 0);
    lv_obj_remove_flag(g_gallery_batch_delete_dialog,
                       LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *batch_delete_card =
        lv_obj_create(g_gallery_batch_delete_dialog);
    lv_obj_set_size(batch_delete_card, 460, 250);
    lv_obj_center(batch_delete_card);
    lv_obj_set_style_radius(batch_delete_card, 28, 0);
    lv_obj_set_style_border_width(batch_delete_card, 0, 0);
    lv_obj_set_style_pad_all(batch_delete_card, 16, 0);
    lv_obj_remove_flag(batch_delete_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(batch_delete_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(batch_delete_card, tap_guard_cb, LV_EVENT_ALL, NULL);

    g_gallery_batch_delete_title =
        make_label(batch_delete_card, settings_text("Delete"),
                   settings_ui_font(&lv_font_montserrat_20), 0x111827);
    lv_obj_set_width(g_gallery_batch_delete_title, 410);
    lv_obj_set_style_text_align(g_gallery_batch_delete_title,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_gallery_batch_delete_title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *batch_delete_hint =
        make_label(batch_delete_card,
                   settings_text("This action cannot be undone."),
                   settings_ui_font(&lv_font_montserrat_14), 0x64748B);
    lv_obj_set_width(batch_delete_hint, 410);
    lv_obj_set_style_text_align(batch_delete_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(batch_delete_hint, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t *batch_delete_cancel = lv_button_create(batch_delete_card);
    lv_obj_set_size(batch_delete_cancel, 172, 52);
    lv_obj_align(batch_delete_cancel, LV_ALIGN_BOTTOM_LEFT, 22, -16);
    lv_obj_set_style_radius(batch_delete_cancel, 22, 0);
    lv_obj_set_style_bg_color(batch_delete_cancel,
                              lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(batch_delete_cancel, 0, 0);
    lv_obj_add_event_cb(batch_delete_cancel, gallery_batch_delete_cancel_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *batch_delete_cancel_text =
        make_label(batch_delete_cancel, settings_text("Cancel"),
                   settings_ui_font(&lv_font_montserrat_16), 0x334155);
    lv_obj_center(batch_delete_cancel_text);

    lv_obj_t *batch_delete_confirm = lv_button_create(batch_delete_card);
    lv_obj_set_size(batch_delete_confirm, 172, 52);
    lv_obj_align(batch_delete_confirm, LV_ALIGN_BOTTOM_RIGHT, -22, -16);
    lv_obj_set_ext_click_area(batch_delete_confirm, 10);
    lv_obj_set_style_radius(batch_delete_confirm, 22, 0);
    lv_obj_set_style_bg_color(batch_delete_confirm,
                              lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_shadow_width(batch_delete_confirm, 0, 0);
    lv_obj_add_event_cb(batch_delete_confirm,
                        gallery_batch_delete_confirm_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *batch_delete_confirm_text =
        make_label(batch_delete_confirm, settings_text("Delete"),
                   settings_ui_font(&lv_font_montserrat_16), 0xFFFFFF);
    lv_obj_center(batch_delete_confirm_text);
    lv_obj_add_flag(g_gallery_batch_delete_dialog, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(g_gallery_view, LV_OBJ_FLAG_HIDDEN);

    /*
     * Use a separate transparent LVGL screen for media viewing.  Photos are
     * drawn by LVGL; for videos the transparent OSD lets the hardware VDEC
     * layer show through while the back control remains touchable.
     */
    g_gallery_media_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_opa(g_gallery_media_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_gallery_media_screen, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_media_screen, 0, 0);
    lv_obj_remove_flag(g_gallery_media_screen, LV_OBJ_FLAG_SCROLLABLE);
    g_gallery_media_content = lv_obj_create(g_gallery_media_screen);
    lv_obj_set_size(g_gallery_media_content, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_gallery_media_content);
    lv_obj_set_style_bg_color(g_gallery_media_content,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_gallery_media_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_gallery_media_content, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_media_content, 0, 0);
    lv_obj_remove_flag(g_gallery_media_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_gallery_media_content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_gallery_media_content, gallery_media_gesture_cb,
                        LV_EVENT_ALL, NULL);
    g_gallery_media_back =
        create_round_button(g_gallery_media_screen, 64, 0x242424);
    lv_obj_set_pos(g_gallery_media_back, 10, 8);
    expand_top_left_back_hit_area(g_gallery_media_back);
    lv_obj_add_event_cb(g_gallery_media_back, gallery_media_back_cb,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_t *media_back_icon =
        make_label(g_gallery_media_back, LV_SYMBOL_LEFT,
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(media_back_icon);
    g_gallery_media_title =
        make_label(g_gallery_media_screen, "Photo",
                   &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_align(g_gallery_media_title, LV_ALIGN_TOP_MID, 0, 26);
    g_gallery_video_controls = lv_obj_create(g_gallery_media_screen);
    lv_obj_set_size(g_gallery_video_controls, 612, 92);
    lv_obj_align(g_gallery_video_controls, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(g_gallery_video_controls,
                              lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(g_gallery_video_controls, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_gallery_video_controls, 0, 0);
    lv_obj_set_style_radius(g_gallery_video_controls, 22, 0);
    lv_obj_set_style_pad_all(g_gallery_video_controls, 8, 0);
    lv_obj_remove_flag(g_gallery_video_controls, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *seek_start = lv_button_create(g_gallery_video_controls);
    lv_obj_set_size(seek_start, 44, 44);
    lv_obj_align(seek_start, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(seek_start, 22, 0);
    lv_obj_set_style_bg_color(seek_start, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(seek_start, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(seek_start, 0, 0);
    lv_obj_add_event_cb(seek_start, gallery_video_seek_button_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    lv_obj_t *seek_start_icon =
        make_label(seek_start, LV_SYMBOL_PREV,
                   &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(seek_start_icon);

    lv_obj_t *pause = lv_button_create(g_gallery_video_controls);
    lv_obj_set_size(pause, 64, 64);
    lv_obj_align(pause, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_set_style_radius(pause, 32, 0);
    lv_obj_set_style_bg_color(pause, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(pause, LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(pause, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(pause, 0, 0);
    lv_obj_set_ext_click_area(pause, 6);
    lv_obj_add_event_cb(pause, gallery_video_pause_cb,
                        LV_EVENT_CLICKED, NULL);
    g_gallery_video_pause_icon =
        make_label(pause, LV_SYMBOL_PAUSE,
                   &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_center(g_gallery_video_pause_icon);

    lv_obj_t *seek_end = lv_button_create(g_gallery_video_controls);
    lv_obj_set_size(seek_end, 44, 44);
    lv_obj_align(seek_end, LV_ALIGN_LEFT_MID, 116, 0);
    lv_obj_set_style_radius(seek_end, 22, 0);
    lv_obj_set_style_bg_color(seek_end, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(seek_end, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(seek_end, 0, 0);
    lv_obj_add_event_cb(seek_end, gallery_video_seek_button_cb,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)1);
    lv_obj_t *seek_end_icon =
        make_label(seek_end, LV_SYMBOL_NEXT,
                   &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_center(seek_end_icon);

    g_gallery_video_slider = lv_slider_create(g_gallery_video_controls);
    lv_obj_set_size(g_gallery_video_slider, 276, 18);
    lv_obj_align(g_gallery_video_slider, LV_ALIGN_LEFT_MID, 168, 0);
    lv_slider_set_range(g_gallery_video_slider, 0, 1000);
    lv_obj_set_style_bg_color(g_gallery_video_slider,
                              lv_color_hex(0xFF3B30),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_gallery_video_slider,
                              lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_width(g_gallery_video_slider, 26, LV_PART_KNOB);
    lv_obj_set_style_height(g_gallery_video_slider, 26, LV_PART_KNOB);
    lv_obj_add_event_cb(g_gallery_video_slider, gallery_slider_cb,
                        LV_EVENT_ALL, NULL);
    g_gallery_video_time =
        make_label(g_gallery_video_controls, "00:00 / 00:00",
                   &lv_font_montserrat_12, 0xFFFFFF);
    lv_obj_set_width(g_gallery_video_time, 142);
    lv_obj_set_style_text_align(g_gallery_video_time,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_gallery_video_time, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(g_gallery_video_controls, LV_OBJ_FLAG_HIDDEN);

    /* ----- Info button (top-right, mirroring the back button) ----- */
    g_gallery_info_button =
        create_round_button(g_gallery_media_screen, 64, 0x242424);
    lv_obj_set_pos(g_gallery_info_button, 566, 8);
    lv_obj_add_event_cb(g_gallery_info_button, gallery_info_show_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *info_icon =
        make_label(g_gallery_info_button, "i",
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(info_icon);

    /* ----- Delete button (bottom-right) ----- */
    g_gallery_delete_button =
        create_round_button(g_gallery_media_screen, 64, 0x242424);
    lv_obj_set_pos(g_gallery_delete_button, 486, 8);
    lv_obj_set_ext_click_area(g_gallery_delete_button, 8);
    lv_obj_add_event_cb(g_gallery_delete_button, gallery_delete_show_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_icon =
        make_label(g_gallery_delete_button, LV_SYMBOL_TRASH,
                   &lv_font_montserrat_20, 0xFF453A);
    lv_obj_center(delete_icon);

    /* ----- Previous / Next arrow buttons (center-left / center-right) ----- */
    g_gallery_prev_button =
        create_round_button(g_gallery_media_screen, 64, 0x242424);
    lv_obj_set_pos(g_gallery_prev_button, 10, 208);
    lv_obj_add_event_cb(g_gallery_prev_button, gallery_prev_media_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_icon =
        make_label(g_gallery_prev_button, LV_SYMBOL_LEFT,
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(prev_icon);

    g_gallery_next_button =
        create_round_button(g_gallery_media_screen, 64, 0x242424);
    lv_obj_set_pos(g_gallery_next_button, 566, 208);
    lv_obj_add_event_cb(g_gallery_next_button, gallery_next_media_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_icon =
        make_label(g_gallery_next_button, LV_SYMBOL_RIGHT,
                   &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(next_icon);

    /* ----- Info dialog (semi-transparent overlay + card) ----- */
    g_gallery_info_dialog = lv_obj_create(g_gallery_media_screen);
    lv_obj_set_size(g_gallery_info_dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_gallery_info_dialog);
    lv_obj_set_style_bg_color(g_gallery_info_dialog,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_gallery_info_dialog, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_gallery_info_dialog, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_info_dialog, 0, 0);
    lv_obj_remove_flag(g_gallery_info_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_gallery_info_dialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_gallery_info_dialog, gallery_info_close_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *info_card = lv_obj_create(g_gallery_info_dialog);
    lv_obj_set_size(info_card, 460, 320);
    lv_obj_center(info_card);
    lv_obj_set_style_radius(info_card, 24, 0);
    lv_obj_set_style_border_width(info_card, 0, 0);
    lv_obj_set_style_pad_all(info_card, 20, 0);
    lv_obj_remove_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(info_card, LV_OBJ_FLAG_CLICKABLE);
    /* Prevent clicks on the card from falling through to the overlay
     * background which would dismiss the dialog. */
    lv_obj_add_event_cb(info_card, tap_guard_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *info_title =
        make_label(info_card, "File Info", &lv_font_montserrat_20, 0x111827);
    lv_obj_align(info_title, LV_ALIGN_TOP_MID, 0, 8);

    /* Info rows: each is a horizontal container with a dim label on the
     * left and the dynamic value on the right. */
    static const char *const info_fields[] = {
        "Resolution", "Type", "Size", "Path"
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *row = lv_obj_create(info_card);
        lv_obj_set_size(row, 420, 34);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 54 + i * 44);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *field_label =
            make_label(row, info_fields[i],
                       &lv_font_montserrat_12, 0x6B7280);
        lv_obj_align(field_label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *value_label =
            make_label(row, "-", &lv_font_montserrat_12, 0x1F2937);
        lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *info_close = lv_button_create(info_card);
    lv_obj_set_size(info_close, 120, 44);
    lv_obj_align(info_close, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_radius(info_close, 22, 0);
    lv_obj_set_style_bg_color(info_close, lv_color_hex(0xE5F1FF), 0);
    lv_obj_set_style_shadow_width(info_close, 0, 0);
    lv_obj_add_event_cb(info_close, gallery_info_close_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *info_close_text =
        make_label(info_close, "Close", &lv_font_montserrat_16, 0x007AFF);
    lv_obj_center(info_close_text);

    lv_obj_add_flag(g_gallery_info_dialog, LV_OBJ_FLAG_HIDDEN);

    /* ----- Delete confirmation dialog ----- */
    g_gallery_delete_dialog = lv_obj_create(g_gallery_media_screen);
    lv_obj_set_size(g_gallery_delete_dialog, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_gallery_delete_dialog);
    lv_obj_set_style_bg_color(g_gallery_delete_dialog,
                              lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_gallery_delete_dialog, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_gallery_delete_dialog, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_delete_dialog, 0, 0);
    lv_obj_remove_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *delete_card = lv_obj_create(g_gallery_delete_dialog);
    lv_obj_set_size(delete_card, 430, 230);
    lv_obj_center(delete_card);
    lv_obj_set_style_radius(delete_card, 28, 0);
    lv_obj_set_style_border_width(delete_card, 0, 0);
    lv_obj_set_style_pad_all(delete_card, 16, 0);
    lv_obj_remove_flag(delete_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *delete_title =
        make_label(delete_card, settings_text("Delete"),
                   settings_ui_font(&lv_font_montserrat_20), 0x111827);
    lv_obj_align(delete_title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *delete_hint =
        make_label(delete_card, settings_text("This action cannot be undone."),
                   settings_ui_font(&lv_font_montserrat_14), 0x64748B);
    lv_obj_align(delete_hint, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *delete_cancel = lv_button_create(delete_card);
    lv_obj_set_size(delete_cancel, 160, 52);
    lv_obj_align(delete_cancel, LV_ALIGN_BOTTOM_LEFT, 24, -16);
    lv_obj_set_style_radius(delete_cancel, 22, 0);
    lv_obj_set_style_bg_color(delete_cancel, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_shadow_width(delete_cancel, 0, 0);
    lv_obj_add_event_cb(delete_cancel, gallery_delete_cancel_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_cancel_text =
        make_label(delete_cancel, settings_text("Cancel"),
                   settings_ui_font(&lv_font_montserrat_16), 0x334155);
    lv_obj_center(delete_cancel_text);

    lv_obj_t *delete_confirm = lv_button_create(delete_card);
    lv_obj_set_size(delete_confirm, 160, 52);
    lv_obj_align(delete_confirm, LV_ALIGN_BOTTOM_RIGHT, -24, -16);
    lv_obj_set_ext_click_area(delete_confirm, 10);
    lv_obj_set_style_radius(delete_confirm, 22, 0);
    lv_obj_set_style_bg_color(delete_confirm, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_shadow_width(delete_confirm, 0, 0);
    lv_obj_add_event_cb(delete_confirm, gallery_delete_confirm_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_confirm_text =
        make_label(delete_confirm, settings_text("Delete"),
                   settings_ui_font(&lv_font_montserrat_16), 0xFFFFFF);
    lv_obj_center(delete_confirm_text);

    lv_obj_add_flag(g_gallery_delete_dialog, LV_OBJ_FLAG_HIDDEN);
}

static void show_camera(void)
{
    g_camera_csi = g_system_camera_csi;
    g_camera_resolution = dshanpi_camera_resolution_load();
    lv_label_set_text(g_camera_resolution_label,
                      dshanpi_camera_resolution_name(g_camera_resolution));
    camera_status_hide();
    camera_focus_hide();
    camera_loading_show();
    lv_obj_remove_flag(g_camera_record_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_camera_shutter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_camera_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_camera_resolution_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(g_camera_record, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_camera_record, LV_OPA_20, 0);
    lv_obj_set_style_text_color(g_camera_record_icon,
                                lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(g_camera_record_icon, LV_SYMBOL_VIDEO);
    g_camera_video_mode = false;
    lv_obj_set_size(g_camera_shutter_inner, 74, 74);
    lv_obj_set_style_radius(g_camera_shutter_inner, 37, 0);
    lv_obj_set_style_bg_color(g_camera_shutter_inner,
                              lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_transform_scale(g_camera_shutter_inner, 256, 0);
    lv_obj_add_flag(g_camera_record_badge, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(g_camera_screen);
    lv_refr_now(NULL);
    g_camera_init_result = -1;
    g_camera_init_cancelled = 0;
    g_camera_init_running = 1;
    if (pthread_create(&g_camera_init_thread, NULL, camera_init_worker,
                       (void *)(intptr_t)g_camera_csi) != 0) {
        g_camera_init_running = 0;
        camera_loading_hide();
        camera_status_show("Unable to start camera worker", 0xFF453A);
        return;
    }
    g_camera_init_timer = lv_timer_create(camera_init_timer_cb, 100, NULL);
}

static void show_dual_camera(void)
{
    dual_camera_cleanup_ui_timers();
    if (!g_dual_camera_resume_session)
        dual_camera_session_media_reset();
    g_dual_camera_resume_session = false;
    g_dual_camera_resolution = dshanpi_camera_resolution_load();
    lv_label_set_text(
        g_dual_camera_resolution_label,
        dshanpi_camera_resolution_name(g_dual_camera_resolution));
    g_dual_camera_video_mode = false;
    dual_camera_mode_visual();
    g_dual_camera_pip_dragging = false;
    g_dual_camera_pip_locked = false;
    lv_obj_remove_flag(g_dual_camera_pip_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_dual_camera_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_dual_camera_loading);
    lv_obj_remove_flag(g_dual_camera_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dual_camera_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_dual_camera_resolution_button,
                       LV_OBJ_FLAG_CLICKABLE);
    lv_screen_load(g_dual_camera_screen);
    lv_refr_now(NULL);

    g_dual_camera_init_result = -1;
    g_dual_camera_init_cancelled = 0;
    g_dual_camera_init_running = 1;
    if (pthread_create(&g_dual_camera_init_thread, NULL,
                       dual_camera_init_worker, NULL) != 0) {
        g_dual_camera_init_running = 0;
        lv_obj_add_flag(g_dual_camera_loading, LV_OBJ_FLAG_HIDDEN);
        dual_camera_status_show("Unable to start dual camera worker",
                                0xFFB4AB);
        return;
    }
    g_dual_camera_init_timer =
        lv_timer_create(dual_camera_init_timer_cb, 100, NULL);
}

static void show_gallery(void)
{
    g_gallery_return_to_camera = false;
    g_gallery_return_to_dual_camera = false;
    /* Present the Gallery surface before touching storage or decoding JPEGs,
     * then populate the first row synchronously and the rest incrementally. */
    if (g_gallery_load_timer != NULL) {
        lv_timer_delete(g_gallery_load_timer);
        g_gallery_load_timer = NULL;
    }
    gallery_set_selection_mode(false);
    lv_obj_clean(g_gallery_content);
    memset(g_gallery_media_cards, 0, sizeof(g_gallery_media_cards));
    memset(g_gallery_selection_badges, 0,
           sizeof(g_gallery_selection_badges));
    lv_obj_remove_flag(g_gallery_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gallery_view);
    lv_refr_now(NULL);
    gallery_refresh();
    lv_refr_now(NULL);
}

static void show_settings(void)
{
    lv_obj_add_flag(g_settings_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_navigation, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_settings_view);
    settings_refresh_nav_values();
    wifi_update_network_status();
}

static void show_paint(void)
{
    if (g_paint_buffer == NULL) {
        show_toast("Drawing canvas is unavailable");
        return;
    }
    g_paint_last_x = -1;
    g_paint_last_y = -1;
    g_paint_recently_released = 0;
    lv_obj_remove_flag(g_paint_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_paint_view);
}

static void create_toast(void)
{
    /* The top layer remains visible over the home, camera, Gallery media and
     * screen-saver screens. */
    g_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_toast, LV_SIZE_CONTENT, 44);
    lv_obj_align(g_toast, LV_ALIGN_BOTTOM_MID, 0, -130);
    lv_obj_set_style_bg_color(g_toast, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(g_toast, 24, 0);
    lv_obj_set_style_border_width(g_toast, 0, 0);
    lv_obj_set_style_pad_hor(g_toast, 20, 0);
    lv_obj_set_style_pad_ver(g_toast, 8, 0);
    lv_obj_remove_flag(g_toast,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    g_toast_label = make_label(g_toast, "Ready", &lv_font_montserrat_14,
                               0xFFFFFF);
    lv_obj_center(g_toast_label);
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}

static void create_modal(lv_obj_t *screen)
{
    g_modal = lv_obj_create(screen);
    lv_obj_set_size(g_modal, UI_WIDTH, UI_HEIGHT);
    lv_obj_center(g_modal);
    lv_obj_set_style_bg_color(g_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_modal, LV_OPA_20, 0);
    lv_obj_set_style_border_width(g_modal, 0, 0);
    lv_obj_set_style_pad_all(g_modal, 0, 0);
    lv_obj_remove_flag(g_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_modal, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(g_modal);
    lv_obj_set_size(card, 420, 300);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 36, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_modal_title = make_label(card, "Application",
                               &lv_font_montserrat_20, 0x1A1A1A);
    lv_obj_align(g_modal_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *close = lv_button_create(card);
    lv_obj_set_size(close, 44, 44);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -8);
    lv_obj_set_style_radius(close, 22, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(0xF1F3F4), 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_add_event_cb(close, close_modal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = make_label(close, LV_SYMBOL_CLOSE,
                                       &lv_font_montserrat_18, 0x5F6368);
    lv_obj_center(close_label);

    g_modal_symbol = make_label(card, LV_SYMBOL_IMAGE,
                                &lv_font_montserrat_28, 0x007AFF);
    lv_obj_align(g_modal_symbol, LV_ALIGN_TOP_MID, 0, 58);

    g_modal_description = make_label(card, "Application description",
                                     &lv_font_montserrat_14, 0x5F6368);
    lv_obj_align(g_modal_description, LV_ALIGN_CENTER, 0, 28);

    lv_obj_t *dismiss = lv_button_create(card);
    lv_obj_set_size(dismiss, 132, 48);
    lv_obj_align(dismiss, LV_ALIGN_BOTTOM_LEFT, 16, 0);
    lv_obj_set_style_bg_opa(dismiss, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(dismiss, lv_color_hex(0xDADCE0), 0);
    lv_obj_set_style_border_width(dismiss, 1, 0);
    lv_obj_set_style_radius(dismiss, 24, 0);
    lv_obj_set_style_shadow_width(dismiss, 0, 0);
    lv_obj_add_event_cb(dismiss, close_modal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dismiss_label = make_label(dismiss, "Close",
                                         &lv_font_montserrat_14, 0x5F6368);
    lv_obj_center(dismiss_label);

    lv_obj_t *launch = lv_button_create(card);
    lv_obj_set_size(launch, 148, 48);
    lv_obj_align(launch, LV_ALIGN_BOTTOM_RIGHT, -16, 0);
    lv_obj_set_style_bg_color(launch, lv_color_hex(0xE8F0FE), 0);
    lv_obj_set_style_radius(launch, 24, 0);
    lv_obj_set_style_shadow_width(launch, 0, 0);
    lv_obj_add_event_cb(launch, launch_app_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *launch_label = make_label(launch, "Open app",
                                        &lv_font_montserrat_14, 0x007AFF);
    lv_obj_center(launch_label);
    lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
}

static void launch_ai_app_cb(lv_event_t *event)
{
    const dshanpi_ai_app_t *app = lv_event_get_user_data(event);
    app_info_t launch_info;

    if (app == NULL || !lv_k230_touch_accept_click()) {
        return;
    }

    printf("[ai-center] selected id=%s name=%s category=%s script=%s\n",
           app->id, app->name, app->category, app->launch_script);
    g_launch_ai_app = app;
    launch_info.name = app->name;
    launch_info.symbol = "AI";
    launch_info.description = app->category;
    launch_info.color = app->color;
    show_app_launch_overlay(&launch_info);
}

static void add_ai_mode_card(lv_obj_t *parent,
                             const dshanpi_ai_app_t *app)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, 192, 112);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_add_event_cb(card, launch_ai_app_cb, LV_EVENT_CLICKED,
                        (void *)app);

    lv_obj_t *icon = lv_obj_create(card);
    lv_obj_set_size(icon, 62, 62);
    lv_obj_set_pos(icon, 0, 4);
    lv_obj_set_style_radius(icon, 18, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(app->color), 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *symbol =
        make_label(icon, LV_SYMBOL_EYE_OPEN, &lv_font_montserrat_20,
                   0x334155);
    lv_obj_center(symbol);

    lv_obj_t *name =
        make_label(card, app->name, &lv_font_montserrat_14, 0x172033);
    lv_obj_set_pos(name, 72, 14);
    lv_obj_set_width(name, 104);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t *category =
        make_label(card, app->category, &lv_font_montserrat_12, 0x667085);
    lv_obj_set_pos(category, 72, 42);

    lv_obj_t *offline =
        make_label(card, "RUN", &lv_font_montserrat_10, 0x007AFF);
    lv_obj_set_pos(offline, 72, 70);
}

static void open_ai_scene(const dshanpi_ai_scene_t *scene)
{
    char subtitle[96];

    if (scene == NULL) {
        return;
    }

    /*
     * Face Studio is a single camera-owning application. Its own overlay
     * provides the six runtime modes, so do not expose the legacy ELF list.
     */
    if (strcmp(scene->id, "face_studio") == 0) {
        printf("[ai-center] launching unified Face Studio\n");
        g_launch_face_studio = true;
        show_app_launch_overlay(&g_apps[APP_FACE_STUDIO]);
        return;
    }
    if (strcmp(scene->id, "hand_studio") == 0) {
        printf("[ai-center] launching unified Hand Studio\n");
        g_launch_hand_studio = true;
        show_app_launch_overlay(&g_apps[APP_HAND_STUDIO]);
        return;
    }
    if (strcmp(scene->id, "human_studio") == 0) {
        printf("[ai-center] launching unified Human Studio\n");
        g_launch_human_studio = true;
        show_app_launch_overlay(&g_apps[APP_HUMAN_STUDIO]);
        return;
    }
    if (strcmp(scene->id, "smart_driving") == 0) {
        printf("[ai-center] launching unified Smart Driving\n");
        g_launch_smart_driving = true;
        show_app_launch_overlay(&g_apps[APP_SMART_DRIVING]);
        return;
    }

    lv_label_set_text(g_ai_scene_title, scene->name);
    snprintf(subtitle, sizeof(subtitle), "%s  |  %u modes",
             scene->description, (unsigned)scene->app_count);
    lv_label_set_text(g_ai_scene_subtitle, subtitle);
    lv_obj_clean(g_ai_scene_content);

    for (size_t i = 0; i < scene->app_count; ++i) {
        const dshanpi_ai_app_t *app =
            dshanpi_ai_app_find(scene->app_ids[i]);
        if (app != NULL) {
            add_ai_mode_card(g_ai_scene_content, app);
        } else {
            printf("[ai-center] scene %s references unknown app %s\n",
                   scene->id, scene->app_ids[i]);
        }
    }

    lv_obj_scroll_to_y(g_ai_scene_content, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(g_ai_scene_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ai_scene_view);
    printf("[ai-center] opened scene=%s modes=%u\n",
           scene->id, (unsigned)scene->app_count);
}

static void open_ai_scene_cb(lv_event_t *event)
{
    if (!lv_k230_touch_accept_click()) {
        return;
    }
    open_ai_scene(lv_event_get_user_data(event));
}

static lv_obj_t *create_ai_scroll_content(lv_obj_t *parent)
{
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, UI_WIDTH, UI_HEIGHT - 72);
    lv_obj_set_pos(content, 0, 72);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_left(content, 18, 0);
    lv_obj_set_style_pad_right(content, 18, 0);
    lv_obj_set_style_pad_top(content, 14, 0);
    lv_obj_set_style_pad_bottom(content, 18, 0);
    lv_obj_set_style_pad_row(content, 14, 0);
    lv_obj_set_style_pad_column(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    return content;
}

static void __attribute__((unused)) create_ai_center_view(lv_obj_t *screen)
{
    size_t scene_count = 0;
    size_t app_count = 0;
    const dshanpi_ai_scene_t *scenes = dshanpi_ai_scenes(&scene_count);
    dshanpi_ai_apps(&app_count);
    if (scene_count == 1 && scenes != NULL) {
        app_count = scenes[0].app_count;
    }

    g_ai_view = lv_obj_create(screen);
    lv_obj_set_size(g_ai_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_ai_view, 0, 0);
    lv_obj_set_style_bg_color(g_ai_view, lv_color_hex(0xF2F2F7), 0);
    lv_obj_set_style_border_width(g_ai_view, 0, 0);
    lv_obj_set_style_pad_all(g_ai_view, 0, 0);
    lv_obj_remove_flag(g_ai_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(g_ai_view);
    lv_obj_set_size(header, UI_WIDTH, 72);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = create_round_button(header, 56, 0xE8EEF8);
    lv_obj_set_pos(back, 8, 8);
    expand_top_left_back_hit_area(back);
    lv_obj_add_event_cb(back, close_fullscreen_cb, LV_EVENT_PRESSED,
                        g_ai_view);
    lv_obj_t *back_icon =
        make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, 0x263238);
    lv_obj_center(back_icon);

    lv_obj_t *title =
        make_label(header, "AI Center", &lv_font_montserrat_28, 0x172033);
    lv_obj_set_pos(title, 82, 12);
    char subtitle_text[64];
    snprintf(subtitle_text, sizeof(subtitle_text),
             "%u scene apps  |  %u AI modes",
             (unsigned)scene_count, (unsigned)app_count);
    lv_obj_t *subtitle =
        make_label(header, subtitle_text, &lv_font_montserrat_12, 0x667085);
    lv_obj_set_pos(subtitle, 84, 43);

    lv_obj_t *content = create_ai_scroll_content(g_ai_view);

    for (size_t i = 0; i < scene_count; ++i) {
        lv_obj_t *card = lv_button_create(content);
        lv_obj_set_size(card, 292, 128);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(card, 24, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_shadow_width(card, 10, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_add_event_cb(card, open_ai_scene_cb, LV_EVENT_CLICKED,
                            (void *)&scenes[i]);

        lv_obj_t *icon = lv_obj_create(card);
        lv_obj_set_size(icon, 76, 76);
        lv_obj_set_pos(icon, 0, 8);
        lv_obj_set_style_radius(icon, 22, 0);
        lv_obj_set_style_bg_color(icon, lv_color_hex(scenes[i].color), 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE |
                                  LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *symbol = make_label(icon, scenes[i].symbol,
                                      &lv_font_montserrat_20, 0x334155);
        lv_obj_center(symbol);

        lv_obj_t *name = make_label(card, scenes[i].name,
                                    &lv_font_montserrat_16, 0x172033);
        lv_obj_set_pos(name, 92, 12);
        lv_obj_set_width(name, 182);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        lv_obj_t *description =
            make_label(card, scenes[i].description,
                       &lv_font_montserrat_12, 0x667085);
        lv_obj_set_pos(description, 92, 42);
        lv_obj_set_width(description, 182);
        lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);

        char mode_text[32];
        snprintf(mode_text, sizeof(mode_text), "%u MODES",
                 (unsigned)scenes[i].app_count);
        lv_obj_t *modes =
            make_label(card, mode_text, &lv_font_montserrat_10, 0x007AFF);
        lv_obj_set_pos(modes, 92, 72);
    }

    g_ai_scene_view = lv_obj_create(screen);
    lv_obj_set_size(g_ai_scene_view, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_pos(g_ai_scene_view, 0, 0);
    lv_obj_set_style_bg_color(g_ai_scene_view, lv_color_hex(0xF2F2F7), 0);
    lv_obj_set_style_border_width(g_ai_scene_view, 0, 0);
    lv_obj_set_style_pad_all(g_ai_scene_view, 0, 0);
    lv_obj_remove_flag(g_ai_scene_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scene_header = lv_obj_create(g_ai_scene_view);
    lv_obj_set_size(scene_header, UI_WIDTH, 72);
    lv_obj_set_pos(scene_header, 0, 0);
    lv_obj_set_style_bg_color(scene_header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(scene_header, 0, 0);
    lv_obj_set_style_pad_all(scene_header, 0, 0);
    lv_obj_remove_flag(scene_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scene_back =
        create_round_button(scene_header, 56, 0xE8EEF8);
    lv_obj_set_pos(scene_back, 8, 8);
    expand_top_left_back_hit_area(scene_back);
    lv_obj_add_event_cb(scene_back, close_fullscreen_cb, LV_EVENT_PRESSED,
                        g_ai_scene_view);
    lv_obj_t *scene_back_icon =
        make_label(scene_back, LV_SYMBOL_LEFT, &lv_font_montserrat_20,
                   0x263238);
    lv_obj_center(scene_back_icon);

    g_ai_scene_title =
        make_label(scene_header, "AI Scene", &lv_font_montserrat_28,
                   0x172033);
    lv_obj_set_pos(g_ai_scene_title, 82, 8);
    g_ai_scene_subtitle =
        make_label(scene_header, "Choose a mode", &lv_font_montserrat_12,
                   0x667085);
    lv_obj_set_pos(g_ai_scene_subtitle, 84, 43);
    g_ai_scene_content = create_ai_scroll_content(g_ai_scene_view);

    lv_obj_add_flag(g_ai_scene_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_ai_view, LV_OBJ_FLAG_HIDDEN);
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    g_home_screen = screen;
    lv_obj_set_size(screen, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFAF9F5), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    create_material_background(screen);
    create_status_bar(screen);
    create_app_pages(screen);
    create_gallery_view(screen);
    create_settings_view(screen);
    create_paint_view(screen);
    create_uart_view(screen);
    create_cloud_model_view(screen);
    create_toast();
    create_modal(screen);
    create_app_launch_overlay(screen);
    create_screensaver_view();

    g_camera_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_camera_screen, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_opa(g_camera_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_camera_screen, 0, 0);
    lv_obj_set_style_pad_all(g_camera_screen, 0, 0);
    lv_obj_remove_flag(g_camera_screen, LV_OBJ_FLAG_SCROLLABLE);
    create_camera_view(g_camera_screen);

    g_dual_camera_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_dual_camera_screen, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_opa(g_dual_camera_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dual_camera_screen, 0, 0);
    lv_obj_set_style_pad_all(g_dual_camera_screen, 0, 0);
    lv_obj_remove_flag(g_dual_camera_screen, LV_OBJ_FLAG_SCROLLABLE);
    create_dual_camera_view(g_dual_camera_screen);

    lv_screen_load(screen);
}

static int launch_registered_ai_app(const dshanpi_ai_app_t *app)
{
    enum { MAX_AI_ARGS = 32, AI_ARG_LEN = 384 };
    char base[256];
    char script_path[384];
    char line[2048];
    char arg_storage[MAX_AI_ARGS][AI_ARG_LEN];
    char *exec_argv[MAX_AI_ARGS + 1];
    int exec_argc = 0;
    FILE *script;

    snprintf(base, sizeof(base), "/sdcard/app/ai/%s", app->id);
    snprintf(script_path, sizeof(script_path), "%s/%s",
             base, app->launch_script);
    script = fopen(script_path, "r");
    if (script == NULL) {
        printf("[ai-center] cannot open launch script %s: %s\n",
               script_path, strerror(errno));
        return -1;
    }

    line[0] = '\0';
    while (fgets(line, sizeof(line), script) != NULL) {
        char *start = line;
        while (*start == ' ' || *start == '\t') {
            ++start;
        }
        if (*start == '#' || *start == '\0' || *start == '\r' ||
            *start == '\n' || strncmp(start, "set ", 4) == 0 ||
            strstr(start, ".elf") == NULL) {
            continue;
        }
        if (start != line) {
            memmove(line, start, strlen(start) + 1);
        }
        break;
    }
    fclose(script);

    if (strstr(line, ".elf") == NULL) {
        printf("[ai-center] no ELF command in %s\n", script_path);
        return -1;
    }

    char *save = NULL;
    char *token = strtok_r(line, " \t\r\n", &save);
    while (token != NULL && exec_argc < MAX_AI_ARGS) {
        const char *plain = token;
        char candidate[AI_ARG_LEN];

        if (exec_argc == 0 && plain[0] == '.' && plain[1] == '/') {
            plain += 2;
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", base, plain);

        if (exec_argc == 0 || (plain[0] != '/' &&
                              access(candidate, F_OK) == 0)) {
            snprintf(arg_storage[exec_argc], AI_ARG_LEN, "%s", candidate);
        } else {
            snprintf(arg_storage[exec_argc], AI_ARG_LEN, "%s", plain);
        }
        exec_argv[exec_argc] = arg_storage[exec_argc];
        ++exec_argc;
        token = strtok_r(NULL, " \t\r\n", &save);
    }
    exec_argv[exec_argc] = NULL;

    if (exec_argc == 0 || access(exec_argv[0], X_OK) != 0) {
        printf("[ai-center] executable unavailable: %s\n",
               exec_argc > 0 ? exec_argv[0] : "(none)");
        return -1;
    }

    printf("[ai-center] launching %s with %d arguments from %s\n",
           app->id, exec_argc, base);
    for (int i = 0; i < exec_argc; ++i) {
        printf("[ai-center] argv[%d]=%s\n", i, exec_argv[i]);
    }

    if (chdir(base) != 0) {
        printf("[ai-center] chdir %s failed: %s\n", base, strerror(errno));
        return -1;
    }

    char csi_text[4];
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    setenv("DSHANPI_CAMERA_CSI", csi_text, 1);

    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, exec_argv[0], exec_argc,
                       exec_argv, environ);
    if (pid <= 0) {
        printf("[ai-center] failed to launch %s, ret=%ld\n", app->id, pid);
        return -1;
    }
    printf("[ai-center] %s launched as pid %ld\n", app->id, pid);
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[ai-center] waitpid(%ld) failed, ret=%ld\n",
               pid, (long)waited);
        return -1;
    }
    printf("[ai-center] %s exited, status=%d\n", app->id, status);
    return 0;
}

static int launch_self_learning(void)
{
    const char *path="/sdcard/app/self_learning/self_learning.elf";
    char *args[]={(char *)path,"/sdcard/app/self_learning/recognition.kmodel",
                  "0.5","1","0",NULL};
    if (access(path, X_OK) != 0) return -1;
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 5, args, environ);
    if (pid <= 0) return -1;
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) return -1;
    usleep(200000);
    return 0;
}

static int launch_uvc_camera(void)
{
    const char *path = "/sdcard/app/uvc_camera/uvc_camera.elf";
    char *args[] = {(char *)path, NULL};
    if (access(path, X_OK) != 0) {
        printf("[uvc-camera] executable unavailable: %s\n", path);
        return -1;
    }
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 1, args, environ);
    if (pid <= 0) {
        printf("[uvc-camera] launch failed: %ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[uvc-camera] waitpid failed: %s\n", strerror(errno));
        return -1;
    }
    usleep(200000);
    return 0;
}

static void cloud_write_result_marker(int task, int status)
{
    printf("[cloud-result] writing marker: path=%s task=%d status=%d\n",
           CLOUD_MODEL_RESULT_PATH, task, status);
    FILE *result = fopen(CLOUD_MODEL_RESULT_PATH, "w");
    if (result == NULL) {
        printf("[cloud-model] cannot write result marker: %s\n",
               strerror(errno));
        return;
    }
    int write_result = fprintf(result, "%d %d\n", task, status);
    int flush_result = fflush(result);
    int close_result = fclose(result);
    printf("[cloud-result] marker write complete: fprintf=%d fflush=%d "
           "fclose=%d readable=%d\n",
           write_result, flush_result, close_result,
           access(CLOUD_MODEL_RESULT_PATH, R_OK) == 0 ? 1 : 0);
}

static void cloud_poll_image_inference(void)
{
    if (!g_cloud_background_running ||
        access(CLOUD_MODEL_RESULT_PATH, R_OK) != 0) return;
    printf("[cloud-result] isolated worker completion marker detected\n");
    g_cloud_background_running = false;
    cloud_close_progress_dialog();
    show_pending_cloud_result();
    cloud_update_file_status();
}

static int launch_cloud_model_request(const char *request_path)
{
    FILE *request = fopen(request_path, "r");
    const cloud_task_info_t *task;
    char executable[384];
    char result_path[384];
    char *args[6];
    int argc;
    int task_id = -1;
    int live_mode = 0;
    int status = -1;

    if (request == NULL || fscanf(request, "%d %d", &task_id,
                                  &live_mode) < 1 ||
        task_id < 0 || task_id >= CLOUD_TASK_COUNT) {
        if (request != NULL) fclose(request);
        unlink(request_path);
        printf("[cloud-model] invalid or missing launch request\n");
        cloud_write_result_marker(CLOUD_TASK_CLASSIFICATION, -1);
        return -1;
    }
    fclose(request);
    unlink(request_path);
    task = &g_cloud_tasks[task_id];
    snprintf(executable, sizeof(executable),
             "/sdcard/app/cloudplat/%s", task->executable);
    snprintf(result_path, sizeof(result_path), "/sdcard/%s",
             task->result_file);
    unlink(result_path);

    if (access(executable, X_OK) != 0) {
        printf("[cloud-model] executable unavailable: %s\n", executable);
        cloud_write_result_marker(task_id, -1);
        return -1;
    }
    args[0] = executable;
    if (task->dual_config) {
        args[1] = "/sdcard/ocrdet_deploy_config.json";
        args[2] = "/sdcard/ocrrec_deploy_config.json";
        args[3] = live_mode ? "None" : "/sdcard/test.jpg";
        args[4] = "0";
        args[5] = NULL;
        argc = 5;
    } else {
        args[1] = "/sdcard/deploy_config.json";
        args[2] = live_mode ? "None" : "/sdcard/test.jpg";
        args[3] = "0";
        args[4] = NULL;
        argc = 4;
    }

    printf("[cloud-model] task=%s executable=%s\n", task->name,
           executable);
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, executable, argc, args, environ);
    if (pid <= 0) {
        printf("[cloud-model] launch failed, ret=%ld\n", pid);
        cloud_write_result_marker(task_id, -1);
        return -1;
    }
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[cloud-model] waitpid(%ld) failed, ret=%ld\n",
               pid, (long)waited);
        cloud_write_result_marker(task_id, -1);
        return -1;
    }
    printf("[cloud-model] task=%s exited, status=%d\n", task->name,
           status);
    if (!live_mode) cloud_write_result_marker(task_id, status);
    usleep(200000);
    return status == 0 ? 0 : -1;
}

static int launch_cloud_model(void)
{
    return launch_cloud_model_request(CLOUD_MODEL_REQUEST_PATH);
}

static int launch_code_scanner(void)
{
    const char *path = "/sdcard/app/code_scanner/code_scanner.elf";
    char csi_text[4]; char *args[3];
    if (access(path, X_OK) != 0) {
        printf("[code-scanner] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0]=(char *)path; args[1]=csi_text; args[2]=NULL;
    extern char **environ;
    long pid=syscall(RTSMART_NRSYS_EXEC,path,2,args,environ);
    if(pid<=0){printf("[code-scanner] launch failed: %ld\n",pid);return -1;}
    int status=0; pid_t waited=(pid_t)syscall(RTSMART_NRSYS_WAITPID,(pid_t)pid,&status,0);
    if(waited!=(pid_t)pid){printf("[code-scanner] waitpid failed: %s\n",strerror(errno));return -1;}
    usleep(200000); return 0;
}

static int launch_plate_ocr(void)
{
    const char *path = "/sdcard/app/plate_ocr/licence_det_rec.elf";
    char *args[] = {
        (char *)path,
        "/sdcard/app/plate_ocr/LPD_640.kmodel",
        "0.1", "0.2", "None",
        "/sdcard/app/plate_ocr/licence_reco.kmodel", "0", NULL
    };
    if (access(path, X_OK) != 0) {
        printf("[plate-ocr] executable unavailable: %s\n", path);
        return -1;
    }
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 7, args, environ);
    if (pid <= 0) {
        printf("[plate-ocr] failed to launch, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[plate-ocr] waitpid(%ld) failed: %s\n", pid, strerror(errno));
        return -1;
    }
    printf("[plate-ocr] exited, status=%d\n", status);
    usleep(200000);
    return 0;
}

static int launch_cv_lite(void)
{
    const char *path = "/sdcard/app/cv_lite/cv_lite.elf";
    char csi_text[4];
    char *args[3];
    if (access(path, X_OK) != 0) {
        printf("[cv-lite] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = NULL;
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 2, args, environ);
    if (pid <= 0) {
        printf("[cv-lite] failed to launch, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[cv-lite] waitpid(%ld) failed: %s\n", pid, strerror(errno));
        return -1;
    }
    printf("[cv-lite] exited, status=%d\n", status);
    usleep(200000);
    return 0;
}

static int launch_face_studio(void)
{
    const char *path = "/sdcard/app/face_studio/face_studio.elf";
    char csi_text[4];
    char *args[4];

    if (access(path, X_OK) != 0) {
        printf("[ai-center] Face Studio executable unavailable: %s\n", path);
        return -1;
    }

    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;

    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        printf("[ai-center] failed to launch Face Studio, ret=%ld\n", pid);
        return -1;
    }
    printf("[ai-center] Face Studio launched as pid %ld on CSI%d\n",
           pid, g_system_camera_csi);

    /*
     * sys_exec() links the new LWP as this process's real child. Wait for the
     * complete C++/nncase shutdown and reap it before another desktop Session
     * is created. Do not combine this wait with a completion signal: an
     * interrupted RT-Smart waitpid leaves the child wait-list occupied.
     */
    int status = 0;
    /*
     * Use the RT-Smart private syscall number directly. musl's waitpid()
     * wrapper follows its Linux ABI path on this SDK and returns ENOENT
     * before the RT-Smart child has even entered main().
     */
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[session-supervisor] waitpid(%ld) failed: %s\n",
               pid, strerror(errno));
        return -1;
    }
    printf("[session-supervisor] Face Studio pid %ld exited, status=%d\n",
           pid, status);
    usleep(200000);
    return 0;
}

static int launch_face_geometry(void)
{
    const char *path = "/sdcard/app/face_geometry/face_geometry.elf";
    char csi_text[4];
    char *args[4];

    if (access(path, X_OK) != 0) {
        printf("[face-geometry] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;

    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        printf("[face-geometry] launch failed, ret=%ld\n", pid);
        return -1;
    }
    printf("[face-geometry] launched as pid %ld on CSI%d\n",
           pid, g_system_camera_csi);
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[session-supervisor] Face Geometry wait failed, ret=%ld\n",
               (long)waited);
        return -1;
    }
    printf("[session-supervisor] Face Geometry pid %ld exited, status=%d\n",
           pid, status);
    return 0;
}

static int launch_hand_studio(void)
{
    const char *path = "/sdcard/app/hand_studio/hand_studio.elf";
    char csi_text[4];
    char *args[4];

    if (access(path, X_OK) != 0) {
        printf("[hand-studio] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;

    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        printf("[hand-studio] launch failed, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[session-supervisor] Hand Studio wait failed, ret=%ld\n",
               (long)waited);
        return -1;
    }
    printf("[session-supervisor] Hand Studio pid %ld exited, status=%d\n",
           pid, status);
    return 0;
}

static int launch_human_studio(void)
{
    const char *path = "/sdcard/app/human_studio/human_studio.elf";
    char csi_text[4];
    char *args[4];

    if (access(path, X_OK) != 0) {
        printf("[human-studio] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        printf("[human-studio] launch failed, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[session-supervisor] Human Studio wait failed, ret=%ld\n",
               (long)waited);
        return -1;
    }
    printf("[session-supervisor] Human Studio pid %ld exited, status=%d\n",
           pid, status);
    return 0;
}

static int launch_smart_driving(void)
{
    const char *path = "/sdcard/app/smart_driving/smart_driving.elf";
    char csi_text[4];
    char *args[4];
    if (access(path, X_OK) != 0) {
        printf("[smart-driving] executable unavailable: %s\n", path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        printf("[smart-driving] launch failed, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        printf("[session-supervisor] Smart Driving wait failed, ret=%ld\n",
               (long)waited);
        return -1;
    }
    printf("[session-supervisor] Smart Driving pid %ld exited, status=%d\n",
           pid, status);
    return 0;
}

static int launch_unified_vision_app(const char *name, const char *path)
{
    char csi_text[4];
    char app_dir[256];
    char old_dir[256];
    char *args[4];

    if (access(path, X_OK) != 0) {
        printf("[%s] executable unavailable: %s\n", name, path);
        return -1;
    }
    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = csi_text;
    args[2] = "0";
    args[3] = NULL;
    snprintf(app_dir, sizeof(app_dir), "%s", path);
    char *slash = strrchr(app_dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    if (getcwd(old_dir, sizeof(old_dir)) == NULL || chdir(app_dir) != 0) {
        printf("[%s] cannot enter resource directory %s\n", name, app_dir);
        return -1;
    }
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 3, args, environ);
    if (pid <= 0) {
        chdir(old_dir);
        printf("[%s] launch failed, ret=%ld\n", name, pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    if (waited != (pid_t)pid) {
        chdir(old_dir);
        printf("[%s] wait failed, ret=%ld\n", name, (long)waited);
        return -1;
    }
    chdir(old_dir);
    printf("[%s] exited, status=%d\n", name, status);
    return 0;
}

static int launch_ocr_detection(void)
{
    printf("[session-supervisor] launching OCR Detection\n");
    fflush(stdout);
    return launch_unified_vision_app(
        "ocr-detection",
        "/sdcard/app/ocr_detection/ocr_detection.elf");
}

static int launch_yolov8_vision(void)
{
    return launch_unified_vision_app(
        "yolov8-vision",
        "/sdcard/app/yolov8_vision/yolov8_vision.elf");
}

static int launch_network_camera(void)
{
    return launch_unified_vision_app(
        "network-camera",
        "/sdcard/app/network_camera/network_camera.elf");
}

static int launch_yolo_models(void)
{
    return launch_unified_vision_app(
        "yolo-models",
        "/sdcard/app/yolo_models/yolo_models.elf");
}

static int launch_gallery_player(void)
{
    const char *player =
        "/sdcard/app/gallery_player/gallery_player.elf";
    char media_path[320] = { 0 };
    FILE *request = fopen(GALLERY_PLAY_REQUEST, "r");
    if (request == NULL ||
        fgets(media_path, sizeof(media_path), request) == NULL) {
        if (request != NULL)
            fclose(request);
        printf("[gallery-player] playback request unavailable\n");
        return -1;
    }
    fclose(request);
    media_path[strcspn(media_path, "\r\n")] = '\0';
    if (access(player, X_OK) != 0 || access(media_path, R_OK) != 0) {
        printf("[gallery-player] player or media unavailable: %s\n",
               media_path);
        return -1;
    }
    char *args[] = { (char *)player, media_path, NULL };
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, player, 2, args, environ);
    if (pid <= 0) {
        printf("[gallery-player] launch failed, ret=%ld\n", pid);
        return -1;
    }
    int status = 0;
    pid_t waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                  (pid_t)pid, &status, 0);
    printf("[gallery-player] pid %ld exited, status=%d\n", pid, status);
    return waited == (pid_t)pid ? 0 : -1;
}

static int run_desktop_worker(void)
{
    const char *path = "/sdcard/app/dshanpi_aimodel";
    char csi_text[4];
    char initial_view_text[8];
    char *args[5];

    snprintf(csi_text, sizeof(csi_text), "%d", g_system_camera_csi);
    args[0] = (char *)path;
    args[1] = "--desktop";
    args[2] = csi_text;
    snprintf(initial_view_text, sizeof(initial_view_text), "%d", g_initial_view);
    args[3] = initial_view_text;
    args[4] = NULL;

    unlink(DESKTOP_STATUS_PATH);
    unlink(DESKTOP_STATUS_PATH ".tmp");
    extern char **environ;
    long pid = syscall(RTSMART_NRSYS_EXEC, path, 4, args, environ);
    g_initial_view = DSHANPI_AUTOSTART_NONE;
    if (pid <= 0) {
        printf("[session-supervisor] failed to launch desktop worker, ret=%ld\n",
               pid);
        return -1;
    }

    g_desktop_worker_pid = (sig_atomic_t)pid;
    printf("[session-supervisor] desktop worker launched as pid %ld\n", pid);
    int status = 0;
    pid_t waited = (pid_t)-1;
    int wait_error = 0;
    int wait_retry = 0;

    /*
     * Register the parent waiter immediately and use the child exit status as
     * the primary hand-off.  RT-Smart can leave a per-process /tmp directory
     * lookup stale even after the desktop has successfully published its
     * completion marker.  Waiting for that marker before waitpid used to
     * strand the supervisor on a black screen forever.
     */
    do {
        errno = 0;
        waited = (pid_t)syscall(RTSMART_NRSYS_WAITPID,
                                (pid_t)pid, &status, 0);
        wait_error = errno;
        if (waited == (pid_t)pid) {
            break;
        }
        if (waited == (pid_t)-1 &&
            (wait_error == EINTR || wait_error == EAGAIN ||
             wait_error == ENOENT) && wait_retry < 50) {
            ++wait_retry;
            printf("[session-supervisor] desktop waitpid(%ld) transient "
                   "failure, retry=%d, errno=%d (%s)\n",
                   pid, wait_retry, wait_error, strerror(wait_error));
            usleep(10000);
            continue;
        }
        break;
    } while (1);

    int reported_status = -1;
    if (waited == (pid_t)pid) {
        /* The marker is diagnostic only when waitpid has supplied a result. */
        FILE *status_file = fopen(DESKTOP_STATUS_PATH, "r");
        if (status_file != NULL) {
            if (fscanf(status_file, "%d", &reported_status) != 1 ||
                reported_status < 0)
                reported_status = -1;
            fclose(status_file);
        }
        unlink(DESKTOP_STATUS_PATH);
        unlink(DESKTOP_STATUS_PATH ".tmp");
        printf("[session-supervisor] desktop worker pid %ld exited, "
               "status=%d\n", pid, status);
        if (reported_status >= 0) {
            printf("[session-supervisor] desktop completion marker: "
                   "status=%d\n", reported_status);
            if (reported_status != status) {
                printf("[session-supervisor] desktop status mismatch: "
                       "marker=%d waitpid=%d\n", reported_status, status);
            }
        }
        g_desktop_worker_pid = -1;
        return reported_status >= 0 ? reported_status : status;
    }

    printf("[session-supervisor] desktop waitpid(%ld) failed, ret=%ld, "
           "errno=%d (%s); using bounded marker fallback\n",
           pid, (long)waited, wait_error, strerror(wait_error));

    /*
     * Some RT-Smart builds can reap a short-lived worker before the private
     * waitpid syscall observes it.  In that case the marker remains useful,
     * but it must never be an unbounded wait.  Five seconds is ample for the
     * worker's flush+rename and prevents a permanent black screen.
     */
    for (int marker_retry = 0; marker_retry < 250; ++marker_retry) {
        FILE *status_file = fopen(DESKTOP_STATUS_PATH, "r");
        if (status_file != NULL) {
            int parsed = fscanf(status_file, "%d", &reported_status);
            fclose(status_file);
            if (parsed == 1 && reported_status >= 0) {
                unlink(DESKTOP_STATUS_PATH);
                unlink(DESKTOP_STATUS_PATH ".tmp");
                printf("[session-supervisor] desktop completion marker: "
                       "status=%d\n", reported_status);
                printf("[session-supervisor] desktop worker already reaped; "
                       "continuing with marker status=%d\n",
                       reported_status);
                g_desktop_worker_pid = -1;
                return reported_status;
            }
        }
        usleep(20000);
    }

    unlink(DESKTOP_STATUS_PATH);
    unlink(DESKTOP_STATUS_PATH ".tmp");
    printf("[session-supervisor] desktop completion status lost; "
           "recovering launcher instead of remaining on a black screen\n");
    g_desktop_worker_pid = -1;
    return DESKTOP_EXIT_RECOVER;
}

static void screenshot_saved_notify_desktop(const char *image_path)
{
    sig_atomic_t pid = g_desktop_worker_pid;

    (void)image_path;
    if (pid <= 0)
        return;
    if (kill((pid_t)pid, SIGUSR1) != 0)
        printf("[screenshot] desktop notification signal failed: %s\n",
               strerror(errno));
}

static void write_desktop_status(int status)
{
    char temporary[96];
    snprintf(temporary, sizeof(temporary), "%s.tmp", DESKTOP_STATUS_PATH);
    FILE *file = fopen(temporary, "w");
    if (file == NULL) {
        printf("[desktop-worker] cannot create completion marker: %s\n",
               strerror(errno));
        return;
    }
    int failed = fprintf(file, "%d\n", status) < 0 || fflush(file) != 0;
    if (fclose(file) != 0) failed = 1;
    if (failed || rename(temporary, DESKTOP_STATUS_PATH) != 0) {
        printf("[desktop-worker] cannot publish completion marker: %s\n",
               strerror(errno));
        unlink(temporary);
        return;
    }
    printf("[desktop-worker] completion marker published: status=%d\n",
           status);
}

static void remember_launch_error(const char *app_name)
{
    FILE *file = fopen(LAUNCH_ERROR_PATH, "w");
    if (file != NULL) {
        fprintf(file, "%s failed to start", app_name);
        fclose(file);
    }
}

static void show_pending_launch_error(void)
{
    char message[96];
    FILE *file = fopen(LAUNCH_ERROR_PATH, "r");
    if (file == NULL) return;
    if (fgets(message, sizeof(message), file) != NULL) {
        message[strcspn(message, "\r\n")] = '\0';
        show_toast(message);
    }
    fclose(file);
    unlink(LAUNCH_ERROR_PATH);
}

static int desktop_main(void)
{
    for (;;) {
        g_stop = 0;
        g_launch_ai_app = NULL;
        g_launch_face_studio = false;
        g_launch_face_geometry = false;
        g_launch_hand_studio = false;
        g_launch_human_studio = false;
        g_launch_smart_driving = false;
        g_launch_ocr_detection = false;
        g_launch_yolov8_vision = false;
        g_launch_network_camera = false;
        g_launch_yolo_models = false;
        g_launch_gallery_player = false;
        g_launch_cv_lite = false;
        g_launch_plate_ocr = false;
        g_launch_code_scanner = false;
        g_launch_self_learning = false;
        g_launch_cloud_model = false;
        g_launch_uvc_camera = false;
        g_paint_canvas = NULL;
        g_paint_draw_buf = NULL;
        g_paint_buffer = NULL;

        printf("K230 LVGL launcher: GC2093 CSI%d (%s), ST7701 640x480\n",
               g_system_camera_csi,
               dshanpi_camera_setting_name(g_system_camera_csi));

        if (vb_init() != 0) {
            printf("VB initialization failed\n");
            return 1;
        }

        if (kd_display_init(ST7701_480_640_DSI_V1) != 0) {
            printf("ST7701 480x640 connector initialization failed\n");
            kd_mpi_vb_exit();
            return 1;
        }

        lv_init();
        g_display = lv_k230_display_create(K_VO_LAYER_OSD0, 255);
        if (g_display == NULL) {
            printf("LVGL display initialization failed\n");
            lv_deinit();
            kd_display_deinit();
            kd_mpi_vb_exit();
            return 1;
        }

        lv_display_set_rotation(g_display, LV_DISPLAY_ROTATION_270);
        lv_display_set_color_format(g_display, LV_COLOR_FORMAT_ARGB8888);
        /* Match UI updates to the 43 Hz panel instead of LVGL's 30 Hz default. */
        lv_timer_set_period(lv_display_get_refr_timer(g_display), 23);
        g_touch_indev = lv_k230_touch_init(0);
        if (g_touch_indev != NULL) {
            /* A small amount of momentum keeps the list fluid, while 8%
             * decay stops it promptly enough for the next tap or reverse
             * swipe.  The old 2% value coasted for too long and made the
             * desktop feel as if it was still ignoring the user. */
            lv_indev_set_scroll_throw(g_touch_indev, 8);
        }
        create_ui();
        if (screenshot_notification_consume())
            show_toast(settings_text("Screenshot saved"));
        if (g_touch_indev != NULL) {
            lv_indev_add_event_cb(g_touch_indev, screensaver_touch_cb,
                                  LV_EVENT_ALL, NULL);
        }
        unlink(CLOUD_IMAGE_REQUEST_PATH);
        unlink(CLOUD_MODEL_RESULT_PATH);
        wifi_update_network_status();
        desktop_status_update();
        show_pending_launch_error();
        show_pending_cloud_result();
        switch (g_initial_view) {
        case DSHANPI_AUTOSTART_SETTINGS: show_settings(); break;
        case DSHANPI_AUTOSTART_CAMERA: show_camera(); break;
        case DSHANPI_AUTOSTART_GALLERY: show_gallery(); break;
        case DSHANPI_AUTOSTART_DRAWING: show_paint(); break;
        case DSHANPI_AUTOSTART_UART_LAB: show_uart_lab(); break;
        case DSHANPI_AUTOSTART_DUAL_CAMERA: show_dual_camera(); break;
        default: break;
        }
        g_initial_view = DSHANPI_AUTOSTART_NONE;

        /* A pending A/B slot is confirmed only after display, LVGL, touch and
         * the complete home UI have initialized and rendered successfully. */
        (void)lv_timer_handler();
        lv_refr_now(NULL);
        dshanpi_ota_confirm_boot_after_health_delay();

        uint32_t ota_heartbeat_tick = lv_tick_get();
        while (!g_stop) {
            uint32_t delay_ms = lv_timer_handler();
            /* The OTA health worker checks progress every two seconds.  A
             * 250 ms heartbeat is comfortably faster than that while
             * avoiding a mutex lock on every 5 ms UI/input iteration. */
            if (lv_tick_elaps(ota_heartbeat_tick) >= 250U) {
                ota_heartbeat_tick = lv_tick_get();
                dshanpi_ota_report_ui_heartbeat();
            }
            if (g_screenshot_saved_pending) {
                g_screenshot_saved_pending = 0;
                screenshot_notification_consume();
                show_toast(settings_text("Screenshot saved"));
            }
            cloud_poll_image_inference();
            /* The touch driver requests a 5 ms sampling period.  Capping the
             * main loop at the old 8 ms value silently limited it to 125 Hz;
             * honor the requested 200 Hz input cadence while display redraws
             * remain synchronized to the 43 Hz panel. */
            if (delay_ms == LV_NO_TIMER_READY || delay_ms > 5) {
                delay_ms = 5;
            }
            if (delay_ms == 0) {
                delay_ms = 1;
            }
            usleep(delay_ms * 1000);
        }

        save_app_scroll_position();
        /* A serial stop can arrive while the embedded Gallery owns VDEC.
         * Close it before the global VB exit; otherwise the decoder pools
         * keep VB active and the desktop worker cannot return to msh. */
        if (g_gallery_video_playing) {
            kd_player_resume();
            kd_player_stop();
            g_gallery_video_playing = false;
        }
        dshanpi_camera_stop();
        dshanpi_dual_camera_stop();
        uart_lab_stop();
        /* Delete the canvas before releasing its referenced draw buffer. */
        if (g_paint_canvas != NULL) {
            lv_obj_delete(g_paint_canvas);
            g_paint_canvas = NULL;
        }
        if (g_paint_draw_buf != NULL) {
            lv_draw_buf_destroy(g_paint_draw_buf);
            g_paint_draw_buf = NULL;
        }
        g_paint_buffer = NULL;

        /*
         * The K230 display port releases its OSD VB blocks from the display
         * LV_EVENT_DELETE callback. lv_deinit() alone did not reliably run
         * that path before the child Session started, so delete explicitly.
         */
        if (g_display != NULL) {
            lv_display_delete(g_display);
            g_display = NULL;
        }
        lv_deinit();
        g_touch_indev = NULL;
        g_screensaver_timer = NULL;
        g_screensaver_active = false;
        kd_display_deinit();

        int vb_ret = -1;
        for (int retry = 0; retry < 20; ++retry) {
            vb_ret = kd_mpi_vb_exit();
            if (vb_ret == 0) {
                break;
            }
            printf("[session-supervisor] VB exit retry %d, ret=%d\n",
                   retry + 1, vb_ret);
            usleep(50000);
        }
        if (vb_ret != 0) {
            printf("[session-supervisor] refusing to launch child: "
                   "VB is still active, ret=%d\n", vb_ret);
            return 1;
        }
        printf("[session-supervisor] desktop media resources released\n");
        /*
         * A successful VB exit is the synchronization point.  Retain only a
         * small hardware settle guard instead of imposing 300 ms on every
         * application launch.
         */
        usleep(80000);

        if (g_launch_face_studio) {
            printf("[desktop-worker] requesting Face Studio session\n");
            return DESKTOP_EXIT_FACE_STUDIO;
        }
        if (g_launch_face_geometry) {
            printf("[desktop-worker] requesting Face Geometry session\n");
            return DESKTOP_EXIT_FACE_GEOMETRY;
        }
        if (g_launch_hand_studio) {
            printf("[desktop-worker] requesting Hand Studio session\n");
            return DESKTOP_EXIT_HAND_STUDIO;
        }
        if (g_launch_human_studio) {
            printf("[desktop-worker] requesting Human Studio session\n");
            return DESKTOP_EXIT_HUMAN_STUDIO;
        }
        if (g_launch_smart_driving) {
            printf("[desktop-worker] requesting Smart Driving session\n");
            return DESKTOP_EXIT_SMART_DRIVING;
        }
        if (g_launch_ocr_detection) {
            return DESKTOP_EXIT_OCR_DETECTION;
        }
        if (g_launch_yolov8_vision) {
            return DESKTOP_EXIT_YOLOV8_VISION;
        }
        if (g_launch_network_camera) {
            return DESKTOP_EXIT_NETWORK_CAMERA;
        }
        if (g_launch_yolo_models) {
            return DESKTOP_EXIT_YOLO_MODELS;
        }
        if (g_launch_gallery_player) {
            return DESKTOP_EXIT_GALLERY_PLAYER;
        }
        if (g_launch_cv_lite) {
            return DESKTOP_EXIT_CV_LITE;
        }
        if (g_launch_plate_ocr) {
            return DESKTOP_EXIT_PLATE_OCR;
        }
        if (g_launch_code_scanner) {
            return DESKTOP_EXIT_CODE_SCANNER;
        }
        if (g_launch_self_learning) return DESKTOP_EXIT_SELF_LEARNING;
        if (g_launch_cloud_model) return DESKTOP_EXIT_CLOUD_MODEL;
        if (g_launch_uvc_camera) return DESKTOP_EXIT_UVC_CAMERA;
        if (g_launch_ai_app != NULL) {
            if (launch_registered_ai_app(g_launch_ai_app) != 0) {
                printf("[desktop-worker] AI mode failed, returning home\n");
            }
            continue;
        }
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, screenshot_saved_signal_handler);

    if (argc > 1 && strcmp(argv[1], "--ota-check") == 0) {
        dshanpi_ota_snapshot_t snapshot;
        int result = dshanpi_ota_check_network();
        dshanpi_ota_get_snapshot(&snapshot);
        printf("[ota-check] result=%d state=%d detail=%s\n", result,
               (int)snapshot.state, snapshot.detail);
        return result == 0 ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--desktop") == 0) {
        dshanpi_system_settings_load(&g_system_settings);
        dshanpi_system_settings_apply_timezone(&g_system_settings);
        if (argc > 2 &&
            (atoi(argv[2]) == DSHANPI_CAMERA_REAR_CSI ||
             atoi(argv[2]) == DSHANPI_CAMERA_FRONT_CSI)) {
            g_system_camera_csi = atoi(argv[2]);
        } else {
            g_system_camera_csi = dshanpi_camera_setting_load();
        }
        if (argc > 3 && atoi(argv[3]) >= DSHANPI_AUTOSTART_NONE &&
            atoi(argv[3]) < DSHANPI_AUTOSTART_COUNT)
            g_initial_view = (dshanpi_autostart_t)atoi(argv[3]);
        if (argc > 4) {
            unsigned long test_seconds = strtoul(argv[4], NULL, 10);
            if (test_seconds > 0 && test_seconds <= 60)
                g_dual_camera_fps_test_seconds =
                    (unsigned int)test_seconds;
        }
        int desktop_status = desktop_main();
        write_desktop_status(desktop_status);
        /*
         * RT-Smart does not reliably tear down the complete process when the
         * desktop main thread merely returns while a detached Wi-Fi worker is
         * still alive.  The completion marker is then visible, but the
         * supervisor remains blocked in waitpid() and Ctrl+C never returns
         * control to msh.
         *
         * desktop_main() has already stopped cameras/UART, deleted LVGL and
         * display objects, and released VB before reaching this point. Flush
         * the diagnostic line and terminate the whole desktop worker without
         * running process-wide exit handlers that may wait on detached
         * threads.  The persistent supervisor will reap this exact status.
         */
        printf("[desktop-worker] terminating process: status=%d\n",
               desktop_status);
        fflush(stdout);
        _exit(desktop_status);
    }

    /*
     * PID 1 is a media-free, persistent supervisor. Every desktop and every
     * Face Studio instance is its direct child and is fully reaped before the
     * next Session starts. This avoids RT-Smart's unreliable recursive
     * desktop -> desktop -> AI process chain.
     */
    g_system_camera_csi = dshanpi_camera_setting_load();
    dshanpi_system_settings_load(&g_system_settings);
    int language_commit_result =
        dshanpi_system_settings_commit_pending_language(
            &g_system_settings);
    if (language_commit_result > 0) {
        printf("[session-supervisor] applied pending language: %s\n",
               dshanpi_language_name(g_system_settings.language));
    } else if (language_commit_result < 0) {
        printf("[session-supervisor] failed to apply pending language\n");
    }
    dshanpi_system_settings_apply_timezone(&g_system_settings);
    printf("[session-supervisor] persistent supervisor started on CSI%d\n",
           g_system_camera_csi);
    if (dshanpi_screenshot_service_start(
            screenshot_saved_notify_desktop) != 0)
        printf("[screenshot] unable to start global key monitor\n");

    /*
     * Autostart is intentionally attempted only once per board boot. When
     * the selected app exits, the supervisor falls through to the desktop
     * instead of immediately launching it again.
     */
    if (dshanpi_ota_boot_is_pending()) {
        printf("[ota] pending slot validation: suppressing one-time autostart\n");
        g_initial_view = DSHANPI_AUTOSTART_NONE;
    } else switch (g_system_settings.autostart) {
    case DSHANPI_AUTOSTART_FACE_STUDIO:
        launch_face_studio();
        break;
    case DSHANPI_AUTOSTART_FACE_GEOMETRY:
        launch_face_geometry();
        break;
    case DSHANPI_AUTOSTART_HAND_STUDIO:
        launch_hand_studio();
        break;
    case DSHANPI_AUTOSTART_HUMAN_STUDIO:
        launch_human_studio();
        break;
    case DSHANPI_AUTOSTART_SMART_DRIVING:
        launch_smart_driving();
        break;
    case DSHANPI_AUTOSTART_OCR_DETECTION:
        launch_ocr_detection();
        break;
    case DSHANPI_AUTOSTART_OBJECT_DETECTION:
        launch_yolov8_vision();
        break;
    case DSHANPI_AUTOSTART_YOLO_MODELS:
        launch_yolo_models();
        break;
    case DSHANPI_AUTOSTART_RTSP_STREAM:
    case DSHANPI_AUTOSTART_RTMP_STREAM:
        launch_network_camera();
        break;
    case DSHANPI_AUTOSTART_CV_LITE:
        launch_cv_lite();
        break;
    case DSHANPI_AUTOSTART_PLATE_OCR:
        launch_plate_ocr();
        break;
    case DSHANPI_AUTOSTART_CODE_SCANNER:
        launch_code_scanner();
        break;
    case DSHANPI_AUTOSTART_SELF_LEARNING:
        launch_self_learning();
        break;
    case DSHANPI_AUTOSTART_UVC_CAMERA:
        launch_uvc_camera();
        break;
    case DSHANPI_AUTOSTART_SETTINGS:
    case DSHANPI_AUTOSTART_CAMERA:
    case DSHANPI_AUTOSTART_GALLERY:
    case DSHANPI_AUTOSTART_DRAWING:
    case DSHANPI_AUTOSTART_UART_LAB:
    case DSHANPI_AUTOSTART_DUAL_CAMERA:
        g_initial_view = g_system_settings.autostart;
        break;
    default:
        break;
    }
    while (!g_stop) {
        int desktop_status = run_desktop_worker();
        if (desktop_status == DESKTOP_EXIT_RECOVER) {
            printf("[session-supervisor] restarting desktop after failed "
                   "worker hand-off\n");
            usleep(50000);
            continue;
        }
        if (desktop_status < 0) {
            return 1;
        }
        if (desktop_status == DESKTOP_EXIT_FACE_STUDIO) {
            if (launch_face_studio() != 0) {
                remember_launch_error("Face Studio");
            }
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_FACE_GEOMETRY) {
            if (launch_face_geometry() != 0) {
                remember_launch_error("Face Geometry");
            }
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_HAND_STUDIO) {
            if (launch_hand_studio() != 0) {
                remember_launch_error("Hand Studio");
            }
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_HUMAN_STUDIO) {
            if (launch_human_studio() != 0) {
                remember_launch_error("Human Studio");
            }
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_SMART_DRIVING) {
            if (launch_smart_driving() != 0) {
                remember_launch_error("Smart Driving");
            }
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_OCR_DETECTION) {
            if (launch_ocr_detection() != 0)
                remember_launch_error("OCR Detection");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_YOLOV8_VISION) {
            if (launch_yolov8_vision() != 0)
                remember_launch_error("Object Detection");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_NETWORK_CAMERA ||
            desktop_status == DESKTOP_EXIT_RTMP_STREAM_LEGACY) {
            if (launch_network_camera() != 0)
                remember_launch_error("Network Camera");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_YOLO_MODELS) {
            if (launch_yolo_models() != 0)
                remember_launch_error("YOLO Models");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_GALLERY_PLAYER) {
            if (launch_gallery_player() != 0)
                remember_launch_error("Gallery Player");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_CV_LITE) {
            if (launch_cv_lite() != 0)
                remember_launch_error("CV Lite");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_PLATE_OCR) {
            if (launch_plate_ocr() != 0)
                remember_launch_error("Plate OCR");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_CODE_SCANNER) {
            if (launch_code_scanner() != 0)
                remember_launch_error("Code Scanner");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_SELF_LEARNING) {
            if (launch_self_learning() != 0)
                remember_launch_error("AI Learning");
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_CLOUD_MODEL) {
            launch_cloud_model();
            continue;
        }
        if (desktop_status == DESKTOP_EXIT_UVC_CAMERA) {
            if (launch_uvc_camera() != 0)
                remember_launch_error("USB Camera");
            continue;
        }
        return desktop_status;
    }
    return 0;
}
