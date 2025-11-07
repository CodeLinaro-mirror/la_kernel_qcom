def register_modules(registry):
    registry.register(
        name = "drivers/eom/eom_driver",
        out = "eom_driver.ko",
        config = "CONFIG_EOM_MSM",
        srcs = [
            # do not sort
            "drivers/eom/phy_core.c",
            "drivers/eom/buffer_manager.h",
            "drivers/eom/pcie_eom_reg.h",
            "drivers/eom/usb_eom_reg.h",
            "drivers/eom/eom_driver.c",
            "drivers/eom/buffer_manager.c",
        ],
        hdrs = [
            "include/linux/phy_core.h",
            "include/uapi/linux/eom_ioctl.h",
        ],
        deps = [
            # do not sort
        ],
    )
