load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_le.bzl", "define_msm_le")
load(":target_variants.bzl", "le_variants")

target_name = "sdxkova.cpe.min"

def define_sdxkova_cpe_min():
    _sdxkova_cpe_min_in_tree_modules = [
        # keep sorted
    ]

    for variant in le_variants:
        mod_list = _sdxkova_cpe_min_in_tree_modules

        define_msm_le(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.sdxkova.cpe.min",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
