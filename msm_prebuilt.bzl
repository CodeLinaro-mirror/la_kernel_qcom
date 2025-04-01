load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":msm_common.bzl", "get_out_dir")

def define_prebuilt_lib_copy(target, msm_target, variant):
    """Creates distribution target to copy lib.so

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """
    native.alias(
        name = "{}_lib".format(target),
        actual = "//prebuilts/kernel-build-tools:linux-x86-libs",
    )

    #linux-x86
    copy_to_dist_dir(
        name = "{}_lib_dist".format(target),
        data = ["//prebuilts/kernel-build-tools:linux-x86-libs", "@openssl//:libcrypto.so.3"],
        dist_dir = "{}/out".format(get_out_dir(msm_target, variant)),
        flat = True,
        wipe_dist_dir = False,
        log = "info",
    )
