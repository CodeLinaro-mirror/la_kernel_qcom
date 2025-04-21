load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":msm_common.bzl", "get_out_dir")

def define_openssl_dist(target, msm_target, variant):
    """Create distribution targets for openssl

    Args:
      target: name of main Bazel target (e.g. `sdxkova_debug-defconfig`)
    """
    openssl_bin_target = "@openssl//:bin"
    openssl_inc_target = "@openssl//:include"
    openssl_lib_target = "@openssl//:lib64"

    openssl_tar_cmd = """
        mkdir -p openssl bin include lib64

        cp $(locations {bin}) bin/
        cp $(locations {include}) include/
        cp $(locations {lib64}) lib64/

        rm -f lib64/libcrypto.so && ln -s libcrypto.so.3 lib64/libcrypto.so
        rm -f lib64/libssl.so && ln -s libssl.so.3 lib64/libssl.so

        chmod 755 bin/* lib64/*
        chmod 644 include/*
        mv bin include lib64 openssl
        tar -czf "$@" openssl
    """.format(
        bin = openssl_bin_target,
        include = openssl_inc_target,
        lib64 = openssl_lib_target,
    )

    native.genrule(
        name = "{}_openssl_tarball".format(target),
        srcs = [
            openssl_bin_target,
            openssl_inc_target,
            openssl_lib_target,
        ],
        outs = ["{}_openssl.tar.gz".format(target)],
        cmd = openssl_tar_cmd,
    )

    native.alias(
        name = "{}_openssl".format(target),
        actual = ":{}_openssl_tarball".format(target),
    )

    copy_to_dist_dir(
        name = "{}_openssl_dist".format(target),
        archives = [":{}_openssl_tarball".format(target)],
        dist_dir = "{}/dist".format(get_out_dir(msm_target, variant)),
        flat = True,
        wipe_dist_dir = False,
        log = "info",
    )
