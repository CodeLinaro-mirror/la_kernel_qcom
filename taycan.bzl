load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_le.bzl", "define_msm_le")
load(":target_variants.bzl", "le_variants")

target_name = "taycan"

def define_taycan():
    _taycan_in_tree_modules = [
        # keep sorted
    ]

    _taycan_debug_in_tree_modules = _taycan_in_tree_modules + [
        # keep sorted
    ]

    for variant in le_variants:
        if variant == "debug-defconfig":
            mod_list = _taycan_debug_in_tree_modules
        else:
            mod_list = _taycan_in_tree_modules

        define_msm_le(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.taycan",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x0098c000",
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
