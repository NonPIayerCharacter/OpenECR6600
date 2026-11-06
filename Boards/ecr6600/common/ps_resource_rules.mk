# PSM/BLE 资源组规则定义。
# 客户只配置 Kconfig，构建系统根据这些规则自动命中内部资源组。

PS_RESOURCE_GROUPS := master noble nopsm nobp

# 当前资源组由这些 Kconfig 特征共同决定。
# 后续若扩展更多库，只需在此补充特征，并为每个资源组补齐规则值。
PS_RESOURCE_FEATURES := CONFIG_ECR_BLE CONFIG_PSM_SURPORT

# 资源组命中值，格式必须与 PS_RESOURCE_FEATURES 顺序一致。
PS_RESOURCE_RULE_master := y:y
PS_RESOURCE_RULE_noble := n:y
PS_RESOURCE_RULE_nopsm := y:n
PS_RESOURCE_RULE_nobp := n:n

# 资源同步目标。
# 后续若扩展更多库，只需补充 target 定义，不再扩展根 Makefile 分支。
PS_RESOURCE_TARGETS := libs ps_kconfig ps_ble_kconfig board_psm_kconfig

PS_RESOURCE_SRC_DIR_libs := $(TOPDIR)/libs
PS_RESOURCE_SRC_PATTERN_libs := libps*.a
PS_RESOURCE_DST_libs := $(TOPDIR)/libs/
PS_RESOURCE_CLEAN_libs := $(TOPDIR)/libs/libps*.a

PS_RESOURCE_SRC_DIR_ps_kconfig := $(TOPDIR)/PS
PS_RESOURCE_SRC_PATTERN_ps_kconfig := Kconfig
PS_RESOURCE_DST_ps_kconfig := $(TOPDIR)/PS/Kconfig

PS_RESOURCE_SRC_DIR_ps_ble_kconfig := $(TOPDIR)/PS/ble
PS_RESOURCE_SRC_PATTERN_ps_ble_kconfig := Kconfig
PS_RESOURCE_DST_ps_ble_kconfig := $(TOPDIR)/PS/ble/Kconfig

PS_RESOURCE_SRC_DIR_board_psm_kconfig := $(TOPDIR)/Boards/ecr6600/common/psm
PS_RESOURCE_SRC_PATTERN_board_psm_kconfig := Kconfig
PS_RESOURCE_DST_board_psm_kconfig := $(TOPDIR)/Boards/ecr6600/common/psm/Kconfig

