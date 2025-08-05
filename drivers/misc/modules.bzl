load(":drivers/misc/lkdtm/modules.bzl", register_lkdtm = "register_modules")

def register_modules(registry):
    register_lkdtm(registry)

    registry.register(
        name = "drivers/misc/qseecom_proxy",
        out = "qseecom_proxy.ko",
        config = "CONFIG_QSEECOM_PROXY",
        srcs = [
            # do not sort
            "drivers/misc/qseecom_proxy.c",
        ],
    )

    registry.register(
        name = "drivers/misc/bootmarker_proxy",
        out = "bootmarker_proxy.ko",
        config = "CONFIG_BOOTMARKER_PROXY",
        srcs = [
            # do not sort
            "drivers/misc/bootmarker_proxy.c",
        ],
    )
