// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * RMNET Data virtual network driver
 */

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/icmp.h>
#include <linux/icmpv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <net/pkt_sched.h>
#include "rmnet_config.h"
#include "rmnet_handlers.h"
#include "rmnet_private.h"
#include "rmnet_map.h"
#include "rmnet_vnd.h"
#include <linux/rmnet_stats.h>

/* RX/TX Fixup */

#define RMNET_CSUM_STATS_COUNT 11

/* ---------- Packet classification ---------- */

enum rmnet_proto {
	RMNET_PROTO_OTHER = 0,
	RMNET_PROTO_ICMP,
	RMNET_PROTO_ICMPV6,
	RMNET_PROTO_DNS,
};

/* rmnet_parse_ip_header() - Determine upper-layer protocol and transport offset
 * in a single pass over the IP header.
 *
 * @skb:   SKB with skb->data pointing at the raw IP header.
 * @proto: Filled with the upper-layer protocol number on success.
 * @toff:  Filled with the byte offset of the transport header from skb->data.
 *
 * For IPv4 validates ihl, checks for non-first fragments, and returns
 * iph->protocol.  For IPv6 walks the extension-header chain once (hop-by-hop,
 * routing, destination, fragment, AH) to find the upper-layer header.
 *
 * Returns 0 on success; -1 if the packet is too short, malformed, or a
 * non-first fragment (transport header unavailable).
 */
static int rmnet_parse_ip_header(struct sk_buff *skb, u8 *proto, u16 *toff)
{
	u8 ver;

	if (skb->len < 1)
		return -1;

	ver = skb->data[0] & 0xF0;

	if (ver == RMNET_IP_VERSION_4) {
		struct iphdr *iph;
		u16 hdr_len;

		if (skb->len < sizeof(*iph))
			return -1;
		iph     = (struct iphdr *)skb->data;
		hdr_len = iph->ihl * 4;
		/* ihl must be >= 5 (20 bytes) and fit within the packet */
		if (iph->ihl < 5 || hdr_len > skb->len)
			return -1;
		/* Non-first fragment: transport header unavailable */
		if (iph->frag_off & htons(IP_OFFSET))
			return -1;
		*proto = iph->protocol;
		*toff  = hdr_len;
		return 0;

	} else if (ver == RMNET_IP_VERSION_6) {
		struct ipv6hdr *ip6h;
		u8  nexthdr;
		u16 offset;

		if (skb->len < sizeof(*ip6h))
			return -1;
		ip6h    = (struct ipv6hdr *)skb->data;
		nexthdr = ip6h->nexthdr;
		offset  = sizeof(*ip6h);

		while (offset < skb->len) {
			switch (nexthdr) {
			case NEXTHDR_HOP:
			case NEXTHDR_ROUTING:
			case NEXTHDR_DEST: {
				struct ipv6_opt_hdr *opth;

				if (offset + sizeof(*opth) > skb->len)
					return -1;
				opth    = (struct ipv6_opt_hdr *)(skb->data + offset);
				nexthdr = opth->nexthdr;
				offset += (opth->hdrlen + 1) * 8;
				break;
			}
			case NEXTHDR_FRAGMENT: {
				struct frag_hdr *fragh;

				if (offset + sizeof(*fragh) > skb->len)
					return -1;
				fragh = (struct frag_hdr *)(skb->data + offset);
				/* Non-first fragment: transport header unavailable */
				if (fragh->frag_off & htons(IP6_OFFSET))
					return -1;
				nexthdr = fragh->nexthdr;
				offset += sizeof(*fragh);
				break;
			}
			case NEXTHDR_AUTH: {
				struct ip_auth_hdr *ah;

				if (offset + sizeof(*ah) > skb->len)
					return -1;
				ah      = (struct ip_auth_hdr *)(skb->data + offset);
				nexthdr = ah->nexthdr;
				offset += (ah->hdrlen + 2) * 4;
				break;
			}
			default:
				/* Upper-layer protocol or unknown extension */
				*proto = nexthdr;
				*toff  = offset;
				return 0;
			}
		}
		return -1;
	}

	return -1;
}

/* rmnet_classify_packet() - Classify skb as ICMP, ICMPv6, DNS or other.
 *
 * Called with skb->data pointing at the raw IP header (before MAP
 * encapsulation on TX; after MAP decapsulation on RX).
 */
static enum rmnet_proto rmnet_classify_packet(struct sk_buff *skb)
{
	u16 src_port, dst_port, transport_offset;
	u8  ip_proto;

	if (rmnet_parse_ip_header(skb, &ip_proto, &transport_offset))
		return RMNET_PROTO_OTHER;

	if (ip_proto == IPPROTO_ICMP)
		return RMNET_PROTO_ICMP;
	if (ip_proto == IPPROTO_ICMPV6)
		return RMNET_PROTO_ICMPV6;

	if (ip_proto != IPPROTO_UDP && ip_proto != IPPROTO_TCP)
		return RMNET_PROTO_OTHER;

	/* Extract ports */
	if (ip_proto == IPPROTO_UDP) {
		struct udphdr *udph;

		if (transport_offset + sizeof(*udph) > skb->len)
			return RMNET_PROTO_OTHER;
		udph     = (struct udphdr *)(skb->data + transport_offset);
		src_port = ntohs(udph->source);
		dst_port = ntohs(udph->dest);
	} else {
		struct tcphdr *tcph;

		if (transport_offset + sizeof(*tcph) > skb->len)
			return RMNET_PROTO_OTHER;
		tcph     = (struct tcphdr *)(skb->data + transport_offset);
		src_port = ntohs(tcph->source);
		dst_port = ntohs(tcph->dest);
	}

	/* DNS: port 53, UDP or TCP */
	if (src_port == 53 || dst_port == 53)
		return RMNET_PROTO_DNS;

	return RMNET_PROTO_OTHER;
}

/* ---------- Protocol stat updaters ---------- */

/* rmnet_aggregate_proto_pcpu() - Sum per-CPU protocol counters into @totals.
 *
 * Uses seqcount snapshot reads so it is safe to call from process or softirq
 * context without taking any lock.
 */
static void rmnet_aggregate_proto_pcpu(struct rmnet_priv *priv,
				       struct rmnet_vnd_stats *totals)
{
	struct rmnet_pcpu_stats *pcpu_ptr;
	struct rmnet_vnd_stats snapshot;
	unsigned int cpu, start;

	for_each_possible_cpu(cpu) {
		pcpu_ptr = per_cpu_ptr(priv->pcpu_stats, cpu);
		do {
			start    = u64_stats_fetch_begin(&pcpu_ptr->syncp);
			snapshot = pcpu_ptr->stats;
		} while (u64_stats_fetch_retry(&pcpu_ptr->syncp, start));

		totals->rx_icmp_pkts   += snapshot.rx_icmp_pkts;
		totals->rx_icmpv6_pkts += snapshot.rx_icmpv6_pkts;
		totals->tx_icmp_pkts   += snapshot.tx_icmp_pkts;
		totals->tx_icmpv6_pkts += snapshot.tx_icmpv6_pkts;
		totals->rx_dns_pkts    += snapshot.rx_dns_pkts;
		totals->tx_dns_pkts    += snapshot.tx_dns_pkts;
	}
}

static void rmnet_update_rx_proto_stats(struct sk_buff *skb,
					struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);
	enum rmnet_proto proto = rmnet_classify_packet(skb);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	switch (proto) {
	case RMNET_PROTO_ICMP:
		pcpu_ptr->stats.rx_icmp_pkts++;
		break;
	case RMNET_PROTO_ICMPV6:
		pcpu_ptr->stats.rx_icmpv6_pkts++;
		break;
	case RMNET_PROTO_DNS:
		pcpu_ptr->stats.rx_dns_pkts++;
		break;
	default:
		break;
	}
	u64_stats_update_end(&pcpu_ptr->syncp);
}

int rmnet_vnd_classify_tx_proto(struct sk_buff *skb)
{
	return (int)rmnet_classify_packet(skb);
}

void rmnet_vnd_update_tx_proto_stats(int proto, struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	switch (proto) {
	case RMNET_PROTO_ICMP:
		pcpu_ptr->stats.tx_icmp_pkts++;
		break;
	case RMNET_PROTO_ICMPV6:
		pcpu_ptr->stats.tx_icmpv6_pkts++;
		break;
	case RMNET_PROTO_DNS:
		pcpu_ptr->stats.tx_dns_pkts++;
		break;
	default:
		break;
	}
	u64_stats_update_end(&pcpu_ptr->syncp);
}

void rmnet_vnd_rx_fixup(struct sk_buff *skb, struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr;

	pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	pcpu_ptr->stats.rx_pkts++;
	pcpu_ptr->stats.rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_ptr->syncp);

	rmnet_update_rx_proto_stats(skb, dev);
}

void rmnet_vnd_tx_fixup_len(unsigned int len, struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_pcpu_stats *pcpu_ptr;

	pcpu_ptr = this_cpu_ptr(priv->pcpu_stats);

	u64_stats_update_begin(&pcpu_ptr->syncp);
	pcpu_ptr->stats.tx_pkts++;
	pcpu_ptr->stats.tx_bytes += len;
	u64_stats_update_end(&pcpu_ptr->syncp);
}

void rmnet_vnd_tx_fixup(struct sk_buff *skb, struct net_device *dev)
{
	rmnet_vnd_tx_fixup_len(skb->len, dev);
}

/* Network Device Operations */

static netdev_tx_t rmnet_vnd_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct rmnet_priv *priv;

	priv = netdev_priv(dev);
	if (priv->real_dev) {
		rmnet_egress_handler(skb);
	} else {
		this_cpu_inc(priv->pcpu_stats->stats.tx_drops);
		kfree_skb(skb);
	}
	return NETDEV_TX_OK;
}

static int rmnet_vnd_headroom(struct rmnet_port *port)
{
	u32 headroom;

	headroom = sizeof(struct rmnet_map_header);

	if (port->data_format & RMNET_FLAGS_EGRESS_MAP_CKSUMV4)
		headroom += sizeof(struct rmnet_map_ul_csum_header);

	return headroom;
}

static int rmnet_vnd_change_mtu(struct net_device *rmnet_dev, int new_mtu)
{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	struct rmnet_port *port;
	u32 headroom;

	port = rmnet_get_port_rtnl(priv->real_dev);

	headroom = rmnet_vnd_headroom(port);

	if (new_mtu < 0 || new_mtu > RMNET_MAX_PACKET_SIZE ||
	    new_mtu > (priv->real_dev->mtu - headroom))
		return -EINVAL;

	rmnet_dev->mtu = new_mtu;
	return 0;
}

static int rmnet_vnd_get_iflink(const struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);

	return priv->real_dev->ifindex;
}

static int rmnet_vnd_init(struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	int err;

	priv->pcpu_stats = alloc_percpu(struct rmnet_pcpu_stats);
	if (!priv->pcpu_stats)
		return -ENOMEM;

	err = gro_cells_init(&priv->gro_cells, dev);
	if (err) {
		free_percpu(priv->pcpu_stats);
		return err;
	}

	return 0;
}

static void rmnet_vnd_uninit(struct net_device *dev)
{
	struct rmnet_priv *priv = netdev_priv(dev);

	gro_cells_destroy(&priv->gro_cells);
	free_percpu(priv->pcpu_stats);
}

static void rmnet_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *s)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_vnd_stats total_stats = { };
	struct rmnet_pcpu_stats *pcpu_ptr;
	struct rmnet_vnd_stats snapshot;
	unsigned int cpu, start;

	for_each_possible_cpu(cpu) {
		pcpu_ptr = per_cpu_ptr(priv->pcpu_stats, cpu);

		do {
			start = u64_stats_fetch_begin(&pcpu_ptr->syncp);
			snapshot = pcpu_ptr->stats;	/* struct assignment */
		} while (u64_stats_fetch_retry(&pcpu_ptr->syncp, start));

		total_stats.rx_pkts += snapshot.rx_pkts;
		total_stats.rx_bytes += snapshot.rx_bytes;
		total_stats.tx_pkts += snapshot.tx_pkts;
		total_stats.tx_bytes += snapshot.tx_bytes;
		total_stats.tx_drops += snapshot.tx_drops;
	}

	s->rx_packets = total_stats.rx_pkts;
	s->rx_bytes = total_stats.rx_bytes;
	s->tx_packets = total_stats.tx_pkts;
	s->tx_bytes = total_stats.tx_bytes;
	s->tx_dropped = total_stats.tx_drops;
}

static const struct net_device_ops rmnet_vnd_ops = {
	.ndo_start_xmit = rmnet_vnd_start_xmit,
	.ndo_change_mtu = rmnet_vnd_change_mtu,
	.ndo_get_iflink = rmnet_vnd_get_iflink,
	.ndo_add_slave  = rmnet_add_bridge,
	.ndo_del_slave  = rmnet_del_bridge,
	.ndo_init       = rmnet_vnd_init,
	.ndo_uninit     = rmnet_vnd_uninit,
	.ndo_get_stats64 = rmnet_get_stats64,
};

static const char rmnet_gstrings_stats[][ETH_GSTRING_LEN] = {
	"Checksum ok",
	"Bad IPv4 header checksum",
	"Checksum valid bit not set",
	"Checksum validation failed",
	"Checksum error bad buffer",
	"Checksum error bad ip version",
	"Checksum error bad transport",
	"Checksum skipped on ip fragment",
	"Checksum skipped",
	"Checksum computed in software",
	"Checksum computed in hardware",
	"RX ICMP packets",
	"RX ICMPv6 packets",
	"TX ICMP packets",
	"TX ICMPv6 packets",
	"RX DNS packets",
	"TX DNS packets",
};

static void rmnet_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &rmnet_gstrings_stats,
		       sizeof(rmnet_gstrings_stats));
		break;
	}
}

static int rmnet_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rmnet_gstrings_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void rmnet_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_priv_stats *csum_st = &priv->stats;
	struct rmnet_vnd_stats proto_totals = { };

	BUILD_BUG_ON(sizeof(*csum_st) != RMNET_CSUM_STATS_COUNT * sizeof(u64));

	if (!data)
		return;

	/* Fill checksum stats (first RMNET_CSUM_STATS_COUNT entries) */
	memcpy(data, csum_st, RMNET_CSUM_STATS_COUNT * sizeof(u64));

	/* Fill protocol stats (remaining entries) */
	rmnet_aggregate_proto_pcpu(priv, &proto_totals);

	data[RMNET_CSUM_STATS_COUNT + 0] = proto_totals.rx_icmp_pkts;
	data[RMNET_CSUM_STATS_COUNT + 1] = proto_totals.rx_icmpv6_pkts;
	data[RMNET_CSUM_STATS_COUNT + 2] = proto_totals.tx_icmp_pkts;
	data[RMNET_CSUM_STATS_COUNT + 3] = proto_totals.tx_icmpv6_pkts;
	data[RMNET_CSUM_STATS_COUNT + 4] = proto_totals.rx_dns_pkts;
	data[RMNET_CSUM_STATS_COUNT + 5] = proto_totals.tx_dns_pkts;
}

static int rmnet_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_port *port;

	port = rmnet_get_port_rtnl(priv->real_dev);

	memset(kernel_coal, 0, sizeof(*kernel_coal));
	kernel_coal->tx_aggr_max_bytes = port->egress_agg_params.bytes;
	kernel_coal->tx_aggr_max_frames = port->egress_agg_params.count;
	kernel_coal->tx_aggr_time_usecs = div_u64(port->egress_agg_params.time_nsec,
						  NSEC_PER_USEC);

	return 0;
}

static int rmnet_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct rmnet_priv *priv = netdev_priv(dev);
	struct rmnet_port *port;

	port = rmnet_get_port_rtnl(priv->real_dev);

	if (kernel_coal->tx_aggr_max_frames < 1 || kernel_coal->tx_aggr_max_frames > 64)
		return -EINVAL;

	if (kernel_coal->tx_aggr_max_bytes > 32768)
		return -EINVAL;

	rmnet_map_update_ul_agg_config(port, kernel_coal->tx_aggr_max_bytes,
				       kernel_coal->tx_aggr_max_frames,
				       kernel_coal->tx_aggr_time_usecs);

	return 0;
}

static const struct ethtool_ops rmnet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_TX_AGGR,
	.get_coalesce = rmnet_get_coalesce,
	.set_coalesce = rmnet_set_coalesce,
	.get_ethtool_stats = rmnet_get_ethtool_stats,
	.get_strings = rmnet_get_strings,
	.get_sset_count = rmnet_get_sset_count,
};

/* Called by kernel whenever a new rmnet<n> device is created. Sets MTU,
 * flags, ARP type, needed headroom, etc...
 */
void rmnet_vnd_setup(struct net_device *rmnet_dev)
{
	rmnet_dev->netdev_ops = &rmnet_vnd_ops;
	rmnet_dev->mtu = RMNET_DFLT_PACKET_SIZE;
	rmnet_dev->needed_headroom = RMNET_NEEDED_HEADROOM;
	eth_hw_addr_random(rmnet_dev);
	rmnet_dev->tx_queue_len = RMNET_TX_QUEUE_LEN;

	/* Raw IP mode */
	rmnet_dev->header_ops = NULL;  /* No header */
	rmnet_dev->type = ARPHRD_RAWIP;
	rmnet_dev->hard_header_len = 0;
	rmnet_dev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);

	rmnet_dev->needs_free_netdev = true;
	rmnet_dev->ethtool_ops = &rmnet_ethtool_ops;

	rmnet_dev->features |= NETIF_F_LLTX;

	/* This perm addr will be used as interface identifier by IPv6 */
	rmnet_dev->addr_assign_type = NET_ADDR_RANDOM;
	eth_random_addr(rmnet_dev->perm_addr);
}

/* Exposed API */

int rmnet_vnd_newlink(u8 id, struct net_device *rmnet_dev,
		      struct rmnet_port *port,
		      struct net_device *real_dev,
		      struct rmnet_endpoint *ep,
		      struct netlink_ext_ack *extack)

{
	struct rmnet_priv *priv = netdev_priv(rmnet_dev);
	u32 headroom;
	int rc;

	if (rmnet_get_endpoint(port, id)) {
		NL_SET_ERR_MSG_MOD(extack, "MUX ID already exists");
		return -EBUSY;
	}

	rmnet_dev->hw_features = NETIF_F_RXCSUM;
	rmnet_dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	rmnet_dev->hw_features |= NETIF_F_SG;

	priv->real_dev = real_dev;

	headroom = rmnet_vnd_headroom(port);

	if (rmnet_vnd_change_mtu(rmnet_dev, real_dev->mtu - headroom)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid MTU on real dev");
		return -EINVAL;
	}

	rc = register_netdevice(rmnet_dev);
	if (!rc) {
		ep->egress_dev = rmnet_dev;
		ep->mux_id = id;
		port->nr_rmnet_devs++;

		rmnet_dev->rtnl_link_ops = &rmnet_link_ops;

		priv->mux_id = id;

		netdev_dbg(rmnet_dev, "rmnet dev created\n");
	}

	return rc;
}

int rmnet_vnd_dellink(u8 id, struct rmnet_port *port,
		      struct rmnet_endpoint *ep)
{
	if (id >= RMNET_MAX_LOGICAL_EP || !ep->egress_dev)
		return -EINVAL;

	ep->egress_dev = NULL;
	port->nr_rmnet_devs--;
	return 0;
}

int rmnet_vnd_do_flow_control(struct net_device *rmnet_dev, int enable)
{
	netdev_dbg(rmnet_dev, "Setting VND TX queue state to %d\n", enable);
	/* Although we expect similar number of enable/disable
	 * commands, optimize for the disable. That is more
	 * latency sensitive than enable
	 */
	if (unlikely(enable))
		netif_wake_queue(rmnet_dev);
	else
		netif_stop_queue(rmnet_dev);

	return 0;
}

int rmnet_vnd_validate_real_dev_mtu(struct net_device *real_dev)
{
	struct hlist_node *tmp_ep;
	struct rmnet_endpoint *ep;
	struct rmnet_port *port;
	unsigned long bkt_ep;
	u32 headroom;

	port = rmnet_get_port_rtnl(real_dev);

	headroom = rmnet_vnd_headroom(port);

	hash_for_each_safe(port->muxed_ep, bkt_ep, tmp_ep, ep, hlnode) {
		if (ep->egress_dev->mtu > (real_dev->mtu - headroom))
			return -1;
	}

	return 0;
}

int rmnet_vnd_update_dev_mtu(struct rmnet_port *port,
			     struct net_device *real_dev)
{
	struct hlist_node *tmp_ep;
	struct rmnet_endpoint *ep;
	unsigned long bkt_ep;
	u32 headroom;

	headroom = rmnet_vnd_headroom(port);

	hash_for_each_safe(port->muxed_ep, bkt_ep, tmp_ep, ep, hlnode) {
		if (ep->egress_dev->mtu <= (real_dev->mtu - headroom))
			continue;

		if (rmnet_vnd_change_mtu(ep->egress_dev,
					 real_dev->mtu - headroom))
			return -1;
	}

	return 0;
}

/* Exported API */

int rmnet_get_proto_stats(struct net_device *dev,
			  struct rmnet_proto_stats *stats)
{
	struct rmnet_priv *priv;
	struct rmnet_vnd_stats totals = { };

	if (!dev || !stats || dev->netdev_ops != &rmnet_vnd_ops)
		return -EINVAL;

	priv = netdev_priv(dev);
	rmnet_aggregate_proto_pcpu(priv, &totals);

	stats->rx_icmp_pkts   = totals.rx_icmp_pkts;
	stats->rx_icmpv6_pkts = totals.rx_icmpv6_pkts;
	stats->tx_icmp_pkts   = totals.tx_icmp_pkts;
	stats->tx_icmpv6_pkts = totals.tx_icmpv6_pkts;
	stats->rx_dns_pkts    = totals.rx_dns_pkts;
	stats->tx_dns_pkts    = totals.tx_dns_pkts;

	return 0;
}
EXPORT_SYMBOL_GPL(rmnet_get_proto_stats);
