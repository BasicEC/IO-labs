/*****************************************************
 * This code was compiled and tested on Ubuntu 18.04.1
 * with kernel version 4.15.0
 *****************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <net/arp.h>
#include <linux/version.h>

#define ERR(...) printk( KERN_ERR "! "__VA_ARGS__ )
#define LOG(...) printk( KERN_INFO "! "__VA_ARGS__ )

static char* link = "enp0s3";
module_param( link, charp, 0 );

static char* ifname = "virt"; 
module_param( ifname, charp, 0 );

static struct net_device *child = NULL;
static struct nf_hook_ops *nfho = NULL;

struct priv {
   struct net_device *parent;
};

static unsigned int hfunc(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct iphdr *iph;
	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	/*if (iph->protocol == IPPROTO_UDP) {
		udph = udp_hdr(skb);
		if (ntohs(udph->dest) == 53) {
            if (udph->dest == 13568) {
                printk(KERN_INFO "SRC port %lu", (unsigned long)udph->source);
                printk(KERN_INFO "DST port %lu", (unsigned long)udph->dest);
			    return NF_ACCEPT;
            }
		}
	}
	else if (iph->protocol == IPPROTO_TCP) {
         printk(KERN_INFO "SRC port %lu", (unsigned long)udph->source);
         printk(KERN_INFO "DST port %lu", (unsigned long)udph->dest);
		return NF_ACCEPT;
	}*/

	printk(KERN_INFO "SRC port %lu", (unsigned long)iph->ip_src);
   printk(KERN_INFO "DST port %lu", (unsigned long)iph->ip_dst);
   return NF_ACCEPT;
	/*return NF_DROP;*/
}

static rx_handler_result_t handle_frame( struct sk_buff **pskb ) {
   struct sk_buff *skb = *pskb;
   if( child ) {
      child->stats.rx_packets++;
      child->stats.rx_bytes += skb->len;
      LOG( "rx: injecting frame from %s to %s", skb->dev->name, child->name );
      skb->dev = child;
      /* netif_receive_skb(skb);
      return RX_HANDLER_CONSUMED; */
      return RX_HANDLER_ANOTHER;
   }
   return RX_HANDLER_PASS;
}

static int open( struct net_device *dev ) {
   netif_start_queue( dev );
   LOG( "%s: device opened", dev->name );
   return 0;
}

static int stop( struct net_device *dev ) {
   netif_stop_queue( dev );
   LOG( "%s: device closed", dev->name );
   return 0;
}

static netdev_tx_t start_xmit( struct sk_buff *skb, struct net_device *dev ) {
   struct priv *priv = netdev_priv( dev );
   child->stats.tx_packets++;
   child->stats.tx_bytes += skb->len;
   if( priv->parent ) {
      skb->dev = priv->parent;
      skb->priority = 1;
      dev_queue_xmit( skb );
      LOG( "tx: injecting frame from %s to %s", dev->name, skb->dev->name );
      return 0;
   }
   return NETDEV_TX_OK;
}

static struct net_device_stats *get_stats( struct net_device *dev ) {
   return &dev->stats;
}

static struct net_device_ops crypto_net_device_ops = {
   .ndo_open = open,
   .ndo_stop = stop,
   .ndo_get_stats = get_stats,
   .ndo_start_xmit = start_xmit,
};

static void setup( struct net_device *dev ) {
   int j;
   ether_setup( dev );
   memset( netdev_priv(dev), 0, sizeof( struct priv ) );
   dev->netdev_ops = &crypto_net_device_ops;
   for( j = 0; j < ETH_ALEN; ++j ) // fill in the MAC address with a phoney 
      dev->dev_addr[ j ] = (char)j;
}

int __init init( void ) {
   int err = 0;
   struct priv *priv;
   char ifstr[ 40 ];
   sprintf( ifstr, "%s%s", ifname, "%d" );
   #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
      child = alloc_netdev( sizeof( struct priv ), ifstr, setup );
   #else 
      child = alloc_netdev( sizeof( struct priv ), ifstr,NET_NAME_UNKNOWN, setup );
   #endif
   if( child == NULL ) {
      ERR( "%s: allocate error", THIS_MODULE->name ); return -ENOMEM;
   }
   priv = netdev_priv( child );
   priv->parent = __dev_get_by_name( &init_net, link ); // parent interface  
   if( !priv->parent ) {
      ERR( "%s: no such net: %s", THIS_MODULE->name, link );
      err = -ENODEV; goto err;
   }
   if( priv->parent->type != ARPHRD_ETHER && priv->parent->type != ARPHRD_LOOPBACK ) {
      ERR( "%s: illegal net type", THIS_MODULE->name );
      err = -EINVAL; goto err;
   }
   /* also, and clone its IP, MAC and other information */
   memcpy( child->dev_addr, priv->parent->dev_addr, ETH_ALEN );
   memcpy( child->broadcast, priv->parent->broadcast, ETH_ALEN );
   if( ( err = dev_alloc_name( child, child->name ) ) ) {
      ERR( "%s: allocate name, error %i", THIS_MODULE->name, err );
      err = -EIO; goto err;
   }
   register_netdev( child );
   rtnl_lock();
   netdev_rx_handler_register( priv->parent, &handle_frame, NULL );
   rtnl_unlock();
   nfho = (struct nf_hook_ops*)kcalloc(1, sizeof(struct nf_hook_ops), GFP_KERNEL);
	
	/* Initialize netfilter hook */
	nfho->hook 	= (nf_hookfn*)hfunc;		/* hook function */
	nfho->hooknum 	= NF_INET_PRE_ROUTING;		/* received packets */
	nfho->pf 	= PF_INET;			/* IPv4 */
	nfho->priority 	= NF_IP_PRI_FIRST;		/* max hook priority */
	
	nf_register_net_hook(&init_net, nfho);
   LOG( "module %s loaded", THIS_MODULE->name );
   LOG( "%s: create link %s", THIS_MODULE->name, child->name );
   LOG( "%s: registered rx handler for %s", THIS_MODULE->name, priv->parent->name );
   return 0;
err:
   free_netdev( child );
   return err;
}

void __exit exit( void ) {
   struct priv *priv = netdev_priv( child );
   if( priv->parent ) {
      rtnl_lock();
      netdev_rx_handler_unregister( priv->parent );
      rtnl_unlock();
      LOG( "unregister rx handler for %s\n", priv->parent->name );
   }
   nf_unregister_net_hook(&init_net, nfho);
   kfree(nfho);
   unregister_netdev( child );
   free_netdev( child );
   LOG( "module %s unloaded", THIS_MODULE->name );
}

module_init(init);
module_exit(exit);

MODULE_AUTHOR( "Krasnoperova, Nitser" );
MODULE_LICENSE( "GPL v2" );
MODULE_VERSION( "2.1" );
