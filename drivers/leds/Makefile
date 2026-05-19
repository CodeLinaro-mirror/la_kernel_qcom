# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(src)

# Subdirectories
obj-y += flash/
obj-y += rgb/

obj-$(CONFIG_LEDS_QTI_FLASH) += leds-qti-flash.o

obj-$(CONFIG_LEDS_TRIGGER_PATTERN) += ledtrig-pattern.o
ledtrig-pattern-objs += trigger/ledtrig-pattern.o

obj-$(CONFIG_LEDS_QPNP_VIBRATOR_LDO) += leds-qpnp-vibrator-ldo.o

obj-$(CONFIG_LEDS_AW2016) += leds-aw2016.o

