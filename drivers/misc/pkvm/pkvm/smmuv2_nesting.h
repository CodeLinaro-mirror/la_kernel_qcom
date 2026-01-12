/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __SMMUV2_NESTED__
#define __SMMUV2_NESTED__

#if defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE)

#include <asm/kvm_pkvm_module.h>

extern const struct pkvm_module_ops		*mod_ops;

#undef memset
#undef memcpy
#undef kern_hyp_va

#define CALL_FROM_OPS(fn, ...)			(mod_ops->(fn)(__VA_ARGS__))

#define hyp_virt_to_phys(x)			CALL_FROM_OPS(hyp_pa, x)
#define hyp_phys_to_virt(x)			CALL_FROM_OPS(hyp_va, x)
#define memcpy(x, y, z)				CALL_FROM_OPS(memcpy, x, y, z)
#define pkvm_udelay(x)				CALL_FROM_OPS(udelay, x)
#define ___pkvm_host_donate_hyp(x, y, z)	CALL_FROM_OPS(host_donate_hyp, x, y, z)
#define kern_hyp_va(x)				\
		((void *)CALL_FROM_OPS(kern_hyp_va, (unsigned long)x))
#define __pkvm_host_donate_hyp(x, y)		CALL_FROM_OPS(host_donate_hyp, x, y, false)
#define kvm_iommu_donate_pages_atomic(x)	CALL_FROM_OPS(iommu_donate_pages_atomic, x)
#define kvm_iommu_reclaim_pages_atomic(x, y)	CALL_FROM_OPS(iommu_reclaim_pages_atomic, x, y)
#define kvm_iommu_snapshot_host_stage2(x)	CALL_FROM_OPS(iommu_snapshot_host_stage2, x)
#define __pkvm_host_share_hyp(x)		CALL_FROM_OPS(host_share_hyp, x)
#define __pkvm_host_unshare_hyp(x)		CALL_FROM_OPS(host_unshare_hyp, x)
#endif

#endif
