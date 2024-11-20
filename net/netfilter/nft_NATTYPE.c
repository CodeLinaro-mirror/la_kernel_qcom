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
#include <linux/netfilter/nf_NATTYPE.h>

static const char *const types[] = { "NFT_TYPE_PORT_ADDRESS_RESTRICTED",
				     "NFT_TYPE_ENDPOINT_INDEPENDENT",
				     "NFT_TYPE_ADDRESS_RESTRICTED" };
static const char *const modes[] = { "NFT_MODE_DNAT", "NFT_MODE_FORWARD_IN",
				     "NFT_MODE_FORWARD_OUT" };
#define DEBUGP(args...) pr_debug(args)

struct nft_nattype {
	struct nf_nattype_info info;
};

struct nft_nattype_item {
	struct list_head list;
	struct timer_list timeout;
	unsigned long timeout_value;
	unsigned int nattype_cookie;
	unsigned short proto; /* Protocol: TCP or UDP */
	struct nf_nat_range2 range; /* LAN side source information */
	unsigned short nat_port; /* Routed NAT port */
	unsigned int dest_addr; /* Original egress packets dst addr */
	unsigned short dest_port; /* Original egress packets destination port */
};

#define NATTYPE_COOKIE 0x11abcdef

static LIST_HEAD(nattype_list);
static DEFINE_SPINLOCK(nattype_lock);

static const struct nla_policy nft_nattype_policy[NFTA_NATTYPE_MAX + 1] = {
	[NFTA_NATTYPE_MODE] = { .type = NLA_U32 },
	[NFTA_NATTYPE_TYPE] = { .type = NLA_U32 },
};

/* netfilter NATTYPE
 * nft_nattype_nte_debug_print()
 */
static void nft_nattype_nte_debug_print(const struct nft_nattype_item *nte,
					const char *s)
{
	DEBUGP("%p:%s-proto[%d],src[%pI4:%d],nat[%d],dest[%pI4:%d]\n", nte, s,
	       nte->proto, &nte->range.min_addr.ip,
	       ntohs(nte->range.min_proto.all), ntohs(nte->nat_port),
	       &nte->dest_addr, ntohs(nte->dest_port));
	DEBUGP("Timeout[%lx], Expires[%lx], Current[%lx]\n", nte->timeout_value,
	       nte->timeout.expires, jiffies);
}

/* netfilter NATTYPE nft_nattype_free()
 * Free the object.
 */
static void nft_nattype_free(struct nft_nattype_item *nte)
{
	kfree(nte);
}

/* netfilter NATTYPE nattype_refresh_timer()
 * Refresh the timer for this object.
 */
bool nft_nattype_refresh_timer_impl(unsigned long nat_type,
				    unsigned long timeout_value)
{
	struct nft_nattype_item *nte = (struct nft_nattype_item *)nat_type;

	if (!nte)
		return false;
	spin_lock_bh(&nattype_lock);
	if (nte->nattype_cookie != NATTYPE_COOKIE) {
		spin_unlock_bh(&nattype_lock);
		return false;
	}
	DEBUGP("%s: timeout_value=%lx, jiffies=%lx", __func__, timeout_value,
	       jiffies);
	if (del_timer(&nte->timeout)) {
		nte->timeout.expires =
			timeout_value + jiffies - nfct_time_stamp;
		add_timer(&nte->timeout);
		spin_unlock_bh(&nattype_lock);
		nft_nattype_nte_debug_print(nte, "refresh");
		return true;
	}
	spin_unlock_bh(&nattype_lock);
	return false;
}

/* netfilter NATTYPE nft_nattype_timer_timeout()
 * The timer has gone off, self-destruct
 */
static void nft_nattype_timer_timeout(struct timer_list *t)
{
	struct nft_nattype_item *nte = from_timer(nte, t, timeout);

	/* netfilter NATTYPE
	 * The race with list deletion is solved by ensuring
	 * that either this code or the list deletion code
	 * but not both will remove the oject.
	 */
	nft_nattype_nte_debug_print(nte, "timeout");
	spin_lock_bh(&nattype_lock);
	list_del(&nte->list);
	memset(nte, 0, sizeof(struct nft_nattype_item));
	spin_unlock_bh(&nattype_lock);
	nft_nattype_free(nte);
}

/* netfilter NATTYPE nft_nattype_packet_in_match()
 * Ingress packet, try to match with this nattype entry.
 */
static bool nft_nattype_packet_in_match(const struct nft_nattype_item *nte,
					struct sk_buff *skb,
					const struct nf_nattype_info *info)
{
	const struct iphdr *iph = ip_hdr(skb);
	u16 dst_port = 0;

	/* If the protocols are not the same, no sense in looking
	 * further.
	 */
	if (nte->proto != iph->protocol) {
		DEBUGP("%s: protocol failed: nte proto:", __func__);
		DEBUGP(" %d, packet proto: %d\n", nte->proto, iph->protocol);
		return false;
	}

	/* In ADDRESS_RESTRICT, the egress destination must match the source
	 * of this ingress packet.
	 */
	if (info->type == NFT_TYPE_ADDRESS_RESTRICTED) {
		if (nte->dest_addr != iph->saddr) {
			DEBUGP("%s: dest/src check", __func__);
			DEBUGP(" failed: dest_addr: %pI4, src dest: %pI4\n",
			       &nte->dest_addr, &iph->saddr);
			return false;
		}
	}

	/* Obtain the destination port value for TCP or UDP.  The nattype
	 * entries are stored in native (not host).
	 */
	if (iph->protocol == IPPROTO_TCP) {
		struct tcphdr _tcph;
		struct tcphdr *tcph;

		tcph = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph),
					  &_tcph);
		if (!tcph)
			return false;
		dst_port = tcph->dest;
	} else if (iph->protocol == IPPROTO_UDP) {
		struct udphdr _udph;
		struct udphdr *udph;

		udph = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_udph),
					  &_udph);
		if (!udph)
			return false;
		dst_port = udph->dest;
	}

	/* Our NAT port must match the ingress packet's
	 * destination packet.
	 */
	if (nte->nat_port != dst_port) {
		DEBUGP("%s fail: ", __func__);
		DEBUGP(" nat port: %d,dest_port: %d\n", ntohs(nte->nat_port),
		       ntohs(dst_port));
		return false;
	}

	/* In either EI or AR mode, the ingress packet's src port
	 * can be anything.
	 */
	nft_nattype_nte_debug_print(nte, "INGRESS MATCH");
	return true;
}

/* netfilter NATTYPE nattype_compare
 * Compare two entries, return true if relevant fields are the same.
 */
static bool nft_nattype_compare(struct nft_nattype_item *n1,
				struct nft_nattype_item *n2,
				const struct nf_nattype_info *info)
{
	/* netfilter NATTYPE Protocol
	 * compare.
	 */
	if (n1->proto != n2->proto) {
		DEBUGP("%s: protocol mismatch: %d:%d\n", __func__, n1->proto,
		       n2->proto);
		return false;
	}

	/* netfilter NATTYPE LAN Source compare.
	 * Since we always keep min/max values the same,
	 * just compare the min values.
	 */
	if (n1->range.min_addr.ip != n2->range.min_addr.ip) {
		DEBUGP("%s: r.min_addr.ip mismatch: %pI4:%pI4\n", __func__,
		       &n1->range.min_addr.ip, &n2->range.min_addr.ip);
		return false;
	}

	if (n1->range.min_proto.all != n2->range.min_proto.all) {
		DEBUGP("%s: r.min mismatch: %d:%d\n", __func__,
		       ntohs(n1->range.min_proto.all),
		       ntohs(n2->range.min_proto.all));
		return false;
	}

	/* netfilter NATTYPE
	 * NAT port
	 */
	if (n1->nat_port != n2->nat_port) {
		DEBUGP("%s: nat_port mistmatch: %d:%d\n", __func__,
		       ntohs(n1->nat_port), ntohs(n2->nat_port));
		return false;
	}

	if (n1->dest_addr != n2->dest_addr) {
		DEBUGP("%s: dest_addr mismatch: %pI4:%pI4\n", __func__,
		       &n1->dest_addr, &n2->dest_addr);
		return false;
	}

	if (n1->dest_port != n2->dest_port) {
		DEBUGP("%s: dest_port mismatch: %d:%d\n", __func__,
		       ntohs(n1->dest_port), ntohs(n2->dest_port));
		return false;
	}
	/* netfilter NATTYPE Destination compare
	 * Destination Comapre for Address Restricted Cone NAT.
	 */
	if (info->type == NFT_TYPE_ADDRESS_RESTRICTED &&
	    n1->dest_addr != n2->dest_addr) {
		DEBUGP("%s: dest_addr mismatch: %pI4:%pI4\n", __func__,
		       &n1->dest_addr, &n2->dest_addr);
		return false;
	}

	return true;
}

/* netfilter NATTYPE nattype_nat()
 * Ingress packet on PRE_ROUTING hook, find match, update conntrack
 * to allow
 */
static int nft_nattype_nat(const struct nft_expr *expr,
			   const struct nft_pktinfo *pkt)
{
	const struct nft_nattype *priv = nft_expr_priv(expr);
	const struct nf_nattype_info *info = &priv->info;
	struct sk_buff *skb = pkt->skb;
	struct nft_nattype_item *nte;

	if (pkt->state->hook != NF_INET_PRE_ROUTING)
		return NFT_BREAK;
	spin_lock_bh(&nattype_lock);
	list_for_each_entry(nte, &nattype_list, list) {
		struct nf_conn *ct;
		enum ip_conntrack_info ctinfo;
		struct nf_nat_range2 newrange;
		unsigned int ret;

		if (!nft_nattype_packet_in_match(nte, skb, info))
			continue;

		/* Copy the LAN source data into the ingress' pacekts
		 * conntrack in the reply direction.
		 */
		newrange = nte->range;
		spin_unlock_bh(&nattype_lock);

		/* netfilter NATTYPE Find the
		 * ingress packet's conntrack.
		 */
		ct = nf_ct_get(skb, &ctinfo);
		if (!ct) {
			DEBUGP("ingress packet conntrack not found\n");
			return XT_CONTINUE;
		}

		/* netfilter
		 * Refresh the timer, if we fail, break
		 * out and forward fail as though we never
		 * found the entry.
		 */
		if (!nattype_refresh_timer((unsigned long)nte,
					   nfct_time_stamp +
						   nte->timeout_value))
			break;

		/* netfilter
		 * Expand the ingress conntrack to include the reply as source
		 */
		DEBUGP("Expand ingress conntrack=%p, type=%d, src[%pI4:%d]\n",
		       ct, ctinfo, &newrange.min_addr.ip,
		       ntohs(newrange.min_proto.all));
		ct->nattype_entry = (unsigned long)nte;
		ret = nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
		DEBUGP("Expand returned: %d\n", ret);
		return ret;
	}
	spin_unlock_bh(&nattype_lock);
	return NFT_BREAK;
}

/* netfilter NATTYPE nattype_forward()
 * Ingress and Egress packet forwarding hook
 */
static int nft_nattype_forward(const struct nft_expr *expr,
			       const struct nft_pktinfo *pkt)
{
	const struct nft_nattype *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	const struct iphdr *iph = ip_hdr(skb);
	void *protoh = (void *)iph + iph->ihl * 4;
	struct nft_nattype_item *nte;
	struct nft_nattype_item *nte2;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_nattype_info *info = &priv->info;
	u16 nat_port;
	enum ip_conntrack_dir dir;
	unsigned long timeout_value;

	if (pkt->state->hook != NF_INET_POST_ROUTING)
		return NFT_BREAK;

	/* netfilter
	 * Egress packet, create a new rule in our list.  If conntrack does
	 * not have an entry, skip this packet.
	 */
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NFT_BREAK;

	/* netfilter
	 * Ingress packet, refresh the timer if we find an entry.
	 */
	if (info->mode == NFT_MODE_FORWARD_IN) {
		spin_lock_bh(&nattype_lock);
		list_for_each_entry(nte, &nattype_list, list) {
			/* netfilter NATTYPE
			 * Compare the ingress packet with the existing
			 * entries looking for a match.
			 */
			if (!nft_nattype_packet_in_match(nte, skb, info))
				continue;

			spin_unlock_bh(&nattype_lock);
			/* netfilter NATTYPE
			 * Refresh the timer, if we fail, break
			 * out and forward fail as though we never
			 * found the entry.
			 */
			if (!nattype_refresh_timer((unsigned long)nte,
						   ct->timeout))
				break;

			/* netfilter NATTYPE
			 * The entry is found and refreshed, the
			 * entry values should not change so print
			 * them outside the lock.
			 */
			nft_nattype_nte_debug_print(nte, "refresh");
			DEBUGP("FORWARD_IN_ACCEPT\n");
			return NF_ACCEPT;
		}
		spin_unlock_bh(&nattype_lock);
		DEBUGP("FORWARD_IN_FAIL\n");
		return NFT_BREAK;
	}

	dir = CTINFO2DIR(ctinfo);

	nat_port = ct->tuplehash[!dir].tuple.dst.u.all;

	/* netfilter NATTYPE
	 * Allocate a new entry
	 */
	nte = kzalloc(sizeof(*nte), GFP_ATOMIC | __GFP_NOWARN);
	if (!nte) {
		DEBUGP("kernel malloc fail\n");
		return NFT_BREAK;
	}

	INIT_LIST_HEAD(&nte->list);
	nte->proto = iph->protocol;
	nte->nat_port = nat_port;
	nte->dest_addr = iph->daddr;
	nte->range.min_addr.ip = iph->saddr;
	nte->range.max_addr.ip = nte->range.min_addr.ip;

	if (iph->protocol == IPPROTO_TCP) {
		nte->range.min_proto.tcp.port =
			((struct tcphdr *)protoh)->source;
		nte->range.max_proto.tcp.port = nte->range.min_proto.tcp.port;
		nte->dest_port = ((struct tcphdr *)protoh)->dest;
	} else if (iph->protocol == IPPROTO_UDP) {
		nte->range.min_proto.udp.port =
			((struct udphdr *)protoh)->source;
		nte->range.max_proto.udp.port = nte->range.min_proto.udp.port;
		nte->dest_port = ((struct udphdr *)protoh)->dest;
	}
	nte->range.flags =
		(NF_NAT_RANGE_MAP_IPS | NF_NAT_RANGE_PROTO_SPECIFIED);

	/* netfilter NATTYPE
	 * Initialize the self-destruct timer.
	 */
	timer_setup(&nte->timeout, nft_nattype_timer_timeout, 0);

	/* netfilter NATTYPE
	 * We have created the new nte; however, it might not be unique.
	 * Search the list for a matching entry.  If found, throw away
	 * the new entry and refresh the old.  If not found, atomically
	 * insert the new entry on the list.
	 */
	spin_lock_bh(&nattype_lock);
	list_for_each_entry(nte2, &nattype_list, list) {
		if (!nft_nattype_compare(nte, nte2, info))
			continue;
		spin_unlock_bh(&nattype_lock);
		/* netfilter NATTYPE
		 * If we can not refresh this entry, insert our new
		 * entry as this one is timed out and will be removed
		 * from the list shortly.
		 */
		timeout_value = nfct_time_stamp + nte2->timeout_value;
		if (!nattype_refresh_timer((unsigned long)nte2, timeout_value))
			break;

		/* netfilter NATTYPE
		 * Found and refreshed an existing entry.  Its values
		 * do not change so print the values outside of the lock.
		 *
		 * Free up the new entry.
		 */
		nft_nattype_nte_debug_print(nte2, "refresh");
		nft_nattype_free(nte);
		return NFT_BREAK;
	}

	/* netfilter NATTYPE
	 * Add the new entry to the list.
	 */
	nte->timeout_value = ct->timeout;
	nte->timeout.expires = ct->timeout + jiffies;
	add_timer(&nte->timeout);
	list_add(&nte->list, &nattype_list);
	ct->nattype_entry = (unsigned long)nte;
	nte->nattype_cookie = NATTYPE_COOKIE;
	spin_unlock_bh(&nattype_lock);
	nft_nattype_nte_debug_print(nte, "ADD");
	return NFT_BREAK;
}

static void nft_nattype_eval(const struct nft_expr *expr, struct nft_regs *regs,
			     const struct nft_pktinfo *pkt)
{
	const struct nft_nattype *priv = nft_expr_priv(expr);
	const struct nf_nattype_info *info = &priv->info;
	struct sk_buff *skb = pkt->skb;

	const struct iphdr *iph = ip_hdr(skb);

	/* netfilter NATTYPE
	 * The default behavior for Linux is PORT and ADDRESS restricted. So
	 * we do not need to create rules/entries if we are in that mode.
	 */
	if (info->type == NFT_TYPE_PORT_ADDRESS_RESTRICTED) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* netfilter NATTYPE
	 * Check if we have enough data in the skb.
	 */
	if (skb->len < ip_hdrlen(skb)) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* netfilter NATTYPE
	 * We can not perform endpoint filtering on anything but UDP and TCP.
	 */
	if (iph->protocol != IPPROTO_TCP && iph->protocol != IPPROTO_UDP) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* netfilter NATTYPE
	 * Check for LAND attack and ignore.
	 */
	if (iph->daddr == iph->saddr) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* netfilter NATTYPE
	 * Check that we have valid source and destination addresses.
	 */
	if (iph->daddr == (__be32)0 || iph->saddr == (__be32)0) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	DEBUGP("%s: type = %s, mode = %s\n", __func__, types[info->type],
	       modes[info->mode]);

	switch (info->mode) {
	case NFT_MODE_DNAT:
		regs->verdict.code = nft_nattype_nat(expr, pkt);
		return;
	case NFT_MODE_FORWARD_OUT:
	case NFT_MODE_FORWARD_IN:
		regs->verdict.code = nft_nattype_forward(expr, pkt);
		return;
	}
	regs->verdict.code = NFT_BREAK;
}

static int nft_nattype_init(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nlattr *const tb[])
{
	struct nft_nattype *priv = nft_expr_priv(expr);
	u32 mode, type;
	int err;

	err = nft_parse_u32_check(tb[NFTA_NATTYPE_MODE], U8_MAX, &mode);
	if (err < 0)
		return err;

	priv->info.mode = mode;

	err = nft_parse_u32_check(tb[NFTA_NATTYPE_TYPE], U8_MAX, &type);
	if (err < 0)
		return err;

	priv->info.type = type;

	return 0;
}

static int nft_nattype_dump(struct sk_buff *skb, const struct nft_expr *expr,
			    bool reset)
{
	const struct nft_nattype *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_NATTYPE_MODE, htonl(priv->info.mode)))
		return -1;
	if (nla_put_be32(skb, NFTA_NATTYPE_TYPE, htonl(priv->info.type)))
		return -1;

	return 0;
}

static struct nft_expr_type nft_nattype_type;
static const struct nft_expr_ops nft_nattype_op = {
	.eval = nft_nattype_eval,
	.size = NFT_EXPR_SIZE(sizeof(struct nft_nattype)),
	.init = nft_nattype_init,
	.dump = nft_nattype_dump,
	.type = &nft_nattype_type,
	.reduce = NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_nattype_type __read_mostly = {
	.ops = &nft_nattype_op,
	.name = "NATTYPE",
	.owner = THIS_MODULE,
	.policy = nft_nattype_policy,
	.maxattr = NFTA_NATTYPE_MAX,
};

static int __init nft_nattype_module_init(void)
{
	WARN_ON(nattype_refresh_timer);
	RCU_INIT_POINTER(nattype_refresh_timer, nft_nattype_refresh_timer_impl);
	return nft_register_expr(&nft_nattype_type);
}

static void __exit nft_nattype_module_exit(void)
{
	nft_unregister_expr(&nft_nattype_type);
}

module_init(nft_nattype_module_init);
module_exit(nft_nattype_module_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NFT_EXPR("NATTYPE");
