load(":target_variants.bzl", "lxc_variants")
load(":msm_kernel_lxc.bzl", "define_msm_lxc")
load(":image_opts.bzl", "boot_image_opts")

target_name = "autogvm"

def define_autogvm_lxc():
    _autogvm_lxc_in_tree_modules = [
        # keep sorted
        "drivers/rpmsg/qcom_glink_cma.ko",
        "drivers/soc/qcom/hgsl/qcom_hgsl.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/dwc3/dwc3-qcom-mp.ko",
        "drivers/usb/gadget/function/usb_f_cdev.ko",
        "drivers/usb/misc/lvstest.ko",
        "drivers/usb/misc/ehset.ko",
        "drivers/usb/gadget/function/f_fs_ipc_log.ko",
        "drivers/usb/phy/phy-msm-ssusb-qmp.ko",
        "drivers/usb/gadget/function/usb_f_qdss.ko",
        "drivers/usb/phy/phy-msm-snps-hs.ko",
        "drivers/soc/qcom/usb_bam.ko",
        "drivers/usb/gadget/function/usb_f_diag.ko",
        "drivers/usb/gadget/function/usb_f_ccid.ko",
        "drivers/usb/gadget/function/usb_f_gsi.ko",
        "drivers/usb/mon/usbmon.ko",
        "drivers/phy/qualcomm/phy-qcom-qmp-combo.ko",
        "drivers/phy/qualcomm/phy-qcom-qmp-pcie.ko",
        "drivers/phy/qualcomm/phy-qcom-qmp-pcie-msm8996.ko",
        "drivers/phy/qualcomm/phy-qcom-qmp-ufs.ko",
        "drivers/phy/qualcomm/phy-qcom-qmp-usb.ko",
        "drivers/phy/qualcomm/phy-qcom-snps-femto-v2.ko",

    ]

    for variant in lxc_variants:
        mod_list = _autogvm_lxc_in_tree_modules

        define_msm_lxc(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.auto",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
