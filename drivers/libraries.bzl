load(":drivers/tty/serial/pkvm-geni/hyp/libraries.bzl", register_pkvm_geni_hyp_libs = "register_libraries")

def register_libraries(registry):
    register_pkvm_geni_hyp_libs(registry)
