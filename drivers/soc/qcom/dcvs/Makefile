# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(src)

obj-$(CONFIG_QCOM_BWMON) += bwmon.o

obj-$(CONFIG_QCOM_DCVS_FP) += dcvs_fp.o

obj-$(CONFIG_QCOM_MEMLAT) += memlat.o

obj-$(CONFIG_QCOM_DCVS) += qcom-dcvs.o
qcom-dcvs-objs += dcvs.o dcvs_epss.o dcvs_icc.o trace-dcvs.o

obj-$(CONFIG_QCOM_PMU_LIB) += qcom-pmu-lib.o
qcom-pmu-lib-objs += pmu_lib.o

obj-$(CONFIG_QTI_QCOM_SCMI_CLIENT) += qcom_scmi_client.o

