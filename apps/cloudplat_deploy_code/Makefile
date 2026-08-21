include ../../mkenv.mk

export RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH := \
    $(SDK_RTSMART_SRC_DIR)/examples/elf/ai/cloudplat_deploy_code

# 直接列出所有子目录
dir-y := \
    classification \
    detection \
    segmentation \
    ocr_detection \
    ocr_recognition \
    ocr \
    metric_learning \
    multilabel_cls

dirs := $(sort $(dir-y))

.PHONY: all clean

# Default build arguments
BUILD_DIR ?= build
BUILD_OUT ?= k230_bin

# Default target
all:
	@rm -rf $(RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH)/*

ifeq ($(CONFIG_RTSMART_AI_EXAMPLES_ENABLE_CLOUDPLAT_DEPLOY_CODE),y)
	@rm -rf $(BUILD_OUT)
	@mkdir -p $(RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH)

	./build_app.sh || exit $?
	if [ -d $(BUILD_OUT) ]; then \
		echo "Copying $(BUILD_OUT) to $(RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH)"; \
		cp -r $(BUILD_OUT)/* $(RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH); \
	else \
		echo "No build output found in $(BUILD_OUT), skipping copy."; \
	fi
endif
	@echo "Make Cloud Training plat deploy samples done."

clean:
	@rm -rf $(RTT_AI_EXAMPLES_CLOUDPLAT_DEPLOY_CODE_ELF_INSTALL_PATH)/*
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_OUT)
	@echo "Clean Cloud Training plat deploy samples done."