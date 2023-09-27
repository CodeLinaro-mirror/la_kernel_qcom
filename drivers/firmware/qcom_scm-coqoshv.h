// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define COQOSHV_SMC_ARGS_NUM 8

/* 64 byte */
struct coqoshv_smc_mailbox {
        __u64 args[COQOSHV_SMC_ARGS_NUM];
} __packed;

void coqoshv_call_qcpe(const struct coqoshv_smc_mailbox *smc,
		       struct arm_smccc_res *res, const bool atomic);
