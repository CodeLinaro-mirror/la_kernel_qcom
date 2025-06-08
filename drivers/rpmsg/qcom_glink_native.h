/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2017, Linaro Ltd
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_GLINK_NATIVE_H__
#define __QCOM_GLINK_NATIVE_H__

#include <linux/types.h>
#include <linux/rpmsg.h>
#include <linux/kthread.h>

#define GLINK_FEATURE_INTENT_REUSE	BIT(0)
#define GLINK_FEATURE_MIGRATION		BIT(1)
#define GLINK_FEATURE_TRACER_PKT	BIT(2)
#define GLINK_FEATURE_ZERO_COPY		BIT(3)
#define GLINK_FEATURE_ZERO_COPY_POOLS	BIT(4)

#define GLINK_NAME_SIZE	32
/**
 * rpmsg rx callback return definitions
 * @RPMSG_HANDLED: rpmsg user is done processing data, framework can free the
 *                 resources related to the buffer
 * @RPMSG_DEFER:   rpmsg user is not done processing data, framework will hold
 *                 onto resources related to the buffer until rpmsg_rx_done is
 *                 called. User should check their endpoint to see if rx_done
 *                 is a supported operation.
 */
#define RPMSG_HANDLED	0
#define RPMSG_DEFER	1

struct qcom_glink_pipe {
	size_t length;

	size_t (*avail)(struct qcom_glink_pipe *glink_pipe);

	void (*peek)(struct qcom_glink_pipe *glink_pipe, void *data,
		     unsigned int offset, size_t count);
	void (*advance)(struct qcom_glink_pipe *glink_pipe, size_t count);

	void (*write)(struct qcom_glink_pipe *glink_pipe,
		      const void *hdr, size_t hlen,
		      const void *data, size_t dlen);
	void (*kick)(struct qcom_glink_pipe *glink_pipe);
};

struct device;
/**
 * struct qcom_glink - driver context, relates to one remote subsystem
 * @dev:	reference to the associated struct device
 * @name:	remote subsystem name
 * @rx_pipe:	pipe object for receive FIFO
 * @tx_pipe:	pipe object for transmit FIFO
 * @kworker:	kworker to handle rx_done work
 * @task:	kthread running @kworker
 * @rx_work:	worker for handling received control messages
 * @rx_lock:	protects the @rx_queue
 * @rx_queue:	queue of received control messages to be processed in @rx_work
 * @tx_lock:	synchronizes operations on the tx fifo
 * @idr_lock:	synchronizes @lcids and @rcids modifications
 * @lcids:	idr of all channels with a known local channel id
 * @rcids:	idr of all channels with a known remote channel id
 * @features:	remote features
 * @intentless:	flag to indicate that there is no intent
 * @tx_avail_notify: Waitqueue for pending tx tasks
 * @sent_read_notify: flag to check cmd sent or not
 * @abort_tx:	flag indicating that all tx attempts should fail
 * @irq:	IRQ for signaling incoming events
 * @irqname:	name of the IRQ
 * @ilc:	ipc logging context reference
 */
struct qcom_glink {
	struct device *dev;

	const char *name;

	struct qcom_glink_pipe *rx_pipe;
	struct qcom_glink_pipe *tx_pipe;

	struct kthread_worker kworker;
	struct task_struct *task;

	struct work_struct rx_work;
	spinlock_t rx_lock;
	struct list_head rx_queue;

	spinlock_t tx_lock;

	spinlock_t idr_lock;
	struct idr lcids;
	struct idr rcids;
	unsigned long features;

	bool intentless;
	wait_queue_head_t tx_avail_notify;
	bool sent_read_notify;

	bool abort_tx;
	int irq;
	char irqname[GLINK_NAME_SIZE];
	void *ilc;
};
extern const struct dev_pm_ops glink_native_pm_ops;

struct qcom_glink *qcom_glink_native_probe(struct device *dev,
					   unsigned long features,
					   struct qcom_glink_pipe *rx,
					   struct qcom_glink_pipe *tx,
					   bool intentless);
int qcom_glink_native_start(struct qcom_glink *glink);
void qcom_glink_native_remove(struct qcom_glink *glink);
void qcom_glink_native_rx(struct qcom_glink *glink);

/* These operations are temporarily exposing signal interfaces */
int qcom_glink_get_signals(struct rpmsg_endpoint *ept);
int qcom_glink_set_signals(struct rpmsg_endpoint *ept, u32 set, u32 clear);
int qcom_glink_register_signals_cb(struct rpmsg_endpoint *ept,
	int (*signals_cb)(struct rpmsg_device *dev, void *priv, u32 old, u32 new));

/* These operations are temporarily exposing deferred freeing interfaces */
bool qcom_glink_rx_done_supported(struct rpmsg_endpoint *ept);
int qcom_glink_rx_done(struct rpmsg_endpoint *ept, void *data);

void *qcom_glink_prepare_da_for_cpu(u64 da, size_t len);

#endif
