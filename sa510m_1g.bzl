load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_le_32.bzl", "define_msm_le_32")
load(":target_variants.bzl", "le_32_variants")

target_name = "sa510m.1g"

def define_sa510m_1g():
    _sa510m_1g_in_tree_modules = [
        "crypto/authenc.ko",
        "crypto/authencesn.ko",
        "crypto/cbc.ko",
        "crypto/crypto_null.ko",
        "crypto/essiv.ko",
        "drivers/block/loop.ko",
        "drivers/char/hw_random/arm_smccc_trng.ko",
        "drivers/hwtracing/coresight/coresight.ko",
        "drivers/hwtracing/coresight/coresight-catu.ko",
        "drivers/hwtracing/coresight/coresight-cpu-debug.ko",
        "drivers/hwtracing/coresight/coresight-csr.ko",
        "drivers/hwtracing/coresight/coresight-cti.ko",
        "drivers/hwtracing/coresight/coresight-dummy.ko",
        "drivers/hwtracing/coresight/coresight-etb10.ko",
        "drivers/hwtracing/coresight/coresight-etm3x.ko",
        "drivers/hwtracing/coresight/coresight-funnel.ko",
        "drivers/hwtracing/coresight/coresight-remote-etm.ko",
        "drivers/hwtracing/coresight/coresight-replicator.ko",
        "drivers/hwtracing/coresight/coresight-stm.ko",
        "drivers/hwtracing/coresight/coresight-tgu.ko",
        "drivers/hwtracing/coresight/coresight-tmc.ko",
        "drivers/hwtracing/coresight/coresight-tmc-sec.ko",
        "drivers/hwtracing/coresight/coresight-tpda.ko",
        "drivers/hwtracing/coresight/coresight-tpdm.ko",
        "drivers/hwtracing/coresight/coresight-tpiu.ko",
        "drivers/md/dm-crypt.ko",
        "drivers/mtd/mtdblock.ko",
        "drivers/mtd/mtd_blkdevs.ko",
        "drivers/mtd/ubi/gluebi.ko",
        # keep sorted
    ]

    for variant in le_32_variants:
        mod_list = _sa510m_1g_in_tree_modules

        define_msm_le_32(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.sa510m.1g",
            in_tree_module_list = mod_list,
            target_variants = le_32_variants,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
