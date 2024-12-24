// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <linux/netfilter/nf_nat.h>
#include <linux/netfilter/nf_tables.h>
#include <net/protocol.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_nat.h>
#include <net/netlink.h>
#include <net/netfilter/nf_tables.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/netfilter/nf_TRIGGER.h>

#define PORTTRIGGER_DEBUG

#if !defined(PORTTRIGGER_DEBUG)
#define DEBUGP(type, args...)
#else
static const char *const modes[] = { "MODE_TRIGGER_DNAT",
				     "MODE_TRIGGER_FORWARD_IN",
				     "MODE_TRIGGER_FORWARD_OUT" };
#define DEBUGP(args...) pr_debug(args)
#endif

struct nft_trigger_port_ranges {
	unsigned short trigger_ports_min;
	unsigned short trigger_ports_max;
	unsigned short forward_ports_min;
	unsigned short forward_ports_max;
};

struct nft_trigger {
	enum nft_porttrigger_mode mode : 8;
	u8 trigger_proto;
	u8 forward_proto;
	u32 timer;
	u8 sreg_tports_min;
	u8 sreg_tports_max;
	u8 sreg_fports_min;
	u8 sreg_fports_max;
};

/* TODO: Add magic value checks to data structure.
 */
struct nft_trigger_item {
	struct list_head list;
	struct timer_list timeout;
	unsigned int src_ip;
	unsigned int dst_ip;
	unsigned short trigger_proto; /* Protocol: TCP or UDP */
	unsigned short forward_proto; /* Protocol: TCP or UDP */
	unsigned short trigger_ports_min;
	unsigned short trigger_ports_max;
	unsigned short forward_ports_min;
	unsigned short forward_ports_max;
};

/* TODO: It might be better to use a hash table for performance in
 * heavy traffic.
 */
static LIST_HEAD(trigger_list);
static DEFINE_SPINLOCK(porttrigger_lock);

static const struct nla_policy nft_trigger_policy[NFTA_TRIGGER_MAX + 1] = {
	[NFTA_TRIGGER_MODE] = { .type = NLA_U32 },
	[NFTA_TRIGGER_TPROTO] = { .type = NLA_U32 },
	[NFTA_TRIGGER_FPROTO] = { .type = NLA_U32 },
	[NFTA_TRIGGER_TIMER] = { .type = NLA_U32 },
	[NFTA_TRIGGER_TPORTS_MIN] = { .type = NLA_U32 },
	[NFTA_TRIGGER_TPORTS_MAX] = { .type = NLA_U32 },
	[NFTA_TRIGGER_FPORTS_MIN] = { .type = NLA_U32 },
	[NFTA_TRIGGER_FPORTS_MAX] = { .type = NLA_U32 },
};

static void nft_porttrigger_pte_debug_print(const struct nft_trigger_item *pte,
					    const char *s)
{
#if defined(PORTTRIGGER_DEBUG)
	DEBUGP("%p: %s - trigger_proto[%d], forward_proto[%d], src_ip[%pI4], dst_ip[%pI4]\n",
	       pte, s, pte->trigger_proto, pte->forward_proto, &pte->src_ip,
	       &pte->dst_ip);
#endif
}

/* nft_porttrigger_free()
 * Free the item object.
 */
static void nft_porttrigger_free(struct nft_trigger_item *pte)
{
	nft_porttrigger_pte_debug_print(pte, "free");
	kfree(pte);
}

/* nft_trigger_refresh_timer()
 * Refresh the timer for this object.
 */
static bool nft_trigger_refresh_timer(struct nft_trigger_item *pte,
				      unsigned long extra_jiffies)
{
	lockdep_assert_held(&porttrigger_lock);

	if (extra_jiffies == 0)
		extra_jiffies = NFT_TRIGGER_TIMEOUT * HZ;
	if (del_timer(&pte->timeout)) {
		pte->timeout.expires = jiffies + extra_jiffies;
		add_timer(&pte->timeout);
		return true;
	}
	return false;
}

static void nft_porttrigger_timer_timeout(struct timer_list *t)
{
	struct nft_trigger_item *pte = from_timer(pte, t, timeout);

	/* The race with list deletion is solved by ensuring
	 * that either this code or the list deletion code
	 * but not both will remove the oject.
	 */
	spin_lock_bh(&porttrigger_lock);
	nft_porttrigger_pte_debug_print(pte, "timeout");
	list_del(&pte->list);
	spin_unlock_bh(&porttrigger_lock);
	nft_porttrigger_free(pte);
}

/* nft_porttrigger_packet_in_match()
 * Ingress packet check match entry function.
 */
static inline bool nft_porttrigger_packet_in_match(const struct nft_trigger_item *pte,
						   const unsigned short proto,
						   const unsigned short dport,
						   const unsigned int src_ip)
{
	DEBUGP("src_ip = %pI4, pte->src_ip = %pI4 proto = %d, dport = %d in match\n",
	       &src_ip, &pte->src_ip, proto, htons(dport));

	/* Check LAND attack. If the ingress packet's src ip is equal to the pte's src ip
	 * (which is the LAN PC's ip), ignore the packet.
	 */
	if (src_ip == pte->src_ip) {
		DEBUGP("LAND attack: ingress packet's src ip is equal to LAN src ip\n");
		return false;
	}

	/* The protocol values can be TCP, UDP or both (0). Check whether we have a
	 * valid protocol or not.
	 */
	if (pte->forward_proto && pte->forward_proto != proto) {
		DEBUGP("Invalid forward_proto: %d\n", pte->forward_proto);
		return false;
	}

	/* Check dport value in forward ports range or not.
	 */
	if (!(htons(dport) >= pte->forward_ports_min &&
	      htons(dport) <= pte->forward_ports_max))
		return false;

	DEBUGP("%s: pte->forward_proto = %d, dport: %d\n", __func__,
	       pte->forward_proto, htons(dport));
	return true;
}

/* nft_porttrigger_packet_out_match()
 * Egress packet check match entry function.
 */
static inline bool nft_porttrigger_packet_out_match(const unsigned short trigger_proto,
						    const unsigned short proto,
						    const short trigger_ports_min,
						    const short trigger_ports_max,
						    unsigned short dport)
{
	DEBUGP("protocol = %d, dport = %d out match\n", proto, htons(dport));

	DEBUGP("%s: trigger_ports: %d-%d.\n", __func__, trigger_ports_min,
	       trigger_ports_max);

	if (trigger_proto && trigger_proto != proto) {
		DEBUGP("Invalid trigger_proto: %d\n", trigger_proto);
		return false;
	}

	if (!(htons(dport) >= trigger_ports_min &&
	      htons(dport) <= trigger_ports_max))
		return false;

	DEBUGP("trigger_proto = %d, dport: %d\n", trigger_proto, htons(dport));
	return true;
}

/* netfilter TRIGGER nft_porttrigger_nat()
 * Ingress packet on PRE_ROUTING hook, find match, update conntrack to allow
 */
static int nft_porttrigger_nat(const struct nft_expr *expr,
			       const struct nft_pktinfo *pkt,
			       const struct nft_trigger_port_ranges *ranges)
{
	struct sk_buff *skb = pkt->skb;
	struct nft_trigger_item *pte;
	unsigned short dst_port = 0;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	enum ip_conntrack_dir dir;
	unsigned int src_addr;
	unsigned short protocol;
	int ret;

	if (pkt->state->hook != NF_INET_PRE_ROUTING)
		return NFT_BREAK;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) {
		DEBUGP("ingress packet conntrack not found\n");
		return NFT_BREAK;
	}

	dir = CTINFO2DIR(ctinfo);
	src_addr = ct->tuplehash[dir].tuple.src.u3.ip;
	dst_port = ct->tuplehash[dir].tuple.dst.u.all;
	protocol = ct->tuplehash[dir].tuple.dst.protonum;
	DEBUGP("%s: src_addr: %pI4 dst_port = %d protocol = %d\n", __func__,
	       &src_addr, htons(dst_port), protocol);

	spin_lock_bh(&porttrigger_lock);
	list_for_each_entry(pte, &trigger_list, list) {
		struct nf_nat_range2 newrange;

		if (!nft_porttrigger_packet_in_match(pte, protocol, dst_port,
						     src_addr))
			continue;

		newrange.flags = NF_NAT_RANGE_MAP_IPS;
		newrange.min_addr.ip = pte->src_ip;
		newrange.max_addr.ip = pte->src_ip;

		spin_unlock_bh(&porttrigger_lock);

		DEBUGP("DNAT: src_addr: %pI4 dst_port: %d protocol: %d\n",
		       &src_addr, htons(dst_port), protocol);
		DEBUGP("DNAT: LAN src IP %pI4\n", &pte->src_ip);
		ret = nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
		DEBUGP("%s: Expand returned: %d\n", __func__, ret);
		return ret;
	}
	spin_unlock_bh(&porttrigger_lock);
	return NFT_BREAK;
}

/* netfilter TRIGGER nft_porttrigger_forward()
 * Ingress and Egress packet forwarding hook
 */
static int nft_porttrigger_forward(const struct nft_expr *expr,
				   const struct nft_pktinfo *pkt,
				   const struct nft_trigger_port_ranges *ranges)
{
	struct sk_buff *skb = pkt->skb;
	const struct nft_trigger *priv = nft_expr_priv(expr);
	struct nft_trigger_item *pte;
	struct nft_trigger_item *pte2;
	unsigned short dst_port = 0;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	enum ip_conntrack_dir dir;
	unsigned int src_addr;
	unsigned short protocol;

	if (pkt->state->hook != NF_INET_POST_ROUTING)
		return NFT_BREAK;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) {
		DEBUGP("%s: conntrack not found", __func__);
		return NFT_BREAK;
	}

	dir = CTINFO2DIR(ctinfo);
	src_addr = ct->tuplehash[dir].tuple.src.u3.ip;
	dst_port = ct->tuplehash[dir].tuple.dst.u.all;
	protocol = ct->tuplehash[dir].tuple.dst.protonum;
	DEBUGP("%s: src_addr: %pI4 dst_port = %d protocol = %d\n", __func__,
	       &src_addr, dst_port, protocol);

	/* Ingress packet, refresh the timer if we find an entry.
	 */
	if (priv->mode == NFT_MODE_TRIGGER_FORWARD_IN) {
		DEBUGP("porttrigger_forward_in\n");
		spin_lock_bh(&porttrigger_lock);
		list_for_each_entry(pte, &trigger_list, list) {
			/* Compare the ingress packet with the existing
			 * entries looking for a match.
			 */
			if (!nft_porttrigger_packet_in_match(pte, protocol, dst_port, src_addr))
				continue;

			/* Refresh the timer, if we fail, break
			 * out and forward fail as though we never
			 * found the entry.
			 */
			if (!nft_trigger_refresh_timer(pte, priv->timer * HZ))
				break;

			/* The entry is found and refreshed, but the entry values
			 * can be changed by another thread in the MODE_TRIGGER_FORWARD_OUT case,
			 * so print them inside the lock.
			 */
			nft_porttrigger_pte_debug_print(pte, "refresh");
			spin_unlock_bh(&porttrigger_lock);
			DEBUGP("FORWARD_IN_ACCEPT: src_addr: %pI4 dst_port: %d protocol: %d\n",
			       &src_addr, dst_port, protocol);
			return NF_ACCEPT;
		}
		spin_unlock_bh(&porttrigger_lock);
		DEBUGP("FORWARD_IN_FAIL\n");
		return NFT_BREAK;
	}
	/* Egress packet, create a new rule in our list.  If we don't have a rule for this port
	 * do not create any rule.
	 */
	DEBUGP("porttrigger_forward: Check for the match\n");
	if (!nft_porttrigger_packet_out_match(priv->trigger_proto,
					      protocol, ranges->trigger_ports_min,
					      ranges->trigger_ports_max, dst_port))
		return NFT_BREAK;

	/* Allocate a new entry
	 */
	DEBUGP("Create a new entry\n");
	pte = kzalloc(sizeof(*pte), GFP_ATOMIC | __GFP_NOWARN);
	if (!pte) {
		DEBUGP("kernel malloc fail\n");
		return NFT_BREAK;
	}

	INIT_LIST_HEAD(&pte->list);

	pte->src_ip = src_addr;
	pte->trigger_proto = protocol;
	pte->forward_proto = priv->forward_proto;
	pte->trigger_ports_min = ranges->trigger_ports_min;
	pte->trigger_ports_max = ranges->trigger_ports_max;
	pte->forward_ports_min = ranges->forward_ports_min;
	pte->forward_ports_max = ranges->forward_ports_max;
	DEBUGP("porttrigger_forward new entry: src_addr:%pI4 trigger_proto=%d forward_proto=%d\n",
	       &pte->src_ip, pte->trigger_proto, pte->forward_proto);

	/* We have created the new pte; however, it might not be unique.
	 * Search the list for a matching entry.  If found, throw away
	 * the new entry and refresh the old.  If not found, atomically
	 * insert the new entry on the list.
	 */
	spin_lock_bh(&porttrigger_lock);
	list_for_each_entry(pte2, &trigger_list, list) {
		if (!nft_porttrigger_packet_out_match(pte2->trigger_proto,
						      protocol, pte2->trigger_ports_min,
						      pte2->trigger_ports_max, dst_port))
			continue;

		/* If we can not refresh this entry, insert our new
		 * entry as this one is timed out and will be removed
		 * from the list shortly.
		 */
		if (!nft_trigger_refresh_timer(pte2, priv->timer * HZ))
			break;

		/* We have an entry in the list with this dst_port (trigger port) and protocol,
		 * but the src ip may be different than this new one,
		 * so refresh the src_ip of the found pte.
		 */
		pte2->src_ip = src_addr;

		/* It is not good to use debug message inside the lock, but we are changing the
		 * value, and we have to print it here by paying the price under debugs on.
		 */
		nft_porttrigger_pte_debug_print(pte2, "refresh");

		/* Found and refreshed an existing entry.  Its values
		 * do not change so print the values outside of the lock.
		 *
		 * Free up the new entry.
		 */
		spin_unlock_bh(&porttrigger_lock);
		nft_porttrigger_free(pte);
		return NF_ACCEPT;
	}

	/* Initialize the self-destruct timer.
	 */
	DEBUGP("Init timer");
	timer_setup(&pte->timeout, nft_porttrigger_timer_timeout, 0);

	/* Add the new entry to the list.
	 */
	if (priv->timer == 0)
		pte->timeout.expires = jiffies + (NFT_TRIGGER_TIMEOUT * HZ);
	else
		pte->timeout.expires = jiffies + (priv->timer * HZ);
	add_timer(&pte->timeout);
	list_add(&pte->list, &trigger_list);
	nft_porttrigger_pte_debug_print(pte, "ADD");
	spin_unlock_bh(&porttrigger_lock);
	return NF_ACCEPT;
}

static void nft_trigger_eval(const struct nft_expr *expr, struct nft_regs *regs,
			     const struct nft_pktinfo *pkt)
{
	const struct nft_trigger *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	enum ip_conntrack_dir dir;
	unsigned int src_addr;
	unsigned int dst_addr;
	unsigned short protocol;
	struct nft_trigger_port_ranges ranges;

	/* Check if we have enough data in the skb.
	 */
	if (skb->len < ip_hdrlen(skb)) {
		DEBUGP("skb is too short for IP header\n");
		regs->verdict.code = NFT_BREAK;
		return;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) {
		DEBUGP("%s: conntrack not found\n", __func__);
		regs->verdict.code = NFT_BREAK;
		return;
	}

	dir = CTINFO2DIR(ctinfo);
	src_addr = ct->tuplehash[dir].tuple.src.u3.ip;
	dst_addr = ct->tuplehash[dir].tuple.dst.u3.ip;
	protocol = ct->tuplehash[dir].tuple.dst.protonum;

	DEBUGP("%s: src_addr=%pI4 dst_addr=%pI4 protocol=%d\n", __func__,
	       &src_addr, &dst_addr, protocol);

	/* Only perform porttriggering on UDP or TCP.
	 */
	if (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* Check for LAND attack and ignore.
	 */
	if (dst_addr == src_addr) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* Check that we have valid source and destination addresses.
	 */
	if (dst_addr == (__be32)0 || src_addr == (__be32)0) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* Setup port ranges.
	 */
	if (priv->sreg_tports_min) {
		ranges.trigger_ports_min = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_tports_min]);
		ranges.trigger_ports_min = htons(ranges.trigger_ports_min);

		ranges.trigger_ports_max = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_tports_max]);
		ranges.trigger_ports_max = htons(ranges.trigger_ports_max);
	}
	if (priv->sreg_fports_min) {
		ranges.forward_ports_min = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_fports_min]);
		ranges.forward_ports_min = htons(ranges.forward_ports_min);

		ranges.forward_ports_max = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_fports_max]);
		ranges.forward_ports_max = htons(ranges.forward_ports_max);
	}

	DEBUGP("%s: mode = %s\n", __func__, modes[priv->mode]);
	DEBUGP("%s: trigger_ports: %d-%d\n", __func__, ranges.trigger_ports_min,
	       ranges.trigger_ports_max);
	DEBUGP("%s: forward_ports: %d-%d\n", __func__, ranges.forward_ports_min,
	       ranges.forward_ports_max);

	/* TODO: mode corresponding with hooks, maybe do not need this
	 * but use hooks from ctx directly.
	 */
	switch (priv->mode) {
	case NFT_MODE_TRIGGER_DNAT:
		regs->verdict.code = nft_porttrigger_nat(expr, pkt, &ranges);
		return;
	case NFT_MODE_TRIGGER_FORWARD_IN:
	case NFT_MODE_TRIGGER_FORWARD_OUT:
		regs->verdict.code =
			nft_porttrigger_forward(expr, pkt, &ranges);
		return;
	}
	regs->verdict.code = NFT_BREAK;
}

static int nft_trigger_validate(const struct nft_ctx *ctx,
				const struct nft_expr *expr,
				const struct nft_data **data)
{
	struct nft_trigger *priv = nft_expr_priv(expr);
	int err;
	struct list_head *cur, *tmp;

	/* When DNAT mode, make sure chain priority is NAT.
	 */
	if (priv->mode == NFT_MODE_TRIGGER_DNAT) {
		err = nft_chain_validate_dependency(ctx->chain,
						    NFT_CHAIN_T_NAT);
		if (err < 0) {
			DEBUGP("%s: bad chain not in NAT.\n", __func__);
			return err;
		}
	}

	switch (priv->mode) {
	case NFT_MODE_TRIGGER_DNAT:
		err = nft_chain_validate_hooks(ctx->chain,
					       (1 << NF_INET_PRE_ROUTING) |
					       (1 << NF_INET_LOCAL_OUT));
		break;
	case NFT_MODE_TRIGGER_FORWARD_IN:
	case NFT_MODE_TRIGGER_FORWARD_OUT:
		err = nft_chain_validate_hooks(ctx->chain,
					       (1 << NF_INET_POST_ROUTING) |
					       (1 << NF_INET_LOCAL_IN));
		break;
	}

	if (err < 0) {
		DEBUGP("%s: bad hooks for TRIGGER mode %s.\n", __func__,
		       modes[priv->mode]);
		return err;
	}
	/* Port range already take care from nftables side, no need here.
	 */

	/* Remove all entries from the trigger list.
	 */
drain:
	spin_lock_bh(&porttrigger_lock);
	list_for_each_safe(cur, tmp, &trigger_list) {
		struct nft_trigger_item *pte = (void *)cur;

		/* If the timeout is in process, it will tear
		 * us down.  Since it is waiting on the spinlock
		 * we have to give up the spinlock to give the
		 * timeout on another CPU a chance to run.
		 */
		if (!del_timer(&pte->timeout)) {
			spin_unlock_bh(&porttrigger_lock);
			goto drain;
		}

		DEBUGP("%p: removing from list\n", pte);
		list_del(&pte->list);
		spin_unlock_bh(&porttrigger_lock);
		nft_porttrigger_free(pte);
		goto drain;
	}
	spin_unlock_bh(&porttrigger_lock);
	return 0;
}

static int nft_trigger_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr *const tb[])
{
	struct nft_trigger *priv = nft_expr_priv(expr);
	unsigned int plen;
	int err;

	if (!tb[NFTA_TRIGGER_MODE] ||
	    (!tb[NFTA_TRIGGER_TPORTS_MIN] &&
	     !tb[NFTA_TRIGGER_FPORTS_MIN]))
		return -EINVAL;

	priv->mode = ntohl(nla_get_be32(tb[NFTA_TRIGGER_MODE]));

	if (tb[NFTA_TRIGGER_TPROTO])
		priv->trigger_proto =
			ntohl(nla_get_be32(tb[NFTA_TRIGGER_TPROTO]));

	if (tb[NFTA_TRIGGER_FPROTO])
		priv->forward_proto =
			ntohl(nla_get_be32(tb[NFTA_TRIGGER_FPROTO]));

	if (tb[NFTA_TRIGGER_TIMER])
		priv->timer = ntohl(nla_get_be32(tb[NFTA_TRIGGER_TIMER]));

	/* As TRIGGER only support TCP/UDP, use nat port length directly */
	plen = sizeof_field(struct nf_nat_range, min_proto.all);
	if (tb[NFTA_TRIGGER_TPORTS_MIN]) {
		err = nft_parse_register_load(tb[NFTA_TRIGGER_TPORTS_MIN],
					      &priv->sreg_tports_min, plen);
		if (err < 0)
			return err;

		if (tb[NFTA_TRIGGER_TPORTS_MAX]) {
			err = nft_parse_register_load(tb[NFTA_TRIGGER_TPORTS_MAX],
						      &priv->sreg_tports_max, plen);

			if (err < 0)
				return err;
		} else {
			priv->sreg_tports_max = priv->sreg_tports_min;
		}
	}

	if (tb[NFTA_TRIGGER_FPORTS_MIN]) {
		err = nft_parse_register_load(tb[NFTA_TRIGGER_FPORTS_MIN],
					      &priv->sreg_fports_min, plen);
		if (err < 0)
			return err;

		if (tb[NFTA_TRIGGER_FPORTS_MAX]) {
			err = nft_parse_register_load(tb[NFTA_TRIGGER_FPORTS_MAX],
						      &priv->sreg_fports_max, plen);

			if (err < 0)
				return err;
		} else {
			priv->sreg_fports_max = priv->sreg_fports_min;
		}
	}

	return 0;
}

static int nft_trigger_dump(struct sk_buff *skb, const struct nft_expr *expr,
			    bool reset)
{
	const struct nft_trigger *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_TRIGGER_MODE, htonl(priv->mode)))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_TRIGGER_TPROTO, htonl(priv->trigger_proto)))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_TRIGGER_FPROTO, htonl(priv->forward_proto)))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_TRIGGER_TIMER, htonl(priv->timer)))
		goto nla_put_failure;

	if (priv->sreg_tports_min) {
		if (nft_dump_register(skb, NFTA_TRIGGER_TPORTS_MIN,
				      priv->sreg_tports_min) ||
		    nft_dump_register(skb, NFTA_TRIGGER_TPORTS_MAX,
				      priv->sreg_tports_max))
			goto nla_put_failure;
	}

	if (priv->sreg_fports_min) {
		if (nft_dump_register(skb, NFTA_TRIGGER_FPORTS_MIN,
				      priv->sreg_fports_min) ||
		    nft_dump_register(skb, NFTA_TRIGGER_FPORTS_MAX,
				      priv->sreg_fports_max))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_trigger_type;
static const struct nft_expr_ops nft_trigger_op = {
	.eval = nft_trigger_eval,
	.size = NFT_EXPR_SIZE(sizeof(struct nft_trigger)),
	.init = nft_trigger_init,
	.dump = nft_trigger_dump,
	.validate = nft_trigger_validate,
	.type = &nft_trigger_type,
	.reduce = NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_trigger_type __read_mostly = {
	.ops = &nft_trigger_op,
	.name = "TRIGGER",
	.owner = THIS_MODULE,
	.policy = nft_trigger_policy,
	.maxattr = NFTA_TRIGGER_MAX,
};

static int __init nft_trigger_module_init(void)
{
	return nft_register_expr(&nft_trigger_type);
}

static void __exit nft_trigger_module_exit(void)
{
	nft_unregister_expr(&nft_trigger_type);
}

module_init(nft_trigger_module_init);
module_exit(nft_trigger_module_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NFT_EXPR("TRIGGER");
