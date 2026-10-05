#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#if NETCARD_WEB_CONTROL || NETCARD_SSH
/* The web controller, outbound GET and Realtime share this TCP packet heap.
 * The original 4 KB can be exhausted by one screen response while a TLS
 * handshake is trying to send, leaving connections stalled under polling. */
#undef MEM_SIZE
#define MEM_SIZE                    (16 * 1024)
/* Re-enable the listener while old HTTP connections are in TIME_WAIT. */
#define SO_REUSE                    1
/* Four HTTP clients plus HTTPS/Realtime, with headroom during closing. */
#define MEMP_NUM_TCP_PCB            10
#endif
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#if NETCARD_TLS
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              1
#define LWIP_ALTCP_TLS_MBEDTLS      1
/* A TLS record is up to 16 KB; a window equal to it deadlocks. */
#undef TCP_WND
#define TCP_WND                     32768
#endif
#endif
