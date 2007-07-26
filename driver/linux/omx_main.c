#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "omx_common.h"

static char * omx_ifnames = NULL;
module_param(omx_ifnames, charp, 0); /* unreadable, since modifiable by the attached sysfs file */

int omx_iface_max = 32;
module_param(omx_iface_max, uint, S_IRUGO);

int omx_endpoint_max = 8;
module_param(omx_endpoint_max, uint, S_IRUGO);

int omx_peer_max = 1024;
module_param(omx_peer_max, uint, S_IRUGO);

static __init int
omx_init(void)
{
	int ret;

	printk(KERN_INFO "Open-MX initializing...\n");

	ret = omx_net_init((const char *) omx_ifnames);
	if (ret < 0)
		goto out;

	ret = omx_dev_init();
	if (ret < 0)
		goto out_with_net;

	printk(KERN_INFO "Open-MX initialized\n");
	return 0;

 out_with_net:
	omx_net_exit();
 out:
	printk(KERN_ERR "Failed to initialize Open-MX\n");
	return ret;
}
module_init(omx_init);

static __exit void
omx_exit(void)
{
	printk(KERN_INFO "Open-MX terminating...\n");
	omx_dev_exit();
	omx_net_exit();
	printk(KERN_INFO "Open-MX terminated\n");
}
module_exit(omx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brice Goglin <Brice.Goglin@inria.fr>");
MODULE_VERSION("0.0");
MODULE_DESCRIPTION("Open-MX: Myrinet Express over generic Ethernet");

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
