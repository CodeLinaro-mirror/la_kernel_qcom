# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(src)
ccflags-y += -I$(src)/xts

obj-$(CONFIG_QCOM_SI_CORE) += si_core_module.o
si_core_module-objs += qcom_scm_invoke.o si_core.o si_core_async.o si_core_helpers.o xts/mem-object.o
si_core_module-$(CONFIG_QCOM_SI_CORE_ADCI) += si_core_adci.o
si_core_module-$(CONFIG_QCOM_SI_CORE_WQ) += si_core_wq.o
si_core_module-$(CONFIG_QCOM_SI_CORE_MEM_FFA) += si_core_ffa.o
si_core_module-$(CONFIG_QCOM_SI_CORE_DOORBELL) += si_core_irq.o

