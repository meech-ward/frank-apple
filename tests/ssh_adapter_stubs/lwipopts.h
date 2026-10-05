/* Native callback tests use real SDK pbuf/mem/memp implementations. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 0
#define LWIP_RAW 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_STATS 0
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define TCP_QUEUE_OOSEQ 0
#define LWIP_CHECKSUM_ON_COPY 0
#define LWIP_SUPPORT_CUSTOM_PBUF 1
#define MEM_ALIGNMENT 8
