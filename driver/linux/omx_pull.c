#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "omx_common.h"
#include "omx_hal.h"

struct mpoe_pull_handle {
	struct mpoe_endpoint * endpoint;
	struct list_head endpoint_pull_handles;
	uint32_t idr_index;

	spinlock_t lock;

	/*
	 * Masks of frames missing (not received at all)
	 * and transferring (received but not copied yet)
	 *
	 * handle is done when frame_transferring = frame_missing = 0
	 * handle is being used when frame_transferring != frame_missing
	 */
	uint32_t frame_missing;
	uint32_t frame_transferring;
	/* FIXME: need a frame window for multiple pull request */

	/* FIXME: need a refcount in the endpoint and a waitqueue? */
};

/*
 * Notes about locking:
 *
 * A reference is hold on the endpoint while using a pull handle:
 * - when manipulating its internal fields
 *   (by taking the endpoint reference as long as we hold the handle lock)
 * - when copying data corresponding to the handle
 *   (the endpoint reference is hold without taking the handle lock)
 */

/******************************
 * Per-endpoint pull handles management
 */

int
mpoe_endpoint_pull_handles_init(struct mpoe_endpoint * endpoint)
{
	spin_lock_init(&endpoint->pull_handle_lock);
	idr_init(&endpoint->pull_handle_idr);
	INIT_LIST_HEAD(&endpoint->pull_handle_list);

	return 0;
}

void
mpoe_endpoint_pull_handles_exit(struct mpoe_endpoint * endpoint)
{
	struct mpoe_pull_handle * handle, * next;

	spin_lock(&endpoint->pull_handle_lock);

	/* release all pull handles of endpoint */
	list_for_each_entry_safe(handle, next,
				 &endpoint->pull_handle_list,
				 endpoint_pull_handles) {
		list_del(&handle->endpoint_pull_handles);
		idr_remove(&endpoint->pull_handle_idr, handle->idr_index);
		kfree(handle);
	}

	spin_unlock(&endpoint->pull_handle_lock);
}

/******************************
 * Endpoint pull-magic management
 */

#define MPOE_ENDPOINT_PULL_MAGIC_XOR 0x22111867
#define MPOE_ENDPOINT_PULL_MAGIC_SHIFT 13

static inline uint32_t
mpoe_endpoint_pull_magic(struct mpoe_endpoint * endpoint)
{
	uint32_t magic;

	magic = (((uint32_t)endpoint->endpoint_index) << MPOE_ENDPOINT_PULL_MAGIC_SHIFT)
		^ MPOE_ENDPOINT_PULL_MAGIC_XOR;

	return magic;
}

static inline struct mpoe_endpoint *
mpoe_endpoint_acquire_by_pull_magic(struct mpoe_iface * iface, uint32_t magic)
{
	uint32_t full_index;
	uint8_t index;

	full_index = (magic ^ MPOE_ENDPOINT_PULL_MAGIC_XOR) >> MPOE_ENDPOINT_PULL_MAGIC_SHIFT;
	if (unlikely(full_index & (~0xff)))
		/* index does not fit in 8 bits, drop the packet */
		return NULL;
	index = full_index;

	return mpoe_endpoint_acquire_by_iface_index(iface, index);
}

/******************************
 * Per-endpoint pull handles create/find/...
 */

/*
 * Create a pull handle and return it as acquired,
 * with a reference on the endpoint
 */
static inline struct mpoe_pull_handle *
mpoe_pull_handle_create(struct mpoe_endpoint * endpoint)
{
	struct mpoe_pull_handle * handle;
	int err;

	/* take a reference on the endpoint since we will return the pull_handle as acquired */
	err = mpoe_endpoint_acquire(endpoint);
	if (unlikely(err < 0))
		goto out;

	/* alloc the pull handle */
	handle = kmalloc(sizeof(struct mpoe_pull_handle), GFP_KERNEL);
	if (unlikely(!handle)) {
		printk(KERN_INFO "MPoE: Failed to allocate a pull handle\n");
		goto out_with_endpoint;
	}

	/* while failed, realloc and retry */
 idr_try_alloc:
	err = idr_pre_get(&endpoint->pull_handle_idr, GFP_KERNEL);
	if (unlikely(!err)) {
		printk(KERN_ERR "MPoE: Failed to allocate idr space for pull handles\n");
		err = -ENOMEM; /* unused for now */
		goto out_with_endpoint;
	}

	spin_lock(&endpoint->pull_handle_lock);

	err = idr_get_new(&endpoint->pull_handle_idr, handle, &handle->idr_index);
	if (unlikely(err == -EAGAIN)) {
		spin_unlock(&endpoint->pull_handle_lock);
		printk("mpoe_pull_handle_create try again\n");
		goto idr_try_alloc;
	}

	/* we are good now, finish filling the handle */
	spin_lock_init(&handle->lock);
	handle->endpoint = endpoint;
	handle->frame_missing = 0;
	handle->frame_transferring = 0;
	list_add_tail(&handle->endpoint_pull_handles,
		      &endpoint->pull_handle_list);

	/* acquire the handle */
	spin_lock(&handle->lock);

	spin_unlock(&endpoint->pull_handle_lock);

	printk("created and acquired pull handle %p\n", handle);
	return handle;

 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return NULL;
}

/*
 * Acquire a pull handle and the corresponding endpoint
 * given by an pull magic and a wire handle
 */
static inline struct mpoe_pull_handle *
mpoe_pull_handle_acquire_by_wire(struct mpoe_iface * iface,
				 uint32_t magic, uint32_t wire_handle)
{
	struct mpoe_pull_handle * handle;
	struct mpoe_endpoint * endpoint;

	endpoint = mpoe_endpoint_acquire_by_pull_magic(iface, magic);
	if (unlikely(!endpoint))
		return NULL;

	spin_lock(&endpoint->pull_handle_lock);
	handle = idr_find(&endpoint->pull_handle_idr, wire_handle);

	/* acquire the handle */
	spin_lock(&handle->lock);

	spin_unlock(&endpoint->pull_handle_lock);

	printk("acquired pull handle %p\n", handle);
	return handle;
}

/*
 * Reacquire a pull handle.
 *
 * A reference is still hold on the endpoint.
 */
static inline void
mpoe_pull_handle_reacquire(struct mpoe_pull_handle * handle)
{
	/* acquire the handle */
	spin_lock(&handle->lock);

	printk("reacquired pull handle %p\n", handle);
}

/*
 * Takes a locked pull handle and unlocked it if it is not done yet,
 * or destory it if it is done.
 */
static inline void
mpoe_pull_handle_release(struct mpoe_pull_handle * handle)
{
	struct mpoe_endpoint * endpoint = handle->endpoint;

	printk("releasing pull handle %p\n", handle);

	/* FIXME: add likely/unlikely */
	if (handle->frame_transferring != handle->frame_missing) {
		/* some transfer are pending,
		 * release the handle but keep the reference on the endpoint
		 * since it will be reacquired later
		 */
		spin_unlock(&handle->lock);

		printk("some frames and being transferred, just release the handle\n");

	} else if (handle->frame_transferring != 0) {
		/* no transfer pending but frames are missing,
		 * release the handle and the endpoint
		 */
		spin_unlock(&handle->lock);

		/* release the endpoint */
		mpoe_endpoint_release(endpoint);

		printk("some frames are missing, release the handle and the endpoint\n");

	} else {
		/* transfer is done,
		 * destroy the handle and release the endpoint */

		/* FIXME: notify recv_large done to the application */
		/* FIXME: if multiple pull requests, start the next one */

		/* destroy the handle */
		spin_lock(&endpoint->pull_handle_lock);
		list_del(&handle->endpoint_pull_handles);
		idr_remove(&endpoint->pull_handle_idr, handle->idr_index);
		kfree(handle);
		spin_unlock(&endpoint->pull_handle_lock);

		/* release the endpoint */
		mpoe_endpoint_release(endpoint);

		printk("frame are all done, destroy the handle and release the endpoint\n");

	}
}

/******************************
 * Pull-related networking
 */

int
mpoe_send_pull(struct mpoe_endpoint * endpoint,
	       void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_pull cmd;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct mpoe_pull_handle * handle;
	struct mpoe_pkt_pull_request * pull;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "MPoE: Failed to read send pull cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	handle = mpoe_pull_handle_create(endpoint);
	if (unlikely(!handle)) {
		printk(KERN_INFO "MPoE: Failed to allocate a pull handle\n");
		ret = -ENOMEM;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   /* pad to ETH_ZLEN */
			   max_t(unsigned long, sizeof(*mh), ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "MPoE: Failed to create pull skb\n");
		ret = -ENOMEM;
		goto out_with_handle;
	}

	/* locate headers */
	mh = mpoe_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	mpoe_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

	/* fill mpoe header */
	pull = &mh->body.pull;
	pull->src_endpoint = endpoint->endpoint_index;
	pull->dst_endpoint = cmd.dest_endpoint;
	pull->ptype = MPOE_PKT_TYPE_PULL;
	pull->length = cmd.length;
	pull->puller_rdma_id = cmd.local_rdma_id;
	pull->puller_offset = cmd.local_offset;
	pull->pulled_rdma_id = cmd.remote_rdma_id;
	pull->pulled_offset = cmd.remote_offset;
	pull->src_pull_handle = handle->idr_index;
	pull->src_magic = mpoe_endpoint_pull_magic(endpoint);

	mpoe_send_dprintk(eh, "PULL handle %lx magic %lx length %ld",
			  (unsigned long) pull->src_pull_handle,
			  (unsigned long) pull->src_magic,
			  (unsigned long) pull->length);

	/* mark the frames as missing and release the handle */
	handle->frame_missing = 1;
	handle->frame_transferring = 1;
	mpoe_pull_handle_release(handle);

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a pull message from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_handle:
	mpoe_pull_handle_release(handle);
 out:
	return ret;
}

static inline int
mpoe_pull_reply_append_user_region_segment(struct sk_buff *skb,
					   struct mpoe_user_region_segment *seg)
{
	return -ENOSYS;
}

int
mpoe_recv_pull(struct mpoe_iface * iface,
	       struct mpoe_hdr * pull_mh,
	       struct sk_buff * orig_skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *pull_eh = &pull_mh->head.eth;
	struct mpoe_pkt_pull_request *pull_request = &pull_mh->body.pull;
	struct mpoe_pkt_pull_reply *pull_reply;
	struct sk_buff *skb;
	struct mpoe_hdr *reply_mh;
	struct ethhdr *reply_eh;
	struct net_device * ifp = iface->eth_ifp;
	struct mpoe_user_region *region;
	uint32_t rdma_id, queued;
//	uint32_t rdma_id, length, queued, iseg;
	int err = 0;

	/* get the destination endpoint */
	endpoint = mpoe_endpoint_acquire_by_iface_index(iface, pull_request->dst_endpoint);
	if (unlikely(!endpoint)) {
		mpoe_drop_dprintk(pull_eh, "PULL packet for unknown endpoint %d",
				  pull_request->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   /* only allocate space for the header now,
			    * we'll attach pages and pad to ETH_ZLEN later
			    */
			   sizeof(*reply_mh));
	if (unlikely(skb == NULL)) {
		mpoe_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply skb");
		err = -ENOMEM;
		goto out_with_endpoint;
	}

	mpoe_recv_dprintk(pull_eh, "PULL handle %lx magic %lx length %ld",
			  (unsigned long) pull_request->src_pull_handle,
			  (unsigned long) pull_request->src_magic,
			  (unsigned long) pull_request->length);

	/* locate headers */
	reply_mh = mpoe_hdr(skb);
	reply_eh = &reply_mh->head.eth;

	/* fill ethernet header */
	memcpy(reply_eh->h_source, ifp->dev_addr, sizeof (reply_eh->h_source));
	reply_eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);
	/* get the destination address */
	memcpy(reply_eh->h_dest, pull_eh->h_source, sizeof(reply_eh->h_dest));

	/* fill mpoe header */
	pull_reply = &reply_mh->body.pull_reply;
	pull_reply->puller_rdma_id = pull_request->puller_rdma_id;
	pull_reply->puller_offset = pull_request->puller_offset;
	pull_reply->ptype = MPOE_PKT_TYPE_PULL_REPLY;
	pull_reply->dst_pull_handle = pull_request->src_pull_handle;
	pull_reply->dst_magic = pull_request->src_magic;

	mpoe_send_dprintk(reply_eh, "PULL REPLY handle %ld magic %ld",
			  (unsigned long) pull_reply->dst_pull_handle,
			  (unsigned long) pull_reply->dst_magic);

	/* get the rdma window */
	rdma_id = pull_request->pulled_rdma_id;
	if (unlikely(rdma_id >= MPOE_USER_REGION_MAX)) {
		printk(KERN_ERR "MPoE: got pull request for invalid window %d\n", rdma_id);
		/* FIXME: send nack */
		goto out_with_skb;
	}
	spin_lock(&endpoint->user_regions_lock);
	region = endpoint->user_regions[rdma_id];

	/* append segment pages */
	queued = 0;
#if 0
	for(iseg = 0;
	    iseg < region->nr_segments && queued < pull_request->length;
	    iseg++) {
		struct mpoe_user_region_segment *segment = &region->segments[iseg];
		uint32_t append;
		append = mpoe_pull_reply_append_user_region_segment(skb, segment);
		if (unlikely(append < 0)) {
			printk(KERN_ERR "MPoE: failed to queue segment to skb, error %d\n", append);
			/* FIXME: release pages */
			goto out_with_region;
		}
		queued += append;
	}
#endif
	spin_unlock(&endpoint->user_regions_lock);

	pull_reply->length = queued;

 	if (unlikely(skb->len < ETH_ZLEN)) {
		/* pad to ETH_ZLEN */
		err = mpoe_skb_pad(skb, ETH_ZLEN);
		if (err)
			/* skb has been freed in skb_pad */
			/* FIXME: release region */
			goto out_with_endpoint;
		skb->len = ETH_ZLEN;
	}

	dev_queue_xmit(skb);

	mpoe_endpoint_release(endpoint);

//	printk(KERN_INFO "MPoE: sent a pull reply from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

#if 0
 out_with_region:
	spin_unlock(&endpoint->user_regions_lock);
#endif
 out_with_skb:
	dev_kfree_skb(skb);
 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return err;
}

int
mpoe_recv_pull_reply(struct mpoe_iface * iface,
		     struct mpoe_hdr * mh,
		     struct sk_buff * skb)
{
	struct mpoe_pkt_pull_reply *pull_reply = &mh->body.pull_reply;
	struct mpoe_pull_handle * handle;
	int err = 0;

	mpoe_recv_dprintk(&mh->head.eth, "PULL REPLY handle %ld magic %ld",
			  (unsigned long) pull_reply->dst_pull_handle,
			  (unsigned long) pull_reply->dst_magic);

	/* FIXME */

	handle = mpoe_pull_handle_acquire_by_wire(iface, pull_reply->dst_magic,
						  pull_reply->dst_pull_handle);
	if (unlikely(!handle)) {
		mpoe_drop_dprintk(&mh->head.eth, "PULL REPLY packet unknown handle %d magic %d",
				  pull_reply->dst_pull_handle, pull_reply->dst_magic);
		err = -EINVAL;
		goto out;
	}

	/* FIXME: store the sender mac in the handle and check it ? */

	handle->frame_missing = 0;

	/* release the handle during the copy */
	mpoe_pull_handle_release(handle);

	/* FIXME: copy stuff */

	/* FIXME: release instead of destroy if not done */
	mpoe_pull_handle_reacquire(handle);

	handle->frame_transferring = 0;

	mpoe_pull_handle_release(handle);

	return 0;

 out:
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
