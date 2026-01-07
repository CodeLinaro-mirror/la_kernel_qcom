# SPDX-License-Identifier: GPL-2.0

# ================================================================
#  common_sources.mk
#
#  Purpose:
#    This Makefile fragment was created to handle the same staging
#    and patching logic defined in common_sources.bzl, but in a
#    Make/Kbuild‑friendly way. It ensures common upstream common kernel
#    sources are staged correctly for SoC builds.
#
#  What it does:
#    - Copies selected upstream common kernel files into the SoC repo
#    - Copies certain SoC headers back into the common kernel tree to
#      override duplicates
#    - Applies patches to upstream sources to produce SoC‑specific
#      variants
#
#  Key sections:
#    - COPY_TO_SOC:    Files copied from common kernel → SoC repo
#    - COPY_TO_COMMON: Headers copied from SoC repo → common kernel
#    - COMMON_PATCH_FILES: Patched files in the format:
#          dst|src|diff
#        where:
#          dst  = output file path in SoC repo
#          src  = original file path in common kernel source
#          diff = patch file path in SoC repo
#
#  How to modify:
#    - To add a new file copy:
#        * Append to COPY_TO_SOC (common kernel → SoC)
#        * Append to COPY_TO_COMMON (SoC → common kernel)
#    - To add a new patch:
#        * Append a new "dst|src|diff" entry to COMMON_PATCH_FILES
#    - Keep paths relative to common kernel/SoC root (no leading '/')
#    - Ensure patch files apply cleanly; failed patches will stop the build
#
#  Notes:
#    - This file is intended to be Kbuild‑safe and maintainable.
#    - Modify the lists above rather than editing macros directly.
#
# ================================================================

PATCH   ?= patch
CP      ?= cp
MKDIR_P ?= mkdir -p

EXT_ROOT := $(patsubst %/,%,$(KCONFIG_EXT_PREFIX))
SRC_ROOT := $(KERNEL_SRC)

# -------------------------------------------------
# Files copied from common kernel → SoC repo
# -------------------------------------------------
COPY_TO_SOC := \
drivers/leds/trigger/ledtrig-pattern.c \
drivers/char/virtio_console.c \
drivers/dma/qcom/gpi.c \
drivers/dma/dmaengine.h \
drivers/dma/virt-dma.h \
drivers/gpio/gpio-virtio.c \
drivers/i2c/busses/i2c-qcom-geni.c \
drivers/i2c/busses/i2c-virtio.c \
drivers/spi/spi-geni-qcom.c \
drivers/virtio/virtio_input.c \
net/core/failover.c \
lib/crc-itu-t.c \
drivers/net/net_failover.c \
drivers/net/virtio_net.c \
drivers/net/pcs/pcs-xpcs.c \
drivers/net/pcs/pcs-xpcs.h \
drivers/net/pcs/pcs-xpcs-nxp.c \
drivers/net/pcs/pcs-xpcs-plat.c \
drivers/net/pcs/pcs-xpcs-wx.c \
drivers/net/phy/aquantia/aquantia_leds.c \
drivers/net/phy/aquantia/aquantia_firmware.c \
drivers/phy/qualcomm/phy-qcom-sgmii-eth.c \
drivers/phy/qualcomm/phy-qcom-qmp-pcs-sgmii.h \
drivers/phy/qualcomm/phy-qcom-qmp-qserdes-com-v5.h \
drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v5.h \
net/wireless/ap.c \
net/wireless/certs/sforshee.hex \
net/wireless/certs/wens.hex \
net/wireless/chan.c \
net/wireless/core.c \
net/wireless/core.h \
net/wireless/debugfs.c \
net/wireless/debugfs.h \
net/wireless/ethtool.c \
net/wireless/ibss.c \
net/wireless/Kconfig \
net/wireless/lib80211.c \
net/wireless/lib80211_crypt_ccmp.c \
net/wireless/lib80211_crypt_tkip.c \
net/wireless/lib80211_crypt_wep.c \
net/wireless/Makefile \
net/wireless/mesh.c \
net/wireless/mlme.c \
net/wireless/nl80211.c \
net/wireless/nl80211.h \
net/wireless/ocb.c \
net/wireless/of.c \
net/wireless/pmsr.c \
net/wireless/radiotap.c \
net/wireless/rdev-ops.h \
net/wireless/reg.c \
net/wireless/reg.h \
net/wireless/scan.c \
net/wireless/sme.c \
net/wireless/sysfs.c \
net/wireless/sysfs.h \
net/wireless/tests/chan.c \
net/wireless/tests/fragmentation.c \
net/wireless/tests/Makefile \
net/wireless/tests/module.c \
net/wireless/tests/scan.c \
net/wireless/tests/util.c \
net/wireless/tests/util.h \
net/wireless/trace.c \
net/wireless/trace.h \
net/wireless/util.c \
net/wireless/wext-compat.c \
net/wireless/wext-compat.h \
net/wireless/wext-core.c \
net/wireless/wext-priv.c \
net/wireless/wext-proc.c \
net/wireless/wext-sme.c \
net/wireless/wext-spy.c \
net/sched/sch_mqprio.c \
net/sched/sch_mqprio_lib.c \
net/sched/sch_mqprio_lib.h \
net/sched/sch_cbs.c \
net/sched/sch_etf.c \
net/sched/cls_flower.c \
drivers/mfd/qcom-pm8008.c \
drivers/regulator/qcom-pm8008-regulator.c \
net/mac80211/aead_api.c \
net/mac80211/aead_api.h \
net/mac80211/aes_ccm.h \
net/mac80211/aes_cmac.c \
net/mac80211/aes_cmac.h \
net/mac80211/aes_gcm.h \
net/mac80211/aes_gmac.c \
net/mac80211/aes_gmac.h \
net/mac80211/agg-rx.c \
net/mac80211/agg-tx.c \
net/mac80211/airtime.c \
net/mac80211/cfg.c \
net/mac80211/chan.c \
net/mac80211/debug.h \
net/mac80211/debugfs.c \
net/mac80211/debugfs.h \
net/mac80211/debugfs_key.c \
net/mac80211/debugfs_key.h \
net/mac80211/debugfs_netdev.c \
net/mac80211/debugfs_netdev.h \
net/mac80211/debugfs_sta.c \
net/mac80211/debugfs_sta.h \
net/mac80211/driver-ops.c \
net/mac80211/driver-ops.h \
net/mac80211/drop.h \
net/mac80211/eht.c \
net/mac80211/ethtool.c \
net/mac80211/fils_aead.c \
net/mac80211/fils_aead.h \
net/mac80211/he.c \
net/mac80211/ht.c \
net/mac80211/ibss.c \
net/mac80211/ieee80211_i.h \
net/mac80211/iface.c \
net/mac80211/key.c \
net/mac80211/key.h \
net/mac80211/led.c \
net/mac80211/led.h \
net/mac80211/link.c \
net/mac80211/main.c \
net/mac80211/mesh.c \
net/mac80211/mesh.h \
net/mac80211/mesh_hwmp.c \
net/mac80211/mesh_pathtbl.c \
net/mac80211/mesh_plink.c \
net/mac80211/mesh_ps.c \
net/mac80211/mesh_sync.c \
net/mac80211/michael.c \
net/mac80211/michael.h \
net/mac80211/mlme.c \
net/mac80211/ocb.c \
net/mac80211/offchannel.c \
net/mac80211/parse.c \
net/mac80211/pm.c \
net/mac80211/rate.c \
net/mac80211/rate.h \
net/mac80211/rc80211_minstrel_ht.c \
net/mac80211/rc80211_minstrel_ht.h \
net/mac80211/rc80211_minstrel_ht_debugfs.c \
net/mac80211/rx.c \
net/mac80211/s1g.c \
net/mac80211/scan.c \
net/mac80211/spectmgmt.c \
net/mac80211/sta_info.c \
net/mac80211/sta_info.h \
net/mac80211/status.c \
net/mac80211/tdls.c \
net/mac80211/tkip.c \
net/mac80211/tkip.h \
net/mac80211/trace.c \
net/mac80211/trace.h \
net/mac80211/trace_msg.h \
net/mac80211/tx.c \
net/mac80211/util.c \
net/mac80211/vht.c \
net/mac80211/wbrf.c \
net/mac80211/wep.c \
net/mac80211/wep.h \
net/mac80211/wme.c \
net/mac80211/wme.h \
net/mac80211/wpa.c \
net/mac80211/wpa.h \
drivers/soc/qcom/qcom-pbs.c \

# -------------------------------------------------
# Headers copied from SoC repo → common kernel
#
# Workaround explanation:
#   In the SoC repo, file drivers/mmc/host/sdhci-msm.c includes:
#       #include "drivers/mmc/host/sdhci-cqhci.h" from common kernel
#   That header (sdhci-cqhci.h) in turn includes "cqhci.h".
#
#   Because the "common" repo also provides its own version of
#   drivers/mmc/host/cqhci.h, the common kernel build system was resolving
#   the include from common instead of using the SoC repo’s version.
#   This caused mismatches during compilation.
#
#   To enforce correct behavior, we explicitly copy the SoC repo’s
#   cqhci.h into the common kernel tree so that the SoC version takes
#   precedence over the common one.
#
# Future expansion:
#   If similar conflicts arise (where a header exists in both common
#   and SoC repos but the SoC version must override), add those
#   headers to COPY_TO_COMMON. This ensures the SoC‑specific header
#   is staged into the common kernel tree before build, avoiding accidental
#   use of the common version.
# -------------------------------------------------
COPY_TO_COMMON := \
drivers/mmc/host/cqhci.h

# -------------------------------------------------
# Patched files in the format: dst|src|diff
#	where:
#          dst  = output file path in SoC repo
#          src  = original file path in common kernel source
#          diff = patch file path in SoC repo
# -------------------------------------------------
COMMON_PATCH_FILES := \
drivers/regulator/qti_fixed_regulator.c|drivers/regulator/fixed.c|drivers/regulator/fixed.diff \
drivers/dma/qcom/gpi_fixed.c|drivers/dma/qcom/gpi.c|drivers/dma/qcom/gpi_fix.diff \
drivers/net/virtio_net.c|drivers/net/virtio_net.c|drivers/net/virtio_net_fix.diff

# -------------------------------------------------
# Helpers
# -------------------------------------------------
define COPY_FROM_COMMON
	$(MKDIR_P) $(EXT_ROOT)/$(dir $(1)); \
	$(CP) $(SRC_ROOT)/$(1) $(EXT_ROOT)/$(1);
endef

define COPY_FROM_SOC
	$(MKDIR_P) $(SRC_ROOT)/$(dir $(1)); \
	$(CP) $(EXT_ROOT)/$(1) $(SRC_ROOT)/$(1);
endef

define PATCHED_DRIVERS
	rec="$(1)"; \
	dst="$${rec%%|*}"; \
	tmp="$${rec#*|}"; \
	src="$${tmp%%|*}"; \
	diff="$${tmp#*|}"; \
	$(MKDIR_P) $(EXT_ROOT)/$$(dirname $$dst); \
	$(PATCH) --follow-symlinks -p0 --batch \
		-o $(EXT_ROOT)/$$dst \
		-i $(EXT_ROOT)/$$diff \
		$(SRC_ROOT)/$$src;
endef

# -------------------------------------------------
# Public macro : To be called from Makefile
# -------------------------------------------------
define STAGE_COMMON_SOURCES
        $(foreach f,$(COPY_TO_SOC),$(call COPY_FROM_COMMON,$(f)))
        $(foreach f,$(COPY_TO_COMMON),$(call COPY_FROM_SOC,$(f)))
        $(foreach p,$(COMMON_PATCH_FILES),$(call PATCHED_DRIVERS,$(p)))
endef

