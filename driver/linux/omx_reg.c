#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/spinlock.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"

/* FIXME: likely/unlikely */

void
mpoe_endpoint_user_regions_init(struct mpoe_endpoint * endpoint)
{
	memset(endpoint->user_regions, 0, sizeof(endpoint->user_regions));
	spin_lock_init(&endpoint->user_regions_lock);
}

static int
mpoe_register_user_region_segment(struct mpoe_cmd_region_segment * useg,
				  struct mpoe_user_region_segment * segment)
{
	struct page ** pages;
	unsigned offset;
	unsigned long aligned_vaddr;
	unsigned long aligned_len;
	unsigned long nr_pages;
	int ret;

	offset = useg->vaddr & (~PAGE_MASK);
	aligned_vaddr = useg->vaddr & PAGE_MASK;
	aligned_len = PAGE_ALIGN(offset + useg->len);
	nr_pages = aligned_len >> PAGE_SHIFT;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		printk(KERN_ERR "MPoE: Failed to allocate user region segment page array\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = get_user_pages(current, current->mm, aligned_vaddr, nr_pages, 1, 0, pages, NULL);
	if (ret < 0) {
		printk(KERN_ERR "MPoE: get_user_pages failed (error %d)\n", ret);
		goto out_with_pages;
	}
	BUG_ON(ret != nr_pages);

	segment->offset = offset;
	segment->length = useg->len;
	segment->nr_pages = nr_pages;
	segment->pages = pages;

	return 0;

 out_with_pages:
	kfree(pages);
 out:
	return ret;
}

static void
mpoe_deregister_user_region_segment(struct mpoe_user_region_segment * segment)
{
	unsigned long i;
	for(i=0; i<segment->nr_pages; i++)
		put_page(segment->pages[i]);
	kfree(segment->pages);
}

static void
mpoe__deregister_user_region(struct mpoe_user_region * region)
{
	int i;
	for(i=0; i<region->nr_segments; i++)
		mpoe_deregister_user_region_segment(&region->segments[i]);
	kfree(region);
}

int
mpoe_register_user_region(struct mpoe_endpoint * endpoint,
			  void __user * uparam)
{
	struct mpoe_cmd_register_region cmd;
	struct mpoe_user_region * region;
	struct mpoe_cmd_region_segment * usegs;
	int ret, i;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read register region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd.id >= MPOE_USER_REGION_MAX) {
		printk(KERN_ERR "MPoE: Cannot register invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	/* get the list of segments */
	usegs = kmalloc(sizeof(struct mpoe_cmd_region_segment) * cmd.nr_segments,
			GFP_KERNEL);
	if (!usegs) {
		printk(KERN_ERR "MPoE: Failed to allocate segments for user region\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(usegs, (void __user *)(unsigned long) cmd.segments,
			     sizeof(struct mpoe_cmd_region_segment) * cmd.nr_segments);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read register region cmd\n");
		ret = -EFAULT;
		goto out_with_usegs;
	}

	/* allocate the region */
	region = kzalloc(sizeof(struct mpoe_user_region)
			 + cmd.nr_segments * sizeof(struct mpoe_user_region_segment),
			 GFP_KERNEL);
	if (!region) {
		printk(KERN_ERR "MPoE: failed to allocate user region\n");
		ret = -ENOMEM;
		goto out_with_usegs;
	}

	/* keep nr_segments exact so that we may call mpoe__deregister_user_region safely */
	region->nr_segments = 0;

	down_write(&current->mm->mmap_sem);

	for(i=0; i<cmd.nr_segments; i++) {
		ret = mpoe_register_user_region_segment(&usegs[i], &region->segments[i]);
		if (ret < 0) {
			up_write(&current->mm->mmap_sem);
			goto out_with_region;
		}
		region->nr_segments++;
	}

	up_write(&current->mm->mmap_sem);

	spin_lock(&endpoint->user_regions_lock);

	if (endpoint->user_regions[cmd.id]) {
		printk(KERN_ERR "MPoE: Cannot register busy region %d\n", cmd.id);
		ret = -EBUSY;
		spin_unlock(&endpoint->user_regions_lock);
		goto out_with_region;
	}
	endpoint->user_regions[cmd.id] = region;

	spin_unlock(&endpoint->user_regions_lock);

	kfree(usegs);
	return 0;

 out_with_region:
	mpoe__deregister_user_region(region);
 out_with_usegs:
	kfree(usegs);
 out:
	return ret;
}

int
mpoe_deregister_user_region(struct mpoe_endpoint * endpoint,
			    void __user * uparam)
{
	struct mpoe_cmd_deregister_region cmd;
	struct mpoe_user_region * region;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read deregister region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd.id >= MPOE_USER_REGION_MAX) {
		printk(KERN_ERR "MPoE: Cannot deregister invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&endpoint->user_regions_lock);

	region = endpoint->user_regions[cmd.id];
	if (!region) {
		printk(KERN_ERR "MPoE: Cannot register unexisting region %d\n", cmd.id);
		ret = -EINVAL;
		spin_unlock(&endpoint->user_regions_lock);
		goto out;
	}

	mpoe__deregister_user_region(region);
	endpoint->user_regions[cmd.id] = NULL;

	spin_unlock(&endpoint->user_regions_lock);

	return 0;

 out:
	return ret;
}

void
mpoe_endpoint_user_regions_exit(struct mpoe_endpoint * endpoint)
{
	struct mpoe_user_region * region;
	int i;

	for(i=0; i<MPOE_USER_REGION_MAX; i++) {
		region = endpoint->user_regions[i];
		if (!region)
			continue;

		printk(KERN_INFO "MPoE: Forcing deregister of window %d on endpoint %d board %d\n",
		       i, endpoint->endpoint_index, endpoint->iface->index);
		mpoe__deregister_user_region(region);
		endpoint->user_regions[i] = NULL;
	}
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
