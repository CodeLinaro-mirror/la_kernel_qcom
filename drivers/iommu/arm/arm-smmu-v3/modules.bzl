def register_modules(registry):
    registry.register(
        name = "drivers/iommu/arm/arm-smmu-v3/arm_smmu_v3",
        out = "arm_smmu_v3.ko",
        config = "CONFIG_ARM_SMMU_V3",
        srcs = [
            # do not sort
            "drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c",
            "drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3-common.c",
            "drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.h",
        ],
        conditional_srcs = {
            "CONFIG_ARM_SMMU_V3_QCOM_VIRTIO": {
                True: [
                    # do not sort
                    "drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3-qcom-virtio.c",
                ],
            },
        },
        deps = [
            # do not sort
            "drivers/iommu/virtio-iommu",
            "drivers/firmware/qcom/qcom-scm",
            "drivers/virt/gunyah/gh_rm_drv",
            "drivers/virt/gunyah/gh_msgq",
            "drivers/virt/gunyah/gh_dbl",
            "arch/arm64/gunyah/gh_arm_drv",
        ],
    )
