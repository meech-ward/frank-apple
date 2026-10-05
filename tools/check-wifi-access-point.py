#!/usr/bin/env python3
"""Exercise the AP adapter's lifecycle and packet routing using SDK/lwIP fakes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
headers = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint16_t u16_t;
typedef int err_t;
typedef struct { uint8_t b[4]; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
struct netif { bool up; ip4_addr_t address,mask,gateway; };
typedef struct { struct netif netif[2]; } cyw43_t;
extern cyw43_t cyw43_state;
extern struct netif *netif_default;
extern const ip_addr_t *IP_ANY_TYPE;
struct pbuf { struct pbuf *next; uint8_t *payload; u16_t len,tot_len; };
struct udp_pcb { unsigned options; struct netif *bound; };
typedef void (*udp_recv_fn)(void *,struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t);
#define CYW43_ITF_AP 1
#define CYW43_AUTH_WPA2_AES_PSK 7
#define ERR_OK 0
#define IPADDR_TYPE_V4 0
#define PBUF_TRANSPORT 1
#define PBUF_RAM 1
#define SOF_BROADCAST 32
#define IP4_ADDR(p,a,b,c,d) (*(p)=(ip4_addr_t){{a,b,c,d}})
#define IP_ADDR4 IP4_ADDR
#define ip_set_option(p,o) ((p)->options|=(o))
bool netif_is_up(struct netif *);
void netif_set_addr(struct netif *,const ip4_addr_t *,const ip4_addr_t *,const ip4_addr_t *);
void netif_set_default(struct netif *);
struct netif *ip_current_input_netif(void);
struct udp_pcb *udp_new_ip_type(int);
err_t udp_bind(struct udp_pcb *,const ip_addr_t *,u16_t);
void udp_bind_netif(struct udp_pcb *,struct netif *);
void udp_recv(struct udp_pcb *,udp_recv_fn,void *);
void udp_remove(struct udp_pcb *);
err_t udp_sendto_if(struct udp_pcb *,struct pbuf *,const ip_addr_t *,u16_t,struct netif *);
struct pbuf *pbuf_alloc(int,u16_t,int);
u16_t pbuf_copy_partial(const struct pbuf *,void *,u16_t,u16_t);
err_t pbuf_take(struct pbuf *,const void *,u16_t);
unsigned pbuf_free(struct pbuf *);
uint64_t time_us_64(void);
void cyw43_wifi_ap_set_channel(cyw43_t *,unsigned);
void cyw43_arch_enable_ap_mode(const char *,const char *,uint32_t);
void cyw43_arch_disable_ap_mode(void);
void cyw43_cb_tcpip_deinit(cyw43_t *,int);
'''
fixture = r'''
#include "stubs.h"
#include "wifi_access_point.h"
#include "wifi_dhcp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
cyw43_t cyw43_state;
struct netif *netif_default;
const ip_addr_t *IP_ANY_TYPE;
static struct netif *incoming;
static struct udp_pcb *live;
static udp_recv_fn receiver;
static unsigned created,removed,enabled,disabled,deinitialized,sent,allocated,freed;
static bool fail_new,fail_bind,fail_enable,fail_allocate,fail_take,fail_copy;
bool netif_is_up(struct netif *p){return p->up;}
void netif_set_addr(struct netif *p,const ip4_addr_t *a,const ip4_addr_t *m,const ip4_addr_t *g){p->address=*a;p->mask=*m;p->gateway=*g;}
void netif_set_default(struct netif *p){netif_default=p;}
struct netif *ip_current_input_netif(void){return incoming;}
struct udp_pcb *udp_new_ip_type(int type){assert(type==IPADDR_TYPE_V4);if(fail_new)return NULL;assert(!live);created++;return live=calloc(1,sizeof(*live));}
err_t udp_bind(struct udp_pcb *p,const ip_addr_t *a,u16_t port){assert(p==live&&a==IP_ANY_TYPE&&port==67);return fail_bind?-1:ERR_OK;}
void udp_bind_netif(struct udp_pcb *p,struct netif *n){assert(p==live);p->bound=n;}
void udp_recv(struct udp_pcb *p,udp_recv_fn fn,void *arg){assert(p==live&&!arg);receiver=fn;}
void udp_remove(struct udp_pcb *p){assert(p==live);removed++;free(p);live=NULL;receiver=NULL;}
err_t udp_sendto_if(struct udp_pcb *p,struct pbuf *packet,const ip_addr_t *to,u16_t port,struct netif *n){
 assert(p==live&&p->bound==&cyw43_state.netif[1]&&(p->options&SOF_BROADCAST));
 assert(n==&cyw43_state.netif[1]&&port==68&&to->b[0]==255&&to->b[3]==255);
 assert(packet->tot_len>=300&&packet->payload[0]==2&&packet->payload[19]==2);sent++;return ERR_OK;
}
struct pbuf *pbuf_alloc(int layer,u16_t size,int type){
 (void)layer;(void)type;if(fail_allocate)return NULL;
 struct pbuf *p=calloc(1,sizeof(*p));assert(p);p->payload=calloc(1,size?size:1);assert(p->payload);p->len=p->tot_len=size;allocated++;return p;
}
u16_t pbuf_copy_partial(const struct pbuf *p,void *out,u16_t len,u16_t offset){
 assert(!offset);if(fail_copy)return 0;u16_t n=0;
 while(p&&n<len){u16_t take=p->len;if(take>len-n)take=len-n;memcpy((uint8_t *)out+n,p->payload,take);n+=take;p=p->next;}return n;
}
err_t pbuf_take(struct pbuf *p,const void *data,u16_t n){if(fail_take)return -1;assert(n<=p->len);memcpy(p->payload,data,n);return ERR_OK;}
unsigned pbuf_free(struct pbuf *p){unsigned n=0;while(p){struct pbuf *next=p->next;free(p->payload);free(p);freed++;n++;p=next;}return n;}
uint64_t time_us_64(void){return 10000000;}
void cyw43_wifi_ap_set_channel(cyw43_t *p,unsigned channel){assert(p==&cyw43_state&&channel==6);}
void cyw43_arch_enable_ap_mode(const char *ssid,const char *password,uint32_t auth){assert(!strcmp(ssid,"Apple II")&&!strcmp(password,"HotspotPassword")&&auth==CYW43_AUTH_WPA2_AES_PSK);enabled++;cyw43_state.netif[1].up=!fail_enable;}
void cyw43_arch_disable_ap_mode(void){disabled++;cyw43_state.netif[1].up=false;}
void cyw43_cb_tcpip_deinit(cyw43_t *p,int itf){assert(p==&cyw43_state&&itf==1);deinitialized++;if(netif_default==&p->netif[1])netif_default=NULL;}
static struct pbuf *discover(void){
 uint8_t data[244]={1,1,6};data[28]=2;data[33]=1;memcpy(data+236,(uint8_t[]){99,130,83,99,53,1,1,255},8);
 struct pbuf *p=pbuf_alloc(0,80,0);p->next=pbuf_alloc(0,164,0);p->tot_len=244;
 memcpy(p->payload,data,80);memcpy(p->next->payload,data+80,164);return p;
}
int main(void){
 wifi_config config={.ssid="Apple II",.password="HotspotPassword",.mode=WIFI_MODE_HOTSPOT};
 assert(!wifi_access_point_active());wifi_access_point_stop();
 assert(!wifi_access_point_start(NULL));config.mode=WIFI_MODE_STATION;assert(!wifi_access_point_start(&config));config.mode=WIFI_MODE_HOTSPOT;
 fail_new=true;assert(!wifi_access_point_start(&config)&&!enabled);fail_new=false;
 fail_bind=true;assert(!wifi_access_point_start(&config)&&created==removed&&!enabled);fail_bind=false;
 fail_enable=true;assert(!wifi_access_point_start(&config)&&created==removed&&!wifi_access_point_active());assert(disabled==1&&deinitialized==1);fail_enable=false;
 assert(wifi_access_point_start(&config)&&wifi_access_point_active());assert(receiver&&netif_default==&cyw43_state.netif[1]);
 assert(!memcmp(netif_default->address.b,(uint8_t[]){192,168,4,1},4));assert(!memcmp(netif_default->mask.b,(uint8_t[]){255,255,255,0},4));
 unsigned previous=created;assert(wifi_access_point_start(&config)&&created==previous);
 incoming=&cyw43_state.netif[0];receiver(NULL,live,discover(),NULL,68);assert(!sent);
 incoming=&cyw43_state.netif[1];receiver(NULL,live,discover(),NULL,67);assert(!sent);
 receiver(NULL,live,pbuf_alloc(0,WIFI_DHCP_PACKET_MAX+1,0),NULL,68);assert(!sent);
 fail_copy=true;receiver(NULL,live,discover(),NULL,68);assert(!sent);fail_copy=false;
 receiver(NULL,live,discover(),NULL,68);assert(sent==1); /* Chained receive -> contiguous valid response. */
 struct pbuf *p=discover();fail_allocate=true;receiver(NULL,live,p,NULL,68);assert(sent==1);fail_allocate=false;
 fail_take=true;receiver(NULL,live,discover(),NULL,68);assert(sent==1);fail_take=false;
 receiver(NULL,live,discover(),NULL,68);assert(sent==2&&allocated==freed);
 wifi_access_point_stop();assert(!wifi_access_point_active()&&!live&&!netif_default&&created==removed);
 previous=disabled;wifi_access_point_stop();assert(disabled==previous);
 assert(wifi_access_point_start(&config));wifi_access_point_stop();assert(created==removed&&allocated==freed);
 puts("PASS: AP startup failures/idempotence/stop, WPA2/channel/address, DHCP AP-only binding, chained pbufs and allocation/copy failures.");
}
'''
with tempfile.TemporaryDirectory(prefix="apple2-ap-") as directory:
    p = Path(directory)
    (p / "stubs.h").write_text(headers)
    for name in ["pico/cyw43_arch.h", "pico/time.h", "lwip/ip.h", "lwip/netif.h", "lwip/pbuf.h", "lwip/udp.h"]:
        header = p / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "stubs.h"\n')
    (p / "check.c").write_text(fixture)
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I", str(p), "-I", str(root / "src"),
        str(p / "check.c"), str(root / "src/wifi_access_point.c"),
        str(root / "src/wifi_dhcp.c"), "-o", str(p / "check"),
    ], check=True)
    subprocess.run([str(p / "check")], check=True)
