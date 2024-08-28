load(":target_variants.bzl", "le_variants")
load(":msm_kernel_le.bzl", "define_msm_le")
load(":image_opts.bzl", "boot_image_opts")

target_name = "sdxkova.512"

def define_sdxkova_512():
    _sdxkova_512_in_tree_modules = [
        # keep sorted
    ]

    for variant in le_variants:
        mod_list = _sdxkova_512_in_tree_modules

        define_msm_le(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.sdxkova.512",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
