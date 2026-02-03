/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __HGSL_H_
#define __HGSL_H_

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include "hgsl_hyp.h"
#include "hgsl_memory.h"
#include "hgsl_tcsr.h"
#include "hgsl_gmugos.h"

#define CP_ALWAYS_ON_COUNTER_LO     0x980
#define CP_ALWAYS_ON_COUNTER_HI     0x981

#define HGSL_TIMELINE_NAME_LEN 64

#define HGSL_ISYNC_32BITS_TIMELINE 0
#define HGSL_ISYNC_64BITS_TIMELINE 1

/* Support upto 3 GVMs: 3 DBQs(Low/Medium/High priority) per GVM */
#define MAX_DB_QUEUE 9
#define HGSL_TCSR_NUM 4

/* Number of the GPU device */
#define HGSL_DEVICE_NUM  (2)
#define HGSL_CONTEXT_NUM (256)

#define HGSL_PROF_RING_SIZE 128
#define HGSL_MAX_IOC_SIZE (128)
#define HGSL_IOCTL_FUNC(_cmd, _func) \
	[_IOC_NR((_cmd))] = \
		{ .cmd = (_cmd), .func = (_func) }

enum {
	HGSL_DB_SIGNAL_NONE = 0,
	HGSL_DB_SIGNAL_TCSR_0,
	HGSL_DB_SIGNAL_TCSR_1,
	HGSL_DB_SIGNAL_TCSR_2,
	HGSL_DB_SIGNAL_TCSR_3,
	HGSL_DB_SIGNAL_GMU_GOS_0,
	HGSL_DB_SIGNAL_GMU_GOS_1,
	HGSL_DB_SIGNAL_GMU_GOS_2,
	HGSL_DB_SIGNAL_GMU_GOS_3,
	HGSL_DB_SIGNAL_GMU_GOS_4,
	HGSL_DB_SIGNAL_GMU_GOS_5,
	HGSL_DB_SIGNAL_GMU_GOS_6,
	HGSL_DB_SIGNAL_GMU_GOS_7,
	HGSL_DB_SIGNAL_MAX = HGSL_DB_SIGNAL_GMU_GOS_7,
	HGSL_DB_SIGNAL_NUM
};

struct hgsl_ioctl {
	unsigned int cmd;
	int (*func)(struct file *filep, void *data);
};
/* For application specific GPU work period stats */
#define HGSL_WORK_PERIOD	0
/* GPU work period time in msec to emulate application work stats */
#define HGSL_WORK_PERIOD_MS	900

#define CP_TYPE7_PKT (7UL << 28UL)
/* Minimal CP/type7 helpers and registers needed for profiling IB build */
#ifndef cp_type7_packet
#define cp_type7_packet(opcode, cnt) \
	((((opcode) & 0xFF) << 16) | ((cnt) & 0x3FFF) | (7 << 8))
#endif

static inline uint32_t Pm4CalcOddParityBit(uint32_t val)
{
	return (0x9669UL >> (0xFUL & ((val) ^
		((val) >> 4UL) ^ ((val) >> 8UL) ^ ((val) >> 12UL) ^
		((val) >> 16UL) ^ ((val) >> 20UL) ^ ((val) >> 24UL) ^
		((val) >> 28UL)))) & 1UL;
}

#define GPU_ADDR_LO(ADDR)   ((uint32_t)(ADDR))
#define GPU_ADDR_HI(ADDR)   ((uint32_t)(ADDR >> 32))

#ifndef CP_REG_TO_MEM
#define CP_REG_TO_MEM 0x3E
#endif

static inline uint32_t CpType7Packet(uint32_t opcode, uint32_t cnt)
{
	return CP_TYPE7_PKT | ((cnt) << 0UL) |
		(Pm4CalcOddParityBit(cnt) << 15UL) |
		(((opcode) & 0x7FUL) << 16UL) |
		((Pm4CalcOddParityBit(opcode) << 23UL));
}

static inline uint16_t CpAddAddr(uint32_t *pCmd, uint64_t addr)
{
	*pCmd++ = GPU_ADDR_LO(addr);
	*pCmd = GPU_ADDR_HI(addr);
	return 2UL;
}

static inline uint16_t Cp32BitRegToMem(uint32_t *pCmd, uint32_t reg,
				       uint64_t gpuAddr)
{
	*pCmd++ = CpType7Packet(CP_REG_TO_MEM, 3UL);
	*pCmd++ = reg;
	pCmd += CpAddAddr(pCmd, gpuAddr);
	return 4UL;
}

/* Size of per-slot IB written by a6xx_get_alwayson_counter
 * (two REG_TO_MEM packets)
 */
#ifndef PROFILE_IB_DWORDS
#define PROFILE_IB_DWORDS 4
#endif

struct qcom_hgsl;
struct hgsl_hsync_timeline;

#pragma pack(push, 4)
struct shadow_ts {
	unsigned int sop;
	unsigned int unused1;
	unsigned int eop;
	unsigned int unused2;
	unsigned int reserved[6];
};
#pragma pack(pop)

struct reg {
	unsigned long paddr;
	unsigned long size;
	void __iomem *vaddr;
};

struct hw_version {
	unsigned int version;
	unsigned int release;
};

struct db_buffer {
	int32_t dwords;
	void  *vaddr;
};

struct dbq_ibdesc_priv {
	bool   buf_inuse;
	uint32_t context_id;
	uint32_t timestamp;
};

struct doorbell_queue {
	struct dma_buf *dma;
	struct iosys_map map;
	void *vbase;
	uint64_t  gmuaddr;
	struct db_buffer data;
	uint32_t state;
	int tcsr_idx;
	uint32_t dbq_idx;
	struct dbq_ibdesc_priv ibdesc_priv;
	uint32_t  ibdesc_max_size;
	struct mutex lock;
	atomic_t seq_num;
};

struct doorbell_context_queue {
	struct hgsl_mem_node *queue_mem;
	struct iosys_map map;
	uint32_t db_signal;
	uint32_t seq_num;
	void *queue_header;
	void *queue_body;
	void *indirect_ibs;
	uint32_t queue_header_gmuaddr;
	uint32_t queue_body_gmuaddr;
	uint32_t indirect_ibs_gmuaddr;
	uint32_t queue_size;
	int irq_idx;
	uint32_t indirect_ib_ts;
};

struct qcom_hgsl {
	struct device *dev;

	/* character device info */
	struct cdev cdev;
	dev_t device_no;
	struct class *driver_class;
	struct device *class_dev;

	/* registers mapping */
	struct reg reg_ver;
	struct reg reg_dbidx;

	struct doorbell_queue dbq[MAX_DB_QUEUE];
	struct hgsl_dbq_info dbq_info[MAX_DB_QUEUE];

	/* Could disable db and use isync only */
	bool db_off;

	/* global doorbell tcsr */
	struct hgsl_tcsr *tcsr[HGSL_TCSR_NUM][HGSL_TCSR_ROLE_MAX];
	int tcsr_idx;

	struct hgsl_context **contexts[HGSL_DEVICE_NUM];
	rwlock_t ctxt_lock;

	struct hgsl_gmugos gmugos[HGSL_DEVICE_NUM];
	/** @wp_list: List of work period allocated per uid */
	struct list_head wp_list;
	/** @wp_list_lock: Lock for accessing the work period list */
	spinlock_t wp_list_lock;
	/* Profiling retire worker */
	struct timer_list prof_retire_timer;
	struct work_struct prof_retire_ws;

	struct list_head active_wait_list;
	spinlock_t active_wait_lock;

	struct workqueue_struct *wq;
	struct work_struct ts_retire_work;

	struct hw_version *ver;
	struct hgsl_hyp_priv_t global_hyp;
	bool global_hyp_inited;
	struct mutex mutex;
	struct list_head active_list;
	struct list_head release_list;
	struct workqueue_struct *release_wq;
	struct work_struct release_work;
	struct idr isync_timeline_idr;
	spinlock_t isync_timeline_lock;
	atomic64_t total_mem_size;
	struct hgsl_cache_flags cache_flags;

	/** @work_period_timer: Timer to capture application GPU work stats */
	struct timer_list work_period_timer;
	/** @work_period_lock: Lock to protect application GPU work periods */
	spinlock_t work_period_lock;
	/** @flags: Flags for gpu_period stats */
	unsigned long flags;

	struct {
		u64 begin;
		u64 end;
	} gpu_period;
	/** @work_period_ws: Work struct to emulate application GPU work events */
	struct work_struct work_period_ws;

	/* Debug nodes */
	struct kobject sysfs;
	struct kobject *clients_sysfs;
	struct dentry *debugfs;
	struct dentry *clients_debugfs;
	struct dentry *debugfs_stat;
	/* @lockless_workqueue: Pointer to a workqueue handler which doesn't hold device mutex */
	struct workqueue_struct *lockless_workqueue;
};

/* Per-context profiling state container moved out of struct hgsl_context */
struct hgsl_ctxt_profile {
	struct hgsl_mem_node *mem_node;
	void                 *buf_vaddr;
	u64                   buf_gpuaddr;
	size_t                buf_size;
	struct iosys_map      map;
	/* Per-context profiling submission ring */
	struct {
		u32 ts;       /* submission timestamp */
		u16 slot_idx; /* data slot index in profiling buffer */
	} ring[HGSL_PROF_RING_SIZE];
	u16 writeIdx;
	u16 readIdx;
	spinlock_t lock;
};

/**
 * HGSL context define
 **/
struct hgsl_context {
	struct hgsl_priv *priv;
	struct iosys_map map;
	uint32_t context_id;
	uint32_t devhandle;
	uint32_t flags;
	struct shadow_ts *shadow_ts;
	wait_queue_head_t wait_q;
	pid_t pid;
	bool dbq_assigned;
	uint32_t dbq_info;
	struct doorbell_queue *dbq;
	struct hgsl_mem_node *shadow_ts_node;
	uint32_t shadow_ts_flags;
	bool is_fe_shadow;
	bool in_destroy;
	bool destroyed;
	struct kref kref;

	uint32_t last_ts;
	struct hgsl_hsync_timeline *timeline;
	uint32_t queued_ts;
	bool is_killed;
	int tcsr_idx;
	struct mutex lock;
	struct doorbell_context_queue *dbcq;
	uint32_t dbcq_export_id;
	uint32_t db_signal;
	/* Per-context profiling state */
	struct hgsl_ctxt_profile cmdbatch_kernel_profiling;
};

struct hgsl_priv {
	struct qcom_hgsl *dev;
	pid_t pid;
	struct list_head node;
	struct hgsl_hyp_priv_t hyp_priv;
	struct mutex lock;
	struct rb_root mem_mapped;
	struct rb_root mem_allocated;
	int open_count;

	atomic64_t total_mem_size;

	/* sysfs stuff */
	struct kobject kobj;
	struct kobject sysfs_client;
	struct kobject sysfs_mem_size;
	struct dentry *debugfs_client;
	struct dentry *debugfs_mem;
	struct dentry *debugfs_memtype;
	struct gpu_work_period *period;
};

static inline bool hgsl_ts32_ge(uint32_t a, uint32_t b)
{
	static const uint32_t TIMESTAMP_WINDOW = 0x80000000;

	return (a - b) < TIMESTAMP_WINDOW;
}

static inline bool hgsl_ts64_ge(uint64_t a, uint64_t b)
{
	static const uint64_t TIMESTAMP_WINDOW = 0x8000000000000000LL;

	return (a - b) < TIMESTAMP_WINDOW;
}

static inline bool hgsl_ts_ge(uint64_t a, uint64_t b, bool is64)
{
	if (is64)
		return hgsl_ts64_ge(a, b);
	else
		return hgsl_ts32_ge((uint32_t)a, (uint32_t)b);
}

static inline bool hgsl_mem_rb_empty(struct hgsl_priv *priv)
{
	return (RB_EMPTY_ROOT(&priv->mem_mapped) &&
		RB_EMPTY_ROOT(&priv->mem_allocated));
}

static inline u32 hgsl_hnd2id(u32 dev_hnd)
{
	return (dev_hnd == GSL_HANDLE_NULL) ? (U32_MAX) :
		((dev_hnd == GSL_HANDLE_DEV1) ? 1 : 0);
}

static inline uint32_t get_context_retired_ts(struct hgsl_context *ctxt)
{
	unsigned int ts = ctxt->shadow_ts->eop;

	/* ensure read is done before comparison */
	dma_rmb();
	return ts;
}

static inline void set_context_retired_ts(struct hgsl_context *ctxt,
	unsigned int ts)
{
	ctxt->shadow_ts->eop = ts;

	/* ensure update is done before return */
	dma_wmb();
}

static inline bool _timestamp_retired(struct hgsl_context *ctxt,
	unsigned int timestamp)
{
	return hgsl_ts32_ge(get_context_retired_ts(ctxt), timestamp);
}

/**
 * struct hgsl_hsync_timeline - A sync timeline attached under each hgsl context
 * @kref: Refcount to keep the struct alive
 * @name: String to describe this timeline
 * @fence_context: Used by the fence driver to identify fences belonging to
 *		   this context
 * @child_list_head: List head for all fences on this timeline
 * @lock: Spinlock to protect this timeline
 * @last_ts: Last timestamp when signaling fences
 */
struct hgsl_hsync_timeline {
	struct kref kref;
	struct hgsl_context *context;

	char name[HGSL_TIMELINE_NAME_LEN];
	u64 fence_context;

	spinlock_t lock;
	struct list_head fence_list;
	unsigned int last_ts;
};

/**
 * struct hgsl_hsync_fence - A struct containing a fence and other data
 *				associated with it
 * @fence: The fence struct
 * @sync_file: Pointer to the sync file
 * @parent: Pointer to the hgsl sync timeline this fence is on
 * @child_list: List of fences on the same timeline
 * @context_id: hgsl context id
 * @ts: Context timestamp that this fence is associated with
 */
struct hgsl_hsync_fence {
	struct dma_fence fence;
	struct sync_file *sync_file;
	struct hgsl_hsync_timeline *timeline;
	struct list_head child_list;
	u32 context_id;
	unsigned int ts;
};

struct hgsl_isync_timeline {
	struct kref kref;
	struct list_head free_list;
	char name[HGSL_TIMELINE_NAME_LEN];
	int id;
	struct hgsl_priv *priv;
	struct list_head fence_list;
	u64 context;
	spinlock_t lock;
	u64 last_ts;
	u32 flags;
	bool is64bits;
};

struct hgsl_isync_fence {
	struct dma_fence fence;
	struct list_head free_list;  /* For free in batch */
	struct hgsl_isync_timeline *timeline;
	struct list_head child_list;
	u64 ts;
};

struct hgsl_active_wait {
	struct list_head head;
	struct hgsl_context *ctxt;
	unsigned int timestamp;
};

/**
 * struct gpu_work_period - App specific GPU work period stats
 */
struct gpu_work_period {
	struct kref refcount;
	struct list_head list;
	/** @uid: application unique identifier */
	uid_t uid;
	/** @active: Total amount of time the GPU spent running work */
	u64 active;
	/** @cmds: Total number of commands completed within work period */
	u32 cmds;
	/** @frames: Total number of frames completed within work period */
	atomic_t frames;
	/** @flags: Flags to accumulate GPU busy stats */
	unsigned long flags;
	/** @active_cmds: The number of active cmds from application */
	atomic_t active_cmds;
	/** @defer_ws: Work struct to clear gpu work period */
	struct work_struct defer_ws;
};
/* Fence for commands. */
struct hgsl_hsync_fence *hgsl_hsync_fence_create(
					struct hgsl_context *context,
					uint32_t ts);
int hgsl_hsync_fence_create_fd(struct hgsl_context *context,
				uint32_t ts);
int hgsl_hsync_timeline_create(struct hgsl_context *context);
void hgsl_hsync_timeline_signal(struct hgsl_hsync_timeline *timeline,
						unsigned int ts);
void hgsl_hsync_timeline_put(struct hgsl_hsync_timeline *timeline);
void hgsl_hsync_timeline_fini(struct hgsl_context *context);

/* Fence for process sync. */
int hgsl_isync_timeline_create(struct hgsl_priv *priv,
				    uint32_t *timeline_id,
				    uint32_t flags,
				    uint64_t initial_ts);
int hgsl_isync_timeline_destroy(struct hgsl_priv *priv, uint32_t id);
void hgsl_isync_fini(struct hgsl_priv *priv);
int hgsl_isync_fence_create(struct hgsl_priv *priv, uint32_t timeline_id,
				uint32_t ts, bool ts_is_valid, int *fence_fd);
int hgsl_isync_fence_signal(struct hgsl_priv *priv, uint32_t timeline_id,
							       int fence_fd);
int hgsl_isync_forward(struct hgsl_priv *priv, uint32_t timeline_id,
								uint64_t ts, bool check_owner);
int hgsl_isync_query(struct hgsl_priv *priv, uint32_t timeline_id,
							uint64_t *ts);
int hgsl_isync_wait_multiple(struct hgsl_priv *priv, struct hgsl_timeline_wait *param);

void hgsl_retire_common(struct qcom_hgsl *hgsl, u32 dev_hnd);

struct hgsl_context *hgsl_get_context(struct qcom_hgsl *hgsl,
	uint32_t dev_hnd, uint32_t context_id);
void hgsl_put_context(struct hgsl_context *ctxt);

#endif /* __HGSL_H_ */
