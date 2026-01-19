load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":target_variants.bzl", "la_variants")

target_name = "gen3gvmcoqos"

def define_gen3gvmcoqos():
    _gen3gvmcoqos_in_tree_modules = [
        # keep sorted
        "drivers/block/virtio_blk.ko",
        "drivers/char/hw_random/virtio-rng.ko",
        "drivers/char/virtio_console.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/dma-buf/heaps/system_heap.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/gpu/drm/virtio/virtio-gpu.ko",
        "drivers/iio/buffer/kfifo_buf.ko",
        "drivers/iio/common/scmi_sensors/scmi_iio.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/net/net_failover.ko",
        "drivers/net/virtio_net.ko",
        "drivers/net/wireless/virt_wifi.ko",
        "drivers/nvdimm/nd_virtio.ko",
        "drivers/nvdimm/virtio_pmem.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/secure_buffer.ko",
        "drivers/usb/gadget/udc/dummy_hcd.ko",
        "drivers/usb/usbip/usbip-core.ko",
        "drivers/usb/usbip/vhci-hcd.ko",
        "drivers/virtio/virtio_balloon.ko",
        "drivers/virtio/virtio_dma_buf.ko",
        "drivers/virtio/virtio_input.ko",
        "drivers/virtio/virtio_mmio.ko",
        "drivers/virtio/virtio_pci.ko",
        "drivers/virtio/virtio_pci_legacy_dev.ko",
        "drivers/virtio/virtio_pci_modern_dev.ko",
        "net/core/failover.ko",
        "net/mac80211/mac80211.ko",
        "net/vmw_vsock/vmw_vsock_virtio_transport.ko",
        "net/wireless/cfg80211.ko",
        "sound/virtio/virtio_snd.ko",
    ]

    _gen3gvmcoqos_consolidate_in_tree_modules = _gen3gvmcoqos_in_tree_modules + [
        # keep sorted
        "drivers/misc/lkdtm/lkdtm.ko",
        "drivers/usb/misc/lvstest.ko",
        "kernel/locking/locktorture.ko",
        "kernel/rcu/rcutorture.ko",
        "kernel/torture.ko",
        "lib/atomic64_test.ko",
        "lib/test_user_copy.ko",
    ]

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _gen3gvmcoqos_consolidate_in_tree_modules
        else:
            mod_list = _gen3gvmcoqos_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "uart8250,mmio32,0x7f000000,115200n8",
                kernel_vendor_cmdline_extras = [
                    # do not sort
                    "console=ttyS0,115200n8",
                    "8250.nr_uarts=4",
                    "bootconfig",
                    "androidboot.first_stage_console=1",
                    "nokaslr",
                    "hibernate=nocompress",
                ],
            ),
        )
