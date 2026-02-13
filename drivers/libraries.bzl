load(":drivers/firmware/qcom/pkvm-smc-filter/hyp/libraries.bzl", register_pkvm_smc_filter_libs = "register_libraries")
load(":drivers/tty/serial/pkvm-geni/hyp/libraries.bzl", register_pkvm_geni_hyp_libs = "register_libraries")

def register_libraries(registry):
    register_pkvm_geni_hyp_libs(registry)
    register_pkvm_smc_filter_libs(registry)
