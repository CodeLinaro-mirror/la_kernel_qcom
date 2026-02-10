def register_modules(registry):
    registry.register(
        name = "drivers/extcon/extcon-qcom-spmi-misc",
        out = "extcon-qcom-spmi-misc.ko",
        config = "CONFIG_EXTCON_QCOM_SPMI_MISC",
        srcs = [
            "drivers/extcon/extcon-qcom-spmi-misc.c",
        ],
    )
