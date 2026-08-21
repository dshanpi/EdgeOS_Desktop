include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

BUILD := $(CURDIR)/build
BIN_DIR := $(CURDIR)/k230_bin
BIN := $(BIN_DIR)/rtsp_stream.elf
TOUCH_SRC := $(CURDIR)/../face_studio/src/drv_touch.c
OBJS := $(BUILD)/main.o $(BUILD)/dual_stream.o $(BUILD)/drv_touch.o
DEPS := $(OBJS:.o=.d)

include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/librtsmart_hal.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/liblvgl.mk

CXX_FLAGS := -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CXX_FLAGS += -std=c++20 -Wall -Wextra -ffunction-sections -fdata-sections
CXX_FLAGS += $(LIB_CFLAGS)
CXX_FLAGS += -I$(SDK_SRC_ROOT_DIR)/include -include generated/autoconf.h
C_FLAGS := $(filter-out -std=c++20,$(CXX_FLAGS)) -std=gnu99
LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static
LDFLAGS += -Wl,--gc-sections $(LIB_LDFLAGS)

.PHONY: all clean

all: $(BIN)
	@echo "[rtsp-stream] package ready: $(BIN_DIR)"

$(BIN): $(OBJS) | $(BIN_DIR)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CXX_FLAGS) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/main.o: main.cpp | $(BUILD)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/dual_stream.o: dual_stream.cpp dual_stream.h | $(BUILD)
	@echo "[CXX] $@"
	$(Q)$(CXX) $(CXX_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD)/drv_touch.o: $(TOUCH_SRC) | $(BUILD)
	@echo "[CC] $@"
	$(Q)$(CC) $(C_FLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD) $(BIN_DIR):
	$(Q)mkdir -p $@

clean:
	$(Q)rm -rf $(BUILD) $(BIN_DIR)

-include $(DEPS)
