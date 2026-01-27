// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#include "smc-filter-hyp.h"
#include <asm/kvm_pkvm_module.h>
#include <asm/kvm_arm.h>

/****************************************
 * QCOM pKVM SMC filter module - EL2
 */

static const struct pkvm_module_ops *m_ops;

/*
 * Handling extended arguments buffer:
 *  - Check that it is within the host's address space;
 *  - Share the host's pages with the hypervisor;
 *  - Pin the pages into the host's memory;
 *  - Return error if any of above actions has failed;
 *  - Forward the SMC to TZ manually;
 *  - Unwind the sharing and pinning actions above.
 */
static inline bool pin_shared_mem(u64 pfn)
{
	return m_ops->pin_shared_mem(m_ops->hyp_va(__pfn_to_phys(pfn)),
				m_ops->hyp_va(__pfn_to_phys(pfn + 1)));
}

static inline void unpin_shared_mem(u64 pfn)
{
	return m_ops->unpin_shared_mem(m_ops->hyp_va(__pfn_to_phys(pfn)),
				m_ops->hyp_va(__pfn_to_phys(pfn + 1)));
}

static bool forward_smc_ext_args(phys_addr_t buf, struct user_pt_regs *regs)
{
	const u64 pfn_start = __phys_to_pfn(buf);
	const u64 pfn_end = __phys_to_pfn(buf + SCM_EXT_ARG_BUF_SIZE - 1);
	enum pfn_err_type error = PFN_NO_ERR;
	u64 pfn;

	/* Check that the buffer spans across at most two pages */
	static_assert(SCM_EXT_ARG_BUF_SIZE <= PAGE_SIZE,
		      "Extended arguments buffer too long");
	if (pfn_start > pfn_end)
		return true;	/* address space wrap-around - error */

	for (pfn = pfn_start; pfn <= pfn_end; pfn++) {
		/* Share the host page with the hypervisor */
		if (m_ops->host_share_hyp(pfn))
			error = PFN_SHARE_ERR;

		/* Pin the host page */
		if (!error && pin_shared_mem(pfn))
			error = PFN_PIN_ERR;

		/* Unwind if any failure */
		if (error) {
			if (pfn != pfn_start) {
				/* unwind for the previous pfn */
				unpin_shared_mem(pfn_start);
				WARN_ON(m_ops->host_unshare_hyp(pfn_start));
			}
			if (error == PFN_PIN_ERR) {
				/* unwind for this pfn */
				WARN_ON(m_ops->host_unshare_hyp(pfn));
			}
			return true;  /* error */
		}
	}

	/* Forward the SMC to TZ */
	/* TODO: uncomment hyp_exit() and hyp_entry() for proper tracing and other handling */
	// m_ops->hyp_exit();
	__forward_smc(regs);
	// m_ops->hyp_enter();

	/* Undo the memory sharing and pinning */
	for (pfn = pfn_start; pfn <= pfn_end; pfn++) {
		unpin_shared_mem(pfn);
		WARN_ON(m_ops->host_unshare_hyp(pfn));
	}

	return false;
}

/*
 * Helper function to handle extended ABI processing for all owners.
 * Returns true if the call was handled (either successfully or with error),
 *  false if the call should be forwarded to TZ via standard path.
 */
static bool handle_scm_extended_abi(struct user_pt_regs *regs)
{
	const int arglen =
		regs->regs[SCM_SMC_ARG_INFO_REG_IDX] & SCM_SMC_ARG_LEN_MASK;

	if (arglen > SCM_SMC_N_REG_ARGS) {
		/*
		 * Using a parameter buffer (extended ABI) - perform the
		 * necessary checks and if those pass, manually forward the SMC
		 * to the TZ, as unwind actions are needed after the SMC return.
		 */
		const phys_addr_t ext_arg_buf = regs->regs[SCM_SMC_LAST_REG_IDX];

		if (forward_smc_ext_args(ext_arg_buf, regs)) {
			// TODO: revert to printf usage once pKVM common changes can be used.
			// m_ops->printf("ERROR: qcom SMC filter Extended ABI handling, ID 0x%x\n",
			//		(u32)regs->regs[SMCCC_FUNC_ID_REG_IDX]);
			m_ops->puts("[pKVM EL2] ERROR: qcom SMC filter Extended ABI, ID:");
			m_ops->putx64(regs->regs[SMCCC_FUNC_ID_REG_IDX]);
			regs->regs[SMCCC_EC_REG_IDX] = SMCCC_RET_INVALID_PARAMETER;
		}
		return true;  /* handled */
	}

	return false;  /* not using extended ABI */
}

/*
 * Handler for ARM_SMCCC_OWNER_SIP calls.
 * If this returns false, call may be forwarded to TZ.
 */
static bool smc_filter_host_sip_handler(u32 func_id)
{
	const u16 svc_cmd = ARM_SMCCC_FUNC_NUM(func_id);

	/*
	 * Check which SIP calls are permitted from the host.
	 * Only allow specific svc/cmd combinations.
	 */
	switch (svc_cmd) {
	case SMC_SIP_CONFIG_HW_FOR_RAM_DUMP_ID:
	case SMC_SIP_INFO_IS_CALL_AVAIL:
	case SMC_SIP_INFO_GET_FEAT_VERSION:
	case SMC_SIP_INFO_GET_SECURE_STATE:
	case SMC_SIP_IO_READ:
	case SMC_SIP_IO_WRITE:
		/* Forward to TZ */
		break;
	default:
		/* Blocked */
		return true;
	}

	/* The SMC can be forwarded to TZ */
	return false;
}

/*
 * Handler for ARM_SMCCC_OWNER_TRUSTED_OS calls.
 * If this returns false, call may be forwarded to TZ.
 */
static bool smc_filter_host_trusted_os_handler(u32 func_id)
{
	const u16 svc_cmd = ARM_SMCCC_FUNC_NUM(func_id);
	const bool is_fast = ARM_SMCCC_IS_FAST_CALL(func_id);
	const bool is_64 = ARM_SMCCC_IS_64(func_id);

	/*
	 * Check which Trusted OS calls are permitted from the host.
	 * Only allow specific svc/cmd combinations.
	 */
	switch (svc_cmd) {
	case SMC_TOS_QSEELOG_REGISTER:
	case SMC_TOS_REQUEST_ENCR_LOG:
	case SMC_TOS_QUERY_LOG_STATUS:
	case SMC_TOS_QUERY_TZ_TIME:
		/* SVC_QSEELOG: forward to TZ */
		break;
	case SMC_TOS_SMCINVOKE_INVOKE_FFA:
	case SMC_TOS_SMCINVOKE_CB_RSP_FFA:
		/* SVC_SMCINVOKE: block if Fast or 32-bit */
		return is_fast || !is_64;
	default:
		/* Blocked */
		return true;
	}

	/* The SMC can be forwarded to TZ */
	return false;
}

static inline bool is_ffa_call(u32 func_id)
{
	return ARM_SMCCC_IS_FAST_CALL(func_id) &&
		ARM_SMCCC_FUNC_NUM(func_id) >= FFA_MIN_FUNC_NUM &&
		ARM_SMCCC_FUNC_NUM(func_id) <= FFA_MAX_FUNC_NUM;
}

/*
 * SMC call interception.
 * If this returns false, call may be forwarded to TZ.
 */
bool smc_filter_host_handler(struct user_pt_regs *regs)
{
	bool blocked = true;
	const u32 func_id = (u32)regs->regs[SMCCC_FUNC_ID_REG_IDX];
	const u32 owner = ARM_SMCCC_OWNER_NUM(func_id);

	/* MBZ bits must be zero */
	if (ARM_SMCCC_MBZ(func_id) != 0U)
		goto terminate_smc;

	switch (owner) {
	case ARM_SMCCC_OWNER_SIP:
		blocked = smc_filter_host_sip_handler(func_id);
		break;
	case ARM_SMCCC_OWNER_STANDARD:
		/* FFA calls that do reach here are being passed through. */
		if (is_ffa_call(func_id))
			return false;
		break;
	case ARM_SMCCC_OWNER_TRUSTED_OS:
		blocked = smc_filter_host_trusted_os_handler(func_id);
		break;
	default:
		/* Unhandled Owner ID */
		break;
	}

	if (blocked)
		goto terminate_smc;

	/*
	 * If the owner-specific handler indicates the call can be forwarded,
	 * check if extended ABI processing is needed.
	 */
	return handle_scm_extended_abi(regs);

terminate_smc:
	// TODO: revert to printf usage once pKVM common changes in module API-s can be used.
	// m_ops->printf("WARNING: qcom SMC filter blocking Function ID 0x%x\n", func_id);
	m_ops->puts("[pKVM EL2] WARNING: qcom SMC filter blocking Function ID:");
	m_ops->putx64(func_id);
	regs->regs[SMCCC_EC_REG_IDX] = SMCCC_RET_NOT_SUPPORTED;
	return true;
}

/*
 * Module init
 */
int smc_filter_hyp_init(const struct pkvm_module_ops *ops)
{
	int ret;

	m_ops = ops;

	ret = ops->register_host_smc_handler(smc_filter_host_handler);
	if (ret) {
		ops->puts("ERROR: qcom SMC filter registration failed");
		return ret;
	}

	ops->puts("qcom SMC filter module loaded");
	return 0;
}
