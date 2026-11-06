.DEFAULT_GOAL := all
.PHONY: $(MAKECMDGOALS) all EXAMPLES_FILE_CREAT EXAMPLES_FILE_CLEAN

export BOARD_DIR := $(realpath $(shell echo $(CURDIR) | sed -e 's/ /\\ /g'))
export TOPDIR := $(realpath ${BOARD_DIR}/../..)
export TARGET_CHIP_DIR := $(TOPDIR)/Boards/ecr6600
export BOARD_COMMON_DIR := $(TARGET_CHIP_DIR)/common
export BOARD_PSM_DIR := $(BOARD_COMMON_DIR)/psm
export BOARD_CPP_DIR := $(BOARD_COMMON_DIR)/cpp
export BOARD_TOOL_DIR := $(TARGET_CHIP_DIR)/standalone
export APP_NAME := $(notdir $(BOARD_DIR))
# Marker consumed by top-level distclean for example-local copied files.
export EXAMPLES_FILE_DIR := $(realpath ${BOARD_DIR})
FORWARD_GOALS := $(filter-out EXAMPLES_FILE_CLEAN,$(MAKECMDGOALS))
EXAMPLES_CONFIG_STAMP := $(BOARD_DIR)/generated/.examples_config.stamp

EXAMPLES_FILE_CREAT:
	@if [ ! -d "$(BOARD_DIR)/ref_bin" ] || \
	    [ ! -f "$(BOARD_DIR)/defconfig" ] || \
	    [ ! -f "$(BOARD_DIR)/Kconfig" ] || \
	    [ ! -f "$(BOARD_DIR)/main.c" ] || \
	    [ ! -f "$(EXAMPLES_CONFIG_STAMP)" ] || \
	    [ "$(BOARD_DIR)/examples_config" -nt "$(BOARD_DIR)/defconfig" ] || \
	    [ "$(BOARD_DIR)/examples_config" -nt "$(EXAMPLES_CONFIG_STAMP)" ] || \
	    { [ -f "$(BOARD_DIR)/examples_kconfig" ] && [ "$(BOARD_DIR)/examples_kconfig" -nt "$(BOARD_DIR)/Kconfig" ]; } || \
	    { [ -f "$(BOARD_DIR)/examples_kconfig" ] && [ "$(BOARD_DIR)/examples_kconfig" -nt "$(EXAMPLES_CONFIG_STAMP)" ]; } || \
	    find "$(BOARD_DIR)/ref_bin" -maxdepth 1 \( -name 'ecw*' -o -name 'ECW*' \) -print -quit | grep -q .; then \
		rm -rf "$(BOARD_DIR)/ref_bin" "$(BOARD_DIR)/defconfig" "$(BOARD_DIR)/Kconfig" "$(BOARD_DIR)/main.c" "$(BOARD_DIR)/pincfg.h" "$(BOARD_DIR)/generated"; \
		cp "$(TARGET_CHIP_DIR)/standalone/ref_bin" "$(BOARD_DIR)/" -r; \
		cp "$(TARGET_CHIP_DIR)/standalone/defconfig" "$(BOARD_DIR)/" -r; \
		cp "$(TARGET_CHIP_DIR)/standalone/Kconfig" "$(BOARD_DIR)/" -r; \
		cp "$(TARGET_CHIP_DIR)/standalone/main.c" "$(BOARD_DIR)/" -r; \
		if [ -f "$(BOARD_DIR)/examples_kconfig" ]; then cat "$(BOARD_DIR)/examples_kconfig" >> "$(BOARD_DIR)/Kconfig"; fi; \
		cat "$(BOARD_DIR)/examples_config" >> "$(BOARD_DIR)/defconfig"; \
		mkdir -p "$(BOARD_DIR)/generated"; \
		touch "$(EXAMPLES_CONFIG_STAMP)"; \
	fi

EXAMPLES_FILE_CLEAN:
	@rm -rf "$(BOARD_DIR)/ref_bin" "$(BOARD_DIR)/defconfig" "$(BOARD_DIR)/Kconfig" "$(BOARD_DIR)/main.c" "$(BOARD_DIR)/pincfg.h" "$(BOARD_DIR)/generated"

ifeq ($(strip $(MAKECMDGOALS)),)
all: EXAMPLES_FILE_CREAT
	$(MAKE) -C $(TOPDIR) BOARD_DIR=$(BOARD_DIR) BOARD_TOOL_DIR=$(BOARD_TOOL_DIR) APP_NAME=$(APP_NAME) BOARD_COMMON_DIR=$(BOARD_COMMON_DIR) BOARD_PSM_DIR=$(BOARD_PSM_DIR) BOARD_CPP_DIR=$(BOARD_CPP_DIR) all
endif

ifneq ($(strip $(FORWARD_GOALS)),)
$(FORWARD_GOALS): EXAMPLES_FILE_CREAT
	$(MAKE) -C $(TOPDIR) BOARD_DIR=$(BOARD_DIR) BOARD_TOOL_DIR=$(BOARD_TOOL_DIR) APP_NAME=$(APP_NAME) BOARD_COMMON_DIR=$(BOARD_COMMON_DIR) BOARD_PSM_DIR=$(BOARD_PSM_DIR) BOARD_CPP_DIR=$(BOARD_CPP_DIR) $@
endif
