#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "../xdma_mod.h"
#include "user.h"
#include "tag.h"

#define ASIX "asix"
#define DSA_HLEN	4
#define SDSA_HLEN	8

static struct sk_buff *sdsa_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_user_priv  *p = netdev_priv(dev);
	u8 *sdsa_header;

	/*
	* Convert the outermost 802.1q tag to a DSA tag and prepend
	* a DSA ethertype field is the packet is tagged, or insert
	* a DSA ethertype plus DSA tag between the addresses and the
	* current ethertype field if the packet is untagged.
	*/
	if (skb->protocol == htons(ETH_P_8021Q)) {
		if (skb_cow_head(skb, DSA_HLEN) < 0) {
			return NULL;
		}
		skb_push(skb, DSA_HLEN);

		memmove(skb->data, skb->data + DSA_HLEN, 2 * ETH_ALEN);
		//dsa_alloc_etype_header(skb,DSA_HLEN);//20250507 - Wales

		/*
		 * Construct tagged FROM_CPU DSA tag from 802.1q tag.
		 */
		sdsa_header = skb->data + 2 * ETH_ALEN;
		//sdsa_header = dsa_etype_header_pos_tx(skb);//20250507 - Wales
		sdsa_header[0] = (ETH_P_SDSA >> 8) & 0xff;
		sdsa_header[1] = ETH_P_SDSA & 0xff;
		sdsa_header[2] = 0x00;
		sdsa_header[3] = 0x00;
		sdsa_header[4] = 0x60 | ((p->dp->ds->index & 0x3e0) >> 5);
		sdsa_header[5] = (p->dp->index) << 3;
	} else {
		if (skb_cow_head(skb, SDSA_HLEN) < 0) {
			return NULL;
		}
		skb_push(skb, SDSA_HLEN);
		memmove(skb->data, skb->data + SDSA_HLEN, 2 * ETH_ALEN);
		//dsa_alloc_etype_header(skb,DSA_HLEN);//20250507 - Wales
		/*
		 * Construct untagged FROM_CPU DSA tag.
		 */
		sdsa_header = skb->data + 2 * ETH_ALEN;
		//sdsa_header = dsa_etype_header_pos_tx(skb);//20250507 - Wales
		sdsa_header[0] = (ETH_P_SDSA >> 8) & 0xff;
		sdsa_header[1] = ETH_P_SDSA & 0xff;
		sdsa_header[2] = 0x00;
		sdsa_header[3] = 0x00;
		sdsa_header[4] = 0x40 | ((p->dp->ds->index & 0x3e0) >> 5);
		sdsa_header[5] = (p->dp->index) << 3;
		sdsa_header[6] = 0x00;
		sdsa_header[7] = 0x00;
	}

	return skb;
}

static struct sk_buff *sdsa_rcv (struct sk_buff *skb, struct net_device *dev)
{
	u8 *sdsa_header;

	int source_port, source_device;

	if (unlikely(!pskb_may_pull(skb, SDSA_HLEN))) {
		return NULL;
	}

	/*
	 * Skip the two null bytes after the ethertype.
	 */
	sdsa_header = skb->data + 2;
	//sdsa_header = dsa_etype_header_pos_rx(skb);//20250507 - Wales

	/*
	 * Check that frame type is TO_CPU
	 */
	if ((sdsa_header[0] & 0xc0) != 0x00) {
		return NULL;
	}

	/* Determine source device and port */
	source_device = 0;
	source_port = ((sdsa_header[0] & 0x1f) << 5) +
				((sdsa_header[1] & 0xF8) >> 3);

	skb->dev = dsa_conduit_find_user(dev, source_device, source_port);
	if (!skb->dev) {
		return NULL;
	}

	/*
	 * If the 'tagged' bit is set, convert the DSA tag to a 802.1q
	 * tag and delete the ethertype part.  If the 'tagged' bit is
	 * clear, delete the ethertype and the DSA tag parts.
	 */
	if (sdsa_header[0] & 0x20) {
		u8 new_header[4];
		/*
		 * Insert 802.1q ethertype and copy the VLAN-related
		 * fields.
		 */
		new_header[0] = (ETH_P_8021Q >> 8) & 0xff;
		new_header[1] = ETH_P_8021Q & 0xff;
		new_header[2] = sdsa_header[2];
		new_header[3] = sdsa_header[3];

		skb_pull_rcsum(skb, DSA_HLEN);

		/*
		 * Update packet checksum if skb is CHECKSUM_COMPLETE.
		 */
		if (skb->ip_summed == CHECKSUM_COMPLETE) {
			__wsum c = skb->csum;
			c = csum_add(c, csum_partial(new_header + 2, 2, 0));
			c = csum_sub(c, csum_partial(sdsa_header + 2, 2, 0));
			skb->csum = c;
		}

		memcpy(sdsa_header, new_header, DSA_HLEN);

		memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - DSA_HLEN,
			2 * ETH_ALEN);
	} else {
		/*
		 * Remove DSA tag and update checksum.
		 */
		skb_pull_rcsum(skb, SDSA_HLEN);
		memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - SDSA_HLEN,
			2 * ETH_ALEN);
	}

	skb->offload_fwd_mark = 1;
	return skb;
}

static const struct dsa_device_ops sdsa_netdev_ops = {
	.name         = ASIX,
	.proto        = DSA_TAG_PROTO_SDSA,	
	.xmit         = sdsa_xmit,
	.rcv          = sdsa_rcv,

};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_SDSA, ASIX);
module_dsa_tag_driver(sdsa_netdev_ops);
