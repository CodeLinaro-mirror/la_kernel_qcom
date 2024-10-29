load(":target_variants.bzl", "le_32_variants")
load(":msm_kernel_le_32.bzl", "define_msm_le_32")
load(":image_opts.bzl", "boot_image_opts")

target_name = "sa510m"

def define_sa510m():
    _sa510m_le_in_tree_modules = [
        # keep sorted
        "drivers/char/hw_random/rng-core.ko",
    ]

    for variant in le_32_variants:
        mod_list = _sa510m_le_in_tree_modules

        define_msm_le_32(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.sa510m",
            in_tree_module_list = mod_list,
            target_variants = le_32_variants,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
