#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#ifdef CONFIG_NETDEVICES_LB

struct netdev_lb {
	struct list_head chain;
	struct net_device *netdev;
	const struct netdev_lb_ops *ops;
};

static struct list_head netdevs_to_lb;
static spinlock_t lock;

static struct netdev_lb *netdev_find(struct net_device *netdev)
{
	struct list_head *pos;
	list_for_each(pos, &netdevs_to_lb) {
		struct netdev_lb *lb = list_entry(pos, struct netdev_lb, chain);
		if (netdev == lb->netdev)
			return lb;
	}
	return NULL;
}

void netdev_lb_register(struct net_device *netdev, const struct netdev_lb_ops *ops)
{
	struct netdev_lb *lb;

	spin_lock(&lock);
	lb = netdev_find(netdev);
	if (!lb) {
		lb = kmalloc(sizeof(*lb), GFP_KERNEL);
		lb->netdev = netdev;
		lb->ops = ops;
		list_add(&lb->chain, &netdevs_to_lb);
		printk(KERN_CRIT "netdev_lb_register: regestered (%p)\n", netdev);
	}
	spin_unlock(&lock);

}
EXPORT_SYMBOL(netdev_lb_register);

static void netdev_lb_unregister(struct net_device *netdev)
{
	struct netdev_lb *lb;

	spin_lock(&lock);
	lb = netdev_find(netdev);
	if (lb) {
		list_del(&lb->chain);
		kfree(lb);
	}
	spin_unlock(&lock);
}

unsigned long netdev_lb_move(int from, int to)
{
	struct list_head *pos;
	unsigned long num_moved = 0;

	spin_lock(&lock);

	list_for_each(pos, &netdevs_to_lb) {
		struct netdev_lb *lb = list_entry(pos, struct netdev_lb, chain);
		num_moved += lb->ops->move(lb->netdev, from, to);
	}

	spin_unlock(&lock);

	return num_moved;
}
EXPORT_SYMBOL(netdev_lb_move);

unsigned long netdev_lb_num_buckets(int cpu)
{
	struct list_head *pos;
	unsigned long num = 0;

	spin_lock(&lock);

	list_for_each(pos, &netdevs_to_lb) {
		struct netdev_lb *lb = list_entry(pos, struct netdev_lb, chain);
		num += lb->ops->num_buckets(lb->netdev, cpu);
	}

	spin_unlock(&lock);

	return num;
}
EXPORT_SYMBOL(netdev_lb_num_buckets);

static int netdev_lb_device_event(struct notifier_block *this, unsigned long event,
			     void *arg)
{
	struct net_device *netdev = arg;

	printk(KERN_CRIT "netdev_lb_device_event: %lu (%p)\n", event, netdev);

	switch (event) {
	case NETDEV_UNREGISTER:
		printk(KERN_CRIT "netdev_lb_device_event: unregestering (%p)\n", netdev);
		netdev_lb_unregister(netdev);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block netdev_notifier = {
	.notifier_call = netdev_lb_device_event,
};

static int __init netdev_lb_init(void)
{
	spin_lock_init(&lock);
	INIT_LIST_HEAD(&netdevs_to_lb);
	register_netdevice_notifier(&netdev_notifier);
	printk(KERN_CRIT "netdev_lb_init called\n");
	return 0;
}

module_init(netdev_lb_init);

#endif
