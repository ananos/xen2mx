#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "mpoe_common.h"

static char * mpoe_ifnames = NULL;
module_param(mpoe_ifnames, charp, 0); /* unreadable, since modifiable by the attached sysfs file */

int mpoe_iface_max = 32;
module_param(mpoe_iface_max, uint, S_IRUGO);

int mpoe_endpoint_max = 8;
module_param(mpoe_endpoint_max, uint, S_IRUGO);

static __init int
mpoe_init(void)
{
	int ret;

	printk(KERN_INFO "MPoE initializing...\n");

	ret = mpoe_net_init((const char *) mpoe_ifnames);
	if (ret < 0)
		goto out;

	ret = mpoe_dev_init();
	if (ret < 0)
		goto out_with_net;

	printk(KERN_INFO "MPoE initialized\n");
	return 0;

 out_with_net:
	mpoe_net_exit();
 out:
	printk(KERN_ERR "Failed to initialize MPoE\n");
	return ret;
}
module_init(mpoe_init);

static __exit void
mpoe_exit(void)
{
	printk(KERN_INFO "MPoE terminating...\n");
	mpoe_dev_exit();
	mpoe_net_exit();
	printk(KERN_INFO "MPoE terminated\n");
}
module_exit(mpoe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brice Goglin <Brice.Goglin@inria.fr>");
MODULE_VERSION("0.0.1");
MODULE_DESCRIPTION("Ethernet implementation of Message-Passing Over Everything");

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
