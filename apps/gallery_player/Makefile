include ../../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

BUILD := $(CURDIR)/build
BIN_DIR := $(CURDIR)/k230_bin
BIN := $(BIN_DIR)/gallery_player.elf
OBJ := $(BUILD)/main.o

include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

CFLAGS := -fopenmp -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os
CFLAGS += -std=gnu99 -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections $(LIB_CFLAGS)
CFLAGS += -I$(SDK_SRC_ROOT_DIR)/include -include generated/autoconf.h
LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static
LDFLAGS += -Wl,--gc-sections $(LIB_LDFLAGS)

.PHONY: all clean

all: $(BIN)
	@echo "[gallery-player] package ready: $(BIN_DIR)"

$(BIN): $(OBJ) | $(BIN_DIR)
	@echo "[LD] $@"
	$(Q)$(CXX) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(OBJ): main.c | $(BUILD)
	@echo "[CC] $@"
	$(Q)$(CC) $(CFLAGS) -MD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD) $(BIN_DIR):
	$(Q)mkdir -p $@

clean:
	$(Q)rm -rf $(BUILD) $(BIN_DIR)

-include $(OBJ:.o=.d)
