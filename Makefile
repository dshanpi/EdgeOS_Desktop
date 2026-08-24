# Include base configuration
include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

# Keep the installed launcher name stable even when the repository is cloned
# with its default EdgeOS_Desktop directory name.  Runtime upgrade and app
# return paths use /sdcard/app/dshanpi_aimodel.
DIR := dshanpi_aimodel
BUILD := $(SDK_APPS_BUILD_DIR)/$(DIR)
BIN := $(SDK_APPS_IMAGE_DIR)/$(DIR)
UI_FONT_SRC := $(CURDIR)/font/DroidSansFallbackFull.ttf
UI_FONT_INSTALL := $(SDK_APPS_IMAGE_DIR)/dshanpi_ui_font.ttf
FACE_SRC := $(CURDIR)/apps/face_detection
FACE_BUILD_BIN := $(FACE_SRC)/k230_bin/face_detection.elf
FACE_INSTALL_BIN := $(SDK_APPS_IMAGE_DIR)/face_detection.elf
FACE_STUDIO_SRC := $(CURDIR)/apps/face_studio
FACE_STUDIO_BUILD_DIR := $(FACE_STUDIO_SRC)/k230_bin
FACE_STUDIO_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/face_studio
FACE_STUDIO_STAMP := $(BUILD)/face-studio.stamp
FACE_GEOMETRY_SRC := $(CURDIR)/apps/face_geometry
FACE_GEOMETRY_BUILD_DIR := $(FACE_GEOMETRY_SRC)/k230_bin
FACE_GEOMETRY_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/face_geometry
FACE_GEOMETRY_STAMP := $(BUILD)/face-geometry.stamp
HAND_STUDIO_SRC := $(CURDIR)/apps/hand_studio
HAND_STUDIO_BUILD_DIR := $(HAND_STUDIO_SRC)/k230_bin
HAND_STUDIO_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/hand_studio
HAND_STUDIO_STAMP := $(BUILD)/hand-studio.stamp
HUMAN_STUDIO_SRC := $(CURDIR)/apps/human_studio
HUMAN_STUDIO_BUILD_DIR := $(HUMAN_STUDIO_SRC)/k230_bin
HUMAN_STUDIO_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/human_studio
HUMAN_STUDIO_STAMP := $(BUILD)/human-studio.stamp
SMART_DRIVING_SRC := $(CURDIR)/apps/smart_driving
SMART_DRIVING_BUILD_DIR := $(SMART_DRIVING_SRC)/k230_bin
SMART_DRIVING_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/smart_driving
SMART_DRIVING_STAMP := $(BUILD)/smart-driving.stamp
OCR_DETECTION_SRC := $(CURDIR)/apps/ocr_detection
OCR_DETECTION_BUILD_DIR := $(OCR_DETECTION_SRC)/k230_bin
OCR_DETECTION_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/ocr_detection
OCR_DETECTION_STAMP := $(BUILD)/ocr-detection.stamp
YOLOV8_VISION_SRC := $(CURDIR)/apps/yolov8_vision
YOLOV8_VISION_BUILD_DIR := $(YOLOV8_VISION_SRC)/k230_bin
YOLOV8_VISION_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/yolov8_vision
YOLOV8_VISION_STAMP := $(BUILD)/yolov8-vision.stamp
NETWORK_CAMERA_SRC := $(CURDIR)/apps/network_camera
NETWORK_CAMERA_BUILD_DIR := $(NETWORK_CAMERA_SRC)/k230_bin
NETWORK_CAMERA_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/network_camera
NETWORK_CAMERA_STAMP := $(BUILD)/network-camera.stamp
YOLO_MODELS_SRC := $(CURDIR)/apps/yolo_models
YOLO_MODELS_BUILD_DIR := $(YOLO_MODELS_SRC)/k230_bin
YOLO_MODELS_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/yolo_models
YOLO_MODELS_STAMP := $(BUILD)/yolo-models.stamp
GALLERY_PLAYER_SRC := $(CURDIR)/apps/gallery_player
GALLERY_PLAYER_BUILD_DIR := $(GALLERY_PLAYER_SRC)/k230_bin
GALLERY_PLAYER_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/gallery_player
GALLERY_PLAYER_STAMP := $(BUILD)/gallery-player.stamp
CV_LITE_SRC := $(CURDIR)/apps/cv_lite
CV_LITE_BUILD_DIR := $(CV_LITE_SRC)/k230_bin
CV_LITE_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/cv_lite
CV_LITE_STAMP := $(BUILD)/cv-lite.stamp
PLATE_OCR_BUILD_DIR = $(AI_SUITE_SRC)/k230_bin/licence_det_rec
PLATE_OCR_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/plate_ocr
PLATE_OCR_STAMP := $(BUILD)/plate-ocr.stamp
CODE_SCANNER_SRC := $(CURDIR)/apps/code_scanner
CODE_SCANNER_BUILD_DIR := $(CODE_SCANNER_SRC)/k230_bin
CODE_SCANNER_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/code_scanner
CODE_SCANNER_STAMP := $(BUILD)/code-scanner.stamp
SELF_LEARNING_BUILD_DIR = $(AI_SUITE_SRC)/k230_bin/self_learning
SELF_LEARNING_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/self_learning
SELF_LEARNING_STAMP := $(BUILD)/self-learning.stamp
UVC_CAMERA_SRC := $(CURDIR)/apps/uvc_camera
UVC_CAMERA_BUILD_DIR := $(UVC_CAMERA_SRC)/k230_bin
UVC_CAMERA_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/uvc_camera
UVC_CAMERA_STAMP := $(BUILD)/uvc-camera.stamp
CLOUDPLAT_SRC := $(CURDIR)/apps/cloudplat_deploy_code
CLOUDPLAT_BUILD_DIR := $(CLOUDPLAT_SRC)/k230_bin
CLOUDPLAT_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/cloudplat
CLOUDPLAT_STAMP := $(BUILD)/cloudplat.stamp
AI_SUITE_SRC := $(CURDIR)/apps/ai_demo
AI_SUITE_BUILD_OUT := $(AI_SUITE_SRC)/k230_bin
AI_SUITE_INSTALL_DIR := $(SDK_APPS_IMAGE_DIR)/ai
AI_SUITE_STAMP := $(BUILD)/ai-suite.stamp
AI_SUITE_PROJECTS := ocr sq_handkp_ocr licence_det licence_det_rec \
	object_detect_yolov8n segment_yolov8n anomaly_det self_learning \
	bytetrack nanotracker puzzle_game finger_guessing sq_handkp_flower \
	space_resize virtual_keyboard dynamic_gesture sq_handkp_class \
	kws tts_zh translate_en_ch
AI_SUITE_BUILD_INPUTS := $(AI_SUITE_SRC)/build_app.sh \
	$(AI_SUITE_SRC)/CMakeLists.txt

C_SRCS := apps/main.c apps/ai_registry.c apps/material_app_icons.c \
	apps/screensaver_asset.c \
	apps/ui_font_source_han_20.c \
	$(shell find middleware system uart -type f -name '*.c' | sort)
C_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))
DEPS := $(C_OBJS:.o=.d)
OBJ_DIRS := $(sort $(dir $(C_OBJS) $(BIN)))
SYSTEM_VERSION_FILE := $(SDK_BOARD_DIR)/system-version.txt
SDK_VERSION_HEADER := $(SDK_RTSMART_SRC_DIR)/rtsmart/kernel/bsp/maix3/sdk_version.h
MATERIAL_ICON_ASSETS := $(wildcard apps/generated/material_icon_*.inc)
SCREENSAVER_ASSETS := apps/generated/screensaver_background.inc

include $(SDK_RTSMART_SRC_DIR)/libs/mk/liblvgl.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/lib3rdparty.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk

CFLAGS := -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS += -I$(SDK_RTSMART_SRC_DIR)/libs/rtsmart_hal/components/k230_ota
CFLAGS += -Iapps -Imiddleware -Isystem $(LIB_CFLAGS)
CFLAGS += -Iuart
CFLAGS += -I$(CURDIR)/third_party/vaxp/include
CFLAGS += -I$(SDK_SRC_ROOT_DIR)/include
CFLAGS += -I$(SDK_RTSMART_SRC_DIR)/rtsmart/kernel/bsp/maix3
CFLAGS += -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_port_config.h\"
CFLAGS += -include "generated/autoconf.h"
CFLAGS += -Wall -Wextra -Werror -ffunction-sections -fdata-sections -std=gnu99

LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections
LDFLAGS += $(LIB_LDFLAGS)

.PHONY: all clean distclean .FORCE

# Small bring-up image: install the desktop launcher and unified studios.
all: $(BIN) $(UI_FONT_INSTALL) $(FACE_STUDIO_STAMP) $(FACE_GEOMETRY_STAMP) $(HAND_STUDIO_STAMP) $(HUMAN_STUDIO_STAMP) $(SMART_DRIVING_STAMP) $(OCR_DETECTION_STAMP) $(YOLOV8_VISION_STAMP) $(NETWORK_CAMERA_STAMP) $(YOLO_MODELS_STAMP) $(GALLERY_PLAYER_STAMP) $(CV_LITE_STAMP) $(PLATE_OCR_STAMP) $(CODE_SCANNER_STAMP) $(SELF_LEARNING_STAMP) $(UVC_CAMERA_STAMP) $(CLOUDPLAT_STAMP)
	$(Q)rm -rf $(AI_SUITE_INSTALL_DIR)
	@$(ECHO) [BUILD] applications $(DIR) done.

.FORCE:

$(BIN): $(C_OBJS) .FORCE | $(OBJ_DIRS)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CFLAGS) $(C_OBJS) $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(UI_FONT_INSTALL): $(UI_FONT_SRC) | $(OBJ_DIRS)
	@echo "[INSTALL] shared CJK application font"
	$(Q)cp -f $< $@

$(FACE_INSTALL_BIN): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] front-camera face detection session"
	$(Q)cd $(FACE_SRC) && ./build_app.sh
	$(Q)cp -f $(FACE_BUILD_BIN) $@

$(FACE_STUDIO_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Face Studio"
	$(Q)rm -f $(FACE_INSTALL_BIN)
	$(Q)cd $(FACE_STUDIO_SRC) && ./build_app.sh
	$(Q)mkdir -p $(FACE_STUDIO_INSTALL_DIR)
	$(Q)rsync -a --delete $(FACE_STUDIO_BUILD_DIR)/ $(FACE_STUDIO_INSTALL_DIR)/
	$(Q)touch $@

$(FACE_GEOMETRY_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Face Geometry"
	$(Q)cd $(FACE_GEOMETRY_SRC) && ./build_app.sh
	$(Q)mkdir -p $(FACE_GEOMETRY_INSTALL_DIR)
	$(Q)rsync -a --delete $(FACE_GEOMETRY_BUILD_DIR)/ $(FACE_GEOMETRY_INSTALL_DIR)/
	$(Q)touch $@

$(HAND_STUDIO_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Hand Studio"
	$(Q)cd $(HAND_STUDIO_SRC) && ./build_app.sh
	$(Q)mkdir -p $(HAND_STUDIO_INSTALL_DIR)
	$(Q)rsync -a --delete $(HAND_STUDIO_BUILD_DIR)/ $(HAND_STUDIO_INSTALL_DIR)/
	$(Q)touch $@

$(HUMAN_STUDIO_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Human Studio"
	$(Q)cd $(HUMAN_STUDIO_SRC) && ./build_app.sh
	$(Q)mkdir -p $(HUMAN_STUDIO_INSTALL_DIR)
	$(Q)rsync -a --delete $(HUMAN_STUDIO_BUILD_DIR)/ $(HUMAN_STUDIO_INSTALL_DIR)/
	$(Q)touch $@

$(SMART_DRIVING_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Smart Driving"
	$(Q)cd $(SMART_DRIVING_SRC) && ./build_app.sh
	$(Q)mkdir -p $(SMART_DRIVING_INSTALL_DIR)
	$(Q)rsync -a --delete $(SMART_DRIVING_BUILD_DIR)/ $(SMART_DRIVING_INSTALL_DIR)/
	$(Q)touch $@

$(OCR_DETECTION_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified OCR Detection"
	$(Q)cd $(OCR_DETECTION_SRC) && ./build_app.sh
	$(Q)mkdir -p $(OCR_DETECTION_INSTALL_DIR)
	$(Q)rsync -a --delete $(OCR_DETECTION_BUILD_DIR)/ $(OCR_DETECTION_INSTALL_DIR)/
	$(Q)touch $@

$(YOLOV8_VISION_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified YOLOv8 Vision"
	$(Q)cd $(YOLOV8_VISION_SRC) && ./build_app.sh
	$(Q)mkdir -p $(YOLOV8_VISION_INSTALL_DIR)
	$(Q)rsync -a --delete $(YOLOV8_VISION_BUILD_DIR)/ $(YOLOV8_VISION_INSTALL_DIR)/
	$(Q)touch $@

$(NETWORK_CAMERA_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] unified Network Camera"
	$(Q)$(MAKE) -C $(NETWORK_CAMERA_SRC)
	$(Q)rm -rf $(SDK_APPS_IMAGE_DIR)/rtsp_stream \
		$(SDK_APPS_IMAGE_DIR)/rtmp_stream
	$(Q)mkdir -p $(NETWORK_CAMERA_INSTALL_DIR)
	$(Q)rsync -a --delete $(NETWORK_CAMERA_BUILD_DIR)/ $(NETWORK_CAMERA_INSTALL_DIR)/
	$(Q)touch $@

$(YOLO_MODELS_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] multi-version YOLO Models"
	$(Q)cd $(YOLO_MODELS_SRC) && ./build_app.sh
	$(Q)mkdir -p $(YOLO_MODELS_INSTALL_DIR)
	$(Q)rsync -a --delete $(YOLO_MODELS_BUILD_DIR)/ $(YOLO_MODELS_INSTALL_DIR)/
	$(Q)touch $@

$(GALLERY_PLAYER_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] isolated Gallery video player"
	$(Q)$(MAKE) -C $(GALLERY_PLAYER_SRC)
	$(Q)mkdir -p $(GALLERY_PLAYER_INSTALL_DIR)
	$(Q)rsync -a --delete $(GALLERY_PLAYER_BUILD_DIR)/ $(GALLERY_PLAYER_INSTALL_DIR)/
	$(Q)touch $@

$(CV_LITE_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] CV Lite"
	$(Q)cd $(CV_LITE_SRC) && ./build_app.sh
	$(Q)mkdir -p $(CV_LITE_INSTALL_DIR)
	$(Q)rsync -a --delete $(CV_LITE_BUILD_DIR)/ $(CV_LITE_INSTALL_DIR)/
	$(Q)touch $@

$(PLATE_OCR_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] Plate OCR"
	$(Q)cd $(AI_SUITE_SRC) && ./build_app.sh licence_det_rec
	$(Q)mkdir -p $(PLATE_OCR_INSTALL_DIR)
	$(Q)rsync -a --delete $(PLATE_OCR_BUILD_DIR)/ $(PLATE_OCR_INSTALL_DIR)/
	$(Q)cp -f $(AI_SUITE_SRC)/resources/ai_poc/utils/SourceHanSansSC-Normal-Min.ttf $(PLATE_OCR_INSTALL_DIR)/
	$(Q)touch $@

$(CODE_SCANNER_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] Code Scanner"
	$(Q)cd $(CODE_SCANNER_SRC) && ./build_app.sh
	$(Q)mkdir -p $(CODE_SCANNER_INSTALL_DIR)
	$(Q)rsync -a --delete $(CODE_SCANNER_BUILD_DIR)/ $(CODE_SCANNER_INSTALL_DIR)/
	$(Q)touch $@

# Both AI Suite packagers recreate the shared apps/ai_demo/k230_bin
# directory.  Keep them ordered even when the application build is parallel.
$(SELF_LEARNING_STAMP): $(PLATE_OCR_STAMP)

$(SELF_LEARNING_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] AI Self Learning"
	$(Q)cd $(AI_SUITE_SRC) && ./build_app.sh self_learning
	$(Q)mkdir -p $(SELF_LEARNING_INSTALL_DIR)
	$(Q)rsync -a --delete $(SELF_LEARNING_BUILD_DIR)/ $(SELF_LEARNING_INSTALL_DIR)/
	$(Q)touch $@

$(UVC_CAMERA_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] USB Camera"
	$(Q)cd $(UVC_CAMERA_SRC) && ./build_app.sh
	$(Q)mkdir -p $(UVC_CAMERA_INSTALL_DIR)
	$(Q)rsync -a --delete $(UVC_CAMERA_BUILD_DIR)/ $(UVC_CAMERA_INSTALL_DIR)/
	$(Q)touch $@

$(CLOUDPLAT_STAMP): .FORCE | $(OBJ_DIRS)
	@echo "[BUILD] CanMV Cloud model deployment"
	$(Q)cd $(CLOUDPLAT_SRC) && ./build_app.sh
	$(Q)mkdir -p $(CLOUDPLAT_INSTALL_DIR)
	$(Q)rsync -a --delete $(CLOUDPLAT_BUILD_DIR)/ $(CLOUDPLAT_INSTALL_DIR)/
	$(Q)touch $@

$(AI_SUITE_STAMP): $(AI_SUITE_BUILD_INPUTS) | $(OBJ_DIRS)
	@echo "[BUILD] complete offline AI application suite"
	$(Q)cd $(AI_SUITE_SRC) && ./build_app.sh $(AI_SUITE_PROJECTS)
	$(Q)touch $@

ai-suite-install: $(AI_SUITE_STAMP) | $(OBJ_DIRS)
	@echo "[INSTALL] offline AI application suite"
	$(Q)mkdir -p $(AI_SUITE_INSTALL_DIR)
	$(Q)rsync -a --delete $(AI_SUITE_BUILD_OUT)/ $(AI_SUITE_INSTALL_DIR)/

$(BUILD)/%.o: %.c | $(OBJ_DIRS)
	@echo "[CC] $@"
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

# The icon artwork lives in generated .inc files rather than standalone C
# sources.  Keep an explicit prerequisite in addition to compiler-generated
# dependencies so top-level incremental firmware builds cannot reuse a stale
# material_app_icons.o after artwork is regenerated.
$(BUILD)/apps/material_app_icons.o: $(MATERIAL_ICON_ASSETS)

$(BUILD)/apps/screensaver_asset.o: $(SCREENSAVER_ASSETS)

# Keep the user-visible version embedded in the launcher synchronized with the
# release filename.  The generated dependency file alone is insufficient on a
# first incremental build when sdk_version.h is refreshed by another sub-make.
ifneq ($(wildcard $(SYSTEM_VERSION_FILE)),)
$(SDK_VERSION_HEADER): $(SYSTEM_VERSION_FILE)
	$(Q)$(SDK_TOOLS_DIR)/gen_verinfo_h.sh $@

$(BUILD)/apps/main.o: $(SDK_VERSION_HEADER)
$(BUILD)/system/ota_update.o: $(SDK_VERSION_HEADER)
endif

$(OBJ_DIRS):
	$(Q)mkdir -p $@

clean:
	@rm -f $(BIN) $(FACE_INSTALL_BIN) $(UI_FONT_INSTALL)
	@rm -rf $(FACE_SRC)/build $(FACE_SRC)/k230_bin
	@rm -rf $(FACE_STUDIO_SRC)/build $(FACE_STUDIO_SRC)/k230_bin
	@rm -rf $(FACE_STUDIO_INSTALL_DIR) $(FACE_STUDIO_STAMP)
	@rm -rf $(FACE_GEOMETRY_SRC)/build $(FACE_GEOMETRY_SRC)/k230_bin
	@rm -rf $(FACE_GEOMETRY_INSTALL_DIR) $(FACE_GEOMETRY_STAMP)
	@rm -rf $(HAND_STUDIO_SRC)/build $(HAND_STUDIO_SRC)/k230_bin
	@rm -rf $(HAND_STUDIO_INSTALL_DIR) $(HAND_STUDIO_STAMP)
	@rm -rf $(HUMAN_STUDIO_SRC)/build $(HUMAN_STUDIO_SRC)/k230_bin
	@rm -rf $(HUMAN_STUDIO_INSTALL_DIR) $(HUMAN_STUDIO_STAMP)
	@rm -rf $(SMART_DRIVING_SRC)/build $(SMART_DRIVING_SRC)/k230_bin
	@rm -rf $(SMART_DRIVING_INSTALL_DIR) $(SMART_DRIVING_STAMP)
	@rm -rf $(OCR_DETECTION_SRC)/build $(OCR_DETECTION_SRC)/k230_bin
	@rm -rf $(OCR_DETECTION_INSTALL_DIR) $(OCR_DETECTION_STAMP)
	@rm -rf $(YOLOV8_VISION_SRC)/build $(YOLOV8_VISION_SRC)/k230_bin
	@rm -rf $(YOLOV8_VISION_INSTALL_DIR) $(YOLOV8_VISION_STAMP)
	@$(MAKE) -C $(NETWORK_CAMERA_SRC) clean
	@rm -rf $(NETWORK_CAMERA_INSTALL_DIR) $(NETWORK_CAMERA_STAMP)
	@rm -rf $(YOLO_MODELS_SRC)/build $(YOLO_MODELS_SRC)/k230_bin
	@rm -rf $(YOLO_MODELS_INSTALL_DIR) $(YOLO_MODELS_STAMP)
	@$(MAKE) -C $(GALLERY_PLAYER_SRC) clean
	@rm -rf $(GALLERY_PLAYER_INSTALL_DIR) $(GALLERY_PLAYER_STAMP)
	@rm -rf $(CV_LITE_SRC)/build $(CV_LITE_SRC)/k230_bin
	@rm -rf $(CV_LITE_INSTALL_DIR) $(CV_LITE_STAMP)
	@rm -rf $(PLATE_OCR_INSTALL_DIR) $(PLATE_OCR_STAMP)
	@rm -rf $(CODE_SCANNER_SRC)/build $(CODE_SCANNER_SRC)/k230_bin
	@rm -rf $(CODE_SCANNER_INSTALL_DIR) $(CODE_SCANNER_STAMP)
	@rm -rf $(SELF_LEARNING_INSTALL_DIR) $(SELF_LEARNING_STAMP)
	@rm -rf $(UVC_CAMERA_SRC)/build $(UVC_CAMERA_SRC)/k230_bin
	@rm -rf $(UVC_CAMERA_INSTALL_DIR) $(UVC_CAMERA_STAMP)
	@rm -rf $(CLOUDPLAT_SRC)/build $(CLOUDPLAT_SRC)/k230_bin
	@rm -rf $(CLOUDPLAT_INSTALL_DIR) $(CLOUDPLAT_STAMP)
	@rm -rf $(AI_SUITE_SRC)/build $(AI_SUITE_SRC)/k230_bin
	@rm -rf $(AI_SUITE_INSTALL_DIR) $(AI_SUITE_STAMP)

distclean: clean
	@rm -rf $(BUILD)

-include $(DEPS)
