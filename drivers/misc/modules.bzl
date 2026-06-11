load(":drivers/misc/isl97900_led/modules.bzl", register_isl97900_led = "register_modules")
load(":drivers/misc/lkdtm/modules.bzl", register_lkdtm = "register_modules")

def register_modules(registry):
    register_lkdtm(registry)
    register_isl97900_led(registry)

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

    registry.register(
        name = "drivers/misc/frpc-adsprpc",
        out = "frpc-adsprpc.ko",
        config = "CONFIG_QTI_FASTRPC",
        srcs = [
            # do not sort
            "drivers/misc/fastrpc.c",
        ],
        deps = [
            # do not sort
            "drivers/firmware/qcom/qcom-scm",
        ],
    )
