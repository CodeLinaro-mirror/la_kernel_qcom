# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(src)

obj-$(CONFIG_ARM_SMMU_V3) += arm_smmu_v3.o
arm_smmu_v3-objs += arm-smmu-v3.o arm-smmu-v3-common-lib.o arm-smmu-v3-common.o
arm_smmu_v3-$(CONFIG_ARM_SMMU_V3_QCOM_VIRTIO) += arm-smmu-v3-qcom-virtio.o

