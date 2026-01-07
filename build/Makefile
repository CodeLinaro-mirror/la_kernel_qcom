# SPDX-License-Identifier: GPL-2.0

M ?= $(shell pwd)

DTSTREE ?= $(shell realpath -s \
	$(dir $(KCONFIG_EXT_PREFIX)/arch/arm64/boot/dts/vendor/Makefile) \
	--relative-to $(KERNEL_SRC))

soc = $(KCONFIG_EXT_PREFIX)
srctree = $(KERNEL_SRC)
REL_SOC := $(shell realpath -s $(dir $(KCONFIG_EXT_PREFIX)) --relative-to $(M))

LINUXINCLUDE := \
  -I$(soc)/. \
  -I$(soc)/arch/arm64/include \
  -I$(soc)/include \
  -I$(soc)/include/uapi \
  -I$(soc)/include/linux \
  -I$(srctree)/ \
  $(LINUXINCLUDE)

# -------------------------------------------------
# Common upstream source staging (macros only)
# -------------------------------------------------
include $(M)/common_sources.mk

# -------------------------------------------------
# Subdirectories
# -------------------------------------------------
obj-y += $(REL_SOC)/arch/arm64/gunyah/
obj-y += $(REL_SOC)/drivers/
obj-y += $(REL_SOC)/kernel/
obj-y += $(REL_SOC)/lib/
obj-y += $(REL_SOC)/mm/
obj-y += $(REL_SOC)/net/
obj-y += $(REL_SOC)/sound/

# -------------------------------------------------
# Targets
# -------------------------------------------------
all:
	$(STAGE_COMMON_SOURCES)
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)
	$(MAKE) -C $(KERNEL_SRC) dtbs V=1 dtstree=$(DTSTREE)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

